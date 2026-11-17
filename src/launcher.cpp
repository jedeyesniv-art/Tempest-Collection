// launcher.cpp - Tempest Collection: the game picker (tempest.exe).
//
// Each game is its own program (t2k.exe, t3k.exe, ...) started as a separate process: the emulators keep a lot of
// global state, so separate processes keep them independent and a crash in one game cannot take the others down.
// While a game runs the launcher stays underneath it (fullscreen, showing "Starting ..."), so no desktop shows between
// the two; the game's window is handed the foreground when it appears (and once more after it goes fullscreen), and
// the launcher comes back to the front when the game quits (as the Minotaur Project launcher does).
//
// Barebones for now (placeholder look, SDL debug font): the list, keyboard / pad control, start, Steam link.
// userdata\launcher_settings.txt: fullscreen.
// --shot=<file.bmp> saves the first frame and exits (tests); --autostart=N starts game N at once and logs the focus
// hand-over on stderr (tests).
// Keys: Up / Down (d-pad, left stick) choose, Left / Right the variant (Space Giraffe: Acid / Nuxx), Enter / A start,
// F11 fullscreen, Esc / B quit. Steam games: Tempest 4000; Space Giraffe (the helper d3d9.dll is copied next to the game
// and its flavour written to sg_helper.txt there before Steam starts it).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "settings.h"

namespace fs = std::filesystem;

namespace {

const char kDefaults[] =
    "# Tempest Collection launcher settings (key = value; '#' starts a comment).\n"
    "\n"
    "# 1 = start fullscreen (F11 toggles and remembers).\n"
    "fullscreen = 1\n"
    "\n"
    "# Space Giraffe flavour: acid or nuxx (LEFT / RIGHT on its entry changes and remembers).\n"
    "sg_flavour = acid\n";

constexpr const char* kSteamAppT4K = "688140";   // Tempest 4000 on Steam (store.steampowered.com/app/688140)
constexpr const char* kSteamAppSG = "27800";     // Space Giraffe on Steam (store.steampowered.com/app/27800)

enum class Kind { Program, Steam, NotYet };

struct Game {
  const char* title;
  const char* detail;   // year, platform
  Kind kind;
  const char* target;   // program file name, or Steam app ID
  std::string status;   // why it can't start (empty = ready)
};

// The program a Steam game runs as (the launcher waits for it to hand it the front and to come back when it quits).
const wchar_t* SteamExe(const char* app) {
  if (std::strcmp(app, kSteamAppSG) == 0) return L"SpaceGiraffePC.exe";
  if (std::strcmp(app, kSteamAppT4K) == 0) return L"Tempest4000.exe";
  return nullptr;
}

// A running process by program name (opened for waiting on), or nullptr.
HANDLE FindProcess(const wchar_t* exe) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return nullptr;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  HANDLE found = nullptr;
  for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
    if (_wcsicmp(pe.szExeFile, exe) == 0)
      found = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
  CloseHandle(snap);
  return found;
}

// ---- Space Giraffe: the helper (src/sg/d3d9_proxy.cpp) next to the game, and its flavour setting ----

std::string ReadFile(const fs::path& p) {
  std::string s;
  if (FILE* f = _wfopen(p.c_str(), L"rb")) {
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) s.append(buf, n);
    std::fclose(f);
  }
  return s;
}

// The value of the first "key" "value" pair after `from` in a Steam .vdf / .acf text.
std::string VdfValue(const std::string& s, const std::string& key, size_t from = 0) {
  const size_t k = s.find("\"" + key + "\"", from);
  if (k == std::string::npos) return "";
  const size_t a = s.find('"', k + key.size() + 2);
  const size_t b = a == std::string::npos ? a : s.find('"', a + 1);
  if (b == std::string::npos) return "";
  std::string v = s.substr(a + 1, b - a - 1);
  for (size_t i = 0; (i = v.find("\\\\", i)) != std::string::npos; ++i) v.erase(i, 1);   // "C:\\x" -> "C:\x"
  return v;
}

// Running under Wine / Proton (the Steam Deck): ntdll has wine_get_version.
bool UnderWine() {
  static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
  return wine;
}

// A path as Steam wrote it, for this process: Linux paths ("/home/deck/...", "/run/media/...") are on Wine's Z:
// drive (LAUNCHER_LINUX_DRIVE overrides it, for tests on Windows); Windows paths stay.
fs::path HostPath(std::string p) {
  if (p.empty() || p[0] != '/') return fs::path(p);
  const char* drive = std::getenv("LAUNCHER_LINUX_DRIVE");
  for (auto& c : p) if (c == '/') c = '\\';
  return fs::path(std::string(drive ? drive : "Z:") + p);
}

// Steam's own folders: Windows Steam's (registry), and under Proton the real Steam client's, which Steam passes to
// every Proton game (a non-Steam shortcut too) as STEAM_COMPAT_CLIENT_INSTALL_PATH (a Linux path).
// LAUNCHER_NO_REGISTRY=1 skips the registry (tests of the Deck path on Windows).
std::vector<fs::path> SteamRoots() {
  std::vector<fs::path> roots;
  if (const char* p = std::getenv("STEAM_COMPAT_CLIENT_INSTALL_PATH"); p && *p) roots.push_back(HostPath(p));
  char steam[MAX_PATH] = {};
  DWORD size = sizeof(steam);
  if (!std::getenv("LAUNCHER_NO_REGISTRY") &&
      RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ, nullptr, steam, &size) ==
          ERROR_SUCCESS)
    roots.push_back(fs::path(steam));
  return roots;
}

// The game's install folder (Steam's library folders, each with steamapps\appmanifest_<id>.acf); empty if not found.
// Also the installed check for the Steam entries (Windows and the Deck alike).
fs::path SteamGameDir(const char* app) {
  std::vector<fs::path> libs;
  for (const auto& root : SteamRoots()) {
    libs.push_back(root);
    const std::string vdf = ReadFile(root / "steamapps" / "libraryfolders.vdf");
    for (size_t at = 0; (at = vdf.find("\"path\"", at)) != std::string::npos; at += 6)
      libs.push_back(HostPath(VdfValue(vdf, "path", at)));
  }
  std::error_code ec;
  for (const auto& lib : libs) {
    const std::string acf = ReadFile(lib / "steamapps" / (std::string("appmanifest_") + app + ".acf"));
    const std::string dir = acf.empty() ? "" : VdfValue(acf, "installdir");
    if (!dir.empty() && fs::exists(lib / "steamapps" / "common" / dir, ec)) return lib / "steamapps" / "common" / dir;
  }
  return {};
}

// Copies `src` to `dst` when missing or different; false on failure.
bool CopyIfDifferent(const fs::path& src, const fs::path& dst) {
  if (fs::exists(dst) && ReadFile(src) == ReadFile(dst)) return true;
  std::error_code ec;
  fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
  return !ec;
}

// Tempest 4000's helper (src/t4k/xinput_proxy.cpp): xinput1_4.dll + xinput1_3.dll into its Win10 and Win7-8 folders.
std::string PrepareT4K(const fs::path& base) {
  const fs::path dir = SteamGameDir(kSteamAppT4K);
  if (dir.empty()) return "";
  for (const fs::path& from : {base / "Tempest 4000 helper", base / "t4k"}) {
    if (!fs::exists(from / "xinput1_4.dll")) continue;
    for (const char* sub : {"Win10", "Win7-8"}) {
      if (!fs::exists(dir / sub)) continue;
      for (const char* dll : {"xinput1_4.dll", "xinput1_3.dll"})
        if (fs::exists(from / dll) && !CopyIfDifferent(from / dll, dir / sub / dll))
          return "could not copy the helper into Tempest 4000's folder";
    }
    break;
  }
  return "";
}

// Puts the helper next to SpaceGiraffePC.exe (when missing or different) and the flavour into its sg_helper.txt
// (other lines kept). Returns an error message, or empty.
std::string PrepareSpaceGiraffe(const fs::path& base, const std::string& flavour) {
  const fs::path dir = SteamGameDir(kSteamAppSG);
  if (dir.empty()) return "";   // not found: Steam starts it as it is (a hand-installed helper still works)
  for (const fs::path& src : {base / "Space Giraffe helper" / "d3d9.dll", base / "sg" / "d3d9.dll"}) {
    if (!fs::exists(src)) continue;
    if (!CopyIfDifferent(src, dir / "d3d9.dll")) return "could not copy the helper into Space Giraffe's folder";
    break;
  }
  const fs::path cfg = dir / "sg_helper.txt";
  std::string text = ReadFile(cfg), out;
  bool done = false;
  for (size_t at = 0; at < text.size();) {
    size_t nl = text.find('\n', at);
    if (nl == std::string::npos) nl = text.size();
    std::string line = text.substr(at, nl - at);
    if (line.rfind("flavour", 0) == 0) { line = "flavour = " + flavour; done = true; }
    out += line + "\n";
    at = nl + 1;
  }
  if (!done) out += "flavour = " + flavour + "\n";
  if (FILE* f = _wfopen(cfg.c_str(), L"wb")) {
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
  } else {
    return "could not write sg_helper.txt";
  }
  return "";
}

std::vector<Game> MakeGames(const fs::path& base) {
  std::vector<Game> g = {
      {"TEMPEST", "1981  ARCADE", Kind::Program, "t81.exe", ""},
      {"TEMPEST 2000", "1994  ATARI JAGUAR", Kind::Program, "t2k.exe", ""},
      {"TEMPEST 3000", "2000  NUON", Kind::Program, "t3k.exe", ""},
      {"TxK", "2014  PS VITA", Kind::Program, "txk.exe", ""},
      {"TEMPEST 4000", "2018  STEAM", Kind::Steam, kSteamAppT4K, ""},
      {"SPACE GIRAFFE", "2008  STEAM", Kind::Steam, kSteamAppSG, ""},
  };
  for (auto& x : g) {
    if (x.kind == Kind::NotYet) x.status = "coming later";
    else if (x.kind == Kind::Program && !fs::exists(base / x.target)) x.status = std::string(x.target) + " not found";
    else if (x.kind == Kind::Steam && SteamGameDir(x.target).empty())
      x.status = "not installed on Steam";
  }
  return g;
}

// A process's visible top-level window, if it has one yet.
HWND FindProcessWindow(DWORD pid) {
  struct Want { DWORD pid; HWND hwnd; } want{pid, nullptr};
  EnumWindows(
      [](HWND h, LPARAM p) -> BOOL {
        auto* w = reinterpret_cast<Want*>(p);
        DWORD owner = 0;
        GetWindowThreadProcessId(h, &owner);
        if (owner == w->pid && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) {
          w->hwnd = h;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&want));
  return want.hwnd;
}

// Starts a game program next to the launcher; returns its process handle (nullptr on failure).
HANDLE StartProgram(const fs::path& exe) {
  AllowSetForegroundWindow(ASFW_ANY);   // we have the foreground, so the game may take it
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + exe.wstring() + L"\" --from-launcher";   // (the game's menu then says "Return to launcher")
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return nullptr;
  CloseHandle(pi.hThread);
  return pi.hProcess;
}

void Text(SDL_Renderer* ren, float x, float y, float scale, const char* s) {
  SDL_SetRenderScale(ren, scale, scale);
  SDL_RenderDebugText(ren, x / scale, y / scale, s);
  SDL_SetRenderScale(ren, 1.f, 1.f);
}

}  // namespace

int main(int argc, char** argv) {
  std::string shot;   // --shot=<file.bmp>: draw one frame, save it, exit (tests)
  int autostart = -1;   // --autostart=N: start game N (list order) at once (tests; logs the focus hand-over)
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]).rfind("--shot=", 0) == 0) shot = argv[i] + 7;
    if (std::string(argv[i]).rfind("--autostart=", 0) == 0) autostart = std::atoi(argv[i] + 12);
  }
  SetProcessDPIAware();
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  const fs::path base(SDL_GetBasePath());
  const fs::path udir = base / "userdata";
  fs::create_directories(udir);
  settings::SetDefaultText(kDefaults);
  settings::Load((udir / "launcher_settings.txt").string());

  SDL_Window* win = SDL_CreateWindow("Tempest Collection", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Renderer* ren = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
  if (!ren) {
    std::fprintf(stderr, "window: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetRenderVSync(ren, 1);
  if (settings::Num("fullscreen", 1) != 0) SDL_SetWindowFullscreen(win, true);

  std::vector<Game> games = MakeGames(base);
  int sel = 1;   // Tempest 2000: the first one that runs
  std::string message;
  HANDLE child = nullptr;
  uint64_t child_started = 0, child_first_push = 0;
  int child_pushed = 0;        // times the game's window was brought to the front
  std::string starting;        // "Starting TxK..." shown underneath while the game loads
  uint64_t ignore_until = 0;   // input that arrives just after a game quits is not for us
  const wchar_t* steam_wait = nullptr;   // a Steam game's program we wait to appear (then it is `child`)
  uint64_t steam_wait_start = 0, last_push = 0;
  float stick_y = 0.f;
  bool running = true;

  // The screen under a starting game: black, "STARTING <title>..." in the middle.
  auto draw_starting = [&] {
    int ww = 0, wh = 0;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    const float s = std::max(1.f, wh / 240.f);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
    const float ts = s * 0.75f;
    Text(ren, (ww - starting.size() * 8 * ts) / 2, (wh - 8 * ts) / 2, ts, starting.c_str());
    SDL_RenderPresent(ren);
  };
  auto start = [&](int i) {
    const Game& g = games[i];
    message.clear();
    if (!g.status.empty()) {
      message = std::string(g.title) + ": " + g.status;
      return;
    }
    if (g.kind == Kind::Steam) {
      const std::string err = std::strcmp(g.target, kSteamAppSG) == 0
                                  ? PrepareSpaceGiraffe(base, settings::Str("sg_flavour", "acid"))
                                  : PrepareT4K(base);
      if (!err.empty()) {
        message = err;
        return;
      }
      // We have the front now: let the game (started by Steam, not by us) take it when its window appears.
      AllowSetForegroundWindow(ASFW_ANY);
      const std::string url = std::string("steam://rungameid/") + g.target;
      if (!SDL_OpenURL(url.c_str())) {
        message = "could not open Steam";
        return;
      }
      // Wait for its program underneath, as for our own games - not under Proton: there each Steam game runs in its own
      // Wine prefix, out of our sight (and the Deck's gamescope puts the new game in front itself).
      if (const wchar_t* exe = UnderWine() ? nullptr : SteamExe(g.target)) {
        starting = std::string("STARTING ") + g.title + "...";
        draw_starting();
        steam_wait = exe;
        steam_wait_start = SDL_GetTicksNS();
      } else {
        message = std::string("starting ") + g.title + " in Steam...";
      }
      return;
    }
    // The launcher stays underneath the game: draw "Starting ..." first (a black screen while the game loads).
    starting = std::string("STARTING ") + g.title + "...";
    draw_starting();
    child = StartProgram(base / g.target);
    if (!child) {
      starting.clear();
      message = std::string("could not start ") + g.target;
      return;
    }
    child_started = SDL_GetTicksNS();
    child_pushed = 0;
  };
  auto move = [&](int d) {
    sel = (sel + d + int(games.size())) % int(games.size());
    message.clear();
  };
  // LEFT / RIGHT: the selected game's variant (Space Giraffe's flavour).
  auto variant = [&](int) {
    if (std::strcmp(games[sel].target, kSteamAppSG) != 0) return;
    settings::Save("sg_flavour", settings::Str("sg_flavour", "acid") == "acid" ? "nuxx" : "acid");
    message.clear();
  };
  stick_y = 0.f;
  float stick_x = 0.f;

  if (autostart >= 0 && autostart < int(games.size())) start(autostart);
  HWND me = static_cast<HWND>(
      SDL_GetPointerProperty(SDL_GetWindowProperties(win), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
  while (running) {
    if (steam_wait && !child) {   // Steam is starting a game: wait for its program (60 s; Esc gives up)
      SDL_Delay(100);
      bool give_up = SDL_GetTicksNS() - steam_wait_start > 60000000000ull;
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(e.gdevice.which);
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) give_up = true;
      }
      if (HANDLE h = FindProcess(steam_wait)) {
        child = h;
        child_started = SDL_GetTicksNS();
        child_pushed = 0;
        steam_wait = nullptr;
        if (autostart >= 0) std::fprintf(stderr, "launcher: Steam game's program found\n");
      } else if (give_up) {
        steam_wait = nullptr;
        starting.clear();
        message = "the game did not start (Steam)";
      } else {
        draw_starting();
      }
      continue;
    }
    if (child) {   // a game is running on top: give it the front, wait for it, then come back
      if (WaitForSingleObject(child, 50) == WAIT_OBJECT_0) {
        CloseHandle(child);
        child = nullptr;
        starting.clear();
        games = MakeGames(base);
        SDL_RaiseWindow(win);
        if (auto* me = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(win),
                                                                SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)))
          SetForegroundWindow(me);
        ignore_until = SDL_GetTicksNS() + 400000000ull;
      } else {
        // Its window to the front when it appears, and once more a second later (after it goes fullscreen).
        const uint64_t t = SDL_GetTicksNS() - child_started;
        if (child_pushed < 2 && t < 20000000000ull && (child_pushed == 0 || t > child_first_push + 1000000000ull)) {
          if (HWND h = FindProcessWindow(GetProcessId(child))) {
            const BOOL ok = SetForegroundWindow(h);
            if (autostart >= 0)
              std::fprintf(stderr, "launcher: game window to the front at %.2f s: %s, foreground is %s\n", t / 1e9,
                           ok ? "ok" : "refused", GetForegroundWindow() == h ? "the game" : "not the game");
            if (child_pushed++ == 0) child_first_push = t;
          }
        }
        // While we (the screen underneath) still have the front in its first 30 s, the game's window gets it - for
        // games whose window comes late or is replaced (Steam games: dialogs first, then the game's own window).
        // Never taken from another program the player switched to.
        const uint64_t now = SDL_GetTicksNS();
        if (t < 30000000000ull && now - last_push > 500000000ull && GetForegroundWindow() == me) {
          if (HWND h = FindProcessWindow(GetProcessId(child))) {
            last_push = now;
            const BOOL ok = SetForegroundWindow(h);
            if (autostart >= 0)
              std::fprintf(stderr, "launcher: launcher in front at %.2f s, game window pushed: %s\n", t / 1e9,
                           ok ? "ok" : "refused");
          }
        }
      }
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(e.gdevice.which);
      }
      continue;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      const bool live = SDL_GetTicksNS() >= ignore_until;
      if (e.type == SDL_EVENT_QUIT) running = false;
      else if (e.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(e.gdevice.which);
      else if (e.type == SDL_EVENT_KEY_DOWN && live) {
        switch (e.key.scancode) {
          case SDL_SCANCODE_UP: move(-1); break;
          case SDL_SCANCODE_DOWN: move(1); break;
          case SDL_SCANCODE_LEFT: variant(-1); break;
          case SDL_SCANCODE_RIGHT: variant(1); break;
          case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: start(sel); break;
          case SDL_SCANCODE_ESCAPE: running = false; break;
          case SDL_SCANCODE_F11: {
            const bool on = !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN);
            SDL_SetWindowFullscreen(win, on);
            settings::Save("fullscreen", on ? "1" : "0");
            break;
          }
          default: break;
        }
      } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && live) {
        switch (e.gbutton.button) {
          case SDL_GAMEPAD_BUTTON_DPAD_UP: move(-1); break;
          case SDL_GAMEPAD_BUTTON_DPAD_DOWN: move(1); break;
          case SDL_GAMEPAD_BUTTON_DPAD_LEFT: variant(-1); break;
          case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: variant(1); break;
          case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: start(sel); break;
          case SDL_GAMEPAD_BUTTON_EAST: running = false; break;
          default: break;
        }
      } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY && live) {
        const float v = e.gaxis.value / 32767.f;   // one step per push past half way
        if (v > 0.5f && stick_y <= 0.5f) move(1);
        if (v < -0.5f && stick_y >= -0.5f) move(-1);
        stick_y = v;
      } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX && live) {
        const float v = e.gaxis.value / 32767.f;
        if ((v > 0.5f && stick_x <= 0.5f) || (v < -0.5f && stick_x >= -0.5f)) variant(v > 0 ? 1 : -1);
        stick_x = v;
      }
    }

    int ww = 0, wh = 0;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    const float s = std::max(1.f, wh / 240.f);   // the 8-pixel font, scaled to the window
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
    Text(ren, 24 * s / 2, 16 * s / 2, s * 1.5f, "TEMPEST COLLECTION");
    for (int i = 0; i < int(games.size()); ++i) {
      const Game& g = games[i];
      const float y = (56 + i * 28) * s / 2;
      const bool ready = g.status.empty();
      if (i == sel) SDL_SetRenderDrawColor(ren, 255, 64, 64, 255);
      else if (ready) SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
      else SDL_SetRenderDrawColor(ren, 110, 110, 110, 255);
      Text(ren, 24 * s / 2, y, s, (std::string(i == sel ? "> " : "  ") + g.title).c_str());
      SDL_SetRenderDrawColor(ren, ready ? 120 : 80, ready ? 160 : 80, ready ? 255 : 80, 255);
      std::string detail = std::string(g.detail) + (ready ? "" : "  - " + g.status);
      if (std::strcmp(g.target, kSteamAppSG) == 0 && ready)   // the flavour it starts in; LEFT / RIGHT change it
        detail += settings::Str("sg_flavour", "acid") == "acid" ? "   < ACID >" : "   < NUXX >";
      Text(ren, 24 * s / 2 + 18 * 8 * s, y, s * 0.6f, detail.c_str());
    }
    SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
    if (!message.empty())
      Text(ren, 24 * s / 2, (56 + int(games.size()) * 28 + 8) * s / 2, s * 0.6f, message.c_str());
    SDL_SetRenderDrawColor(ren, 140, 140, 140, 255);
    Text(ren, 24 * s / 2, wh - 16 * s / 2, s * 0.5f,
         "UP/DOWN choose   LEFT/RIGHT flavour   ENTER/A start   F11 fullscreen   ESC/B quit");
    if (!shot.empty()) {
      if (SDL_Surface* img = SDL_RenderReadPixels(ren, nullptr)) {
        SDL_SaveBMP(img, shot.c_str());
        SDL_DestroySurface(img);
      }
      running = false;
    }
    SDL_RenderPresent(ren);
  }
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
