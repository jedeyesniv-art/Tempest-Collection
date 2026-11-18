// launcher.cpp - Tempest Collection: the game picker (tempest.exe).
//
// Each game is its own program (t2k.exe, t3k.exe, ...) started as a separate process: the emulators keep a lot of
// global state, so separate processes keep them independent and a crash in one game cannot take the others down.
// While a game runs the launcher stays underneath it (fullscreen, showing "Starting ..."), so no desktop shows between
// the two; the game's window is handed the foreground when it appears (and once more after it goes fullscreen), and
// the launcher comes back to the front when the game quits (as the Minotaur Project launcher does).
//
// The look (src/launcher_view.cpp, from tools/launcher_mockup): portrait cover panels scrolled left / right, each
// game's logo and moving background; the art in art\ next to the program (play folder) or the repo's art\panels\
// (tools/launcher_art.py; never in git).
// userdata\launcher_settings.txt: fullscreen.
// --shot=<file.bmp> saves the 90th frame and exits (tests); --autostart=N starts game N at once and logs the focus
// hand-over on stderr (tests); --select=N starts with entry N selected. Test environment: LAUNCHER_FAKE_WINE=1 (act as
// under Proton), LAUNCHER_PROC_DIR (instead of Z:\proc), LAUNCHER_DRY_URL=1 (don't open steam:// links),
// LAUNCHER_NO_REGISTRY=1 + STEAM_COMPAT_CLIENT_INSTALL_PATH + LAUNCHER_LINUX_DRIVE (the Deck's Steam lookup).
// --steam-button presses Y at start (logs the result); LAUNCHER_STEAM_NOT_RUNNING=1 never closes the real Steam (tests).
// Steam entries say what to do when not set up: buy / install (A opens the store page), a missing helper, or on the
// Deck the launch option to add (read from Steam's localconfig.vdf; Steam rewrites it, so we can't set it).
// Keys: Left / Right (d-pad, left stick) choose, Up / Down the variant (Space Giraffe: Acid / Nuxx), Enter / A start,
// Y (keyboard S) adds the collection to Steam with its library art (Steam is closed for it and started again), or
// when it is in Steam already (always on the Deck) writes the art (steam_shortcut.cpp). F11 fullscreen, Esc / B quit. Steam games: Tempest 4000; Space Giraffe (the helper d3d9.dll is copied next to the game
// and its flavour written to sg_helper.txt there before Steam starts it).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "launcher_sound.h"
#include "launcher_view.h"
#include "menu.h"
#include "steam_shortcut.h"
#include "settings.h"
#include "t3k_compress.h"

namespace fs = std::filesystem;

namespace {

const char kDefaults[] =
    "# Tempest Collection launcher settings (key = value; '#' starts a comment).\n"
    "\n"
    "# 1 = start fullscreen (F11 toggles and remembers).\n"
    "fullscreen = 1\n"
    "\n"
    "# Space Giraffe flavour: acid or nuxx (UP / DOWN on its entry changes and remembers).\n"
    "sg_flavour = acid\n"
    "\n"
    "# Tempest 3000's disc: 1 = on starting it, offer to compress the .iso (743 MB -> ~196 MB), then to delete the\n"
    "# .iso; answering no sets these to 0 (never asked again; t3k.exe plays the compressed disc when it is there).\n"
    "t3k_offer_compress = 1\n"
    "t3k_offer_delete = 1\n"
    "\n"
    "# Sound (the settings menu: Back / Esc): every game's volume, 0 .. 1 (read by the games when they start);\n"
    "# 1 = every game at the same loudness (each one's gain measured once; 0 = as each game made it);\n"
    "# the launcher's own sounds, 0 (off) .. 1.\n"
    "master_volume = 1\n"
    "even_loudness = 1\n"
    "sting_volume = 1\n"
    "\n"
    "# The launcher's own: how much the backgrounds and logos move: full, reduced or still;\n"
    "# 1 = leave out the games that can't be started here (not installed, program missing).\n"
    "motion = full\n"
    "hide_missing = 0\n";

constexpr const char* kSteamAppT4K = "688140";   // Tempest 4000 on Steam (store.steampowered.com/app/688140)
constexpr const char* kSteamAppSG = "27800";     // Space Giraffe on Steam (store.steampowered.com/app/27800)

enum class Kind { Program, Steam, NotYet, Promo };   // Promo: the MORE LLAMASOFT panel (A opens its list)

// The MORE LLAMASOFT list: Jeff's games on Steam (by year), then our other two collections (free, on GitHub).
struct MoreGame {
  const char* key;     // its picture: art\more_<key>.png (tools/more_art.py)
  const char* title;
  const char* line;
  const char* app;     // Steam app ID, or nullptr (ours)
  const char* url;     // the store / project page
};
const MoreGame kMore[] = {
    {"gridrunner_revolution", "GRIDRUNNER REVOLUTION", "2010 - STEAM", "27810",
     "https://store.steampowered.com/app/27810/Gridrunner_Revolution/"},
    {"minotaur_arcade", "MINOTAUR ARCADE VOLUME 1", "2018 - STEAM - GRIDRUNNER AND GOATUP", "906110",
     "https://store.steampowered.com/app/906110/Minotaur_Arcade_Volume_1/"},
    {"polybius", "POLYBIUS", "2018 - STEAM", "906120", "https://store.steampowered.com/app/906120/POLYBIUS/"},
    {"moose_life", "MOOSE LIFE", "2020 - STEAM", "1342740", "https://store.steampowered.com/app/1342740/Moose_Life/"},
    {"akka_arrh", "AKKA ARRH", "2023 - STEAM", "2005680", "https://store.steampowered.com/app/2005680/Akka_Arrh/"},
    {"jeff_minter_story", "LLAMASOFT: THE JEFF MINTER STORY", "2024 - STEAM", "2236680",
     "https://store.steampowered.com/app/2236680/Llamasoft_The_Jeff_Minter_Story/"},
    {"i_robot", "I, ROBOT", "2025 - STEAM", "2932910", "https://store.steampowered.com/app/2932910/I_Robot/"},
    {"minotaur_project", "MINOTAUR PROJECT REMASTERED", "JEFF MINTER'S MINOTAUR PROJECT iOS GAMES ON PC", nullptr,
     "https://github.com/jedeyesniv-art/MinotaurProjectRemastered"},
    {"llamasoft_st", "LLAMASOFT ON THE ST REMASTERED", "JEFF MINTER'S ATARI ST GAMES, REMASTERED", nullptr,
     "https://github.com/jedeyesniv-art/Llamatron-PC-Recomp"},
};
constexpr int kMoreFirstOurs = 7;

struct Game {
  const char* id;       // art and look (launcher_view): t81 t2k t3k sg txk t4k
  const char* title;
  const char* detail;   // year, platform
  const char* by;
  Kind kind;
  const char* target;   // program file name, or Steam app ID
  std::string status;   // why it can't start (empty = ready); for Steam games also "setup needed" (it still starts)
  std::vector<std::string> help;   // what to do, shown under the list while this entry is selected
  bool store = false;   // a Steam game not installed: A opens its Steam store page
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
  if (a == std::string::npos) return "";
  std::string v;   // up to the closing quote; \" and \\ escaped (launch options: WINEDLLOVERRIDES=\"d3d9=n,b\")
  for (size_t i = a + 1; i < s.size(); ++i) {
    if (s[i] == '"') return v;
    if (s[i] == '\\' && i + 1 < s.size()) ++i;
    v += s[i];
  }
  return "";
}

// Running under Wine / Proton (the Steam Deck): ntdll has wine_get_version. LAUNCHER_FAKE_WINE=1: act so on Windows
// (tests).
bool UnderWine() {
  static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr ||
                           std::getenv("LAUNCHER_FAKE_WINE") != nullptr;
  return wine;
}

// Under Proton a Steam game runs in its own Wine prefix, out of our process list - but Linux's own list is on Wine's
// Z: drive: true while some process's command line (Z:\proc\<pid>\cmdline) names `exe`. LAUNCHER_PROC_DIR replaces
// Z:\proc (tests). False also when the list can't be read (then we never see the game).
bool LinuxProcessRunning(const wchar_t* exe) {
  const char* dir_env = std::getenv("LAUNCHER_PROC_DIR");
  const std::wstring dir = dir_env ? fs::path(dir_env).wstring() : L"Z:\\proc";
  std::string want;
  for (const wchar_t* c = exe; *c; ++c) want += char(std::tolower(char(*c)));
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return false;
  bool found = false;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] < L'0' || fd.cFileName[0] > L'9') continue;
    if (FILE* f = _wfopen((dir + L"\\" + fd.cFileName + L"\\cmdline").c_str(), L"rb")) {
      char buf[4096];
      const size_t n = std::fread(buf, 1, sizeof buf, f);
      std::fclose(f);
      std::string s(buf, n);
      for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
      found = s.find(want) != std::string::npos;
    }
  } while (!found && FindNextFileW(h, &fd));
  FindClose(h);
  return found;
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

// Windows Steam's program (registry SteamPath), or empty.
fs::path SteamExePath() {
  char steam[MAX_PATH] = {};
  DWORD size = sizeof(steam);
  if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ, nullptr, steam, &size) !=
      ERROR_SUCCESS)
    return {};
  const fs::path exe = fs::path(steam) / "steam.exe";
  return fs::exists(exe) ? exe : fs::path{};
}

// Runs a program with arguments and does not wait; false on failure.
bool RunDetached(const fs::path& exe, const std::wstring& args) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + exe.wstring() + L"\" " + args;
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &si, &pi))
    return false;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
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
  // Under Proton (the Deck): no music - the game plays Tracks_xwma when it is there, and Proton can't decode xWMA. The
  // Win7-8 build also has the music as ADPCM WAVs (Tracks_adpcm), used when Tracks_xwma is missing (ProtonDB, owner
  // 2026-11-17), so that folder is renamed out of the way (kept: a rename back restores it; a Steam "verify files"
  // brings it back and the next start renames it again). The Win10 build has only xWMA: the Deck needs the Win7-8 one
  // (Steam's "Tempest 4000" launch entry, or the launch option's Win10 -> Win7-8 swap).
  if (UnderWine()) {
    const fs::path snd = dir / "Win7-8" / "Assets" / "Sounds";
    std::error_code ec;
    if (fs::exists(snd / "Tracks_xwma", ec) && fs::exists(snd / "Tracks_adpcm", ec)) {
      fs::path off = snd / "Tracks_xwma.proton-off";
      if (fs::exists(off, ec)) fs::remove_all(off, ec);   // (a "verify files" restored the original: renamed again)
      fs::rename(snd / "Tracks_xwma", off, ec);
      if (ec) return "could not switch Tempest 4000 to its WAV music (" + ec.message() + ")";
    }
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

// The Steam launch options the player set for `app` (Steam's userdata\<id>\config\localconfig.vdf, apps\<app>\
// LaunchOptions), or nullopt-like "?" when Steam's settings can't be read. Read only: Steam keeps that file in memory
// while it runs and writes it back, so the launcher can't set the options - it tells the player instead.
std::string SteamLaunchOptions(const char* app) {
  bool any_config = false;
  for (const auto& root : SteamRoots()) {
    std::error_code ec;
    for (fs::directory_iterator it(root / "userdata", ec), end; !ec && it != end; it.increment(ec)) {
      const std::string s = ReadFile(it->path() / "config" / "localconfig.vdf");
      if (s.empty()) continue;
      any_config = true;
      const std::string key = std::string("\"") + app + "\"";
      for (size_t at = s.find(key); at != std::string::npos; at = s.find(key, at + 1)) {
        const size_t open = s.find_first_not_of(" \t\r\n", at + key.size());
        if (open == std::string::npos || s[open] != '{') continue;
        int depth = 0;
        size_t close = open;
        for (; close < s.size(); ++close) {
          if (s[close] == '{') ++depth;
          else if (s[close] == '}' && --depth == 0) break;
        }
        const std::string block = s.substr(open, close - open);
        if (block.find("\"LaunchOptions\"") != std::string::npos) return VdfValue(block, "LaunchOptions");
      }
    }
  }
  return any_config ? "" : "?";
}

struct SteamSetup {
  const char* app;
  const char* name;       // as Steam lists it
  const char* override;   // the launch option the helper needs under Proton
  const char* needle;     // what that option must contain
  const char* helper;     // the helper file in the launcher's folder
  const char* helper_dev; // ... or in build\ (development)
};
const SteamSetup kSetups[] = {
    // Tempest 4000: also the Win7-8 build - under Proton Steam picks "Play on Windows 10", whose music is xWMA only,
    // which Proton doesn't play (no music on the Deck); the Win7-8 build has the same tracks as ADPCM WAVs.
    {kSteamAppT4K, "Tempest 4000",
     "WINEDLLOVERRIDES=\"xinput1_3,xinput1_4=n,b\" bash -c 'exec \"${@/Win10/Win7-8}\"' -- %command%", "Win7-8",
     "Tempest 4000 helper\\xinput1_4.dll", "t4k\\xinput1_4.dll"},
    {kSteamAppSG, "Space Giraffe", "WINEDLLOVERRIDES=\"d3d9=n,b\" %command%", "d3d9", "Space Giraffe helper\\d3d9.dll",
     "sg\\d3d9.dll"},
};

std::vector<Game> MakeGames(const fs::path& base) {
  std::vector<Game> g = {
      {"t81", "TEMPEST", "1981 - ARCADE", "ATARI - DAVE THEURER", Kind::Program, "t81.exe", ""},
      {"t2k", "TEMPEST 2000", "1994 - ATARI JAGUAR", "LLAMASOFT", Kind::Program, "t2k.exe", ""},
      {"t3k", "TEMPEST 3000", "2000 - NUON", "LLAMASOFT", Kind::Program, "t3k.exe", ""},
      {"sg", "SPACE GIRAFFE", "2008 - PC - STEAM", "LLAMASOFT", Kind::Steam, kSteamAppSG, ""},
      {"txk", "TxK", "2014 - PS VITA", "LLAMASOFT", Kind::Program, "txk.exe", ""},
      {"t4k", "TEMPEST 4000", "2018 - PC - STEAM", "LLAMASOFT", Kind::Steam, kSteamAppT4K, ""},
      {"more", "MORE LLAMASOFT", "JEFF MINTER'S GAMES ON STEAM", "AND OUR OTHER REMASTERED COLLECTIONS", Kind::Promo, "",
       ""},
  };
  for (auto& x : g) {
    if (x.kind == Kind::NotYet) x.status = "coming later";
    else if (x.kind == Kind::Program && !fs::exists(base / x.target)) x.status = std::string(x.target) + " not found";
    else if (x.kind == Kind::Steam) {
      const SteamSetup* su = nullptr;
      for (const auto& s : kSetups)
        if (std::strcmp(s.app, x.target) == 0) su = &s;
      if (SteamGameDir(x.target).empty()) {
        x.status = "not installed";
        x.store = true;
        x.help = {std::string("Buy and install ") + su->name + " in Steam (A opens its store page).",
                  "Then come back here: the launcher sets it up each time it starts it."};
      } else if (!fs::exists(base / su->helper) && !fs::exists(base / su->helper_dev)) {
        x.status = "helper missing";
        x.help = {std::string("The helper is missing from the launcher's folder: ") + su->helper,
                  "(copy it back from the Tempest Collection package)"};
      } else if (UnderWine()) {
        const std::string opts = SteamLaunchOptions(x.target);
        if (opts != "?" && opts.find(su->needle) == std::string::npos) {
          x.status = "setup needed";
          x.help = {"One-time setup on the Steam Deck - in Steam:",
                    std::string(su->name) + " > Properties > General > Launch Options:",
                    std::string("    ") + su->override,
                    "(A starts it anyway, without the helper)"};
        }
      }
    }
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

// The view's entries for the games (their status and setup help with them).
std::vector<lview::Entry> ViewEntries(const std::vector<Game>& games) {
  std::vector<lview::Entry> out;
  for (const Game& g : games) {
    lview::Entry e;
    e.id = g.id;
    e.title = g.title;
    e.line1 = g.detail;
    e.line2 = g.by;
    e.status = g.status;
    e.help = g.help;
    e.ready = g.status.empty() || g.status == "setup needed" || g.store;
    if (g.kind == Kind::Promo) e.action = "SEE THE LIST";
    if (std::strcmp(g.target, kSteamAppSG) == 0 && (g.status.empty() || g.status == "setup needed"))
      e.variant = settings::Str("sg_flavour", "acid") == "acid" ? "ACID" : "NUXX";
    out.push_back(e);
  }
  return out;
}

// Can't be started here at all (left out with hide_missing): not installed, not made yet, its program missing. A Steam
// game that is installed but not set up stays (it still starts).
bool Missing(const Game& g) {
  return g.store || g.kind == Kind::NotYet || (g.kind == Kind::Program && !g.status.empty());
}

// The first open pad (the settings menu reads its held buttons).
SDL_Gamepad* FirstPad() {
  int n = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&n);
  SDL_Gamepad* pad = nullptr;
  for (int k = 0; k < n && !pad; ++k) pad = SDL_GetGamepadFromID(ids[k]);
  SDL_free(ids);
  return pad;
}

// The launcher menu's own actions (menu::Update returns them).
enum LauncherAction : int { kCompressDisc = menu::kGameAction, kDeleteIso, kAskDiscAgain };

// Tempest 3000's disc: the .iso and the .nuz beside it, in the first folder (as t3k.exe searches) that has either.
struct T3kDisc { fs::path iso, nuz; };
T3kDisc FindT3kDisc(const fs::path& base) {
  for (const fs::path& d : {base, base / "game", base.parent_path().parent_path() / "Nuance_067"}) {
    std::error_code ec;
    fs::path iso, nuz;
    for (auto& e : fs::directory_iterator(d, ec)) {
      std::string ext = e.path().extension().string();
      for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
      if (e.path().filename().string().find("Tempest") == std::string::npos) continue;
      if (ext == ".iso") iso = e.path();
      if (ext == ".nuz") nuz = e.path();
    }
    if (!iso.empty()) return {iso, fs::path(iso).replace_extension(".nuz")};
    if (!nuz.empty()) return {{}, nuz};
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  std::string shot;   // --shot=<file.bmp>: draw one frame, save it, exit (tests)
  int select_arg = -1;   // --select=N: start with entry N selected (tests: its instructions in --shot)
  bool steam_button_arg = false;   // --steam-button: press Y (add to Steam / its art) at start (tests)
  bool press_start = false;        // --press-start: press A on the selected entry at start (tests: its questions)
  std::string compress_test;       // --compress-test=<iso>
  // Tests of the T3K disc questions (with --press-start): --answers=<y|n per question, in order; c = cancel the
  // compression>, and
  // --prompt-shots=<prefix> saves <prefix>N.bmp of each question (and the progress every 5 s); exits at the start warp.
  std::string answers_arg, prompt_shots;
  int menu_test = 0;
  int more_test = -1;
  std::string menu_action;
  int autostart = -1;   // --autostart=N: start game N (list order) at once (tests; logs the focus hand-over)
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]).rfind("--shot=", 0) == 0) shot = argv[i] + 7;
    if (std::string(argv[i]).rfind("--autostart=", 0) == 0) autostart = std::atoi(argv[i] + 12);
    if (std::string(argv[i]).rfind("--select=", 0) == 0) select_arg = std::atoi(argv[i] + 9);
    if (std::string(argv[i]) == "--steam-button") steam_button_arg = true;
    if (std::string(argv[i]) == "--press-start") press_start = true;
    if (std::string(argv[i]).rfind("--compress-test=", 0) == 0) compress_test = argv[i] + 16;
    if (std::string(argv[i]).rfind("--answers=", 0) == 0) answers_arg = argv[i] + 10;
    if (std::string(argv[i]).rfind("--prompt-shots=", 0) == 0) prompt_shots = argv[i] + 15;
    if (std::string(argv[i]) == "--menu") menu_test = 1;       // the settings menu open at start (tests)
    if (std::string(argv[i]) == "--menu=all") menu_test = 2;   // ... with every section open
    if (std::string(argv[i]).rfind("--more=", 0) == 0) more_test = std::atoi(argv[i] + 7);   // the list open, row N
    // --menu-action=compress|delete: the settings menu's T3K disc item chosen at start (tests, with --answers /
    // --prompt-shots as --press-start)
    if (std::string(argv[i]).rfind("--menu-action=", 0) == 0) menu_action = argv[i] + 14;
  }
  // --compress-test=<iso>: compress it to <iso>.nuz without the window (tests), print the result, exit
  if (!compress_test.empty()) {
    const fs::path iso(compress_test);
    t3kc::Job job;
    const uint64_t t0 = SDL_GetTicks();
    t3kc::Start(&job, iso, fs::path(iso).replace_extension(".nuz"),
                fs::path(SDL_GetBasePath()) / "libsndfile_x64.dll");
    while (job.phase != 3) SDL_Delay(100);
    job.worker.join();
    std::fprintf(stderr, "compress test: %s in %.1f s %s\n", job.ok ? "ok" : "FAILED", (SDL_GetTicks() - t0) / 1000.0,
                 job.error.c_str());
    return job.ok ? 0 : 1;
  }
  SetProcessDPIAware();
  // The Deck: the pad must reach the launcher after a Steam game even before it has input focus (see bg_ok).
  if (UnderWine()) SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
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
  SDL_HideCursor();   // pad / keyboard only
  if (settings::Num("fullscreen", 1) != 0) SDL_SetWindowFullscreen(win, true);

  // the art: art\ next to the program (the play folder), else the repo's art\panels (build\tempest.exe)
  const fs::path art = fs::exists(base / "art") ? base / "art" : base / ".." / "art" / "panels";
  lview::Init(ren, art);
  std::vector<Game> games;
  int sel = 0;
  // The list (again after a game, or when hide_missing changes): the selection stays on the same game if it is there.
  auto reload_games = [&] {
    const std::string was = sel < int(games.size()) ? games[sel].id : "t2k";   // (at start: Tempest 2000)
    std::vector<Game> all = MakeGames(base), shown;
    if (settings::Num("hide_missing", 0) != 0)
      for (Game& g : all)
        if (!Missing(g)) shown.push_back(g);
    games = shown.empty() ? all : shown;   // (never an empty list)
    sel = 0;
    for (int k = 0; k < int(games.size()); ++k)
      if (was == games[k].id) sel = k;
    lview::SetEntries(ViewEntries(games));
  };
  reload_games();
  // the stings: each game's lane click on selecting it, its death on starting it (sounds\ or the repo's art\sounds)
  {
    const fs::path snd = fs::exists(base / "sounds") ? base / "sounds" : base / ".." / "art" / "sounds";
    std::vector<std::string> ids;
    for (const Game& g : MakeGames(base)) ids.push_back(g.id);   // (every game: hidden ones may come back)
    const int n = lsound::Init(snd, ids);
    if (!shot.empty()) std::fprintf(stderr, "launcher: %d sound stings from %s\n", n, snd.string().c_str());
  }
  if (select_arg >= 0 && select_arg < int(games.size())) sel = select_arg;
  std::string message;
  HANDLE child = nullptr;
  uint64_t child_started = 0, child_first_push = 0;
  int child_pushed = 0;        // times the game's window was brought to the front
  std::string starting;        // "Starting TxK..." shown underneath while the game loads
  uint64_t ignore_until = 0;   // input that arrives just after a game quits is not for us
  const wchar_t* steam_wait = nullptr;   // a Steam game's program we wait to appear (then it is `child`)
  uint64_t steam_wait_start = 0, last_push = 0;
  // Under Proton (the Deck): a started Steam game is watched in Linux's process list; when it ends the launcher takes
  // the front and the pad back by itself (gamescope shows it again, but without input focus it needed a screen tap).
  // The pad works without focus there (SDL background events) only when bg_ok: at start, and after a watched game
  // ended - never while a game may be running.
  const wchar_t* deck_exe = nullptr;
  uint64_t deck_start = 0, deck_check = 0;
  bool deck_seen = false, bg_ok = true;
  int deck_misses = 0;
  float stick_y = 0.f;
  bool running = true;
  uint64_t last_frame = 0;
  int frames = 0;

  // The screen under a starting game: its logo and "STARTING <title>..." on black.
  int starting_i = -1;
  auto draw_starting = [&] {
    lview::DrawStarting(ren, starting_i, starting);
    SDL_RenderPresent(ren);
  };
  auto start = [&](int i) {
    const Game& g = games[i];
    starting_i = i;
    message.clear();
    if (g.store) {   // not installed: its Steam store page
      const std::string url = std::string("steam://store/") + g.target;
      message = SDL_OpenURL(url.c_str()) ? "opening the Steam store page..." : "could not open Steam";
      return;
    }
    if (!g.status.empty() && g.status != "setup needed") {   // (setup needed: it starts without the helper)
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
      if (std::getenv("LAUNCHER_DRY_URL")) {   // tests: don't really start it
        std::fprintf(stderr, "launcher: would open %s\n", url.c_str());
      } else if (!SDL_OpenURL(url.c_str())) {
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
        if (UnderWine() && SteamExe(g.target)) {   // watch Linux's process list for it (see LinuxProcessRunning)
          deck_exe = SteamExe(g.target);
          deck_start = SDL_GetTicksNS();
          deck_seen = false;
          deck_misses = 0;
          bg_ok = false;   // the pad is the game's now
        }
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
  // ---- Steam: add the collection (Y) / its library art ----
  wchar_t self_buf[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self_buf, MAX_PATH);
  const fs::path self_exe(self_buf);
  // ---- MORE LLAMASOFT: the list (A on its panel), A on a row opens the page ----
  bool more_open = false;
  int more_sel = 0;
  std::string more_msg;
  {
    std::vector<lview::MoreItem> items;
    for (const MoreGame& m : kMore)
      items.push_back({m.key, m.title, m.line, m.app ? "OPEN ITS STEAM PAGE" : "OPEN THE PROJECT PAGE (GITHUB)"});
    lview::SetMore(items, kMoreFirstOurs);
  }
  auto more_move = [&](int d) {
    more_sel = std::clamp(more_sel + d, 0, int(std::size(kMore)) - 1);
    more_msg.clear();
  };
  auto more_link = [&] {   // Steam pages in the Steam client when there is one (on the Deck: always), else the browser
    const MoreGame& m = kMore[more_sel];
    std::string url = m.url;
    if (m.app && (UnderWine() || !SteamRoots().empty())) url = std::string("steam://store/") + m.app;
    else if (!m.app && UnderWine()) url = "steam://openurl/" + url;   // (gaming mode: Steam's own browser)
    if (std::getenv("LAUNCHER_DRY_URL")) std::fprintf(stderr, "launcher: would open %s\n", url.c_str());
    else if (!SDL_OpenURL(url.c_str())) { more_msg = "could not open " + url; return; }
    more_msg = m.app ? "opening the Steam page..." : "opening the project page...";
  };
  const fs::path steam_art = fs::exists(base / "steam_art") ? base / "steam_art" : base / ".." / "art" / "steam";
  steamsc::Shortcut shortcut;
  uint64_t steam_confirm_until = 0;   // "press Y again" (Steam is running and has to close)
  auto refresh_steam = [&] {
    shortcut = steamsc::Find(SteamRoots(), self_exe.filename().wstring());
    std::string hint;
    if (!shortcut.config.empty() && fs::exists(steam_art)) {
      if (shortcut.found) hint = "Y  STEAM ART";
      else if (!UnderWine()) hint = "Y  ADD TO STEAM";
    }
    lview::SetExtraHint(hint);
  };
  refresh_steam();
  auto steam_button = [&] {
    message.clear();
    if (shortcut.config.empty()) { message = "Steam not found"; return; }
    if (!fs::exists(steam_art)) { message = "no steam_art folder next to the launcher"; return; }
    if (shortcut.found) {   // in Steam already: the art only (Steam may run)
      const std::string err = steamsc::WriteArt(shortcut, steam_art);
      message = err.empty() ? "Steam art set - Steam shows it after a restart" : err;
      return;
    }
    if (UnderWine()) {   // the Deck: it runs from Steam, so it is in there under another name or account
      message = "add TempestCollection.exe to Steam in desktop mode";
      return;
    }
    const fs::path steam_exe = SteamExePath();
    bool reopen = false;
    HANDLE running_steam = std::getenv("LAUNCHER_STEAM_NOT_RUNNING") ? nullptr : FindProcess(L"steam.exe");   // (tests)
    if (HANDLE h = running_steam) {   // Steam keeps the list in memory: it has to close first
      CloseHandle(h);
      const uint64_t now = SDL_GetTicksNS();
      if (now > steam_confirm_until) {
        steam_confirm_until = now + 6000000000ull;
        message = "Steam closes for a moment to add it - press Y again";
        return;
      }
      steam_confirm_until = 0;
      if (steam_exe.empty() || !RunDetached(steam_exe, L"-shutdown")) { message = "could not close Steam"; return; }
      const uint64_t t0 = SDL_GetTicksNS();
      bool closed = false;
      while (SDL_GetTicksNS() - t0 < 40000000000ull) {   // up to 40 s
        lview::DrawStarting(ren, -1, "CLOSING STEAM...");
        SDL_RenderPresent(ren);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {}
        SDL_Delay(250);
        HANDLE s = FindProcess(L"steam.exe");
        if (!s) { closed = true; break; }
        CloseHandle(s);
      }
      if (!closed) { message = "Steam did not close - try again with Steam closed"; return; }
      SDL_Delay(1000);   // (its last writes)
      reopen = true;
    }
    std::string err = steamsc::Add(shortcut, "Tempest Collection", self_exe, self_exe);
    if (err.empty()) err = steamsc::WriteArt(shortcut, steam_art);
    if (reopen && !steam_exe.empty()) RunDetached(steam_exe, L"");
    message = err.empty() ? "added to Steam as Tempest Collection" : err;
    refresh_steam();
  };

  int pending = -1;   // a game to start once the start warp has played
  // ---- Tempest 3000's disc: on starting it, compress the .iso (asked once), then delete the .iso (asked once) ----
  enum class Ask { None, Compress, Compressing, Delete };
  Ask ask = Ask::None;
  Ask test_last = Ask::None;   // (--answers / --prompt-shots)
  int test_frames = 0, test_answered = 0, test_shot = 0;
  int ask_game = -1;
  T3kDisc disc;
  double iso_mb = 0;
  int iso_links = 1;
  std::unique_ptr<t3kc::Job> job;
  auto go = [&](int i) {   // the start warp, then the game
    pending = i;
    lview::StartWarp(i);
    lsound::Launch(games[i].id);
  };
  // ask_start: the questions came up on starting Tempest 3000 (it starts after them; "no" = never ask again); false =
  // from the settings menu (nothing starts; "no" only closes the question).
  bool ask_start = true;
  bool disc_iso = false, disc_nuz = false;   // what is there (looked at by the questions and the settings menu)
  auto look_at_disc = [&] {
    disc = FindT3kDisc(base);
    std::error_code ec;
    disc_iso = !disc.iso.empty() && fs::exists(disc.iso, ec);
    disc_nuz = !disc.nuz.empty() && fs::exists(disc.nuz, ec);
    if (disc_iso) {
      iso_mb = double(fs::file_size(disc.iso, ec)) / 1e6;
      iso_links = t3kc::LinkCount(disc.iso);
    }
  };
  auto disc_question = [&](int i) {   // true: a question is up (the game starts after it)
    if (std::strcmp(games[i].id, "t3k") != 0) return false;
    look_at_disc();
    if (disc_iso && !disc_nuz && settings::Num("t3k_offer_compress", 1) != 0) ask = Ask::Compress;
    else if (disc_iso && disc_nuz && settings::Num("t3k_offer_delete", 1) != 0) ask = Ask::Delete;
    else return false;
    ask_game = i;
    ask_start = true;
    return true;
  };
  auto t3k_index = [&] {   // (the questions' colour; -1 when it is hidden)
    for (int k = 0; k < int(games.size()); ++k)
      if (std::strcmp(games[k].id, "t3k") == 0) return k;
    return -1;
  };
  auto answer = [&](bool yes) {   // A = yes, B = no (while compressing: B cancels)
    const int i = ask_game;
    if (ask == Ask::Compress) {
      if (yes) {
        job = std::make_unique<t3kc::Job>();
        t3kc::Start(job.get(), disc.iso, disc.nuz, base / "libsndfile_x64.dll");
        ask = Ask::Compressing;
      } else {
        ask = Ask::None;
        if (ask_start) {
          settings::Save("t3k_offer_compress", "0");
          go(i);
        }
      }
    } else if (ask == Ask::Compressing) {
      if (!yes && job) job->cancel = true;
    } else if (ask == Ask::Delete) {
      if (yes) {
        std::error_code ec;
        fs::remove(disc.iso, ec);
        message = ec ? "could not delete the .iso: " + ec.message() : "deleted " + disc.iso.filename().string();
      } else if (ask_start) {
        settings::Save("t3k_offer_delete", "0");
      }
      ask = Ask::None;
      if (ask_start) go(i);
    }
  };
  auto move = [&](int d) {
    if (pending >= 0) return;
    const int was = sel;
    sel = std::clamp(sel + d, 0, int(games.size()) - 1);
    if (sel != was) lsound::Move(games[sel].id);
    message.clear();
  };
  // A / Enter: the warp, then the start - straight away when it can't start (its message instead) or opens the store.
  auto request_start = [&](int i) {
    if (pending >= 0) return;
    const Game& g = games[i];
    if (g.kind == Kind::Promo) {
      more_open = true;
      more_msg.clear();
      return;
    }
    if (g.store || (!g.status.empty() && g.status != "setup needed")) {
      start(i);
      return;
    }
    if (ask != Ask::None || disc_question(i)) return;
    go(i);
  };
  // UP / DOWN: the selected game's variant (Space Giraffe's flavour).
  auto variant = [&](int) {
    if (std::strcmp(games[sel].target, kSteamAppSG) != 0) return;
    settings::Save("sg_flavour", settings::Str("sg_flavour", "acid") == "acid" ? "nuxx" : "acid");
    lview::SetEntries(ViewEntries(games));
    message.clear();
  };
  stick_y = 0.f;
  float stick_x = 0.f;

  // ---- the settings menu (Back / Esc), the games' shared one in the launcher's colour ----
  {
    menu::SetAccent(1.f, 80 / 255.f, 1.f);   // the header's magenta
    menu::SetPausedNote(false);
    menu::Page p;
    p.Button("Resume", menu::kClose, true);
    p.Header("SOUND");
    p.Choice("Master volume (every game)", "master_volume",
             {"0", "0.1", "0.2", "0.3", "0.4", "0.5", "0.6", "0.7", "0.8", "0.9", "1"},
             {"Off", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"})->def_value = "1";
    p.Choice("Even out the games' loudness", "even_loudness", {"1", "0"}, {"On", "Off"});
    p.Choice("Launcher sounds", "sting_volume", {"0", "0.25", "0.5", "0.75", "1"}, {"Off", "25%", "50%", "75%", "Full"})
        ->def_value = "1";
    p.Header("LAUNCHER");
    p.Button("Fullscreen", menu::kToggleFullscreen);
    p.Choice("Background motion", "motion", {"full", "reduced", "still"}, {"Full", "Reduced", "Still"});
    p.Choice("Hide games that can't start here", "hide_missing", {"0", "1"}, {"Off", "On"});
    p.Header("TEMPEST 3000 DISC");
    p.items.back().when = [&] { return disc_iso; };   // (nothing to do without the .iso)
    p.Button("Compress the disc now", kCompressDisc, true)->when = [&] { return disc_iso && !disc_nuz; };
    p.Button("Delete the original .iso", kDeleteIso, true)->when = [&] { return disc_iso && disc_nuz; };
    p.Button("Ask about the disc again on start", kAskDiscAgain)->when = [&] {
      return disc_iso && (settings::Num("t3k_offer_compress", 1) == 0 || settings::Num("t3k_offer_delete", 1) == 0);
    };
    p.Header("");
    p.Button("Quit the collection", menu::kQuit, true);
    menu::SetPage("TEMPEST COLLECTION - SETTINGS", std::move(p));
  }
  std::string hide_was = settings::Str("hide_missing", "0");
  auto apply_settings = [&] {   // every frame: what the menu may have changed
    lsound::SetVolume(float(settings::Num("sting_volume", 1) * settings::Num("master_volume", 1)));
    const std::string m = settings::Str("motion", "full");
    lview::SetMotion(m == "still" ? 0.f : m == "reduced" ? 0.35f : 1.f);
    const std::string hide = settings::Str("hide_missing", "0");
    if (hide != hide_was) {
      hide_was = hide;
      reload_games();
    }
  };
  auto open_menu = [&] {
    look_at_disc();
    menu::Open(menu_test == 2);
  };
  auto ask_from_menu = [&](Ask a) {
    look_at_disc();
    if (!disc_iso || (a == Ask::Delete && !disc_nuz)) return;
    ask = a;
    ask_start = false;
    ask_game = t3k_index();
  };
  if (menu_test) open_menu();
  if (more_test >= 0) {
    more_open = true;
    more_sel = std::min(more_test, int(std::size(kMore)) - 1);
  }

  if (autostart >= 0 && autostart < int(games.size())) start(autostart);
  if (steam_button_arg) {
    steam_button();
    std::fprintf(stderr, "launcher: steam button: %s\n", message.c_str());
  }
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
        reload_games();
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
    if (deck_exe && SDL_GetTicksNS() - deck_check > 1000000000ull) {   // the Deck: is the Steam game still running?
      deck_check = SDL_GetTicksNS();
      if (LinuxProcessRunning(deck_exe)) {
        if (!deck_seen && autostart >= 0) std::fprintf(stderr, "launcher: Steam game seen in the Linux process list\n");
        deck_seen = true;
        deck_misses = 0;
      } else if (deck_seen && ++deck_misses >= 2) {   // gone (twice, so not a restart in between): back to us
        deck_exe = nullptr;
        bg_ok = true;
        message.clear();
        reload_games();
        SDL_RaiseWindow(win);
        if (me) SetForegroundWindow(me);
        ignore_until = SDL_GetTicksNS() + 400000000ull;
        if (autostart >= 0) std::fprintf(stderr, "launcher: Steam game ended, launcher to the front\n");
      } else if (!deck_seen && SDL_GetTicksNS() - deck_start > 90000000000ull) {
        deck_exe = nullptr;   // never seen (the list can't be read here): a screen tap gives the launcher focus
        if (autostart >= 0) std::fprintf(stderr, "launcher: Steam game never seen - not watched\n");
      }
    }
    // Input is ours when the window has focus, or (the Deck) when no Steam game may be running.
    const bool focused = (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const bool pad_ours = !(deck_exe && deck_seen) && (focused || bg_ok);
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      const bool live = SDL_GetTicksNS() >= ignore_until && (e.type == SDL_EVENT_KEY_DOWN ? focused : pad_ours);
      if (e.type == SDL_EVENT_QUIT) running = false;
      else if (e.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(e.gdevice.which);
      else if (menu::IsOpen()) {   // the menu reads the held state itself (below)
      } else if (more_open) {   // the MORE LLAMASOFT list: up / down, A opens the page, B / Back / Esc closes
        if (e.type == SDL_EVENT_KEY_DOWN && live) {
          switch (e.key.scancode) {
            case SDL_SCANCODE_UP: more_move(-1); break;
            case SDL_SCANCODE_DOWN: more_move(1); break;
            case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: more_link(); break;
            case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: more_open = false; break;
            default: break;
          }
        } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && live) {
          switch (e.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_DPAD_UP: more_move(-1); break;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: more_move(1); break;
            case SDL_GAMEPAD_BUTTON_SOUTH: more_link(); break;
            case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_BACK: more_open = false; break;
            default: break;
          }
        } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY && live) {
          const float v = e.gaxis.value / 32767.f;   // one row per push past half way
          if ((v > 0.5f && stick_y <= 0.5f) || (v < -0.5f && stick_y >= -0.5f)) more_move(v > 0 ? 1 : -1);
          stick_y = v;
        }
      } else if (ask != Ask::None) {   // a question: A / Enter yes, B / Esc no
        const bool key = e.type == SDL_EVENT_KEY_DOWN && live, btn = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && live;
        if ((key && (e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_KP_ENTER)) ||
            (btn && e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH))
          answer(true);
        else if ((key && (e.key.scancode == SDL_SCANCODE_ESCAPE || e.key.scancode == SDL_SCANCODE_BACKSPACE)) ||
                 (btn && e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST))
          answer(false);
      } else if (e.type == SDL_EVENT_KEY_DOWN && live) {
        switch (e.key.scancode) {
          case SDL_SCANCODE_LEFT: move(-1); break;
          case SDL_SCANCODE_RIGHT: move(1); break;
          case SDL_SCANCODE_UP: variant(-1); break;
          case SDL_SCANCODE_DOWN: variant(1); break;
          case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: request_start(sel); break;
          case SDL_SCANCODE_S: steam_button(); break;
          case SDL_SCANCODE_ESCAPE: if (!e.key.repeat && pending < 0) open_menu(); break;
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
          case SDL_GAMEPAD_BUTTON_DPAD_LEFT: move(-1); break;
          case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: move(1); break;
          case SDL_GAMEPAD_BUTTON_DPAD_UP: variant(-1); break;
          case SDL_GAMEPAD_BUTTON_DPAD_DOWN: variant(1); break;
          case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: request_start(sel); break;
          case SDL_GAMEPAD_BUTTON_NORTH: steam_button(); break;
          case SDL_GAMEPAD_BUTTON_EAST: running = false; break;
          case SDL_GAMEPAD_BUTTON_BACK: if (pending < 0) open_menu(); break;
          default: break;
        }
      } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY && live) {
        const float v = e.gaxis.value / 32767.f;   // one flavour change per push past half way
        if ((v > 0.5f && stick_y <= 0.5f) || (v < -0.5f && stick_y >= -0.5f)) variant(v > 0 ? 1 : -1);
        stick_y = v;
      } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX && live) {
        const float v = e.gaxis.value / 32767.f;   // one step per push past half way
        if (v > 0.5f && stick_x <= 0.5f) move(1);
        if (v < -0.5f && stick_x >= -0.5f) move(-1);
        stick_x = v;
      }
    }

    if (menu::IsOpen()) {
      const menu::Nav nav = pad_ours ? menu::ReadNav(FirstPad(), focused ? SDL_GetKeyboardState(nullptr) : nullptr)
                                     : menu::Nav{};
      switch (menu::Update(nav, SDL_GetTicksNS())) {
        case menu::kQuit: running = false; break;
        case menu::kToggleFullscreen: {
          const bool on = !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN);
          SDL_SetWindowFullscreen(win, on);
          settings::Save("fullscreen", on ? "1" : "0");
          break;
        }
        case kCompressDisc: ask_from_menu(Ask::Compress); break;
        case kDeleteIso: ask_from_menu(Ask::Delete); break;
        case kAskDiscAgain:
          settings::Save("t3k_offer_compress", "1");
          settings::Save("t3k_offer_delete", "1");
          menu::Toast("Tempest 3000 asks about its disc again", SDL_GetTicksNS());
          break;
        default: break;
      }
      if (!menu::IsOpen()) ignore_until = SDL_GetTicksNS() + 150000000ull;   // (the closing press is not for us)
    }
    apply_settings();
    if (ask == Ask::Compressing && job && job->phase == 3) {   // the compression finished
      job->worker.join();
      const bool ok = job->ok;
      const std::string err = job->error;
      job.reset();
      if (ok && (!ask_start || settings::Num("t3k_offer_delete", 1) != 0)) {
        ask = Ask::Delete;
      } else {
        ask = Ask::None;
        if (ok) go(ask_game);
        else message = "Tempest 3000 disc not compressed: " + err;
      }
    }
    // the start warp has played: start the game
    if (pending >= 0 && !lview::Warping()) {
      const int i = pending;
      pending = -1;
      start(i);
      continue;
    }
    const uint64_t now_ns = SDL_GetTicksNS();
    const double dt = last_frame ? (now_ns - last_frame) / 1e9 : 1.0 / 60;
    last_frame = now_ns;
    lview::Draw(ren, sel, shot.empty() ? dt : 1.0 / 60, message);
    if (more_open) lview::DrawMore(ren, more_sel, shot.empty() ? dt : 1.0 / 60, more_msg);
    if (ask != Ask::None) {
      char mb[96];
      if (ask == Ask::Compress) {
        std::snprintf(mb, sizeof mb, "%.0f MB -> ABOUT %.0f MB", iso_mb, iso_mb * 0.263);
        lview::DrawPrompt(ren, ask_game, "COMPRESS THE TEMPEST 3000 DISC?",
                          {mb, "THE MUSIC BECOMES OGG VORBIS (ABOUT 260 KBPS).",
                           "THE GAME ITSELF IS UNCHANGED. IT TAKES A MINUTE OR TWO."},
                          ask_start ? "A  COMPRESS          B  NO - DON'T ASK AGAIN" : "A  COMPRESS          B  NOT NOW");
      } else if (ask == Ask::Compressing && job) {
        const int ph = job->phase, done = job->tracks_done;
        std::snprintf(mb, sizeof mb, "MUSIC TRACK %d OF %d", std::min(done + 1, int(job->tracks)), int(job->tracks));
        const std::string what = ph == 0 ? mb : ph == 1 ? "WRITING THE NEW DISC..." : "CHECKING IT...";
        const float prog = 0.9f * done / float(job->tracks) + (ph >= 1 ? 0.05f : 0.f) + (ph >= 2 ? 0.05f : 0.f);
        lview::DrawPrompt(ren, ask_game, "COMPRESSING TEMPEST 3000...", {what}, "B  CANCEL", prog);
      } else if (ask == Ask::Delete) {
        std::snprintf(mb, sizeof mb, "DELETING IT FREES %.0f MB.", iso_mb);
        const std::string frees = iso_links > 1
            ? "IT HAS " + std::to_string(iso_links - 1) + " MORE NAME(S) ON THIS DISK: DELETING THIS ONE FREES NOTHING."
            : std::string(mb);
        lview::DrawPrompt(ren, ask_game, "DELETE THE ORIGINAL DISC IMAGE?",
                          {disc.iso.filename().string(), "THE COMPRESSED DISC IS PLAYED EITHER WAY.", frees},
                          ask_start ? "A  DELETE          B  KEEP IT - DON'T ASK AGAIN" : "A  DELETE          B  KEEP IT");
      }
    }
    {
      int ow = 0, oh = 0;
      SDL_GetRenderOutputSize(ren, &ow, &oh);
      if (menu::Shown()) menu::Draw(ren, ow, oh);
      menu::DrawToast(ren, ow, oh, SDL_GetTicksNS());
    }
    ++frames;
    if (press_start && frames == 2) request_start(sel);
    if (!menu_action.empty() && frames == 2) ask_from_menu(menu_action == "delete" ? Ask::Delete : Ask::Compress);
    if ((press_start || !menu_action.empty()) && (!answers_arg.empty() || !prompt_shots.empty()) && frames > 2) {
      if (ask != test_last) {
        test_last = ask;
        test_frames = 0;
        std::fprintf(stderr, "question %d\n", int(ask));
      }
      ++test_frames;
      const bool snap = ask != Ask::None && (test_frames == 20 || (ask == Ask::Compressing && test_frames % 300 == 0));
      if (snap && !prompt_shots.empty())
        if (SDL_Surface* img = SDL_RenderReadPixels(ren, nullptr)) {
          SDL_SaveBMP(img, (prompt_shots + std::to_string(test_shot++) + ".bmp").c_str());
          SDL_DestroySurface(img);
        }
      if ((ask == Ask::Compress || ask == Ask::Delete) && test_frames == 30 &&
          test_answered < int(answers_arg.size()))
        answer(answers_arg[test_answered++] == 'y');
      if (ask == Ask::Compressing && test_frames == 120 && test_answered < int(answers_arg.size()) &&
          answers_arg[test_answered] == 'c') {   // 'c': B (cancel) two seconds into the compression
        ++test_answered;
        answer(false);
      }
      if (ask == Ask::None && (pending >= 0 || frames > 120)) {
        std::fprintf(stderr, "end: %s; message \"%s\"\n", pending >= 0 ? games[pending].id : "no start",
                     message.c_str());
        running = false;
      }
    }
    if (!shot.empty() && frames >= 90) {
      if (SDL_Surface* img = SDL_RenderReadPixels(ren, nullptr)) {
        SDL_SaveBMP(img, shot.c_str());
        SDL_DestroySurface(img);
      }
      running = false;
    }
    SDL_RenderPresent(ren);
  }
  lview::Shutdown();
  lsound::Shutdown();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
