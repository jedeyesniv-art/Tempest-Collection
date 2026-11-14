// txk_main.cpp - TxK (PS Vita, 2014) on PC: SDL3 front end for the Vita3K libretro core (stage 1).
//
//   txk.exe [game]                        play in a window (game: the TxK folder, e.g. PCSB00494, next to the program,
//                                         in game\, or two folders up - the repo root for build\txk.exe)
//   --headless --frames=N                 run N frames without a window, paced to real time (default 1200)
//   --shot=F:<file.bmp>                   save the picture after frame F (repeatable)
//   --press=F:<btn>[:len]                 hold a RetroPad button from frame F (default 6 frames)
//   --stick=F:x,y[:len]                   hold the left stick at x,y (-1..1) from frame F (headless)
//   --opt=key=value                       a core option (vita3k_...) for this run
//   --wav=<file.wav>                      dump the audio
//   --userdata=<dir>                      save folder (default userdata next to the program)
//   --setopt=F:key=value                  change a core option at frame F (headless; tests)
//   --reset=F                             restart the game at frame F (headless; tests)
//   --from-launcher                       started by tempest.exe (the menu's last item says "Return to launcher")
//   --menu[=all]                          open the settings menu at start (all sections open; tests)
//   --winshot=N:<file.bmp>                save the window after N presents, then quit (tests)
// Settings (userdata\txk_settings.txt): fullscreen, vsync, and any vita3k_* key is handed to the core as an option.
// Firmware: vita_firmware\PSVUPDAT.PUP (system) and PSP2UPDAT.PUP (fonts) are linked into system\vita3k\ as
// firmware.pup / font.pup, the names the core tells the parts apart by.
// Pad (Xbox, remappable in the menu): A = Cross, B = Circle, X = Square, Y = Triangle, LB / RB = L / R, LT = Select,
// Start = Start; fixed: d-pad, sticks, Back = settings menu (the game pauses while it is open).
// Keys: arrows, Z = Cross, X = Circle, A = Square, S = Triangle, Q / W = L / R, Enter = Start, Tab = Select,
// F11 fullscreen, Esc = settings menu.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <windows.h>
#undef min
#undef max

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "controls.h"
#include "menu.h"
#include "settings.h"
#include "txk_menu.h"
#include "vita_host.h"

namespace fs = std::filesystem;

namespace {

const char* kDefaults = R"(# TxK settings.

# 1 = start fullscreen (F11 toggles and saves it).
fullscreen = 1
# 1 = wait for the display when presenting (no tearing).
vsync = 1
# A system message (OK button only) whose text contains this is answered OK without being shown. Offline, TxK
# always shows "Network features are disabled" at start, and it wants a tap on the touch screen. Empty = show it.
auto_ok_dialog = Network features are disabled

# --- Core options (Vita3K): any vita3k_* key here is handed to the core; missing ones use the core's defaults ---
# Internal resolution: 1 = the Vita's 960x544, 2 = 1920x1088, 3 = 2880x1632, 4 = 3840x2176 (applies when the game
# (re)starts).
vita3k_resolution_multiplier = 1
# Anisotropic texture filtering: 1 (off), 2, 4, 8, 16.
vita3k_anisotropic_filtering = 1

# Picture scaling to the window: smooth (bilinear) or sharp (nearest pixel).
picture_filter = smooth
)";

struct Shot { int frame; std::string path; };
struct Press { int frame; int btn; int len; };
struct StickHold { int frame; float x, y; int len; };

int BtnByName(const std::string& n) {
  static const char* names[] = {"b", "y", "select", "start", "up", "down", "left", "right",
                                "a", "x", "l", "r", "l2", "r2", "l3", "r3"};
  for (int i = 0; i < 16; ++i)
    if (n == names[i]) return i;
  return -1;
}

bool WriteBmp(const std::string& path, const vita::Frame& f) {
  if (f.width == 0) return false;
  std::ofstream o(path, std::ios::binary);
  if (!o) return false;
  uint32_t w = f.width, h = f.height, size = 54 + w * h * 4;
  uint8_t hdr[54] = {'B', 'M'};
  auto put32 = [&](int at, uint32_t v) { std::memcpy(hdr + at, &v, 4); };
  put32(2, size); put32(10, 54); put32(14, 40); put32(18, w); put32(22, uint32_t(-int32_t(h)));
  hdr[26] = 1; hdr[28] = 32;
  o.write(reinterpret_cast<char*>(hdr), 54);
  o.write(reinterpret_cast<const char*>(f.pixels.data()), std::streamsize(w) * h * 4);
  return true;
}

bool WriteWav(const std::string& path, const std::vector<int16_t>& s, uint32_t rate) {
  std::ofstream o(path, std::ios::binary);
  if (!o) return false;
  uint32_t data = uint32_t(s.size() * 2), v;
  uint16_t v16;
  o.write("RIFF", 4); v = 36 + data; o.write((char*)&v, 4); o.write("WAVEfmt ", 8);
  v = 16; o.write((char*)&v, 4); v16 = 1; o.write((char*)&v16, 2); v16 = 2; o.write((char*)&v16, 2);
  o.write((char*)&rate, 4); v = rate * 4; o.write((char*)&v, 4); v16 = 4; o.write((char*)&v16, 2);
  v16 = 16; o.write((char*)&v16, 2); o.write("data", 4); o.write((char*)&data, 4);
  o.write(reinterpret_cast<const char*>(s.data()), data);
  return true;
}

// The game: a folder holding eboot.bin and sce_sys\param.sfo (TxK: PCSB00494).
std::string FindGame(const std::string& given) {
  if (!given.empty()) return given;
  const fs::path base(SDL_GetBasePath());
  for (const fs::path& d : {base, base / "game", base.parent_path().parent_path()}) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(d, ec)) {
      if (e.is_directory() && fs::exists(e.path() / "eboot.bin") && fs::exists(e.path() / "sce_sys" / "param.sfo") &&
          e.path().filename().string().rfind("PCS", 0) == 0)
        return e.path().string();
    }
  }
  return {};
}

// The firmware .pup files into system\vita3k\ under the names the core reads them by (hard links: no second copy).
void PlaceFirmware(const fs::path& vita_dir) {
  const fs::path base(SDL_GetBasePath());
  fs::path src;
  for (const fs::path& d : {base / "vita_firmware", base.parent_path().parent_path() / "vita_firmware"})
    if (fs::exists(d / "PSVUPDAT.PUP")) { src = d; break; }
  if (src.empty()) return;
  fs::create_directories(vita_dir);
  const std::pair<const char*, const char*> parts[] = {{"PSVUPDAT.PUP", "firmware.pup"}, {"PSP2UPDAT.PUP", "font.pup"}};
  for (auto [from, to] : parts) {
    std::error_code ec;
    if (!fs::exists(src / from) || fs::exists(vita_dir / to)) continue;
    fs::create_hard_link(src / from, vita_dir / to, ec);
    if (ec) fs::copy_file(src / from, vita_dir / to, ec);
    std::fprintf(stderr, "firmware: %s -> %s%s\n", from, (vita_dir / to).string().c_str(), ec ? " (failed)" : "");
  }
}

// Game buttons: the functions bound in the settings menu (txk_menu.cpp). Fixed: the d-pad (and the sticks, passed
// as they are), Back = settings menu, the keyboard.
uint16_t ReadPad(SDL_Gamepad* pad, const bool* keys) {
  uint16_t b = txk_menu::VitaButtons(controls::Held(pad));
  auto set = [&](int bit, bool on) { if (on) b |= uint16_t(1u << bit); };
  if (pad) {
    auto btn = [&](SDL_GamepadButton x) { return SDL_GetGamepadButton(pad, x); };
    set(vita::UP, btn(SDL_GAMEPAD_BUTTON_DPAD_UP));
    set(vita::DOWN, btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    set(vita::LEFT, btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
    set(vita::RIGHT, btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
  }
  if (keys) {
    set(vita::UP, keys[SDL_SCANCODE_UP]);
    set(vita::DOWN, keys[SDL_SCANCODE_DOWN]);
    set(vita::LEFT, keys[SDL_SCANCODE_LEFT]);
    set(vita::RIGHT, keys[SDL_SCANCODE_RIGHT]);
    set(vita::B, keys[SDL_SCANCODE_Z]);
    set(vita::A, keys[SDL_SCANCODE_X]);
    set(vita::Y, keys[SDL_SCANCODE_A]);
    set(vita::X, keys[SDL_SCANCODE_S]);
    set(vita::L, keys[SDL_SCANCODE_Q]);
    set(vita::R, keys[SDL_SCANCODE_W]);
    set(vita::START, keys[SDL_SCANCODE_RETURN]);
    set(vita::SELECT, keys[SDL_SCANCODE_TAB]);
  }
  return b;
}

}  // namespace

int main(int argc, char** argv) {
  std::string game, wav, userdata, winshot_file;
  bool headless = false, from_launcher = false;
  int frames = -1, menu_test = 0, winshot_at = -1;
  struct SetOpt { int frame; std::string key, value; };
  std::vector<SetOpt> setopts;
  std::vector<int> resets;
  std::vector<Shot> shots;
  std::vector<Press> presses;
  std::vector<StickHold> sticks;
  std::vector<std::pair<std::string, std::string>> opts;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&](const char* p) { return a.substr(std::strlen(p)); };
    if (a == "--headless") headless = true;
    else if (a == "--from-launcher") from_launcher = true;
    else if (a == "--menu" || a == "--menu=all") menu_test = a == "--menu" ? 1 : 2;
    else if (a.rfind("--winshot=", 0) == 0) {
      const std::string v = val("--winshot=");
      winshot_at = std::atoi(v.substr(0, v.find(':')).c_str());
      winshot_file = v.substr(v.find(':') + 1);
    } else if (a.rfind("--reset=", 0) == 0) resets.push_back(std::atoi(val("--reset=").c_str()));
    else if (a.rfind("--setopt=", 0) == 0) {
      const std::string v = val("--setopt=");
      const size_t c = v.find(':'), eq = v.find('=', c);
      if (c != std::string::npos && eq != std::string::npos)
        setopts.push_back({std::atoi(v.substr(0, c).c_str()), v.substr(c + 1, eq - c - 1), v.substr(eq + 1)});
    }
    else if (a.rfind("--frames=", 0) == 0) frames = std::atoi(val("--frames=").c_str());
    else if (a.rfind("--wav=", 0) == 0) wav = val("--wav=");
    else if (a.rfind("--userdata=", 0) == 0) userdata = val("--userdata=");
    else if (a.rfind("--opt=", 0) == 0) {
      const std::string v = val("--opt=");
      const size_t eq = v.find('=');
      if (eq != std::string::npos) opts.push_back({v.substr(0, eq), v.substr(eq + 1)});
    } else if (a.rfind("--shot=", 0) == 0) {
      auto v = val("--shot=");
      shots.push_back({std::atoi(v.substr(0, v.find(':')).c_str()), v.substr(v.find(':') + 1)});
    } else if (a.rfind("--stick=", 0) == 0) {
      StickHold h{0, 0.f, 0.f, 6};
      std::sscanf(val("--stick=").c_str(), "%d:%f,%f:%d", &h.frame, &h.x, &h.y, &h.len);
      sticks.push_back(h);
    } else if (a.rfind("--press=", 0) == 0) {
      auto v = val("--press=");
      auto c1 = v.find(':'), c2 = v.find(':', c1 + 1);
      int btn = BtnByName(v.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1));
      if (btn < 0) { std::fprintf(stderr, "unknown button in %s\n", a.c_str()); return 1; }
      presses.push_back({std::atoi(v.substr(0, c1).c_str()), btn,
                         c2 == std::string::npos ? 6 : std::atoi(v.substr(c2 + 1).c_str())});
    } else if (a.rfind("--", 0) == 0) { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
    else game = a;
  }
  if (!game.empty()) game = fs::absolute(game).string();
  if (!wav.empty()) wav = fs::absolute(wav).string();
  for (auto& s : shots) s.path = fs::absolute(s.path).string();
  if (!winshot_file.empty()) winshot_file = fs::absolute(winshot_file).string();

  if (!SDL_Init(SDL_INIT_VIDEO | (headless ? 0 : SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  const fs::path base(SDL_GetBasePath());
  const fs::path udir = userdata.empty() ? base / "userdata" : fs::absolute(userdata);
  fs::create_directories(udir);
  settings::SetDefaultText(kDefaults);
  settings::Load((udir / "txk_settings.txt").string());
  txk_menu::Install(from_launcher);
  game = FindGame(game);
  if (game.empty()) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TxK", "Put your TxK game folder (PCSB00494) next to the program.",
                             nullptr);
    return 1;
  }
  const fs::path system_dir = base / "system";
  PlaceFirmware(system_dir / "vita3k");
  // Core options: vita3k_* keys from the settings file, then --opt.
  {
    std::ifstream f(udir / "txk_settings.txt");
    std::string line;
    while (std::getline(f, line)) {
      line = line.substr(0, line.find('#'));
      auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string k = line.substr(0, eq);
      k.erase(0, k.find_first_not_of(" \t"));
      k.erase(k.find_last_not_of(" \t\r") + 1);
      if (k.rfind("vita3k_", 0) == 0) vita::SetOption(k, settings::Str(k, ""));
    }
  }
  for (auto& [k, v] : opts) vita::SetOption(k, v);
  // (read by the core's sceMsgDialogInit, our branch llamasoft-vita). The process environment, before the DLL loads:
  // the core has its own C runtime, which copies the environment when it starts (_putenv would only reach ours).
  SetEnvironmentVariableA("VITA3K_AUTO_OK_DIALOG", settings::Str("auto_ok_dialog", "").c_str());

  std::string err;
  if (!vita::Load((base / "vita3k_libretro.dll").string(), game, system_dir.string(), udir.string(), &err)) {
    std::fprintf(stderr, "load: %s\n", err.c_str());
    if (!headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TxK", err.c_str(), nullptr);
    return 1;
  }
  std::fprintf(stderr, "loaded %s: %.3f fps, %.0f Hz audio\n", game.c_str(), vita::Fps(), vita::SampleRate());

  auto scripted = [&](int f) {
    uint16_t b = 0;
    for (auto& p : presses)
      if (f >= p.frame && f < p.frame + p.len) b |= uint16_t(1u << p.btn);
    return b;
  };
  std::vector<int16_t> wav_samples;
  if (headless) {
    if (frames < 0) frames = 1200;
    // Paced to real time: the emulator runs on its own threads (and prepares the title on one), so frames handed out
    // faster than 60 a second would only skip ahead of it.
    const uint64_t t0 = SDL_GetTicksNS();
    const double frame_ns = 1e9 / vita::Fps();
    for (int f = 0; f < frames; ++f) {
      const uint64_t due = t0 + uint64_t(f * frame_ns);
      if (const uint64_t now = SDL_GetTicksNS(); now < due) SDL_DelayPrecise(due - now);
      vita::SetButtons(scripted(f));
      float sx = 0, sy = 0;
      for (auto& h : sticks)
        if (f >= h.frame && f < h.frame + h.len) { sx = h.x; sy = h.y; }
      vita::SetSticks(int(sx * 32767), int(sy * 32767), 0, 0);
      for (auto& o : setopts)
        if (o.frame == f) vita::SetOption(o.key, o.value);
      for (int r : resets)
        if (r == f) vita::Reset();
      vita::RunFrame();
      auto& au = vita::Audio();
      if (!wav.empty()) wav_samples.insert(wav_samples.end(), au.begin(), au.end());
      au.clear();
      for (auto& s : shots)
        if (s.frame == f && !WriteBmp(s.path, vita::LastFrame())) std::fprintf(stderr, "shot %s failed\n", s.path.c_str());
      if ((f + 1) % 600 == 0) {
        static uint64_t last_new = 0;
        std::fprintf(stderr, "frame %d: %.1f s, picture %ux%u, %llu new pictures in the last 600 frames\n", f + 1,
                     (SDL_GetTicksNS() - t0) / 1e9, vita::LastFrame().width, vita::LastFrame().height,
                     (unsigned long long)(vita::NewFrames() - last_new));
        last_new = vita::NewFrames();
      }
    }
    if (!wav.empty()) WriteWav(wav, wav_samples, uint32_t(vita::SampleRate()));
    vita::Unload();
    SDL_Quit();
    return 0;
  }

  SDL_Window* win = SDL_CreateWindow("TxK", 1440, 816, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Renderer* ren = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
  if (!ren) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
  if (settings::Num("fullscreen", 1) != 0) SDL_SetWindowFullscreen(win, true);
  SDL_SetRenderVSync(ren, settings::Num("vsync", 1) != 0 ? 1 : 0);
  SDL_HideCursor();
  SDL_Texture* tex = nullptr;
  unsigned tw = 0, th = 0;
  SDL_AudioSpec spec{SDL_AUDIO_S16, 2, int(vita::SampleRate())};
  SDL_AudioStream* audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (audio) SDL_ResumeAudioStreamDevice(audio);
  SDL_Gamepad* pad = nullptr;
  bool running = true;
  const double frame_ns = 1e9 / vita::Fps();
  uint64_t next = SDL_GetTicksNS();
  int frame = 0, presents = 0;
  bool prev_back = false;
  uint32_t prev_held = 0;
  auto set_fullscreen = [&](bool on) {
    SDL_SetWindowFullscreen(win, on);
    settings::Save("fullscreen", on ? "1" : "0");
  };
  // Core options the menu changes: handed to the core as they change (some apply when the game restarts).
  auto apply_core_options = [&] {
    for (const char* k : {"vita3k_resolution_multiplier", "vita3k_anisotropic_filtering"}) {
      const std::string v = settings::Str(k, "");
      if (!v.empty()) vita::SetOption(k, v);
    }
  };
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running = false;
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE && !menu::IsOpen()) menu::Open();
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F11)
        set_fullscreen(!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
      else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) SDL_HideCursor();
      else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) SDL_ShowCursor();
      else if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
      else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && SDL_GetGamepadID(pad) == e.gdevice.which) {
        SDL_CloseGamepad(pad);
        pad = nullptr;
      }
    }
    const uint64_t now = SDL_GetTicksNS();
    // Settings menu (Back / Esc): no frames run while it is open (the game waits for its next vblank).
    {
      const bool back = pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
      const uint32_t held = controls::Held(pad), pressed = held & ~prev_held;
      prev_held = held;
      if (!menu::IsOpen() && back && !prev_back) menu::Open();
      prev_back = back;
      if (menu::IsOpen()) {
        menu::Nav nav = menu::ReadNav(pad, SDL_GetKeyboardState(nullptr));
        for (int b = 0; b < controls::kButtonCount; ++b)
          if (pressed >> b & 1) { nav.new_button = b; break; }
        const std::string res_before = settings::Str("vita3k_resolution_multiplier", "1");
        switch (menu::Update(nav, now)) {
          case menu::kQuit: running = false; break;
          case menu::kToggleFullscreen: set_fullscreen(!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)); break;
          case txk_menu::kRestart:
            vita::Reset();
            menu::Toast("Restarting the game", now);
            break;
          default: break;
        }
        apply_core_options();
        if (settings::Str("vita3k_resolution_multiplier", "1") != res_before)
          menu::Toast("Resolution: restart the game to apply", now);
        next = now;   // paused: no catching up afterwards
      }
    }
    int todo = 0;
    if (!menu::IsOpen()) {
      while (now >= next && todo < 4) { next += uint64_t(frame_ns); ++todo; }
      if (now > next + uint64_t(frame_ns * 8)) next = now;
    }
    for (int k = 0; k < todo; ++k) {
      vita::SetButtons(ReadPad(pad, SDL_GetKeyboardState(nullptr)) | scripted(frame));
      if (pad)
        vita::SetSticks(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY),
                        SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX), SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
      vita::RunFrame();
      ++frame;
      auto& au = vita::Audio();
      if (audio && !au.empty()) {
        if (SDL_GetAudioStreamQueued(audio) > int(vita::SampleRate()) * 4 / 10) SDL_ClearAudioStream(audio);
        SDL_PutAudioStreamData(audio, au.data(), int(au.size() * 2));
      }
      au.clear();
      for (auto& s : shots)
        if (s.frame == frame - 1) WriteBmp(s.path, vita::LastFrame());
      if (frames >= 0 && frame >= frames) running = false;
    }
    const vita::Frame& f = vita::LastFrame();
    if (f.width) {
      if (!tex || tw != f.width || th != f.height) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, int(f.width), int(f.height));
        tw = f.width;
        th = f.height;
      }
      SDL_UpdateTexture(tex, nullptr, f.pixels.data(), int(f.width * 4));
      SDL_SetTextureScaleMode(tex, settings::Str("picture_filter", "smooth") == "sharp" ? SDL_SCALEMODE_NEAREST
                                                                                         : SDL_SCALEMODE_LINEAR);
    }
    int ww = 0, wh = 0;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (tex) {   // the Vita's 960:544, as big as fits
      float h = float(wh), w = h * 960.f / 544.f;
      if (w > ww) { w = float(ww); h = w * 544.f / 960.f; }
      SDL_FRect dst{(ww - w) / 2, (wh - h) / 2, w, h};
      SDL_RenderTexture(ren, tex, nullptr, &dst);
    }
    if (menu::Shown()) menu::Draw(ren, ww, wh);
    menu::DrawToast(ren, ww, wh, SDL_GetTicksNS());
    if (menu_test && presents == 0) menu::Open(menu_test == 2);
    if (winshot_at >= 0 && presents == winshot_at) {
      if (SDL_Surface* sf = SDL_RenderReadPixels(ren, nullptr)) {
        if (!SDL_SaveBMP(sf, winshot_file.c_str())) std::fprintf(stderr, "winshot: %s\n", SDL_GetError());
        SDL_DestroySurface(sf);
      }
      running = false;
    }
    ++presents;
    SDL_RenderPresent(ren);
  }
  if (audio) SDL_DestroyAudioStream(audio);
  if (tex) SDL_DestroyTexture(tex);
  vita::Unload();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
