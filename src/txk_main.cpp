// txk_main.cpp - TxK (PS Vita, 2014) on PC: SDL3 front end for the Vita3K libretro core (stage 1).
//
//   txk.exe [game]                        play in a window (game: the TxK folder, e.g. PCSB00494, next to the program,
//                                         in game\, or two folders up - the repo root for build\txk.exe)
//   --headless --frames=N                 run N frames without a window, paced to real time (default 1200)
//   --shot=F:<file.bmp>                   save the picture after frame F (repeatable)
//   --press=F:<btn>[:len]                 hold a RetroPad button from frame F (default 6 frames)
//   --stick=F:x,y[:len]                   hold the left stick at x,y (-1..1) from frame F (headless)
//   --touch=F:x,y[:len]                   touch the screen at x,y (0..1 on the picture) from frame F (headless)
//   --opt=key=value                       a core option (vita3k_...) for this run
//   --wav=<file.wav>                      dump the audio
//   --userdata=<dir>                      save folder (default userdata next to the program)
//   --setopt=F:key=value                  change a core option at frame F (headless; tests)
//   --reset=F                             restart the game at frame F (headless; tests)
//   --from-launcher                       started by tempest.exe (the menu's last item says "Return to launcher")
//   --menu[=all]                          open the settings menu at start (all sections open; tests)
//   --winshot=N:<file.bmp>                save the window after N presents, then quit (tests)
//   --memdump=F:<file>                    after frame F, every allocated guest range ("VMEM", then per range u32 addr,
//                                         u32 len, bytes; headless; for memory searches, tools/txk_memsearch.py)
//   --memseq=F0:F1:<dir>:<start>:<len>    after each frame F0..F1, guest bytes start..start+len (hex) to <dir>/fNNNNN.bin
//   --peek=F0:F1:<file>:<addr>[,<addr>..]  after each frame F0..F1, the 32-bit words at these guest addresses (hex)
//                                         as text, one line per frame (headless; claw position logs). An address
//                                         p>o reads the pointer at p, plus offset o; p>o0-o1 every word o0..o1.
//                                         TxK claw: 810E6850>24 (position, float)
// Settings (userdata\txk_settings.txt): fullscreen, vsync, and any vita3k_* key is handed to the core as an option.
// Firmware: vita_firmware\PSVUPDAT.PUP (system) and PSP2UPDAT.PUP (fonts) are linked into system\vita3k\ as
// firmware.pup / font.pup, the names the core tells the parts apart by.
// Pad (Xbox, remappable in the menu): A = Cross, B = Circle, X = Square, Y = Triangle, LB = Select, LT / RT = L / R
// (hold the claw / jump), Start = Start; fixed: d-pad, sticks, Back = settings menu (the game pauses while it is open).
// The game's prompts are rewritten to match (PatchPrompts): SELECT -> LB, its button symbols -> A / B / X / Y / LT / RT.
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
#include "lr_host.h"

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
struct MemDump { int frame; std::string path; };
struct MemSeq { int f0, f1; std::string dir; uint32_t start, len; };
// A peeked word: at addr, or (ptr) at the pointer stored at addr plus off.
struct PeekAddr { uint32_t addr; uint32_t off; bool ptr; };
struct PeekLog { int f0, f1; std::string path; std::vector<PeekAddr> addrs; FILE* out = nullptr; };

using ReadFn = size_t (*)(uint32_t, void*, size_t);
using WriteFn = size_t (*)(uint32_t, const void*, size_t);
using RangeFn = uint32_t (*)(uint32_t, uint32_t*);

// The game's button prompts, rewritten for our pad layout (txk_menu.cpp). The texts sit in the game's data (eboot.bin,
// loaded as is) and are overwritten in guest memory, each with a replacement no longer than it, zero-padded:
// - SELECT -> LB in the three prompts that name it (title options, level select mode, pause quit), and level select's
//   "& BUTTON STARTS" ('&' is the Cross symbol of the title font) -> A: exact strings, found by their bytes and
//   terminators.
// - The Vita button symbols of its menu font, in every text that says "button": 0x84 Cross -> A, 0x85 Circle -> B,
//   0x82 Triangle -> Y, 0x83 Square -> X, 0x88 / 0x89 L / R -> LT / RT (the d-pad arrows 0x80 0x81 0x86 0x87 stay).
//   Where "LT" / "RT" make a text longer, a colour code (~9xxx) is dropped from it to make room.
// Tried every 120 frames until done (at most 30 tries); again after a restart of the game (g_restarts).
int g_restarts = 0;

// A text with the button symbols named for the Xbox pad, fitted into `room` bytes; empty if it can't be.
std::string XboxGlyphs(const std::string& s, size_t room) {
  std::string out;
  for (char ch : s) {
    switch (static_cast<uint8_t>(ch)) {
      case 0x82: out += 'Y'; break;
      case 0x83: out += 'X'; break;
      case 0x84: out += 'A'; break;
      case 0x85: out += 'B'; break;
      case 0x88: out += "LT"; break;
      case 0x89: out += "RT"; break;
      default: out += ch;
    }
  }
  while (out.size() > room) {   // drop the last colour code
    size_t at = std::string::npos;
    for (size_t i = out.size(); i-- > 0;)
      if (out[i] == '~' && i + 5 <= out.size() && out[i + 1] == '9') { at = i; break; }
    if (at == std::string::npos) return "";
    out.erase(at, 5);
  }
  return out;
}

void PatchPrompts(int frame) {
  struct Patch { std::string from, to; bool done = false; };
  static std::vector<Patch> patches = {
      {"_?PRESS ~7779SELECT_? FOR OPTIONS", "_?PRESS ~7779LB_? FOR OPTIONS"},
      {"~9999PRESS SELECT TO CHANGE MODE", "~9999PRESS LB TO CHANGE MODE"},
      {"~9909PRESS _1START ~9909TO RESUME OR _3SELECT ~9909TO QUIT", "~9909PRESS _1START ~9909TO RESUME OR _3LB ~9909TO QUIT"},
      // Level select, in the title font: '&' is its Cross symbol (elsewhere '&' is an ampersand, so only here).
      {"_1DIRECTIONAL BUTTONS SELECT LEVEL   ~7779&  _1BUTTON STARTS", "_1DIRECTIONAL BUTTONS SELECT LEVEL   ~7779A  _1BUTTON STARTS"},
  };
  static bool glyphs_done = false;
  static int tries = 0, epoch = 0;
  if (epoch != g_restarts) {
    epoch = g_restarts;
    tries = 0;
    glyphs_done = false;
    for (auto& p : patches) p.done = false;
  }
  if (tries >= 30 || frame % 120 != 119) return;
  bool all = glyphs_done;
  for (auto& p : patches) all &= p.done;
  if (all) return;
  ++tries;
  auto rd = reinterpret_cast<ReadFn>(lr::CoreProc("llama_vita_read"));
  auto wr = reinterpret_cast<WriteFn>(lr::CoreProc("llama_vita_write"));
  auto next = reinterpret_cast<RangeFn>(lr::CoreProc("llama_vita_next_range"));
  if (!rd || !wr || !next) { tries = 30; std::fprintf(stderr, "prompts: the core can't write guest memory\n"); return; }
  auto eq = [](uint8_t a, char b) { return a == static_cast<uint8_t>(b); };   // bytes (the symbols are >= 0x80)
  // The game's own image is loaded at 0x81000000 and up; its data is a few MB at most.
  std::vector<uint8_t> buf;
  uint32_t len = 0;
  int glyph_texts = 0;
  for (uint32_t at = next(0x81000000u, &len); at && at < 0x84000000u; at = next(at + len, &len)) {
    if (len > (64u << 20)) continue;
    buf.resize(len);
    rd(at, buf.data(), len);
    bool found_here = false;
    for (auto& p : patches) {
      if (p.done) continue;
      const std::string want = std::string(1, '\0') + p.from + '\0';   // whole string: terminator before and after
      auto it = std::search(buf.begin(), buf.end(), want.begin(), want.end(), eq);
      if (it == buf.end()) continue;
      const uint32_t addr = at + uint32_t(it - buf.begin()) + 1;
      std::string out = p.to;
      out.resize(p.from.size(), '\0');
      p.done = wr(addr, out.data(), out.size()) == out.size();
      found_here = true;
      std::fprintf(stderr, "prompts: %s at %08X: %.40s\n", p.done ? "rewritten" : "write failed", addr, p.to.c_str());
    }
    // The symbols: in the range that holds the game's texts (where the SELECT prompts are), once.
    if (glyphs_done || !found_here) continue;
    glyphs_done = true;
    for (size_t i = 0; i < buf.size(); ++i) {
      const uint8_t c = buf[i];
      if (!(c == 0x82 || c == 0x83 || c == 0x84 || c == 0x85 || c == 0x88 || c == 0x89)) continue;
      size_t s = i, e = i;
      while (s > 0 && buf[s - 1]) --s;
      while (e < buf.size() && buf[e]) ++e;
      const std::string text(reinterpret_cast<const char*>(buf.data() + s), e - s);
      i = e;   // next text
      bool is_text = text.find("utton") != std::string::npos;   // "button" / "Button": a prompt, not other data
      for (char ch : text) {
        const uint8_t u = static_cast<uint8_t>(ch);
        if (!(u == '\n' || (u >= 0x20 && u < 0x7F) || (u >= 0x80 && u <= 0x89))) is_text = false;
      }
      if (!is_text) continue;
      std::string out = XboxGlyphs(text, text.size());
      if (out.empty()) { std::fprintf(stderr, "prompts: no room in a text at %08zX\n", at + s); continue; }
      out.resize(text.size(), '\0');
      if (wr(at + uint32_t(s), out.data(), out.size()) == out.size()) ++glyph_texts;
    }
    std::fprintf(stderr, "prompts: button symbols named in %d texts\n", glyph_texts);
  }
}

bool DumpGuest(const std::string& path) {
  auto rd = reinterpret_cast<ReadFn>(lr::CoreProc("llama_vita_read"));
  auto next = reinterpret_cast<RangeFn>(lr::CoreProc("llama_vita_next_range"));
  if (!rd || !next) { std::fprintf(stderr, "memdump: the core has no llama_vita_read\n"); return false; }
  FILE* o = std::fopen(path.c_str(), "wb");
  if (!o) return false;
  std::fwrite("VMEM", 1, 4, o);
  std::vector<uint8_t> buf;
  uint64_t total = 0;
  int ranges = 0;
  for (uint32_t at = 0x1000, len = 0;; at += len) {
    const uint32_t start = next(at, &len);
    if (!start) break;
    at = start;
    if (start >= 0x60000000u && start < 0x7F000000u) continue;   // GPU memory (Vita3K's 0x6..., 0x7...): no game state
    buf.resize(len);
    rd(start, buf.data(), len);
    std::fwrite(&start, 4, 1, o);
    std::fwrite(&len, 4, 1, o);
    std::fwrite(buf.data(), 1, len, o);
    total += len;
    ++ranges;
    if (uint64_t(start) + len >= 0x100000000ull) break;
  }
  std::fclose(o);
  std::fprintf(stderr, "memdump %s: %d ranges, %.1f MB\n", path.c_str(), ranges, total / 1048576.0);
  return true;
}

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
    set(lr::UP, btn(SDL_GAMEPAD_BUTTON_DPAD_UP));
    set(lr::DOWN, btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    set(lr::LEFT, btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
    set(lr::RIGHT, btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
  }
  if (keys) {
    set(lr::UP, keys[SDL_SCANCODE_UP]);
    set(lr::DOWN, keys[SDL_SCANCODE_DOWN]);
    set(lr::LEFT, keys[SDL_SCANCODE_LEFT]);
    set(lr::RIGHT, keys[SDL_SCANCODE_RIGHT]);
    set(lr::B, keys[SDL_SCANCODE_Z]);
    set(lr::A, keys[SDL_SCANCODE_X]);
    set(lr::Y, keys[SDL_SCANCODE_A]);
    set(lr::X, keys[SDL_SCANCODE_S]);
    set(lr::L, keys[SDL_SCANCODE_Q]);
    set(lr::R, keys[SDL_SCANCODE_W]);
    set(lr::START, keys[SDL_SCANCODE_RETURN]);
    set(lr::SELECT, keys[SDL_SCANCODE_TAB]);
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
  std::vector<StickHold> sticks, touches;   // touches: x, y from 0 to 1 on the picture
  std::vector<MemDump> memdumps;
  std::vector<MemSeq> memseqs;
  std::vector<PeekLog> peeks;
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
    } else if (a.rfind("--memdump=", 0) == 0) {
      auto v = val("--memdump=");
      memdumps.push_back({std::atoi(v.substr(0, v.find(':')).c_str()), v.substr(v.find(':') + 1)});
    } else if (a.rfind("--memseq=", 0) == 0) {
      MemSeq m{};
      char dir[512] = {};
      if (std::sscanf(val("--memseq=").c_str(), "%d:%d:%511[^:]:%x:%x", &m.f0, &m.f1, dir, &m.start, &m.len) == 5) {
        m.dir = dir;
        memseqs.push_back(m);
      }
    } else if (a.rfind("--peek=", 0) == 0) {
      auto v = val("--peek=");
      PeekLog p;
      const size_t c1 = v.find(':'), c2 = v.find(':', c1 + 1), c3 = v.rfind(':');
      p.f0 = std::atoi(v.substr(0, c1).c_str());
      p.f1 = std::atoi(v.substr(c1 + 1, c2 - c1 - 1).c_str());
      p.path = v.substr(c2 + 1, c3 - c2 - 1);
      for (size_t at = c3 + 1; at < v.size();) {
        size_t e = v.find(',', at);
        if (e == std::string::npos) e = v.size();
        const std::string item = v.substr(at, e - at);
        const size_t gt = item.find('>');
        const uint32_t ad = uint32_t(std::strtoul(item.substr(0, gt).c_str(), nullptr, 16));
        if (gt == std::string::npos) {
          p.addrs.push_back({ad, 0, false});
        } else {   // addr>off or addr>off0-off1 (every word from off0 to off1 inclusive)
          const std::string offs = item.substr(gt + 1);
          const size_t dash = offs.find('-');
          const uint32_t o0 = uint32_t(std::strtoul(offs.substr(0, dash).c_str(), nullptr, 16));
          const uint32_t o1 = dash == std::string::npos ? o0 : uint32_t(std::strtoul(offs.substr(dash + 1).c_str(), nullptr, 16));
          for (uint32_t o = o0; o <= o1; o += 4) p.addrs.push_back({ad, o, true});
        }
        at = e + 1;
      }
      peeks.push_back(p);
    } else if (a.rfind("--stick=", 0) == 0) {
      StickHold h{0, 0.f, 0.f, 6};
      std::sscanf(val("--stick=").c_str(), "%d:%f,%f:%d", &h.frame, &h.x, &h.y, &h.len);
      sticks.push_back(h);
    } else if (a.rfind("--touch=", 0) == 0) {
      StickHold h{0, 0.5f, 0.5f, 6};
      std::sscanf(val("--touch=").c_str(), "%d:%f,%f:%d", &h.frame, &h.x, &h.y, &h.len);
      touches.push_back(h);
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
      if (k.rfind("vita3k_", 0) == 0) lr::SetOption(k, settings::Str(k, ""));
    }
  }
  for (auto& [k, v] : opts) lr::SetOption(k, v);
  // (read by the core's sceMsgDialogInit, our branch llamasoft-vita). The process environment, before the DLL loads:
  // the core has its own C runtime, which copies the environment when it starts (_putenv would only reach ours).
  SetEnvironmentVariableA("VITA3K_AUTO_OK_DIALOG", settings::Str("auto_ok_dialog", "").c_str());

  std::string err;
  if (!lr::Load((base / "vita3k_libretro.dll").string(), game, system_dir.string(), udir.string(), &err)) {
    std::fprintf(stderr, "load: %s\n", err.c_str());
    if (!headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TxK", err.c_str(), nullptr);
    return 1;
  }
  std::fprintf(stderr, "loaded %s: %.3f fps, %.0f Hz audio\n", game.c_str(), lr::Fps(), lr::SampleRate());

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
    const double frame_ns = 1e9 / lr::Fps();
    for (int f = 0; f < frames; ++f) {
      const uint64_t due = t0 + uint64_t(f * frame_ns);
      if (const uint64_t now = SDL_GetTicksNS(); now < due) SDL_DelayPrecise(due - now);
      lr::SetButtons(scripted(f));
      float sx = 0, sy = 0;
      for (auto& h : sticks)
        if (f >= h.frame && f < h.frame + h.len) { sx = h.x; sy = h.y; }
      lr::SetSticks(int(sx * 32767), int(sy * 32767), 0, 0);
      {
        bool down = false;
        float tx = 0, ty = 0;
        for (auto& h : touches)
          if (f >= h.frame && f < h.frame + h.len) { down = true; tx = h.x; ty = h.y; }
        lr::SetTouch(down, tx, ty);
      }
      for (auto& o : setopts)
        if (o.frame == f) lr::SetOption(o.key, o.value);
      for (int r : resets)
        if (r == f) { lr::Reset(); ++g_restarts; }
      lr::RunFrame();
      PatchPrompts(f);
      auto& au = lr::Audio();
      if (!wav.empty()) wav_samples.insert(wav_samples.end(), au.begin(), au.end());
      au.clear();
      for (auto& s : shots)
        if (s.frame == f && !WriteBmp(s.path, lr::LastFrame())) std::fprintf(stderr, "shot %s failed\n", s.path.c_str());
      for (auto& d : memdumps)
        if (d.frame == f) DumpGuest(d.path);
      for (auto& m : memseqs) {
        if (f < m.f0 || f > m.f1) continue;
        auto rd = reinterpret_cast<ReadFn>(lr::CoreProc("llama_vita_read"));
        if (!rd) continue;
        std::vector<uint8_t> buf(m.len);
        rd(m.start, buf.data(), m.len);
        char name[64];
        std::snprintf(name, sizeof name, "/f%05d.bin", f);
        if (FILE* o = std::fopen((m.dir + name).c_str(), "wb")) {
          std::fwrite(buf.data(), 1, buf.size(), o);
          std::fclose(o);
        }
      }
      for (auto& p : peeks) {
        if (f < p.f0 || f > p.f1) continue;
        auto rd = reinterpret_cast<ReadFn>(lr::CoreProc("llama_vita_read"));
        if (!rd) continue;
        if (!p.out) p.out = std::fopen(p.path.c_str(), "w");
        if (!p.out) continue;
        float sx = 0, sy = 0;
        for (auto& h : sticks)
          if (f >= h.frame && f < h.frame + h.len) { sx = h.x; sy = h.y; }
        std::fprintf(p.out, "%d %.3f %.3f", f, sx, sy);
        for (const PeekAddr& pa : p.addrs) {
          uint32_t w = 0, ad = pa.addr;
          if (pa.ptr) {
            uint32_t base = 0;
            rd(pa.addr, &base, 4);
            ad = base + pa.off;
          }
          rd(ad, &w, 4);
          float fl;
          std::memcpy(&fl, &w, 4);
          std::fprintf(p.out, " %08X %g", w, fl);
        }
        std::fprintf(p.out, "\n");
        if (f == p.f1) { std::fclose(p.out); p.out = nullptr; }
      }
      if ((f + 1) % 600 == 0) {
        static uint64_t last_new = 0;
        std::fprintf(stderr, "frame %d: %.1f s, picture %ux%u, %llu new pictures in the last 600 frames\n", f + 1,
                     (SDL_GetTicksNS() - t0) / 1e9, lr::LastFrame().width, lr::LastFrame().height,
                     (unsigned long long)(lr::NewFrames() - last_new));
        last_new = lr::NewFrames();
      }
    }
    if (!wav.empty()) WriteWav(wav, wav_samples, uint32_t(lr::SampleRate()));
    lr::Unload();
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
  SDL_FRect pic{};   // where the picture was last drawn (render output pixels): touches map onto it
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
  // Core options the menu changes: handed to the core as they change (some apply when the game restarts).
  auto apply_core_options = [&] {
    for (const char* k : {"vita3k_resolution_multiplier", "vita3k_anisotropic_filtering"}) {
      const std::string v = settings::Str(k, "");
      if (!v.empty()) lr::SetOption(k, v);
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
            lr::Reset();
            ++g_restarts;   // (the prompts are rewritten again)
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
    {   // The Vita's touchscreen: the left mouse button on the picture - and a touch, which SDL (and on the Deck,
        // Proton) turns into it. TxK: a tap launches the Supertapper (its smart bomb). Not while our menu is open.
      float mx = 0, my = 0, rx = 0, ry = 0;
      const SDL_MouseButtonFlags mb = SDL_GetMouseState(&mx, &my);
      SDL_RenderCoordinatesFromWindow(ren, mx, my, &rx, &ry);
      const bool down = (mb & SDL_BUTTON_LMASK) && !menu::IsOpen() && pic.w > 0 && pic.h > 0;
      lr::SetTouch(down, down ? (rx - pic.x) / pic.w : 0.f, down ? (ry - pic.y) / pic.h : 0.f);
    }
    for (int k = 0; k < todo; ++k) {
      lr::SetButtons(ReadPad(pad, SDL_GetKeyboardState(nullptr)) | scripted(frame));
      if (pad)
        lr::SetSticks(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY),
                        SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX), SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
      lr::RunFrame();
      ++frame;
      PatchPrompts(frame);
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
    const lr::Frame& f = lr::LastFrame();
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
      pic = dst;
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
  lr::Unload();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
