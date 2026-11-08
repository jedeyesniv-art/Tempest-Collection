// main.cpp - Llamasoft on the Jaguar: SDL3 front end (stage 1: plain emulation).
//
//   t2k.exe [rom]                         play in a window
//   t2k.exe rom --headless --frames=N     run N frames with no window
//     --shot=F:<file.bmp>                 save the frame after frame F (repeatable)
//     --press=F:<btn>[:len]               hold a RetroPad button from frame F (default 6 frames)
//     --wav=<file.wav>                    dump the audio
//     --opt=key=value                     set a core option
//     --userdata=<dir>                    save folder (default userdata)
//     --stick=F:x,y[:len]                 hold the left stick at x,y (-1..1) from frame F (default 6 frames)
//     --set=key=value                     override a settings.txt value
//     --trace=F0:F1                       print the claw state every frame in F0..F1
//   Keys in the window: Esc / pad Back = settings menu, F2 stick mode, F5 reloads userdata\settings.txt, F11 fullscreen.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "controls.h"
#include "gpulog.h"
#include "host.h"
#include "remaster.h"
#include "menu.h"
#include "settings.h"
#include "t2k.h"

namespace fs = std::filesystem;

namespace {

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

bool WriteBmp(const std::string& path, const host::Frame& f) {
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

std::string FindRom(const std::string& given) {
  if (!given.empty()) return given;
  // Next to the exe, then the working folder: the first Tempest 2000 image found.
  std::vector<fs::path> dirs = {fs::path(SDL_GetBasePath()), fs::path(SDL_GetBasePath()) / "game",
                                fs::current_path()};
  for (auto& d : dirs) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(d, ec)) {
      auto name = e.path().filename().string();
      auto ext = e.path().extension().string();
      if ((ext == ".jag" || ext == ".j64" || ext == ".rom") && name.find("Tempest") != std::string::npos)
        return e.path().string();
    }
  }
  return {};
}

// Game buttons. Functions come from controls:: (owner's defaults: A fire, X / LT / RT jump, B / RB Superzapper,
// Y Option, Start Pause, LB camera). The game's own meaning: Jaguar B = fire, A = jump, C = Superzapper; the core
// maps RetroPad B -> Jag B, A -> Jag A, Y -> Jag C, SELECT -> Pause, START -> Option, X -> keypad 0.
// Fixed: d-pad / left stick move, Back = settings menu. Keyboard: Z fire, X jump, C Superzapper, O Option,
// P / Enter Pause, arrows move, Esc menu.
host::Input ReadPad(SDL_Gamepad* pad, uint32_t held, const bool* keys, t2k::Pad* tp) {
  host::Input in;
  *tp = {};
  auto set = [&](int b, bool on) { if (on) in.buttons |= uint16_t(1u << b); };
  set(host::B, controls::FnHeld(held, controls::kFire));
  set(host::A, controls::FnHeld(held, controls::kJump));
  set(host::Y, controls::FnHeld(held, controls::kZap));
  set(host::START, controls::FnHeld(held, controls::kOption));
  set(host::SELECT, controls::FnHeld(held, controls::kPause));
  if (pad) {
    auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
    set(host::UP, btn(SDL_GAMEPAD_BUTTON_DPAD_UP));
    set(host::DOWN, btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    set(host::LEFT, btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
    set(host::RIGHT, btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    in.lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    in.ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    tp->dpad_lr = btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    tp->lx = std::max(-1.f, in.lx / 32767.f);
    tp->ly = std::max(-1.f, in.ly / 32767.f);
    // The stick also works as a d-pad for the game's menus (in play the claw hook ignores the pad bits).
    set(host::LEFT, in.lx < -16000);
    set(host::RIGHT, in.lx > 16000);
    set(host::UP, in.ly < -16000);
    set(host::DOWN, in.ly > 16000);
  }
  if (keys) {
    set(host::UP, keys[SDL_SCANCODE_UP]);
    set(host::DOWN, keys[SDL_SCANCODE_DOWN]);
    set(host::LEFT, keys[SDL_SCANCODE_LEFT]);
    set(host::RIGHT, keys[SDL_SCANCODE_RIGHT]);
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_RIGHT]) tp->dpad_lr = true;
    set(host::B, keys[SDL_SCANCODE_Z]);
    set(host::A, keys[SDL_SCANCODE_X]);
    set(host::Y, keys[SDL_SCANCODE_C]);
    set(host::START, keys[SDL_SCANCODE_O]);
    set(host::SELECT, keys[SDL_SCANCODE_P] || keys[SDL_SCANCODE_RETURN]);
  }
  return in;
}

menu::Nav ReadNav(SDL_Gamepad* pad, const bool* keys) {
  menu::Nav n;
  if (pad) {
    auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
    int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    n.up = btn(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -20000;
    n.down = btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > 20000;
    n.left = btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -20000;
    n.right = btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > 20000;
    n.ok = btn(SDL_GAMEPAD_BUTTON_SOUTH);
    n.back = btn(SDL_GAMEPAD_BUTTON_EAST) || btn(SDL_GAMEPAD_BUTTON_BACK);
    n.cancel = btn(SDL_GAMEPAD_BUTTON_BACK);
  }
  if (keys) {
    n.up |= keys[SDL_SCANCODE_UP];
    n.down |= keys[SDL_SCANCODE_DOWN];
    n.left |= keys[SDL_SCANCODE_LEFT];
    n.right |= keys[SDL_SCANCODE_RIGHT];
    n.ok |= keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_Z];
    n.back |= keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_BACKSPACE];
    n.cancel |= keys[SDL_SCANCODE_ESCAPE];
  }
  return n;
}

const char* kViewNames[] = {"standard", "whole web", "close-up"};

// The cartridge EEPROM (high scores, keys, options) lives in userdata\tempest2000.eeprom. Written when it
// changes (checked every second) and at exit; the previous file is kept as .bak.
struct Eeprom {
  fs::path path;
  std::vector<uint8_t> last;

  void Load(const fs::path& dir) {
    path = dir / "tempest2000.eeprom";
    size_t n = 0;
    uint8_t* mem = host::SaveRam(&n);
    // No file yet: keep the core's all-zero buffer. Not 0xFF like a factory-blank chip: the game's eepromsave
    // only writes words that differ from its copy (zero when the chip was invalid), so from an all-ones chip
    // the first save leaves stale words, the next boot fails the checksum and resets the settings.
    std::ifstream f(path, std::ios::binary);
    if (mem && n && f) {
      std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
      if (d.size() == n) std::memcpy(mem, d.data(), n);
      else std::fprintf(stderr, "eeprom: %s is %zu bytes, expected %zu - not loaded\n", path.string().c_str(),
                        d.size(), n);
    }
    if (mem && n) last.assign(mem, mem + n);
  }

  void SaveIfChanged() {
    size_t n = 0;
    uint8_t* mem = host::SaveRam(&n);
    if (!mem || !n || (last.size() == n && std::memcmp(last.data(), mem, n) == 0)) return;
    std::error_code ec;
    if (fs::exists(path)) fs::copy_file(path, path.string() + ".bak", fs::copy_options::overwrite_existing, ec);
    fs::path tmp = path.string() + ".tmp";
    {
      std::ofstream o(tmp, std::ios::binary);
      o.write(reinterpret_cast<const char*>(mem), std::streamsize(n));
    }
    fs::rename(tmp, path, ec);
    last.assign(mem, mem + n);
  }
};

fs::path StatePath(const fs::path& dir) {
  return dir / "states" / ("slot" + std::to_string(int(settings::Num("state_slot", 1))) + ".state");
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

}  // namespace

int main(int argc, char** argv) {
  std::string rom, wav, userdata = "userdata";
  bool headless = false;
  int frames = 600;
  std::vector<Shot> shots;
  std::vector<Press> presses;
  std::vector<StickHold> sticks;
  std::vector<std::pair<std::string, std::string>> sets;
  int trace0 = -1, trace1 = -1, title_at = -1, save_at = -1, load_at = -1;
  std::string save_file, load_file, gpulog_spec, start_state, winshot_file;
  int winshot_at = -1;
  std::vector<Shot> rmshots;
  int rm_scale = 4;
  std::string pause_line = "press y to quit to title";
  std::vector<std::pair<std::string, std::string>> opts;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&](const char* p) { return a.substr(std::strlen(p)); };
    if (a == "--headless") headless = true;
    else if (a.rfind("--frames=", 0) == 0) frames = std::atoi(val("--frames=").c_str());
    else if (a.rfind("--wav=", 0) == 0) wav = val("--wav=");
    else if (a.rfind("--userdata=", 0) == 0) userdata = val("--userdata=");
    else if (a.rfind("--shot=", 0) == 0) {
      auto v = val("--shot="); auto c = v.find(':');
      shots.push_back({std::atoi(v.substr(0, c).c_str()), v.substr(c + 1)});
    } else if (a.rfind("--press=", 0) == 0) {
      auto v = val("--press="); auto c1 = v.find(':'); auto c2 = v.find(':', c1 + 1);
      std::string b = v.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
      presses.push_back({std::atoi(v.substr(0, c1).c_str()), BtnByName(b),
                         c2 == std::string::npos ? 6 : std::atoi(v.substr(c2 + 1).c_str())});
    } else if (a.rfind("--stick=", 0) == 0) {
      auto v = val("--stick="); auto c1 = v.find(':'); auto cm = v.find(',', c1); auto c2 = v.find(':', cm);
      sticks.push_back({std::atoi(v.substr(0, c1).c_str()), float(std::atof(v.substr(c1 + 1, cm - c1 - 1).c_str())),
                        float(std::atof(v.substr(cm + 1).c_str())),
                        c2 == std::string::npos ? 6 : std::atoi(v.substr(c2 + 1).c_str())});
    } else if (a.rfind("--set=", 0) == 0) {
      auto v = val("--set="); auto e = v.find('=');
      sets.push_back({v.substr(0, e), v.substr(e + 1)});
    } else if (a.rfind("--savestate=", 0) == 0 || a.rfind("--loadstate=", 0) == 0) {
      bool sv = a[2] == 's';
      auto v = val("--savestate="); auto c = v.find(':');
      (sv ? save_at : load_at) = std::atoi(v.substr(0, c).c_str());
      (sv ? save_file : load_file) = v.substr(c + 1);
    } else if (a.rfind("--rmshot=", 0) == 0) {
      auto v = val("--rmshot="); auto c = v.find(':');
      rmshots.push_back({std::atoi(v.substr(0, c).c_str()), v.substr(c + 1)});
    } else if (a.rfind("--rmorigin=", 0) == 0) {
      auto v = val("--rmorigin="); auto c = v.find(',');
      remaster::SetOrigin(float(std::atof(v.substr(0, c).c_str())), float(std::atof(v.substr(c + 1).c_str())));
    } else if (a.rfind("--rmscale=", 0) == 0) {
      rm_scale = std::atoi(val("--rmscale=").c_str());
    } else if (a.rfind("--state=", 0) == 0) {
      start_state = val("--state=");
    } else if (a.rfind("--winshot=", 0) == 0) {
      auto v = val("--winshot="); auto c = v.find(':');
      winshot_at = std::atoi(v.substr(0, c).c_str());
      winshot_file = v.substr(c + 1);
    } else if (a.rfind("--gpulog=", 0) == 0) {
      gpulog_spec = val("--gpulog=");
    } else if (a.rfind("--title=", 0) == 0) {
      title_at = std::atoi(val("--title=").c_str());
    } else if (a.rfind("--pauseline=", 0) == 0) {
      pause_line = val("--pauseline=");
    } else if (a.rfind("--trace=", 0) == 0) {
      auto v = val("--trace="); auto c = v.find(':');
      trace0 = std::atoi(v.substr(0, c).c_str()); trace1 = std::atoi(v.substr(c + 1).c_str());
    } else if (a.rfind("--opt=", 0) == 0) {
      auto v = val("--opt="); auto e = v.find('=');
      opts.push_back({v.substr(0, e), v.substr(e + 1)});
    } else if (a[0] != '-') rom = a;
  }

  if (!SDL_Init(headless ? 0 : (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  rom = FindRom(rom);
  if (rom.empty()) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tempest 2000",
                             "Put your Tempest 2000 cartridge image (.jag) next to the program.", nullptr);
    return 1;
  }
  fs::create_directories(userdata);
  settings::Load((fs::path(userdata) / "settings.txt").string());
  for (auto& [k, v] : sets) settings::Set(k, v);
  for (auto& [k, v] : opts) host::SetOption(k, v);
  std::string err;
  if (!host::Load(rom, fs::absolute(userdata).string(), &err)) {
    std::fprintf(stderr, "load: %s\n", err.c_str());
    return 1;
  }
  std::fprintf(stderr, "loaded %s: %.3f fps, %.0f Hz audio\n", rom.c_str(), host::Fps(), host::SampleRate());
  if (!t2k::Install(&err)) std::fprintf(stderr, "t2k: %s (stick controls off)\n", err.c_str());
  const fs::path udir = fs::absolute(userdata);
  if (!remaster::Install()) std::fprintf(stderr, "remaster: install failed\n");
  remaster::SetEnabled(settings::Num("remaster", 1) != 0);
  if (!start_state.empty()) {   // --state=<file>: start from a save state (tests)
    std::ifstream sf(start_state, std::ios::binary);
    std::vector<uint8_t> st((std::istreambuf_iterator<char>(sf)), {});
    host::RunFrame();
    std::fprintf(stderr, "state %s: %s\n", start_state.c_str(), host::LoadState(st) ? "loaded" : "FAILED");
  }
  remaster::SetLineWidth(float(settings::Num("remaster_line_width", 1.5)));
  if (!gpulog_spec.empty()) {
    auto c1 = gpulog_spec.find(':'), c2 = gpulog_spec.find(':', c1 + 1);
    if (!gpulog::Install(std::atoi(gpulog_spec.substr(0, c1).c_str()),
                         std::atoi(gpulog_spec.substr(c1 + 1, c2 - c1 - 1).c_str()), gpulog_spec.substr(c2 + 1).c_str()))
      std::fprintf(stderr, "gpulog: install failed\n");
  }
  Eeprom eeprom;
  eeprom.Load(udir);

  if (headless) {
    std::vector<int16_t> all_audio;
    auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < frames; ++f) {
      host::Input in;
      for (auto& p : presses)
        if (p.btn >= 0 && f >= p.frame && f < p.frame + p.len) in.buttons |= uint16_t(1u << p.btn);
      t2k::Pad tp;
      for (auto& s : sticks)
        if (f >= s.frame && f < s.frame + s.len) { tp.lx = s.x; tp.ly = s.y; }
      host::SetInput(0, in);
      t2k::BeforeFrame(tp);
      t2k::SetPauseLine(pause_line);
      if (f == title_at) t2k::ReturnToTitle();
      gpulog::SetFrame(f);
      if (f == load_at) {
        std::ifstream lf(load_file, std::ios::binary);
        std::vector<uint8_t> st((std::istreambuf_iterator<char>(lf)), {});
        std::fprintf(stderr, "load state %zu bytes: %s\n", st.size(), host::LoadState(st) ? "ok" : "FAILED");
      }
      host::RunFrame();
      t2k::AfterFrame();
      remaster::AfterFrame();
      for (auto& s : rmshots)
        if (s.frame == f) {
          WriteBmp(s.path, remaster::Overlay(host::LastFrame(), rm_scale));
          remaster::DumpLines(s.path + ".lines");
          WriteBmp(s.path + ".orig.bmp", host::LastFrame());
          std::fprintf(stderr, "rmshot %d -> %s (%s)\n", f, s.path.c_str(), remaster::Stats().c_str());
        }
      if (f == save_at) {
        auto st = host::SaveState();
        std::ofstream(save_file, std::ios::binary).write(reinterpret_cast<const char*>(st.data()), std::streamsize(st.size()));
        std::fprintf(stderr, "saved state %zu bytes\n", st.size());
      }
      if (f >= trace0 && f <= trace1) std::fprintf(stderr, "%5d %s\n", f, t2k::Status().c_str());
      if (!wav.empty()) all_audio.insert(all_audio.end(), host::Audio().begin(), host::Audio().end());
      host::Audio().clear();
      for (auto& s : shots)
        if (s.frame == f) {
          const auto& fr = host::LastFrame();
          std::fprintf(stderr, "shot %d: %ux%u -> %s\n", f, fr.width, fr.height, s.path.c_str());
          WriteBmp(s.path, fr);
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "%d frames in %.2f s (%.0f fps, %.1fx real time)\n", frames, secs, frames / secs,
                 frames / secs / host::Fps());
    if (!wav.empty()) WriteWav(wav, all_audio, uint32_t(host::SampleRate()));
    eeprom.SaveIfChanged();
    host::Unload();
    SDL_Quit();
    return 0;
  }

  SDL_Window* win = SDL_CreateWindow("Tempest 2000", 1280, 960, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (settings::Num("fullscreen", 0) != 0) SDL_SetWindowFullscreen(win, true);
  SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
  SDL_HideCursor();
  SDL_SetRenderVSync(ren, 1);
  SDL_Texture* tex = nullptr;
  unsigned tw = 0, th = 0;

  SDL_AudioSpec spec{SDL_AUDIO_S16, 2, int(host::SampleRate())};
  SDL_AudioStream* audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (audio) SDL_ResumeAudioStreamDevice(audio);

  auto set_fullscreen = [&](bool on) {
    SDL_SetWindowFullscreen(win, on);
    settings::Save("fullscreen", on ? "1" : "0");
  };

  SDL_Gamepad* pad = nullptr;
  const double frame_ns = 1e9 / host::Fps();
  uint64_t next = SDL_GetTicksNS();
  bool running = true;
  bool prev_back = false;
  uint32_t prev_held = 0;
  int music_frames = 0;    // frames left holding keypad 0 (the game's music toggle)
  int quiet_frames = 0;    // frames the pause line stays hidden after quitting to the title
  uint64_t frame_count = 0;
  menu::SetSlotInfo([&] { return SlotInfo(udir); });
  while (running) {
    uint64_t now = SDL_GetTicksNS();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running = false;
      else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) SDL_HideCursor();   // no mouse use: keep it out of view
      else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) SDL_ShowCursor();
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE && !menu::IsOpen())
        menu::Open();
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F5) {
        settings::Reload();
        menu::Toast("Settings reloaded", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F2) {
        settings::Save("stick_mode", settings::Str("stick_mode", "lr") == "aim" ? "lr" : "aim");
        menu::Toast(settings::Str("stick_mode", "lr") == "aim" ? "Stick: aim" : "Stick: left / right", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F8) {
        remaster::SetEnabled(!remaster::Enabled());
        settings::Save("remaster", remaster::Enabled() ? "1" : "0");
        menu::Toast(remaster::Enabled() ? "Remaster on" : "Remaster off (original picture)", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F11)
        set_fullscreen(!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
      else if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
      else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && SDL_GetGamepadID(pad) == e.gdevice.which) {
        SDL_CloseGamepad(pad);
        pad = nullptr;
      }
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    bool back = pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
    uint32_t held = controls::Held(pad);
    uint32_t pressed = held & ~prev_held;
    prev_held = held;
    auto fn_pressed = [&](controls::Fn f) { return controls::FnHeld(pressed, f); };

    if (menu::IsOpen()) {
      menu::Nav nav = ReadNav(pad, keys);
      for (int b = 0; b < controls::kButtonCount; ++b)
        if (pressed >> b & 1) { nav.new_button = b; break; }
      switch (menu::Update(nav, now)) {
        case menu::Action::Quit: running = false; break;
        case menu::Action::ToggleMusic: music_frames = 4; break;
        case menu::Action::ToggleFullscreen: set_fullscreen(!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)); break;
        case menu::Action::ReturnToTitle:
          t2k::ReturnToTitle();
          quiet_frames = 180;
          break;
        case menu::Action::SaveState: {
          auto st = host::SaveState();
          std::error_code ec;
          fs::create_directories(StatePath(udir).parent_path(), ec);
          bool ok = false;
          {
            std::ofstream o(StatePath(udir), std::ios::binary);
            o.write(reinterpret_cast<const char*>(st.data()), std::streamsize(st.size()));
            ok = bool(o) && !st.empty();
          }
          menu::Toast(ok ? "State saved (" + SlotInfo(udir) + ")" : "Save failed", now);
          break;
        }
        case menu::Action::LoadState: {
          std::ifstream f(StatePath(udir), std::ios::binary);
          std::vector<uint8_t> st((std::istreambuf_iterator<char>(f)), {});
          bool ok = !st.empty() && host::LoadState(st);
          menu::Toast(ok ? "State loaded (" + SlotInfo(udir) + ")" : "No state in this slot", now);
          host::Audio().clear();
          break;
        }
        default: break;
      }
      next = now;   // paused: no catching up afterwards
    } else {
      if (back && !prev_back) menu::Open();
      if (fn_pressed(controls::kCamera)) {
        int v = (int(settings::Num("camera_view", 0)) + 1) % 3;
        settings::Save("camera_view", std::to_string(v));
        menu::Toast(std::string("Camera: ") + kViewNames[v], now);
      }
      // On the game's pause screen the Option button quits to the title.
      if (t2k::Paused() && fn_pressed(controls::kOption)) {
        t2k::ReturnToTitle();
        quiet_frames = 180;
      }
    }
    prev_back = back;

    // The pause screen's extra line names the first button bound to Option.
    {
      std::string b = controls::ButtonsFor(controls::kOption);
      b = b.substr(0, b.find(','));
      for (auto& c : b) c = char(std::tolower(static_cast<unsigned char>(c)));
      t2k::SetPauseLine(quiet_frames > 0 || b == "-" ? "" : "press " + b + " to quit to title");
    }

    // Emulate as many frames as real time asks for (at most 4 to catch up).
    int todo = 0;
    if (!menu::IsOpen()) {
      while (now >= next && todo < 4) { next += uint64_t(frame_ns); ++todo; }
      if (now > next + uint64_t(frame_ns * 8)) next = now;  // stalled: resync
    }
    for (int i = 0; i < todo; ++i) {
      t2k::Pad tp;
      host::Input in = ReadPad(pad, held, keys, &tp);
      if (music_frames > 0) { in.buttons |= 1u << host::X; --music_frames; }
      if (quiet_frames > 0) --quiet_frames;
      host::SetInput(0, in);
      t2k::BeforeFrame(tp);
      host::RunFrame();
      t2k::AfterFrame();
      remaster::AfterFrame();
      if (++frame_count % 60 == 0) eeprom.SaveIfChanged();
      auto& a = host::Audio();
      if (audio && !a.empty()) {
        // Keep the queue short: drop audio if more than ~100 ms is waiting.
        if (SDL_GetAudioStreamQueued(audio) < int(host::SampleRate() * 4 / 10))
          SDL_PutAudioStreamData(audio, a.data(), int(a.size() * 2));
      }
      a.clear();
    }

    int ww, wh;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    const auto& fr = host::LastFrame();
    if (fr.width) {
      if (!tex || tw != fr.width || th != fr.height) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, fr.width, fr.height);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        tw = fr.width; th = fr.height;
        todo = 1;
      }
      if (todo) SDL_UpdateTexture(tex, nullptr, fr.pixels.data(), int(fr.width * 4));
      float sw = float(wh) * 4.f / 3.f, sh = float(wh);
      if (sw > ww) { sw = float(ww); sh = sw * 3.f / 4.f; }
      SDL_FRect dst{(ww - sw) / 2, (wh - sh) / 2, sw, sh};
      SDL_RenderTexture(ren, tex, nullptr, &dst);
      if (remaster::Enabled()) {
        remaster::SetLineWidth(float(settings::Num("remaster_line_width", 1.5)));
        remaster::Draw(ren, dst, fr.width, fr.height);
      }
    }
    if (menu::IsOpen()) menu::Draw(ren, ww, wh);
    menu::DrawToast(ren, ww, wh, now);
    if (winshot_at >= 0 && int(frame_count) >= winshot_at) {   // --winshot=N:<bmp>: the window after N frames
      if (SDL_Surface* shot = SDL_RenderReadPixels(ren, nullptr)) {
        SDL_SaveBMP(shot, winshot_file.c_str());
        SDL_DestroySurface(shot);
      }
      running = false;
    }
    SDL_RenderPresent(ren);
  }

  eeprom.SaveIfChanged();
  host::Unload();
  if (pad) SDL_CloseGamepad(pad);
  if (audio) SDL_DestroyAudioStream(audio);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
