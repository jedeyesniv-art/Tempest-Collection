// main.cpp - Llamasoft on the Jaguar: SDL3 front end (stage 1: plain emulation).
//
//   t2k.exe [rom]                         play in a window
//   t2k.exe rom --headless --frames=N     run N frames with no window
//     --shot=F:<file.bmp>                 save the frame after frame F (repeatable)
//     --press=F:<btn>[:len]               hold a RetroPad button from frame F (default 6 frames)
//     --wav=<file.wav>                    dump the audio
//     --aspect=W:H                        rmshots that shape: 4:3 picture centred, remaster around it
//     --runahead=N                        run-ahead N frames (shots / rmshots show frame f + N)
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
#include <sstream>
#include <string>
#include <vector>

#include "controls.h"
#include "gpulog.h"
#include "jagsurvey.h"
#include "d2k.h"
#include "host.h"
#include "remaster.h"
#include "xbr.h"
#include "crt.h"

namespace {
#include "../res/icon_rgba.h"
}
extern "C" uint8_t jagMemSpace[];
extern "C" uint32_t vjOPSkipLo, vjOPSkipHi, vjOPSkip2Lo, vjOPSkip2Hi;   // core hook: an OP overlay range not drawn
#include "menu.h"
#include "perf.h"
#include "t2k_menu.h"
#include "d2k_menu.h"
#include "runlog.h"
#include "settings.h"
#include "volume.h"
#include "t2k.h"

namespace fs = std::filesystem;

namespace {

struct Shot { int frame; std::string path; };
struct Press { int frame; int btn; int len; };
struct StickHold { int frame; float x, y; int len; };

// Keypad names for scripted presses: kp0..kp9, kpstar, kphash -> 100 + bit.
int KeypadByName(const std::string& n) {
  if (n.size() == 3 && n[0] == 'k' && n[1] == 'p' && n[2] >= '0' && n[2] <= '9') return 100 + (n[2] - '0');
  if (n == "kpstar") return 110;
  if (n == "kphash") return 111;
  return -1;
}

int BtnByName(const std::string& n) {
  if (int k = KeypadByName(n); k >= 0) return k;
  if (n == "thrust") return 200;    // Defender 2000's arcade buttons (d2k::Arcade)
  if (n == "reverse") return 201;
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

// Defender 2000: this program named d2k.exe (or anything with "defender" in it) looks for that cartridge instead and
// keeps its own settings / log; or any name started with the Defender 2000 image.
bool g_defender = false;

std::string FindRom(const std::string& given, const char* title_word) {
  if (!given.empty()) return given;
  // Next to the exe, then the working folder: the first image of the game found.
  // (Also the folder above the exe's: the repo root for build\t2k.exe started from the launcher.)
  std::vector<fs::path> dirs = {fs::path(SDL_GetBasePath()), fs::path(SDL_GetBasePath()) / "game",
                                fs::current_path(), fs::path(SDL_GetBasePath()).parent_path().parent_path()};
  for (auto& d : dirs) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(d, ec)) {
      auto name = e.path().filename().string();
      auto ext = e.path().extension().string();
      if ((ext == ".jag" || ext == ".j64" || ext == ".rom") && name.find(title_word) != std::string::npos)
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
// Defender 2000's buttons (d2k_menu): Jaguar B fire, C smart bomb, A lightning laser (Plus); Thrust / Reverse are
// the arcade's, done on the d-pad (d2k::Arcade). Setting d2k_stick_lr off: the stick moves up / down only (arcade);
// the d-pad keeps the game's own turn-and-thrust and works its menus. Keyboard: Z fire, C smart bomb, X laser,
// Space thrust, Shift reverse, O Option, P / Enter Pause, arrows.
host::Input ReadPadD2K(SDL_Gamepad* pad, uint32_t held, const bool* keys) {
  host::Input in;
  auto set = [&](int b, bool on) { if (on) in.buttons |= uint16_t(1u << b); };
  set(host::B, controls::FnHeld(held, d2k_menu::kFire));
  set(host::Y, controls::FnHeld(held, d2k_menu::kBomb));
  set(host::A, controls::FnHeld(held, d2k_menu::kLaser));
  set(host::START, controls::FnHeld(held, d2k_menu::kOption));
  set(host::SELECT, controls::FnHeld(held, d2k_menu::kPause));
  bool thrust = controls::FnHeld(held, d2k_menu::kThrust), reverse = controls::FnHeld(held, d2k_menu::kReverse);
  const bool stick_lr = settings::Str("d2k_stick_lr", "game") != "off";
  if (pad) {
    auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
    set(host::UP, btn(SDL_GAMEPAD_BUTTON_DPAD_UP));
    set(host::DOWN, btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    set(host::LEFT, btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
    set(host::RIGHT, btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    const int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    if (stick_lr) {
      set(host::LEFT, lx < -16000);
      set(host::RIGHT, lx > 16000);
    }
    set(host::UP, ly < -16000);
    set(host::DOWN, ly > 16000);
  }
  if (keys) {
    set(host::UP, keys[SDL_SCANCODE_UP]);
    set(host::DOWN, keys[SDL_SCANCODE_DOWN]);
    set(host::LEFT, keys[SDL_SCANCODE_LEFT]);
    set(host::RIGHT, keys[SDL_SCANCODE_RIGHT]);
    set(host::B, keys[SDL_SCANCODE_Z]);
    set(host::Y, keys[SDL_SCANCODE_C]);
    set(host::A, keys[SDL_SCANCODE_X]);
    set(host::START, keys[SDL_SCANCODE_O]);
    set(host::SELECT, keys[SDL_SCANCODE_P] || keys[SDL_SCANCODE_RETURN]);
    thrust = thrust || keys[SDL_SCANCODE_SPACE];
    reverse = reverse || keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
  }
  const int dir = d2k::Arcade(thrust, reverse);
  set(host::LEFT, dir < 0);
  set(host::RIGHT, dir > 0);
  if (dir) in.buttons &= uint16_t(~(1u << (dir < 0 ? host::RIGHT : host::LEFT)));   // (the arcade button wins)
  return in;
}

host::Input ReadPad(SDL_Gamepad* pad, uint32_t held, const bool* keys, t2k::Pad* tp) {
  *tp = {};
  if (g_defender) return ReadPadD2K(pad, held, keys);
  host::Input in;
  auto set = [&](int b, bool on) { if (on) in.buttons |= uint16_t(1u << b); };
  set(host::B, controls::FnHeld(held, t2k_menu::kFire));
  set(host::A, controls::FnHeld(held, t2k_menu::kJump));
  set(host::Y, controls::FnHeld(held, t2k_menu::kZap));
  set(host::START, controls::FnHeld(held, t2k_menu::kOption));
  set(host::SELECT, controls::FnHeld(held, t2k_menu::kPause));
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
    // The PC numpad is the Jaguar keypad (numpad / = #).
    static const SDL_Scancode kKp[12] = {SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3,
                                         SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7,
                                         SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_MULTIPLY,
                                         SDL_SCANCODE_KP_DIVIDE};
    for (int k = 0; k < 12; ++k)
      if (keys[kKp[k]]) in.keypad |= uint16_t(1u << k);
  }
  return in;
}

const char* kViewNames[] = {"standard", "whole web", "close-up"};

// The cartridge EEPROM (high scores, keys, options) lives in userdata\tempest2000.eeprom (Defender 2000:
// defender2000.eeprom). Written when it
// changes (checked every second) and at exit; the previous file is kept as .bak.
struct Eeprom {
  fs::path path;
  std::vector<uint8_t> last;

  void Load(const fs::path& dir) {
    path = dir / (d2k::Active() ? "defender2000.eeprom" : "tempest2000.eeprom");
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
  return dir / "states" /
         ((d2k::Active() ? "d2k_slot" : "slot") + std::to_string(int(settings::Num("state_slot", 1))) + ".state");
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

// Run-ahead: after a real frame, emulate n more with the same input, show the last one's picture and remaster
// scene, then rewind the core and our hooks to the real frame. Hides the game's own input lag (and the frame of
// delay interpolation adds). The real frame's audio is kept; the hidden frames' is dropped.
void RunAhead(int n, const host::Input& in, const t2k::Pad& tp) {
  static std::vector<uint8_t> st;
  if (!host::SaveStateInto(st)) {
    host::SetVideoEnabled(true);
    return;
  }   // (can't rewind: show the real frame's picture, keep the last scene)
  t2k::Mark();
  remaster::Mark();
  std::vector<int16_t> keep;
  keep.swap(host::Audio());
  for (int k = 1; k <= n; ++k) {
    host::SetInput(0, in);
    t2k::BeforeFrame(tp);
    host::SetVideoEnabled(k == n);   // only the last run-ahead frame's picture is shown
    host::RunFrame();
    t2k::AfterFrame();
    remaster::AfterFrame(k == n);
    host::Audio().clear();
  }
  host::Audio().swap(keep);
  host::SetVideoEnabled(true);
  host::LoadState(st);
  t2k::Rewind();
  remaster::Rewind();
  host::SetInput(0, in);
}

int main(int argc, char** argv) {
  std::string rom, wav, userdata = "userdata";
  bool userdata_given = false;
  bool headless = false;
  int frames = 600;
  std::vector<Shot> shots;
  std::vector<Press> presses;
  std::vector<StickHold> sticks;
  std::vector<std::pair<std::string, std::string>> sets;
  int trace0 = -1, trace1 = -1, title_at = -1, save_at = -1, load_at = -1;
  std::vector<int> menuback_at;   // --menuback=F[,F..]: B (back) in the game's menus at these frames
  std::vector<std::pair<int, std::string>> d2k_checks;
  std::string save_file, load_file, gpulog_spec, survey_spec, start_state, winshot_file;
  int winshot_at = -1;
  struct Poke { int frame; uint32_t addr, value; };
  std::vector<Poke> pokes;   // --poke=F:hexaddr:hexword: write a RAM word before frame F (tests)
  int peek0 = -1, peek1 = -1;
  uint32_t peek_addr = 0;   // --peek=F0:F1:hexaddr: a RAM long each frame (stderr)
  bool novsync = false;   // --novsync: present unthrottled (performance tests)
  bool from_launcher = false;   // --from-launcher: started by tempest.exe (the menu says "Return to launcher")
  int menu_test = 0;   // --menu (1) / --menu=all (2): open the menu at start (tests; winshot counts presents)
  std::vector<Shot> rmshots;
  int runahead = 0;
  int win_w = 1280, win_h = 960;   // --winsize=WxH
  double aspect = 0;  // --aspect=W:H (headless rmshots: the 4:3 picture centred, our layer filling the rest)   // --runahead=N (headless; the window uses setting run_ahead)
  int rm_scale = 4;
  std::string pause_line = "press y to quit to title";
  std::vector<std::pair<std::string, std::string>> opts;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&](const char* p) { return a.substr(std::strlen(p)); };
    if (a == "--headless") headless = true;
    else if (a.rfind("--frames=", 0) == 0) frames = std::atoi(val("--frames=").c_str());
    else if (a.rfind("--runahead=", 0) == 0) runahead = std::atoi(val("--runahead=").c_str());
    else if (a.rfind("--winsize=", 0) == 0) {
      auto v = val("--winsize="); auto c = v.find('x');
      win_w = std::atoi(v.substr(0, c).c_str());
      win_h = std::atoi(v.substr(c + 1).c_str());
    } else if (a.rfind("--aspect=", 0) == 0) {
      auto v = val("--aspect="); auto c = v.find(':');
      aspect = std::atof(v.substr(0, c).c_str()) / std::atof(v.substr(c + 1).c_str());
    }
    else if (a.rfind("--wav=", 0) == 0) wav = val("--wav=");
    else if (a.rfind("--userdata=", 0) == 0) userdata = val("--userdata="), userdata_given = true;
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
    } else if (a.rfind("--poke=", 0) == 0) {
      Poke p{};
      std::sscanf(val("--poke=").c_str(), "%d:%x:%x", &p.frame, &p.addr, &p.value);
      pokes.push_back(p);
    } else if (a.rfind("--peek=", 0) == 0) {
      auto v = val("--peek=");
      std::sscanf(v.c_str(), "%d:%d:%x", &peek0, &peek1, &peek_addr);
    } else if (a == "--novsync") {
      novsync = true;
    } else if (a == "--from-launcher") {
      from_launcher = true;
    } else if (a == "--menu" || a == "--menu=all") {
      menu_test = a == "--menu" ? 1 : 2;
    } else if (a.rfind("--winshot=", 0) == 0) {
      auto v = val("--winshot="); auto c = v.find(':');
      winshot_at = std::atoi(v.substr(0, c).c_str());
      winshot_file = v.substr(c + 1);
    } else if (a.rfind("--d2kcheck=", 0) == 0) {   // --d2kcheck=F:<prefix>: Defender 2000 renderer vs the core
      std::string v = val("--d2kcheck=");
      d2k_checks.push_back({std::atoi(v.c_str()), v.substr(v.find(':') + 1)});
    } else if (a.rfind("--survey=", 0) == 0) {
      survey_spec = val("--survey=");
    } else if (a.rfind("--gpulog=", 0) == 0) {
      gpulog_spec = val("--gpulog=");
    } else if (a.rfind("--title=", 0) == 0) {
      title_at = std::atoi(val("--title=").c_str());
    } else if (a.rfind("--menuback=", 0) == 0) {
      std::stringstream ss(val("--menuback="));
      for (std::string t; std::getline(ss, t, ',');) menuback_at.push_back(std::atoi(t.c_str()));
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
  // Saves / settings next to the program unless given (Steam and shortcuts start it from other folders).
  if (!userdata_given) userdata = (fs::path(SDL_GetBasePath()) / "userdata").string();
  {
    std::string exe = fs::path(argv[0]).stem().string();
    for (auto& c : exe) c = char(std::tolower(static_cast<unsigned char>(c)));
    const bool d2k_app = exe.find("d2k") != std::string::npos || exe.find("defender") != std::string::npos;
    rom = FindRom(rom, d2k_app ? "Defender" : "Tempest");
    g_defender = !rom.empty() && d2k::IsDefender(rom);
    if (rom.empty()) {
      const char* game = d2k_app ? "Defender 2000" : "Tempest 2000";
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, game,
                               (std::string("Put your ") + game + " cartridge image (.jag) next to the program.").c_str(),
                               nullptr);
      return 1;
    }
  }
  const char* app = g_defender ? "d2k" : "t2k";
  if (from_launcher) runlog::Start(fs::absolute(userdata), app);   // no console: userdata\logs\<app>.log
  fs::create_directories(userdata);
  // Defender 2000 keeps its own settings next to Tempest 2000's (the EEPROM and save states are separate too).
  settings::Load((fs::path(userdata) / (g_defender ? "d2k_settings.txt" : "settings.txt")).string());
  volume::Init(fs::absolute(userdata), app);
  for (auto& [k, v] : sets) settings::Set(k, v);
  if (g_defender) d2k_menu::Install(from_launcher);
  else t2k_menu::Install(from_launcher);
  for (auto& [k, v] : opts) host::SetOption(k, v);
  std::string err;
  if (!host::Load(rom, fs::absolute(userdata).string(), &err)) {
    std::fprintf(stderr, "load: %s\n", err.c_str());
    return 1;
  }
  std::fprintf(stderr, "loaded %s: %.3f fps, %.0f Hz audio\n", rom.c_str(), host::Fps(), host::SampleRate());
  if (d2k::IsDefender(rom)) {
    d2k::Install();
    std::fprintf(stderr, "d2k: Defender 2000 renderer on\n");
  } else if (!t2k::Install(&err)) {
    std::fprintf(stderr, "t2k: %s (stick controls off)\n", err.c_str());
  }
  const fs::path udir = fs::absolute(userdata);
  if (!d2k::Active() && !remaster::Install()) std::fprintf(stderr, "remaster: install failed\n");
  remaster::SetEnabled(settings::Num("remaster", 1) != 0);
  if (!start_state.empty()) {   // --state=<file>: start from a save state (tests)
    std::ifstream sf(start_state, std::ios::binary);
    std::vector<uint8_t> st((std::istreambuf_iterator<char>(sf)), {});
    host::RunFrame();
    std::fprintf(stderr, "state %s: %s\n", start_state.c_str(), host::LoadState(st) ? "loaded" : "FAILED");
  }
  remaster::SetLineWidth(float(settings::Num("remaster_line_width", 1.5)));
  if (!survey_spec.empty()) {
    auto c1 = survey_spec.find(':'), c2 = survey_spec.find(':', c1 + 1);
    if (!jagsurvey::Install(std::atoi(survey_spec.substr(0, c1).c_str()),
                            std::atoi(survey_spec.substr(c1 + 1, c2 - c1 - 1).c_str()), survey_spec.substr(c2 + 1).c_str()))
      std::fprintf(stderr, "survey: can't write %s\n", survey_spec.substr(c2 + 1).c_str());
  }
  if (!gpulog_spec.empty()) {
    auto c1 = gpulog_spec.find(':'), c2 = gpulog_spec.find(':', c1 + 1);
    if (!gpulog::Install(std::atoi(gpulog_spec.substr(0, c1).c_str()),
                         std::atoi(gpulog_spec.substr(c1 + 1, c2 - c1 - 1).c_str()), gpulog_spec.substr(c2 + 1).c_str()))
      std::fprintf(stderr, "gpulog: install failed\n");
  }
  Eeprom eeprom;
  eeprom.Load(udir);

  if (headless) {
    if (aspect > 0) remaster::SetView(-1e4, -1e4, 1e4, 1e4);   // the overlay crops
    std::vector<int16_t> all_audio;
    auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < frames; ++f) {
      host::Input in;
      bool thrust = false, reverse = false;
      for (auto& p : presses)
        if (p.btn >= 0 && f >= p.frame && f < p.frame + p.len) {
          if (p.btn == 200) thrust = true;
          else if (p.btn == 201) reverse = true;
          else if (p.btn >= 100) in.keypad |= uint16_t(1u << (p.btn - 100));
          else in.buttons |= uint16_t(1u << p.btn);
        }
      if (g_defender)
        if (const int dir = d2k::Arcade(thrust, reverse)) in.buttons |= uint16_t(1u << (dir < 0 ? host::LEFT : host::RIGHT));
      t2k::Pad tp;
      for (auto& s : sticks)
        if (f >= s.frame && f < s.frame + s.len) { tp.lx = s.x; tp.ly = s.y; }
      for (int m : menuback_at)
        if (f == m) {
          std::fprintf(stderr, "frame %d: menu back (%s)\n", f, t2k::MenuScreen() ? "menu" : "not a menu");
          t2k::MenuBack();
        }
      if (t2k::MenuTick()) in.buttons |= uint16_t(1u << host::B);
      host::SetInput(0, in);
      t2k::BeforeFrame(tp);
      t2k::SetPauseLine(pause_line);
      if (f == title_at) t2k::ReturnToTitle();
      gpulog::SetFrame(f);
      jagsurvey::SetFrame(f);
      if (f == load_at) {
        std::ifstream lf(load_file, std::ios::binary);
        std::vector<uint8_t> st((std::istreambuf_iterator<char>(lf)), {});
        std::fprintf(stderr, "load state %zu bytes: %s\n", st.size(), host::LoadState(st) ? "ok" : "FAILED");
      }
      for (auto& p : pokes)
        if (p.frame == f) {
          jagMemSpace[p.addr & 0xFFFFFF] = uint8_t(p.value >> 8);
          jagMemSpace[(p.addr + 1) & 0xFFFFFF] = uint8_t(p.value);
        }
      d2k::BeforeFrame();
      host::RunFrame();
      d2k::AfterFrame();
      for (auto& [cf, cp] : d2k_checks)
        if (cf == f) d2k::Check(cp);
      t2k::AfterFrame();
      if (runahead > 0) {
        remaster::AfterFrame(false);
        RunAhead(runahead, in, tp);
      } else {
        remaster::AfterFrame();
      }
      for (auto& s : rmshots)
        if (s.frame == f) {
          if (const char* t = std::getenv("RM_T")) remaster::Interpolate(float(std::atof(t)));   // debug: blend
          WriteBmp(s.path, remaster::Overlay(host::LastFrame(), rm_scale, aspect));
          remaster::DumpLines(s.path + ".lines");
          WriteBmp(s.path + ".orig.bmp", host::LastFrame());
          if (std::getenv("RM_HUDDUMP")) WriteBmp(s.path + ".hud.bmp", remaster::Hud());   // debug: screen3
          std::fprintf(stderr, "rmshot %d -> %s (%s)\n", f, s.path.c_str(), remaster::Stats().c_str());
        }
      if (f == save_at) {
        auto st = host::SaveState();
        std::ofstream(save_file, std::ios::binary).write(reinterpret_cast<const char*>(st.data()), std::streamsize(st.size()));
        std::fprintf(stderr, "saved state %zu bytes\n", st.size());
      }
      if (f >= trace0 && f <= trace1) std::fprintf(stderr, "%5d %s\n", f, t2k::Status().c_str());
      if (f >= peek0 && f <= peek1) {
        const uint8_t* m = jagMemSpace + (peek_addr & 0xFFFFFF);
        std::fprintf(stderr, "peek %5d %06X: %02X%02X%02X%02X\n", f, peek_addr, m[0], m[1], m[2], m[3]);
      }
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

  SDL_Window* win = SDL_CreateWindow(g_defender ? "Defender 2000" : "Tempest 2000", win_w, win_h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (win) {   // the claw-ship icon (the exe carries it too: res/t2k.rc)
    if (SDL_Surface* icon = SDL_CreateSurfaceFrom(64, 64, SDL_PIXELFORMAT_ABGR8888, (void*)k_icon_rgba, 64 * 4)) {
      SDL_SetWindowIcon(win, icon);
      SDL_DestroySurface(icon);
    }
  }
  if (settings::Num("fullscreen", 0) != 0) SDL_SetWindowFullscreen(win, true);
  // SDL's GPU renderer (D3D12) for the CRT shaders; the default renderer if it isn't available (no CRT then).
  SDL_Renderer* ren = crt::CreateRenderer(win);
  if (ren && !crt::Init(ren)) std::fprintf(stderr, "crt: unavailable on renderer %s\n", SDL_GetRendererName(ren));
  SDL_HideCursor();
  SDL_SetRenderVSync(ren, novsync ? 0 : 1);
  SDL_Texture* tex = nullptr;
  unsigned tw = 0, th = 0;
  // Sharp bilinear: the picture scaled up k times with nearest into `big`, then fitted with linear filtering, so
  // the game's pixel art (text, HUD, menus) is crisp without uneven pixels (setting picture_filter sharp | smooth).
  // x4: the xBR 4x version (premultiplied alpha), when picture_filter = xbr.
  struct Sharp { SDL_Texture* big = nullptr; int k = 0, w = 0, h = 0; SDL_Texture* x4 = nullptr; };
  Sharp pic_sharp, hud_sharp;
  SDL_Texture* hudtex = nullptr;   // the HUD strip (screen3), 384 x 48

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
  // T2K_PERF=1: every ~5 s, the average time a shown frame spends emulating (incl. run-ahead), composing (our layer,
  // picture, HUD, CRT) and presenting, and frames shown a second.
  uint64_t perf_t1 = 0, perf_t2 = 0;   // (T2K_PERF: perf.h)
  uint64_t cap_slot = 0;   // frame_cap: the next present's slot on a fixed grid
  while (running) {
    // Frame rate (setting frame_cap: 0 = the display's, 60 = at most 60 pictures a second - less work, e.g. the Steam
    // Deck at 90 Hz). Slots on a fixed 60 Hz grid; each picture goes to the first refresh after its slot (on 90 Hz:
    // one, then two refreshes - 60 on average, never sliding to 45). The picture is drawn for the time it is shown
    // (now is read after the wait), so motion stays right. Without vsync it simply waits for the slot.
    if (const double cap = settings::Num("frame_cap", 0); cap >= 24 && !novsync) {
      const uint64_t step = uint64_t(1e9 / cap), t = SDL_GetTicksNS();
      if (cap_slot == 0 || t > cap_slot + 4 * step) cap_slot = t;   // start / resync after a stall
      if (t + 1000000 < cap_slot) SDL_DelayPrecise(cap_slot - t - 1000000);
      cap_slot += step;
    } else {
      cap_slot = 0;
    }
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
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F2 && !g_defender) {
        settings::Save("stick_mode", settings::Str("stick_mode", "lr") == "aim" ? "lr" : "aim");
        menu::Toast(settings::Str("stick_mode", "lr") == "aim" ? "Stick: aim" : "Stick: left / right", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F9) {
        int m = (int(settings::Num("crt", 2)) + 1) % 3;
        settings::Save("crt", std::to_string(m));
        crt::LoadSettings();
        menu::Toast(!crt::Available() ? "CRT unavailable (needs the GPU renderer)"
                    : m == 2 ? "CRT++" : m == 1 ? "CRT+" : "CRT off", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F8 && d2k::Active()) {
        const bool on = settings::Num("d2k_native", 1) == 0;
        settings::Save("d2k_native", on ? "1" : "0");
        menu::Toast(on ? "Native picture" : "Emulated picture", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F7 && d2k::Active()) {
        const bool on = settings::Num("d2k_interpolate", 1) == 0;
        settings::Save("d2k_interpolate", on ? "1" : "0");
        menu::Toast(on ? "Smooth motion on" : "Smooth motion off", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F6 && d2k::Active()) {
        const bool on = settings::Num("d2k_widescreen", 1) == 0;
        settings::Save("d2k_widescreen", on ? "1" : "0");
        menu::Toast(on ? "Wide screen on" : "Wide screen off (4:3)", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F4 && d2k::Active()) {
        const bool on = settings::Num("d2k_hires", 1) == 0;
        settings::Save("d2k_hires", on ? "1" : "0");
        menu::Toast(on ? "High-res art on" : "High-res art off (original)", now);
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
    auto fn_pressed = [&](int f) { return controls::FnHeld(pressed, f); };

    if (menu::IsOpen()) {
      menu::Nav nav = menu::ReadNav(pad, keys);
      for (int b = 0; b < controls::kButtonCount; ++b)
        if (pressed >> b & 1) { nav.new_button = b; break; }
      switch (menu::Update(nav, now)) {
        case menu::kQuit: running = false; break;
        case t2k_menu::kToggleMusic: music_frames = 4; break;
        case t2k_menu::kCheatWarp:
          t2k::CheatWarp();
          menu::Toast("Warp after this level", now);
          break;
        case menu::kToggleFullscreen: set_fullscreen(!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)); break;
        case menu::kReturnToTitle:
          t2k::ReturnToTitle();
          quiet_frames = 180;
          break;
        case menu::kSaveState: {
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
        case menu::kLoadState: {
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
      // B (and Backspace) = back in the game's own menus.
      static bool prev_bksp = false;
      const bool bksp = keys[SDL_SCANCODE_BACKSPACE];
      if (((pressed >> 1) & 1 || (bksp && !prev_bksp)) && t2k::MenuScreen()) t2k::MenuBack();
      prev_bksp = bksp;
      if (!g_defender && fn_pressed(t2k_menu::kCamera)) {
        int v = (int(settings::Num("camera_view", 0)) + 1) % 3;
        settings::Save("camera_view", std::to_string(v));
        menu::Toast(std::string("Camera: ") + kViewNames[v], now);
      }
      // On the game's pause screen the Option button quits to the title.
      if (!g_defender && t2k::Paused() && fn_pressed(t2k_menu::kOption)) {
        t2k::ReturnToTitle();
        quiet_frames = 180;
      }
    }
    prev_back = back;

    // The pause screen's extra line names the first button bound to Option.
    if (!g_defender) {
      std::string b = controls::ButtonsFor(t2k_menu::kOption);
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
    // Original mode (setting native): every improvement off - the emulator's own picture, no remaster, no
    // run-ahead, no HUD layer, plain bilinear 4:3, no menu pacing. Controls (stick, remapping) stay.
    const bool native = settings::Num("native", 0) != 0;
    remaster::SetEnabled(!native && settings::Num("remaster", 1) != 0);
    const int run_ahead =
        !native && t2k::LatencyMatters() ? std::clamp(int(settings::Num("run_ahead", 2)), 0, 4) : 0;
    {
      // The HUD layer: the core leaves screen3's HUD rows out of the picture (we draw them at the window's corners).
      int ow = 0, oh = 0;
      SDL_GetRenderOutputSize(ren, &ow, &oh);
      const bool wide = ow * 3 != oh * 4;
      const bool layer = !native && remaster::Enabled() && settings::Num("hud_corners", 1) != 0 && t2k::HudShown() &&
                         !t2k::Demo() && wide;
      vjOPSkipLo = layer ? 0x50000u : 0;
      vjOPSkipHi = layer ? 0x50000u + 384u * 2u * 48u : 0;
      // Native scar feedback: the play buffers hold stale pictures (their scar was skipped): only the overlays show.
      remaster::SetFeedback(settings::Num("remaster_feedback", 1) != 0);
      const bool fb = !native && remaster::FeedbackLately();
      vjOPSkip2Lo = fb ? 0x100000u : 0;
      vjOPSkip2Hi = fb ? 0x134800u + 0x34800u : 0;   // (screensize: 384 x 280 words)
    }
    const uint64_t perf_t0 = perf::Now();
    perf::Count("frames emulated", todo);
    perf::Count("run-ahead frames", todo > 0 ? run_ahead : 0);
    for (int i = 0; i < todo; ++i) {
      t2k::Pad tp;
      // In the game's menus pad B means back: kept away from the game (where any button confirms).
      host::Input in = ReadPad(pad, t2k::MenuScreen() ? held & ~2u : held, keys, &tp);
      if (t2k::MenuTick()) in.buttons |= 1u << host::B;
      if (music_frames > 0) { in.buttons |= 1u << host::X; --music_frames; }
      if (quiet_frames > 0) --quiet_frames;
      host::SetInput(0, in);
      t2k::BeforeFrame(tp);
      host::SetVideoEnabled(i == todo - 1 && run_ahead == 0);   // (catch-up frames / the frame before run-ahead: unseen)
      d2k::BeforeFrame();
      host::RunFrame();
      d2k::AfterFrame();
      host::SetVideoEnabled(true);
      t2k::AfterFrame();
      // Run-ahead after the last real frame of this batch only (its picture is the one shown).
      if (run_ahead > 0 && i == todo - 1) {
        remaster::AfterFrame(false);
        perf::Span sp("  of it run-ahead");
        RunAhead(run_ahead, in, tp);
      } else {
        remaster::AfterFrame(i == todo - 1 || run_ahead == 0);
      }
      if (++frame_count % 60 == 0) eeprom.SaveIfChanged();
      auto& a = host::Audio();
      if (audio && !a.empty()) {
        // Keep the queue short: drop audio if more than ~100 ms is waiting.
        if (SDL_GetAudioStreamQueued(audio) < int(host::SampleRate() * 4 / 10))
          volume::Put(audio, a.data(), a.size());
      }
      a.clear();
    }

    perf_t1 = SDL_GetTicksNS();
    perf::Add("emulate", perf_t1 - perf_t0);
    perf::Add("input + events", perf_t0 - now);
    int ww, wh;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    // The CRT (setting crt, F9): everything drawn into a window-sized scene, which the CRT then draws to the window.
    crt::LoadSettings();
    const bool use_crt = !native && crt::Mode() > 0;
    static SDL_Texture* scene = nullptr;
    static int scene_w = 0, scene_h = 0;
    // Feedback textures (the native scar): the last frame shown and the next step, window-sized.
    static SDL_Texture* fb_last = nullptr;
    static SDL_Texture* fb_next = nullptr;
    static uint64_t fb_serial = 0;
    // Steps come every few frames (paced): the shown image crossfades from the step before (fb_next keeps it after
    // the swap) to the latest over the time between the last two steps.
    static uint64_t fb_step_at = 0, fb_step_gap = 0;
    static bool fb_have_prev = false;
    if (!scene || scene_w != ww || scene_h != wh) {
      for (SDL_Texture** t : {&fb_last, &fb_next}) {
        if (*t) SDL_DestroyTexture(*t);
        *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, ww, wh);
        SDL_SetTextureBlendMode(*t, SDL_BLENDMODE_NONE);
      }
      if (scene) SDL_DestroyTexture(scene);
      scene = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, ww, wh);
      SDL_SetTextureBlendMode(scene, SDL_BLENDMODE_NONE);
      SDL_SetTextureScaleMode(scene, SDL_SCALEMODE_LINEAR);
      scene_w = ww; scene_h = wh;
    }
    SDL_Texture* const frame_target = scene;
    SDL_SetRenderTarget(ren, frame_target);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    float crt_src_w = 0, crt_src_h = 0;   // the window in game pixels (scanlines / mask follow the game's lines)
    bool scar_scene = false;
    const auto& fr = host::LastFrame();
    if (fr.width) {
      if (!tex || tw != fr.width || th != fr.height) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, fr.width, fr.height);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        tw = fr.width; th = fr.height;
        todo = 1;
      }
      float sw = float(wh) * 4.f / 3.f, sh = float(wh);
      if (sw > ww) { sw = float(ww); sh = sw * 3.f / 4.f; }
      SDL_FRect dst{(ww - sw) / 2, (wh - sh) / 2, sw, sh};
      // HUD layer: in the game on a window that isn't 4:3 we draw the HUD strip (screen3) ourselves, the score / lives
      // part at the window's top left and the icon part at the top right (setting hud_corners), and its pixels are
      // see-through in the picture. screen3 (0, 0) is picture (-24, 14) (measured: exact match).
      constexpr int kHudX = -24, kHudY = 14;
      const bool hud_layer = vjOPSkipHi != 0;   // (set before emulating this batch)
      const host::Frame& hud = remaster::Hud();
      const int hud_h = hud.pixels.empty() ? 0 : std::min(t2k::HudRows(), int(hud.height));
      const std::string filter = native ? "smooth" : settings::Str("picture_filter", "sharp");
      const bool use_xbr = filter == "xbr";
      const bool sharp = filter == "sharp";
      static bool hud_was = false;
      static std::string filter_was;
      const bool dirty = todo > 0 || hud_layer != hud_was || filter != filter_was;
      filter_was = filter;
      hud_was = hud_layer;
      auto upload_x4 = [&](Sharp& sh, const std::vector<uint32_t>& px, int w, int h) {
        if (!sh.x4 || sh.w != w || sh.h != h || sh.k != -4) {
          if (sh.x4) SDL_DestroyTexture(sh.x4);
          sh.x4 = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w * 4, h * 4);
          SDL_SetTextureBlendMode(sh.x4, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
          SDL_SetTextureScaleMode(sh.x4, SDL_SCALEMODE_LINEAR);
          sh.w = w; sh.h = h; sh.k = -4;
        }
        SDL_UpdateTexture(sh.x4, nullptr, px.data(), w * 4 * 4);
      };
      static std::vector<uint32_t> hud_px(384 * 48);
      const uint64_t perf_pic = perf::Now();
      if (dirty) {
        // Black is see-through (alpha 0) so the remaster's layer, drawn first, shows where the game drew nothing.
        static std::vector<uint32_t> argb;
        argb.resize(fr.pixels.size());
        for (size_t i = 0; i < argb.size(); ++i) {
          uint32_t p = fr.pixels[i] & 0xFFFFFF;
          argb[i] = p ? (p | 0xFF000000u) : 0;
        }
        if (hud_layer) {
          if (!hudtex) {
            hudtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 384, 48);
            SDL_SetTextureBlendMode(hudtex, SDL_BLENDMODE_BLEND);
          }
          auto& hp = hud_px;
          std::fill(hp.begin(), hp.end(), 0u);
          for (int hy = 0; hy < hud_h && hy < 48; ++hy)
            for (int hx = 0; hx < 384 && hx < int(hud.width); ++hx) hp[size_t(hy) * 384 + hx] = hud.pixels[size_t(hy) * hud.width + hx];
          SDL_UpdateTexture(hudtex, nullptr, hp.data(), 384 * 4);
        }
        SDL_UpdateTexture(tex, nullptr, argb.data(), int(fr.width * 4));
        if (use_xbr) {
          static std::vector<uint32_t> up;
          xbr::Scale4x(argb.data(), int(fr.width), int(fr.height), up);
          upload_x4(pic_sharp, up, int(fr.width), int(fr.height));
          if (hud_layer && hudtex) {
            xbr::Scale4x(hud_px.data(), 384, 48, up);
            upload_x4(hud_sharp, up, 384, 48);
          }
        }
      }
      const int k = std::clamp(int(std::ceil(dst.h / float(fr.height))), 1, 12);
      // Sharp bilinear: the source scaled up k times with nearest into a target, then fitted with linear filtering.
      bool cleared = false;
      auto prep = [&](Sharp& sh, SDL_Texture* src, int w, int h, bool changed) {
        if (!sharp) { SDL_SetTextureScaleMode(src, SDL_SCALEMODE_LINEAR); return; }
        if (sh.k < 0) sh.k = 0;   // was the xBR texture's: rebuild the sharp target
        bool fresh = false;
        if (!sh.big || sh.k != k || sh.w != w || sh.h != h) {
          if (sh.big) SDL_DestroyTexture(sh.big);
          sh.big = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w * k, h * k);
          SDL_SetTextureBlendMode(sh.big, SDL_BLENDMODE_BLEND);
          SDL_SetTextureScaleMode(sh.big, SDL_SCALEMODE_LINEAR);
          sh.k = k; sh.w = w; sh.h = h;
          fresh = true;
        }
        if (!sh.big || !(changed || fresh)) return;
        SDL_SetRenderTarget(ren, sh.big);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
        SDL_RenderClear(ren);
        SDL_SetTextureScaleMode(src, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(src, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(ren, src, nullptr, nullptr);
        SDL_SetTextureBlendMode(src, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(ren, frame_target);
        cleared = true;
      };
      // A part of a source (in its pixels; null = all) to a window rectangle.
      auto blit = [&](const Sharp& sh, SDL_Texture* src, const SDL_FRect* s, const SDL_FRect* d) {
        if (use_xbr && sh.x4) {
          if (!s) { SDL_RenderTexture(ren, sh.x4, nullptr, d); return; }
          SDL_FRect s4{s->x * 4, s->y * 4, s->w * 4, s->h * 4};
          SDL_RenderTexture(ren, sh.x4, &s4, d);
          return;
        }
        if (!sharp || !sh.big) { SDL_RenderTexture(ren, src, s, d); return; }
        if (!s) { SDL_RenderTexture(ren, sh.big, nullptr, d); return; }
        SDL_FRect sk{s->x * sh.k, s->y * sh.k, s->w * sh.k, s->h * sh.k};
        SDL_RenderTexture(ren, sh.big, &sk, d);
      };
      prep(pic_sharp, tex, int(fr.width), int(fr.height), dirty);
      if (hud_layer && hudtex) prep(hud_sharp, hudtex, 384, 48, dirty);
      if (cleared) {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
      }
      perf::Add("picture upload", perf::Now() - perf_pic);
      const uint64_t perf_rm = perf::Now();
      if (remaster::Enabled() && !d2k::Active()) {
        remaster::SetLineWidth(float(settings::Num("remaster_line_width", 1.5)));
        remaster::SetEffects(float(settings::Num("remaster_bolt_width", 2.0)), settings::Num("remaster_bolt_glow", 1) != 0,
                             float(settings::Num("remaster_particle_size", 1.5)),
                             float(settings::Num("remaster_shot_glow", 0.5)),
                             float(settings::Num("remaster_shot_size", 0.5)));
        remaster::SetPowerUps(float(settings::Num("remaster_pup_floor", 0.7)),
                              float(settings::Num("remaster_pup_glow", 1.0)));
        // Blend between the last two frames by how far real time is into the current frame's slot (the frame
        // due at next - frame_ns was just emulated). Paused / menu: the latest frame.
        remaster::SetInterpolate(settings::Num("remaster_interpolate", 1) != 0);
        double t = menu::IsOpen() ? 1.0 : 1.0 - double(int64_t(next) - int64_t(SDL_GetTicksNS())) / frame_ns;
        remaster::SetWindow(dst, ww, wh, fr.width, fr.height);
        remaster::Interpolate(float(t));
        float corners[4][2], dim = 0;
        uint64_t serial = 0;
        bool melt = false;
        if (remaster::ScarMap(dst, fr.width, fr.height, ww, wh, corners, &dim, &serial, &melt)) {
          // A scar scene: the feedback steps once per published scene (the last frame through the map, dimmed, then
          // the scene's own draws on top), and the latest step is shown under the overlays.
          if (serial != fb_serial) {
            SDL_SetRenderTarget(ren, fb_next);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            // The melt: wobble / drip / blob (settings melt_*; owner: the original blobs and distorts).
            crt::Melt m{float(settings::Num("melt_wobble", 0.010)), float(settings::Num("melt_drip", 0.006)),
                        float(settings::Num("melt_blob", 0.35)), serial,
                        float(settings::Num("melt_spin", 1.0) * 3.14159265 / 180.0),
                        float(settings::Num("melt_mosaic", 0.15)),
                        float(settings::Num("melt_mosaic_size", 12) / 240.0)};   // (game pixels of the 240-line height)
            crt::DrawScar(ren, fb_last, corners, ww, wh, dim, melt ? &m : nullptr);
            remaster::Interpolate(1.f);
            remaster::SetHardPixels(true);
            remaster::Draw(ren, dst, fr.width, fr.height);
            remaster::SetHardPixels(false);
            std::swap(fb_last, fb_next);
            const uint64_t tnow = SDL_GetTicksNS();
            fb_have_prev = fb_serial != 0 && fb_step_at != 0;
            fb_step_gap = fb_step_at ? std::clamp<uint64_t>(tnow - fb_step_at, uint64_t(frame_ns), uint64_t(frame_ns * 8)) : 0;
            fb_step_at = tnow;
            fb_serial = serial;
            SDL_SetRenderTarget(ren, frame_target);
          }
          double a = 1.0;
          if (fb_have_prev && fb_step_gap && !menu::IsOpen() && settings::Num("remaster_interpolate", 1) != 0)
            a = std::clamp(double(SDL_GetTicksNS() - fb_step_at) / double(fb_step_gap), 0.0, 1.0);
          if (a < 1.0) {
            SDL_RenderTexture(ren, fb_next, nullptr, nullptr);
            SDL_SetTextureBlendMode(fb_last, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaModFloat(fb_last, float(a));
            SDL_RenderTexture(ren, fb_last, nullptr, nullptr);
            SDL_SetTextureAlphaModFloat(fb_last, 1.f);
            SDL_SetTextureBlendMode(fb_last, SDL_BLENDMODE_NONE);
          } else {
            SDL_RenderTexture(ren, fb_last, nullptr, nullptr);
          }
          scar_scene = true;
        } else {
          remaster::Draw(ren, dst, fr.width, fr.height);
        }
      }
      perf::Add("remaster (all)", perf::Now() - perf_rm);
      const uint64_t perf_ov = perf::Now();
      // The emulated picture over ours (text, not-yet-ours), then the HUD layer. Defender 2000: our picture
      // instead (setting d2k_native; F8 toggles), positions interpolated between the game's updates.
      if (d2k::Active() && !native && settings::Num("d2k_native", 1) != 0) {
        const double t = menu::IsOpen() ? 1.0 : 1.0 - double(int64_t(next) - int64_t(SDL_GetTicksNS())) / frame_ns;
        d2k::Draw(ren, dst, SDL_FRect{0, 0, float(ww), float(wh)}, float(std::clamp(t, 0.0, 1.0)));
      } else {
        blit(pic_sharp, tex, nullptr, &dst);
      }
      if (hud_layer && hudtex) {
        const float sx = dst.w / float(fr.width), sy = dst.h / float(fr.height);
        // The strip's content in clusters (columns with pixels, gaps of 16+ empty columns between them), each to
        // the nearer top corner whole: score / lives (however many lives) left, the warp pyramids right; head to
        // head, player 1 left and player 2 right. (Fixed zones cut a long lives row and the pyramids in two.)
        std::vector<bool> lit(384, false);
        for (int hy = 0; hy < hud_h; ++hy)
          for (int hx = 0; hx < 384 && hx < int(hud.width); ++hx)
            if (hud.pixels[size_t(hy) * hud.width + hx]) lit[hx] = true;
        auto part = [&](float x0, float x1, float dx, float dy) {
          SDL_FRect s{x0, 0, x1 - x0, float(hud_h)};
          SDL_FRect d{dst.x + (x0 + kHudX) * sx + dx, dst.y + kHudY * sy + dy, (x1 - x0) * sx, hud_h * sy};
          blit(hud_sharp, hudtex, &s, &d);
        };
        for (int x = 0; x < 384;) {
          if (!lit[x]) { ++x; continue; }
          int x0 = x, x1 = x, gap = 0;
          for (; x < 384 && gap < 16; ++x) {
            if (lit[x]) x1 = x, gap = 0;
            else ++gap;
          }
          bool left = (x0 + x1) / 2 < 192;
          part(float(std::max(0, x0 - 2)), float(std::min(384, x1 + 3)), left ? -dst.x : dst.x, -dst.y);
        }
      }
      if (!scar_scene) {   // the feedback starts from what was shown last
        SDL_SetRenderTarget(ren, fb_last);
        SDL_RenderTexture(ren, scene, nullptr, nullptr);
        SDL_SetRenderTarget(ren, frame_target);
      }
      perf::Add("picture + hud", perf::Now() - perf_ov);
      crt_src_w = ww * float(fr.width) / dst.w;
      crt_src_h = wh * float(fr.height) / dst.h;
    }
    const uint64_t perf_crt = perf::Now();
    SDL_SetRenderTarget(ren, nullptr);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (use_crt) crt::DrawPicture(ren, scene, SDL_FRect{0, 0, float(ww), float(wh)}, crt_src_w, crt_src_h);
    else SDL_RenderTexture(ren, scene, nullptr, nullptr);
    if (menu::Shown()) menu::Draw(ren, ww, wh);
    menu::DrawToast(ren, ww, wh, now);
    static int presents = 0;
    if (menu_test && presents++ == 0) menu::Open(menu_test == 2);
    if (winshot_at >= 0 && (menu_test ? presents : int(frame_count)) >= winshot_at) {   // --winshot=N:<bmp>: the window after N frames
      if (SDL_Surface* shot = SDL_RenderReadPixels(ren, nullptr)) {
        SDL_SaveBMP(shot, winshot_file.c_str());
        perf::DiscardFrame();
        SDL_DestroySurface(shot);
      }
      running = false;
    }
    perf_t2 = SDL_GetTicksNS();
    perf::Add("crt + menu", perf_t2 - perf_crt);
    perf::Add("compose", perf_t2 - perf_t1);
    SDL_RenderPresent(ren);
    {
      const uint64_t t3 = SDL_GetTicksNS();
      perf::Add("present (+vsync)", t3 - perf_t2);
      if (running) perf::EndFrame(t3 - now, ww, wh);
    }
  }
  perf::Flush();

  eeprom.SaveIfChanged();
  host::Unload();
  if (pad) SDL_CloseGamepad(pad);
  if (audio) SDL_DestroyAudioStream(audio);
  crt::Shutdown();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
