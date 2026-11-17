// t81_main.cpp - Tempest (Atari, 1981 arcade) on PC: SDL3 front end for MAME's libretro core, built with the Tempest
// driver only (tools/build_mame.sh -> tempest_libretro.dll).
//
//   t81.exe [romset.zip]                  play in a window (romset: tempest.zip next to the program, in game\, or two
//                                         folders up - the repo root for build\t81.exe)
//   --headless --frames=N                 run N frames without a window (default 1200; as fast as the PC can)
//   --shot=F:<file.bmp>                   save the picture after frame F (repeatable)
//   --press=F:<btn>[:len]                 hold a RetroPad button from frame F (default 6 frames; b fire, a zap,
//                                         select coin, start start)
//   --spin=F:x[:len]                      hold the spinner stick at x (-1..1) from frame F (headless)
//   --opt=key=value                       a core option (mame_...) for this run
//   --autoplay=F                          from frame F: fire pulses and spinner sweeps (tests)
//   --set=key=value                       a setting for this run (not saved; e.g. framing=fill)
//   --winsize=WxH                         a window of this size, not fullscreen (tests)
//   --linedump=F0:F1:<file.csv>           (headless) the core's vector lines per frame (T81_LINEDUMP_STEP=n)
//   --savestate=F:<file> / --loadstate=F:<file>   (headless; tests)
//   --wav=<file.wav>                      dump the audio
//   --userdata=<dir>                      save folder (default userdata next to the program)
//   --from-launcher                       started by tempest.exe (the menu's last item says "Return to launcher")
//   --menu[=all]                          open the settings menu at start (all sections open; tests)
//   --winshot=N:<file.bmp>                save the window after N presents, then quit (tests)
//   --frameshot=F:<file.bmp>              save the window after game frame F (repeatable; quits after the last)
// Settings: userdata\t81_settings.txt (fullscreen, vsync, spinner_speed, picture_filter; mame_* keys go to the core).
// Pad (remappable in the menu): A / RT fire, B / RB / X Superzapper, Start = start, Y = coin; fixed: left stick and
// d-pad left / right = the spinner, Back = settings menu (the game pauses while it is open).
// Keys: Left / Right spin, Z fire, X Superzapper, 5 coin, 1 start, F11 fullscreen, Esc = settings menu.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "controls.h"
#include "lr_host.h"
#include "menu.h"
#include "settings.h"
#include "t81_menu.h"
#include "t81_vectors.h"

namespace fs = std::filesystem;

namespace {

const char* kDefaults = R"(# Tempest (1981 arcade) settings.

# 1 = start fullscreen (F11 toggles and saves it).
fullscreen = 1
# 1 = wait for the display when presenting (no tearing).
vsync = 1
# Spinner (left stick / d-pad): how fast the claw goes round at full push.
spinner_speed = 1.0
# Picture scaling to the window: smooth (bilinear) or sharp (nearest pixel). Only for MAME's picture.
picture_filter = smooth

# --- Vectors ---
# 1 = t81 draws the vectors itself (beams with glow, sharp at any size); 0 = MAME's picture.
vector_renderer = 1
# arcade = the whole 3:4 monitor; fill = the part the game uses; wide = fill, and in play the web fills the
# height with the score / lives and the high score in the top corners.
framing = wide
# arcade = the cabinet's 3:4 tube (webs a little tall); round = the game's own square units (round webs).
aspect = arcade
# Beam: width in pixels at 1080 lines, glow strength (0 = none) and size (x the width), brightness, and how much the
# width follows the beam's intensity (0 = all the same).
line_width = 2.5
glow = 1.0
glow_size = 6
brightness = 1.2
width_by_intensity = 1
# How white the beam centre runs (0 = the pure colour).
hot_core = 0.2

# --- Core options (MAME): any mame_* key here is handed to the core ---
# MAME rotates the vertical monitor itself, draws the vectors at this resolution (sharp lines at full screen size),
# and reads the spinner from relative mouse movement (which the stick drives).
mame_rotation_mode = internal
mame_alternate_renderer = enabled
mame_altres = 1920x1440
mame_mouse_enable = enabled
)";

struct Shot { int frame; std::string path; };
struct Press { int frame; int btn; int len; };
struct Spin { int frame; float x; int len; };

int BtnByName(const std::string& n) {
  static const char* names[] = {"b", "y", "select", "start", "up", "down", "left", "right",
                                "a", "x", "l", "r", "l2", "r2", "l3", "r3"};
  for (int i = 0; i < 16; ++i)
    if (n == names[i]) return i;
  return -1;
}

bool WriteBmp(const std::string& path, const lr::Frame& f) {
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

// The romset: tempest.zip (MAME's current set: parent rev 3 plus the clones).
std::string FindRomset(const std::string& given) {
  if (!given.empty()) return given;
  const fs::path base(SDL_GetBasePath());
  for (const fs::path& d : {base, base / "game", base.parent_path().parent_path()})
    if (fs::exists(d / "tempest.zip")) return (d / "tempest.zip").string();
  return {};
}

// The spinner: relative movement a frame from the stick (with a curve, so small pushes are fine control) and the
// d-pad / arrow keys (full speed). 4.8 units a frame at speed 1 and full push (the owner's pick: the first default
// of 12 was far too quick). The fraction carries over between frames, so slow turns still move.
int SpinDelta(float stick_x, bool left, bool right) {
  static double carry = 0;
  float x = std::fabs(stick_x) < 0.15f ? 0.f : stick_x;
  if (x != 0.f) x = (x > 0 ? 1.f : -1.f) * std::pow((std::fabs(x) - 0.15f) / 0.85f, 1.5f);
  if (left) x = -1.f;
  if (right) x = 1.f;
  if (x == 0.f) { carry = 0; return 0; }
  const double want = x * 4.8 * settings::Num("spinner_speed", 1.0) + carry;
  const int d = int(want);   // toward zero; the rest waits for the next frame
  carry = want - d;
  return d;
}

uint16_t ReadPad(SDL_Gamepad* pad, const bool* keys) {
  uint16_t b = t81_menu::PadButtons(controls::Held(pad));
  auto set = [&](int bit, bool on) { if (on) b |= uint16_t(1u << bit); };
  if (keys) {
    set(lr::B, keys[SDL_SCANCODE_Z]);
    set(lr::A, keys[SDL_SCANCODE_X]);
    set(lr::SELECT, keys[SDL_SCANCODE_5]);
    set(lr::START, keys[SDL_SCANCODE_1]);
  }
  return b;
}

fs::path StatePath(const fs::path& dir) {
  return dir / "states" / ("t81_slot" + std::to_string(int(settings::Num("state_slot", 1))) + ".state");
}

std::string SlotInfo(const fs::path& dir) {
  std::error_code ec;
  auto p = StatePath(dir);
  std::string slot = "slot " + std::to_string(int(settings::Num("state_slot", 1)));
  if (!fs::exists(p, ec)) return slot + ": empty";
  auto t = std::chrono::clock_cast<std::chrono::system_clock>(fs::last_write_time(p, ec));
  std::time_t tt = std::chrono::system_clock::to_time_t(t);
  char buf[64];
  std::strftime(buf, sizeof buf, "%d %b %H:%M", std::localtime(&tt));
  return slot + ": " + buf;
}

bool SaveStateFile(const fs::path& path) {
  std::vector<uint8_t> st;
  if (!lr::SaveState(st)) return false;
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream o(path, std::ios::binary);
  o.write(reinterpret_cast<const char*>(st.data()), std::streamsize(st.size()));
  return bool(o);
}

bool LoadStateFile(const fs::path& path) {
  std::ifstream f(path, std::ios::binary);
  std::vector<uint8_t> st((std::istreambuf_iterator<char>(f)), {});
  return lr::LoadState(st);
}

}  // namespace

int main(int argc, char** argv) {
  std::string romset, wav, userdata, winshot_file;
  bool headless = false, from_launcher = false;
  int frames = -1, menu_test = 0, winshot_at = -1, win_w = 0, win_h = 0;
  std::vector<Shot> shots, savestates, loadstates, frameshots;
  std::vector<Press> presses;
  std::vector<Spin> spins;
  std::vector<std::pair<std::string, std::string>> opts, sets;
  int linedump0 = -1, linedump1 = -1, linedump_step = 1, autoplay = -1;
  std::string linedump;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&](const char* p) { return a.substr(std::strlen(p)); };
    auto frame_file = [&](const char* p) {
      const std::string v = val(p);
      return Shot{std::atoi(v.substr(0, v.find(':')).c_str()), v.substr(v.find(':') + 1)};
    };
    if (a == "--headless") headless = true;
    else if (a == "--from-launcher") from_launcher = true;
    else if (a == "--menu" || a == "--menu=all") menu_test = a == "--menu" ? 1 : 2;
    else if (a.rfind("--frames=", 0) == 0) frames = std::atoi(val("--frames=").c_str());
    else if (a.rfind("--wav=", 0) == 0) wav = val("--wav=");
    else if (a.rfind("--userdata=", 0) == 0) userdata = val("--userdata=");
    else if (a.rfind("--winsize=", 0) == 0) std::sscanf(val("--winsize=").c_str(), "%dx%d", &win_w, &win_h);
    else if (a.rfind("--frameshot=", 0) == 0) frameshots.push_back(frame_file("--frameshot="));
    else if (a.rfind("--winshot=", 0) == 0) { auto s = frame_file("--winshot="); winshot_at = s.frame; winshot_file = s.path; }
    else if (a.rfind("--shot=", 0) == 0) shots.push_back(frame_file("--shot="));
    else if (a.rfind("--savestate=", 0) == 0) savestates.push_back(frame_file("--savestate="));
    else if (a.rfind("--loadstate=", 0) == 0) loadstates.push_back(frame_file("--loadstate="));
    else if (a.rfind("--linedump=", 0) == 0) {
      char path[512] = {};
      std::sscanf(val("--linedump=").c_str(), "%d:%d:%511s", &linedump0, &linedump1, path);
      if (const char* st = std::getenv("T81_LINEDUMP_STEP")) linedump_step = std::max(1, std::atoi(st));
      linedump = fs::absolute(path).string();
    } else if (a.rfind("--opt=", 0) == 0) {
      const std::string v = val("--opt=");
      const size_t eq = v.find('=');
      if (eq != std::string::npos) opts.push_back({v.substr(0, eq), v.substr(eq + 1)});
    } else if (a.rfind("--autoplay=", 0) == 0) {
      autoplay = std::atoi(val("--autoplay=").c_str());
    } else if (a.rfind("--set=", 0) == 0) {
      const std::string v = val("--set=");
      const size_t eq = v.find('=');
      if (eq != std::string::npos) sets.push_back({v.substr(0, eq), v.substr(eq + 1)});
    } else if (a.rfind("--spin=", 0) == 0) {
      Spin s{0, 0.f, 6};
      std::sscanf(val("--spin=").c_str(), "%d:%f:%d", &s.frame, &s.x, &s.len);
      spins.push_back(s);
    } else if (a.rfind("--press=", 0) == 0) {
      auto v = val("--press=");
      auto c1 = v.find(':'), c2 = v.find(':', c1 + 1);
      int btn = BtnByName(v.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1));
      if (btn < 0) { std::fprintf(stderr, "unknown button in %s\n", a.c_str()); return 1; }
      presses.push_back({std::atoi(v.substr(0, c1).c_str()), btn,
                         c2 == std::string::npos ? 6 : std::atoi(v.substr(c2 + 1).c_str())});
    } else if (a.rfind("--", 0) == 0) { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
    else romset = a;
  }
  if (!romset.empty()) romset = fs::absolute(romset).string();
  if (!wav.empty()) wav = fs::absolute(wav).string();
  if (!winshot_file.empty()) winshot_file = fs::absolute(winshot_file).string();
  for (auto* v : {&shots, &savestates, &loadstates, &frameshots})
    for (auto& s : *v) s.path = fs::absolute(s.path).string();

  if (!SDL_Init(SDL_INIT_VIDEO | (headless ? 0 : SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  const fs::path base(SDL_GetBasePath());
  const fs::path udir = userdata.empty() ? base / "userdata" : fs::absolute(userdata);
  fs::create_directories(udir);
  settings::SetDefaultText(kDefaults);
  settings::Load((udir / "t81_settings.txt").string());
  for (auto& [k, v] : sets) settings::Set(k, v);
  t81_menu::Install(from_launcher);
  menu::SetSlotInfo([&] { return SlotInfo(udir); });
  romset = FindRomset(romset);
  if (romset.empty()) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tempest", "Put the MAME romset tempest.zip next to the program.",
                             nullptr);
    return 1;
  }
  // Core options: the mame_* keys of the settings (defaults included), then --opt.
  for (const char* k : {"mame_rotation_mode", "mame_alternate_renderer", "mame_altres", "mame_mouse_enable"})
    lr::SetOption(k, settings::Str(k, ""));
  {
    std::ifstream f(udir / "t81_settings.txt");
    std::string line;
    while (std::getline(f, line)) {
      line = line.substr(0, line.find('#'));
      auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string k = line.substr(0, eq);
      k.erase(0, k.find_first_not_of(" \t"));
      k.erase(k.find_last_not_of(" \t\r") + 1);
      if (k.rfind("mame_", 0) == 0) lr::SetOption(k, settings::Str(k, ""));
    }
  }
  // With our beams MAME's picture is only a fallback: let it rasterise small (CPU time on the Deck).
  auto altres = [] {
    return settings::Num("vector_renderer", 1) != 0 ? std::string("640x480") : settings::Str("mame_altres", "1920x1440");
  };
  lr::SetOption("mame_altres", altres());
  for (auto& [k, v] : opts) lr::SetOption(k, v);

  std::string err;
  const fs::path system_dir = base / "system";
  fs::create_directories(system_dir);
  if (!lr::Load((base / "tempest_libretro.dll").string(), romset, system_dir.string(), udir.string(), &err)) {
    std::fprintf(stderr, "load: %s\n", err.c_str());
    if (!headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tempest", err.c_str(), nullptr);
    return 1;
  }
  std::fprintf(stderr, "loaded %s: %.3f fps, %.0f Hz audio, aspect %.3f, rotation %u\n", romset.c_str(), lr::Fps(),
               lr::SampleRate(), lr::AspectRatio(), lr::Rotation());

  // Our own vector drawing needs the core's line export (third_party/mame-libretro, branch llamasoft-mame).
  auto lines_fn = reinterpret_cast<t81_vectors::LinesFn>(lr::CoreProc("llama_mame_lines"));
  if (!lines_fn) lr::SetOption("mame_altres", settings::Str("mame_altres", "1920x1440"));
  auto scripted = [&](int f) {
    uint16_t b = 0;
    if (autoplay >= 0 && f >= autoplay && f % 10 < 5) b |= uint16_t(1u << lr::B);   // fire pulses
    for (auto& p : presses)
      if (f >= p.frame && f < p.frame + p.len) b |= uint16_t(1u << p.btn);
    return b;
  };
  std::vector<int16_t> wav_samples;
  if (headless) {
    if (frames < 0) frames = 1200;
    const uint64_t t0 = SDL_GetTicksNS();
    FILE* dump = linedump.empty() ? nullptr : std::fopen(linedump.c_str(), "w");
    if (dump) std::fprintf(dump, "frame,i,x0,y0,x1,y1,width,a,r,g,b\n");
    for (int f = 0; f < frames; ++f) {
      lr::SetButtons(scripted(f));
      float sx = 0;
      for (auto& s : spins)
        if (f >= s.frame && f < s.frame + s.len) sx = s.x;
      if (autoplay >= 0 && f >= autoplay) sx = (f / 90) % 2 ? 0.7f : -0.7f;   // sweep the rim both ways
      lr::SetMouse(SpinDelta(sx, false, false), 0);
      for (auto& s : loadstates)
        if (s.frame == f && !LoadStateFile(s.path)) std::fprintf(stderr, "loadstate %s failed\n", s.path.c_str());
      lr::RunFrame();
      auto& au = lr::Audio();
      if (!wav.empty()) wav_samples.insert(wav_samples.end(), au.begin(), au.end());
      au.clear();
      if (dump && lines_fn && f >= linedump0 && f <= linedump1 && (f - linedump0) % linedump_step == 0) {
        int n = 0;
        float tw = 0, th = 0;
        const float* l = lines_fn(&n, &tw, &th);
        for (int i = 0; i < n; ++i, l += 9)
          std::fprintf(dump, "%d,%d,%.5f,%.5f,%.5f,%.5f,%.2f,%.3f,%.3f,%.3f,%.3f\n", f, i, l[0], l[1], l[2], l[3], l[4],
                       l[5], l[6], l[7], l[8]);
      }
      for (auto& s : savestates)
        if (s.frame == f && !SaveStateFile(s.path)) std::fprintf(stderr, "savestate %s failed\n", s.path.c_str());
      for (auto& s : shots)
        if (s.frame == f && !WriteBmp(s.path, lr::LastFrame())) std::fprintf(stderr, "shot %s failed\n", s.path.c_str());
      if ((f + 1) % 600 == 0)
        std::fprintf(stderr, "frame %d: %.1f s, picture %ux%u\n", f + 1, (SDL_GetTicksNS() - t0) / 1e9,
                     lr::LastFrame().width, lr::LastFrame().height);
    }
    if (dump) std::fclose(dump);
    if (!wav.empty()) WriteWav(wav, wav_samples, uint32_t(lr::SampleRate()));
    lr::Unload();
    SDL_Quit();
    return 0;
  }

  SDL_Window* win = SDL_CreateWindow("Tempest", win_w > 0 ? win_w : 1080, win_h > 0 ? win_h : 1080,
                                     SDL_WINDOW_RESIZABLE | (win_w > 0 ? 0 : SDL_WINDOW_HIGH_PIXEL_DENSITY));
  SDL_Renderer* ren = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
  if (!ren) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
  if (settings::Num("fullscreen", 1) != 0 && win_w <= 0) SDL_SetWindowFullscreen(win, true);
  SDL_SetRenderVSync(ren, settings::Num("vsync", 1) != 0 ? 1 : 0);
  SDL_HideCursor();
  SDL_Texture* tex = nullptr;
  unsigned tw = 0, th = 0;
  SDL_AudioSpec spec{SDL_AUDIO_S16, 2, int(lr::SampleRate())};
  SDL_AudioStream* audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (audio) SDL_ResumeAudioStreamDevice(audio);
  SDL_Gamepad* pad = nullptr;
  bool running = true;
  const double frame_ns = 1e9 / lr::Fps();
  uint64_t next = SDL_GetTicksNS();
  int frame = 0, presents = 0;
  bool prev_back = false;
  uint32_t prev_held = 0;
  auto set_fullscreen = [&](bool on) {
    SDL_SetWindowFullscreen(win, on);
    settings::Save("fullscreen", on ? "1" : "0");
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
    // Settings menu (Back / Esc): no frames run while it is open.
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
        switch (menu::Update(nav, now)) {
          case menu::kQuit: running = false; break;
          case menu::kToggleFullscreen: set_fullscreen(!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)); break;
          case t81_menu::kReset:
            lr::Reset();
            menu::Toast("Machine reset", now);
            break;
          case menu::kSaveState:
            menu::Toast(SaveStateFile(StatePath(udir)) ? "State saved (" + SlotInfo(udir) + ")" : "Save failed", now);
            break;
          case menu::kLoadState:
            menu::Toast(LoadStateFile(StatePath(udir)) ? "State loaded (" + SlotInfo(udir) + ")"
                                                       : "No state in this slot", now);
            lr::Audio().clear();
            break;
          default: break;
        }
        lr::SetOption("mame_altres", lines_fn ? altres() : settings::Str("mame_altres", "1920x1440"));
        next = now;   // paused: no catching up afterwards
      }
    }
    int todo = 0;
    if (!menu::IsOpen()) {
      while (now >= next && todo < 4) { next += uint64_t(frame_ns); ++todo; }
      if (now > next + uint64_t(frame_ns * 8)) next = now;
    }
    for (int k = 0; k < todo; ++k) {
      const bool* keys = SDL_GetKeyboardState(nullptr);
      lr::SetButtons(ReadPad(pad, keys) | scripted(frame));
      float sx = pad ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f : 0.f;
      if (autoplay >= 0 && frame >= autoplay) sx = (frame / 90) % 2 ? 0.7f : -0.7f;
      const bool left = keys[SDL_SCANCODE_LEFT] || (pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT));
      const bool right = keys[SDL_SCANCODE_RIGHT] || (pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
      lr::SetMouse(SpinDelta(std::clamp(sx, -1.f, 1.f), left, right), 0);
      for (auto& s : loadstates)
        if (s.frame == frame && !LoadStateFile(s.path)) std::fprintf(stderr, "loadstate %s failed\n", s.path.c_str());
      lr::RunFrame();
      if (lines_fn) {
        t81_vectors::Frame vf;
        vf.lines = lines_fn(&vf.count, nullptr, nullptr);
        t81_vectors::NewFrame(vf);
      }
      ++frame;
      auto& au = lr::Audio();
      if (audio && !au.empty()) {
        if (SDL_GetAudioStreamQueued(audio) > int(lr::SampleRate()) * 4 / 10) SDL_ClearAudioStream(audio);
        SDL_PutAudioStreamData(audio, au.data(), int(au.size() * 2));
      }
      au.clear();
      for (auto& s : shots)
        if (s.frame == frame - 1) WriteBmp(s.path, lr::LastFrame());
      if (frames >= 0 && frame >= frames) running = false;
    }
    // Our vectors (default) or MAME's rasterised picture (vector_renderer 0, or a core without the line export).
    const bool own_vectors = lines_fn && settings::Num("vector_renderer", 1) != 0;
    const lr::Frame& f = lr::LastFrame();
    if (f.width && !own_vectors) {
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
    const uint64_t draw_now = SDL_GetTicksNS();
    static uint64_t last_draw = draw_now;
    if (own_vectors) {
      t81_vectors::Draw(ren, ww, wh, float(draw_now - last_draw) / 1e9f);
    } else if (tex) {   // the core's aspect (the picture's own when it gives none), as big as fits
      const float aspect = lr::AspectRatio() > 0 ? float(lr::AspectRatio()) : float(tw) / float(th);
      float h = float(wh), w = h * aspect;
      if (w > ww) { w = float(ww); h = w / aspect; }
      SDL_FRect dst{(ww - w) / 2, (wh - h) / 2, w, h};
      SDL_RenderTexture(ren, tex, nullptr, &dst);
    }
    last_draw = draw_now;
    if (menu::Shown()) menu::Draw(ren, ww, wh);
    menu::DrawToast(ren, ww, wh, SDL_GetTicksNS());
    if (menu_test && presents == 0) menu::Open(menu_test == 2);
    for (auto& s : frameshots)
      if (s.frame >= 0 && frame > s.frame) {   // the first present after game frame F
        if (SDL_Surface* sf = SDL_RenderReadPixels(ren, nullptr)) {
          SDL_SaveBMP(sf, s.path.c_str());
          SDL_DestroySurface(sf);
        }
        s.frame = -1;
        if (std::all_of(frameshots.begin(), frameshots.end(), [](const Shot& x) { return x.frame < 0; })) running = false;
      }
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
  lr::Unload();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
