// t3k_main.cpp - Tempest 3000 (Nuon, 2000) on PC: SDL3 front end for the Nuance libretro core (stage 1).
//
//   t3k.exe [disc]                        play in a window (disc: Tempest 3000 .iso next to the program, in game\,
//                                         or in Nuance_067\)
//   --headless --frames=N                 run N fields without showing a window (as fast as the PC can; with
//                                         --timing=real one takes a real 60th of a second)
//   --timing=emulated|real --rounds=N     override settings timing / nuon_rounds
//   --shot=F:<file.bmp>                   save the picture after frame F (repeatable)
//   --press=F:<btn>[:len]                 hold a RetroPad button from frame F (default 6 frames)
//   --wav=<file.wav>                      dump the audio
//   --disasm=F:mpe:hexaddr:count          print count packets of MPE code after field F (headless)
//   --aotdump=F:mpe:src:len:base:file      dump a code module for the AOT generator (headless, absolute file)
//   --memdump=F:<prefix>                  dump the Nuon's memory after field F (headless; repeatable)
//   --perflog=<file.csv>                  one line per frame: timing and the core's run statistics
//   --userdata=<dir>                      save folder (default userdata next to the program)
//   (--frames and --press work in the window too: scripted timing tests)
//   --novsync                             present without waiting for the display
//   T3K_PERF=1                            a timing summary every 5 s on stderr
// Keys: arrows d-pad, Z = A, X = B, Enter = Start, Tab = Nuon, Q / E = L / R, I J K L = C up / left / down / right,
// F11 fullscreen, Esc quits (no settings menu yet).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <windows.h>
#undef min
#undef max

#include <algorithm>
#include <atomic>
#include <bit>
#include <map>
#include <mutex>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "nuon_host.h"
#include "settings.h"

namespace fs = std::filesystem;

namespace {

// A crash: the exception, the faulting address and the call stack as module+offset on stderr, for
// llvm-symbolizer --obj=<module> <offset> (with the debug core: tools\build_nuance_dbg.bat).
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
  auto where = [](void* a) {
    HMODULE m = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)a, &m))
      GetModuleFileNameA(m, name, sizeof(name));
    const char* base = std::strrchr(name, '\\');
    std::fprintf(stderr, "  %s+0x%llx\n", base ? base + 1 : name, (unsigned long long)((uint8_t*)a - (uint8_t*)m));
  };
  std::fprintf(stderr, "CRASH: exception 0x%08lX at:\n", ep->ExceptionRecord->ExceptionCode);
  where(ep->ExceptionRecord->ExceptionAddress);
  if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
    std::fprintf(stderr, "  (%s 0x%llx)\n", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                 (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
  // Walk the faulting thread's stack from the exception context.
  CONTEXT ctx = *ep->ContextRecord;
  for (int i = 0; i < 24 && ctx.Rip; ++i) {
    DWORD64 image = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &image, nullptr);
    if (i) where((void*)ctx.Rip);
    if (!fn) {   // a leaf (or JIT code): return address on top of the stack
      ctx.Rip = *(DWORD64*)ctx.Rsp;
      ctx.Rsp += 8;
      continue;
    }
    void* handler_data = nullptr;
    DWORD64 frame = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx.Rip, fn, &ctx, &handler_data, &frame, nullptr);
  }
  std::fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}

const char* kDefaults = R"(# Tempest 3000 settings.

# 1 = start fullscreen (F11 toggles and saves it).
fullscreen = 1
# 1 = wait for the display when presenting (no tearing).
vsync = 1
# emulated = steady game speed: every video field gets the same amount of emulation (nuon_rounds);
# real = Nuance's own timing (follows the PC clock: uneven).
timing = emulated
# Rounds per video field (one block on each of the 4 processors a round): more = the game draws more pictures a
# second (up to the flip_cap rate) but needs a faster PC. 200000 holds 30 pictures/s everywhere seen so far.
nuon_rounds = 200000
# Fields the emulation may work ahead (1-3): 2 lets a slow field borrow time from fast ones, for one field
# (16.7 ms) more input lag. 1 = lowest lag, but busy moments may show a field for 1 or 3 refreshes.
buffer_fields = 2
# Game speed. Tempest 3000 moves a fixed step per picture, so its speed is its picture rate: the minimum video
# fields between two pictures. 3 = 20 pictures/s (the owner's choice: most playable), 2 = 30, 1 = 60;
# 2.5 = 24 (on a 120 Hz display still even: 5 refreshes a picture; elsewhere 2-then-3 fields).
flip_cap = 3
)";

struct Shot { int frame; std::string path; };
struct Press { int frame; int btn; int len; };

int BtnByName(const std::string& n) {
  static const char* names[] = {"b", "y", "select", "start", "up", "down", "left", "right",
                                "a", "x", "l", "r", "l2", "r2", "l3", "r3"};
  for (int i = 0; i < 16; ++i)
    if (n == names[i]) return i;
  return -1;
}

bool WriteBmp(const std::string& path, const nuon::Frame& f) {
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

std::string FindDisc(const std::string& given) {
  if (!given.empty()) return given;
  fs::path base(SDL_GetBasePath());
  std::vector<fs::path> dirs = {base, base / "game", fs::current_path(), fs::current_path() / "Nuance_067",
                                base.parent_path().parent_path() / "Nuance_067"};
  for (auto& d : dirs) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(d, ec)) {
      auto name = e.path().filename().string();
      auto ext = e.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if ((ext == ".iso" || ext == ".chd") && name.find("Tempest") != std::string::npos) return e.path().string();
    }
  }
  return {};
}

// The owner's Xbox layout (as in their Nuance setup): A = Nuon A, B = B, X = C-left, Y = C-up, LB / RB = L / R,
// LT / L3 = C-down, RT / R3 = C-right, Start = Start, Back = Nuon. D-pad and left stick = Nuon d-pad.
uint16_t ReadPad(SDL_Gamepad* pad, const bool* keys) {
  uint16_t b = 0;
  auto set = [&](int bit, bool on) { if (on) b |= uint16_t(1u << bit); };
  if (pad) {
    auto btn = [&](SDL_GamepadButton x) { return SDL_GetGamepadButton(pad, x); };
    set(nuon::A, btn(SDL_GAMEPAD_BUTTON_SOUTH));
    set(nuon::B, btn(SDL_GAMEPAD_BUTTON_EAST));
    set(nuon::Y, btn(SDL_GAMEPAD_BUTTON_WEST));    // C-left
    set(nuon::X, btn(SDL_GAMEPAD_BUTTON_NORTH));   // C-up
    set(nuon::L, btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    set(nuon::R, btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    set(nuon::L2, btn(SDL_GAMEPAD_BUTTON_LEFT_STICK) || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000);
    set(nuon::R2, btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK) || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000);
    set(nuon::START, btn(SDL_GAMEPAD_BUTTON_START));
    set(nuon::SELECT, btn(SDL_GAMEPAD_BUTTON_BACK));
    const int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    set(nuon::UP, btn(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -16000);
    set(nuon::DOWN, btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > 16000);
    set(nuon::LEFT, btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -16000);
    set(nuon::RIGHT, btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > 16000);
  }
  if (keys) {
    set(nuon::UP, keys[SDL_SCANCODE_UP]);
    set(nuon::DOWN, keys[SDL_SCANCODE_DOWN]);
    set(nuon::LEFT, keys[SDL_SCANCODE_LEFT]);
    set(nuon::RIGHT, keys[SDL_SCANCODE_RIGHT]);
    set(nuon::A, keys[SDL_SCANCODE_Z]);
    set(nuon::B, keys[SDL_SCANCODE_X]);
    set(nuon::START, keys[SDL_SCANCODE_RETURN]);
    set(nuon::SELECT, keys[SDL_SCANCODE_TAB]);
    set(nuon::L, keys[SDL_SCANCODE_Q]);
    set(nuon::R, keys[SDL_SCANCODE_E]);
    set(nuon::X, keys[SDL_SCANCODE_I]);
    set(nuon::Y, keys[SDL_SCANCODE_J]);
    set(nuon::L2, keys[SDL_SCANCODE_K]);
    set(nuon::R2, keys[SDL_SCANCODE_L]);
  }
  return b;
}

// Per-frame timing (--perflog / T3K_PERF).
struct Perf {
  FILE* csv = nullptr;
  bool summary = false;
  uint64_t start = 0, last = 0, from = 0;
  uint64_t n = 0, run_sum = 0, run_max = 0, frame_max = 0, budget_breaks = 0, iters_sum = 0, stall_sum = 0;
  uint32_t last_field = 0, fields_skipped = 0;

  void Frame(uint64_t frame, uint64_t t_run0, uint64_t t_run1, uint64_t t_end) {
    nuon::RunStats s{};
    const bool have = nuon::GetRunStats(&s);
    const uint64_t frame_ns = last ? t_end - last : 0;
    last = t_end;
    if (csv) {
      std::fprintf(csv, "%llu,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%u,%u,%u,%llu,%llu,%llu,%llu,%u,%u\n",
                   (unsigned long long)frame, (t_end - start) / 1e6, frame_ns / 1e6, (t_run1 - t_run0) / 1e6,
                   (t_end - t_run1) / 1e6, (unsigned long long)s.iterations, (unsigned long long)s.mpe3_stalled,
                   s.field_fired, s.field_counter, s.vidsync_calls, (unsigned long long)s.mpe_packets[0],
                   (unsigned long long)s.mpe_packets[1], (unsigned long long)s.mpe_packets[2],
                   (unsigned long long)s.mpe_packets[3], s.vid_flips, s.time_elapsed_calls);
    }
    if (!summary || !have) return;
    ++n;
    run_sum += t_run1 - t_run0;
    run_max = std::max<uint64_t>(run_max, t_run1 - t_run0);
    frame_max = std::max(frame_max, frame_ns);
    if (!s.field_fired) ++budget_breaks;
    if (last_field && s.field_counter > last_field + 1) fields_skipped += s.field_counter - last_field - 1;
    last_field = s.field_counter;
    iters_sum += s.iterations;
    stall_sum += s.mpe3_stalled;
    if (t_end - from >= 5000000000ull) {
      const double secs = (t_end - from) / 1e9;
      std::fprintf(stderr,
                   "perf: %.1f frames/s, run avg %.2f ms max %.2f ms, frame max %.2f ms, %llu budget ends, "
                   "%u fields skipped, %.0f k iterations/frame, MPE3 waiting %.0f%%\n",
                   n / secs, run_sum / 1e6 / n, run_max / 1e6, frame_max / 1e6, (unsigned long long)budget_breaks,
                   fields_skipped, iters_sum / 1e3 / n, iters_sum ? 100.0 * stall_sum / iters_sum : 0.0);
      n = run_sum = run_max = frame_max = budget_breaks = iters_sum = stall_sum = 0;
      fields_skipped = 0;
      from = t_end;
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  std::string disc, wav, perflog, userdata, timing_arg, rounds_arg, flipcap_arg;
  bool headless = false, novsync = false;
  int frames = -1;   // headless default 600; the window runs until closed unless given
  std::vector<Shot> shots, memdumps;   // memdumps: --memdump=F:<prefix>
  std::vector<std::string> disasms;      // --disasm=F:mpe:hexaddr:count
  std::vector<std::string> aotdumps;     // --aotdump=F:mpe:hexsrc:len:hexbase:file
  std::vector<Press> presses;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&](const char* p) { return a.substr(std::strlen(p)); };
    if (a == "--headless") headless = true;
    else if (a == "--novsync") novsync = true;
    else if (a.rfind("--frames=", 0) == 0) frames = std::atoi(val("--frames=").c_str());
    else if (a.rfind("--wav=", 0) == 0) wav = val("--wav=");
    else if (a.rfind("--timing=", 0) == 0) timing_arg = val("--timing=");
    else if (a.rfind("--rounds=", 0) == 0) rounds_arg = val("--rounds=");
    else if (a.rfind("--flipcap=", 0) == 0) flipcap_arg = val("--flipcap=");
    else if (a.rfind("--perflog=", 0) == 0) perflog = val("--perflog=");
    else if (a.rfind("--disasm=", 0) == 0) disasms.push_back(val("--disasm="));
    else if (a.rfind("--aotdump=", 0) == 0) aotdumps.push_back(val("--aotdump="));
    else if (a.rfind("--memdump=", 0) == 0) {
      auto v = val("--memdump=");
      memdumps.push_back({std::atoi(v.substr(0, v.find(':')).c_str()), v.substr(v.find(':') + 1)});
    }
    else if (a.rfind("--userdata=", 0) == 0) userdata = val("--userdata=");
    else if (a.rfind("--shot=", 0) == 0) {
      auto v = val("--shot="), c = v.substr(0, v.find(':'));
      shots.push_back({std::atoi(c.c_str()), v.substr(v.find(':') + 1)});
    } else if (a.rfind("--press=", 0) == 0) {
      auto v = val("--press=");
      auto c1 = v.find(':'), c2 = v.find(':', c1 + 1);
      int btn = BtnByName(v.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1));
      if (btn < 0) { std::fprintf(stderr, "unknown button in %s\n", a.c_str()); return 1; }
      presses.push_back({std::atoi(v.substr(0, c1).c_str()), btn,
                         c2 == std::string::npos ? 6 : std::atoi(v.substr(c2 + 1).c_str())});
    } else if (a.rfind("--", 0) == 0) { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
    else disc = a;
  }
  // The core changes the working folder: make every path absolute first.
  if (!disc.empty()) disc = fs::absolute(disc).string();
  if (!wav.empty()) wav = fs::absolute(wav).string();
  if (!perflog.empty()) perflog = fs::absolute(perflog).string();
  for (auto& s : shots) s.path = fs::absolute(s.path).string();
  for (auto& s : memdumps) s.path = fs::absolute(s.path).string();
  const std::string sample_out = std::getenv("T3K_SAMPLE") ? fs::absolute(std::getenv("T3K_SAMPLE")).string() : "";

  SetUnhandledExceptionFilter(CrashFilter);
  if (!SDL_Init(SDL_INIT_VIDEO | (headless ? 0 : SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  const fs::path base(SDL_GetBasePath());
  const fs::path udir = userdata.empty() ? base / "userdata" : fs::absolute(userdata);
  fs::create_directories(udir);
  settings::SetDefaultText(kDefaults);
  settings::Load((udir / "t3k_settings.txt").string());   // (build/userdata is shared with t2k.exe)
  disc = FindDisc(disc);
  if (disc.empty()) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tempest 3000",
                             "Put your Tempest 3000 disc image (.iso) next to the program.", nullptr);
    return 1;
  }
  std::string err;
  if (!nuon::Load((base / "nuance_libretro.dll").string(), disc, (base / "system").string(), udir.string(), &err)) {
    std::fprintf(stderr, "load: %s\n", err.c_str());
    if (!headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tempest 3000", err.c_str(), nullptr);
    return 1;
  }
  std::fprintf(stderr, "loaded %s: %.3f fps, %.0f Hz audio\n", disc.c_str(), nuon::Fps(), nuon::SampleRate());
  if (!timing_arg.empty()) settings::Set("timing", timing_arg);
  if (!rounds_arg.empty()) settings::Set("nuon_rounds", rounds_arg);
  bool emulated = settings::Str("timing", "emulated") != "real";
  if (!nuon::SetTiming(emulated, settings::Num("nuon_rounds", 200000))) emulated = false;
  if (!flipcap_arg.empty()) settings::Set("flip_cap", flipcap_arg);
  const double flip_cap = settings::Num("flip_cap", 3);
  nuon::SetFlipCap(flip_cap);
  std::fprintf(stderr, "timing: %s\n",
               emulated ? ("emulated, " + settings::Str("nuon_rounds", "200000") + " rounds a field").c_str() : "real (PC clock)");

  Perf perf;
  perf.summary = std::getenv("T3K_PERF") != nullptr;
  if (!perflog.empty()) {
    perf.csv = std::fopen(perflog.c_str(), "w");
    if (perf.csv) std::fprintf(perf.csv, "frame,t_ms,frame_ms,run_ms,rest_ms,iterations,mpe3_stalled,field_fired,"
                                         "field_counter,vidsync_calls,mpe0,mpe1,mpe2,mpe3,vid_flips,time_elapsed\n");
  }
  perf.start = perf.from = SDL_GetTicksNS();
  std::vector<int16_t> wav_samples;

  auto scripted = [&](uint64_t f) {
    uint16_t b = 0;
    for (auto& p : presses)
      if (int64_t(f) >= p.frame && int64_t(f) < p.frame + p.len) b |= uint16_t(1u << p.btn);
    return b;
  };
  // T3K_SAMPLE=<file> (headless): a sampling profiler - another thread stops this one ~2000 times a second and counts
// where it was (module + offset; "jit" outside any module); tools/t3k_sample.py symbolizes the file.
  std::atomic<bool> sampling{false};
  std::thread sampler;
  const bool sample_on = !sample_out.empty();
  if (headless && sample_on) {
    HANDLE me = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &me, 0, FALSE, DUPLICATE_SAME_ACCESS);
    sampling = true;
    std::string out = sample_out;
    sampler = std::thread([me, out, &sampling] {
      std::map<std::pair<std::string, uint64_t>, uint64_t> counts;
      std::map<HMODULE, std::string> names;
      while (sampling) {
        Sleep(0);
        SDL_DelayPrecise(500000);
        if (SuspendThread(me) == DWORD(-1)) continue;
        CONTEXT c{};
        c.ContextFlags = CONTEXT_CONTROL;
        const bool ok = GetThreadContext(me, &c);
        ResumeThread(me);
        if (!ok) continue;
        HMODULE m = nullptr;
        std::string mod = "jit";
        uint64_t off = c.Rip;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)c.Rip, &m)) {
          auto it = names.find(m);
          if (it == names.end()) {
            char name[MAX_PATH] = "?";
            GetModuleFileNameA(m, name, sizeof(name));
            it = names.emplace(m, name).first;
          }
          mod = it->second;
          off = c.Rip - uint64_t(m);
        } else off = 0;   // JIT code: one bucket
        counts[{mod, off}]++;
      }
      FILE* f = std::fopen(out.c_str(), "w");
      if (f) {
        for (auto& [k, n] : counts) std::fprintf(f, "%llu\t%s\t0x%llx\n", (unsigned long long)n, k.first.c_str(), (unsigned long long)k.second);
        std::fclose(f);
      }
      CloseHandle(me);
    });
  }
  if (headless) {
    if (frames < 0) frames = 600;
    for (int f = 0; f < frames; ++f) {
      nuon::SetButtons(scripted(f));
      const uint64_t t0 = SDL_GetTicksNS();
      nuon::RunFrame();
      const uint64_t t1 = SDL_GetTicksNS();
      auto& au = nuon::Audio();
      if (!wav.empty()) wav_samples.insert(wav_samples.end(), au.begin(), au.end());
      au.clear();
      for (auto& spec : disasms) {   // F:mpe:hexaddr:count -> stdout
        int df = 0, dm = 0, dn = 0;
        unsigned da = 0;
        if (std::sscanf(spec.c_str(), "%d:%d:%x:%d", &df, &dm, &da, &dn) == 4 && df == f)
          std::printf("--- mpe%d %08X (field %d)\n%s", dm, da, f, nuon::Disasm(dm, da, dn).c_str());
      }
      for (auto& spec : aotdumps) {
        int df = 0, dm = 0;
        unsigned src = 0, len = 0, base = 0;
        char file[512] = {};
        if (std::sscanf(spec.c_str(), "%d:%d:%x:%u:%x:%511s", &df, &dm, &src, &len, &base, file) == 6 && df == f)
          std::fprintf(stderr, "aotdump %s: %d packets\n", file, nuon::AotDump(dm, src, len, base, file));
      }
      for (auto& d : memdumps)
        if (d.frame == f) {   // <prefix>_sys.bin ($80000000), _main.bin ($40000000), _mpe3.bin ($20000000)
          const char* names[] = {"_sys.bin", "_main.bin", "_mpe0.bin", nullptr, nullptr, "_mpe3.bin"};
          for (int w : {0, 1, 2, 5}) {
            size_t n = 0;
            const uint8_t* m = nuon::Memory(w, &n);
            if (!m) continue;
            std::ofstream o(d.path + names[w], std::ios::binary);
            o.write(reinterpret_cast<const char*>(m), std::streamsize(n));
          }
        }
      for (auto& s : shots)
        if (s.frame == f && !WriteBmp(s.path, nuon::LastFrame())) std::fprintf(stderr, "shot %s failed\n", s.path.c_str());
      perf.Frame(f, t0, t1, SDL_GetTicksNS());
    }
    if (sampler.joinable()) { sampling = false; sampler.join(); }
    if (!wav.empty()) WriteWav(wav, wav_samples, uint32_t(nuon::SampleRate()));
    if (perf.csv) std::fclose(perf.csv);
    nuon::Unload();
    SDL_Quit();
    return 0;
  }

  SDL_Window* win = SDL_CreateWindow("Tempest 3000", 1440, 1080, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Renderer* ren = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
  if (!ren) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
  if (settings::Num("fullscreen", 1) != 0) SDL_SetWindowFullscreen(win, true);
  const bool vsync_on = !novsync && settings::Num("vsync", 1) != 0;
  SDL_SetRenderVSync(ren, vsync_on ? 1 : 0);
  SDL_HideCursor();
  SDL_Texture* tex = nullptr;
  unsigned tw = 0, th = 0;

  SDL_AudioSpec spec{SDL_AUDIO_S16, 2, int(nuon::SampleRate())};
  SDL_AudioStream* audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (audio) SDL_ResumeAudioStreamDevice(audio);

  SDL_Gamepad* pad = nullptr;
  bool running = true;

  // The emulation runs on its own thread: field k starts at start + k / 60 s (emulated timing; real timing: as fast
  // as the core returns, it paces itself). The main thread presents at every vblank the newest field that is surely
  // finished (due one field ago), so at 120 Hz each field is shown for exactly two refreshes as long as a field
  // costs under 16.7 ms of PC time.
  const uint64_t field_ns = uint64_t(1e9 / nuon::Fps());
  const uint64_t buffer_fields = uint64_t(std::clamp(settings::Num("buffer_fields", 2), 1.0, 3.0));
  // pic: the game's picture count after the field (VidConfig / VidChangeBase calls).
  struct Slot { int64_t field = -1; uint64_t due = 0; uint32_t pic = 0; nuon::Frame frame; };
  Slot ring[8];
  std::mutex ring_mutex;
  std::atomic<uint16_t> pad_buttons{0};
  std::atomic<bool> emu_quit{false};
  std::atomic<int64_t> fields_done{0};
  // The field grid's origin, shared: the main thread nudges it away from the vblanks (see below).
  std::atomic<uint64_t> grid_start{SDL_GetTicksNS() + field_ns};
  nuon::ReleaseContext();   // the emulation thread takes the core's OpenGL context
  std::thread emu([&] {
    // (grid_start may move: the main thread shifts it by half a refresh when the grid sits on the vblanks.)
    for (int64_t k = 0; !emu_quit; ++k) {
      uint64_t due = 0;
      if (emulated) {
        due = grid_start.load() + uint64_t(k) * field_ns;
        const uint64_t now = SDL_GetTicksNS();
        if (now > due + 8 * field_ns) {   // fell far behind (a stall): restart the schedule from now
          grid_start = now - uint64_t(k) * field_ns;
          due = now;
        } else if (now + (buffer_fields - 1) * field_ns < due) {
          // buffer_fields > 1: a field may start that many fields early, so a slow field borrows time from fast
          // ones (it is still shown at due + one field).
          SDL_DelayPrecise(due - (buffer_fields - 1) * field_ns - now);
        }
      }
      nuon::SetButtons(pad_buttons.load() | scripted(uint64_t(k)));
      const uint64_t t0 = SDL_GetTicksNS();
      nuon::RunFrame();
      const uint64_t t1 = SDL_GetTicksNS();
      auto& au = nuon::Audio();
      if (audio && !au.empty()) {
        // The display's clock and the sound card's drift apart slowly: keep the queue short (~100 ms).
        if (SDL_GetAudioStreamQueued(audio) > int(nuon::SampleRate()) * 4 / 10) SDL_ClearAudioStream(audio);
        SDL_PutAudioStreamData(audio, au.data(), int(au.size() * 2));
      }
      au.clear();
      {
        std::lock_guard<std::mutex> lock(ring_mutex);
        Slot& sl = ring[k & 7];
        sl.field = k;
        sl.due = due ? due : SDL_GetTicksNS();
        nuon::RunStats rs{};
        sl.pic = nuon::GetRunStats(&rs) ? rs.vid_flips : uint32_t(k);
        sl.frame = nuon::LastFrame();
      }
      perf.Frame(uint64_t(k), t0, t1, SDL_GetTicksNS());
      fields_done = k + 1;
      if (frames >= 0 && k + 1 >= frames) break;
    }
    nuon::ReleaseContext();
  });

  int64_t shown = -1;
  // Picture pacing: with a fractional flip_cap (2.5: pictures come 2, 3, 2, 3 fields apart) each picture is shown
  // flip_cap fields after the previous one - the later of that and "finished, plus one field" - so on a 120 Hz
  // display every picture stays up for the same number of refreshes (5 at 2.5). Fields that repeat the picture
  // follow it. At a whole flip_cap the two times are the same.
  const uint64_t picture_ns = (emulated && flip_cap > 0) ? uint64_t(flip_cap * double(field_ns)) : 0;
  struct Planned { int64_t field; uint64_t show_at; };
  std::vector<Planned> plan;
  int64_t planned_to = -1;
  uint32_t plan_pic = 0;
  bool plan_any = false;
  uint64_t plan_show = 0;
  uint32_t shown_pic = 0;
  // Refreshes each picture stayed on screen, and fields never shown (stderr at exit: the smoothness measure).
  int presents_of_shown = 0;
  int64_t skipped = 0;
  std::vector<int64_t> hold_hist(8, 0);
  uint32_t near_edge = 0;   // the last 16 presents: 1 = the field grid was within 1.5 ms of the wake-up
  int grid_shifts = 0;
  std::vector<int64_t> odd_holds;   // fields not shown for exactly 2 refreshes (T3K_HOLDS=<file> writes them)
  uint64_t refresh_ns = 0;  // the display's refresh period (vsync on)
  if (const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win))) {
    const double hz = dm->refresh_rate_numerator > 0 ? double(dm->refresh_rate_numerator) / dm->refresh_rate_denominator
                                                     : double(dm->refresh_rate);
    if (hz > 20) refresh_ns = uint64_t(1e9 / hz);
    std::fprintf(stderr, "display %.3f Hz\n", hz);
  }
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running = false;
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) running = false;
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F11) {
        const bool on = !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN);
        SDL_SetWindowFullscreen(win, on);
        settings::Save("fullscreen", on ? "1" : "0");
      } else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) SDL_HideCursor();
      else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) SDL_ShowCursor();
      else if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
      else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && SDL_GetGamepadID(pad) == e.gdevice.which) {
        SDL_CloseGamepad(pad);
        pad = nullptr;
      }
    }
    pad_buttons = ReadPad(pad, SDL_GetKeyboardState(nullptr));

    // The newest finished field whose time has passed: due <= now - one field (emulated timing), else the newest;
    // with picture pacing, the newest planned field whose picture's time has come.
    {
      const uint64_t now = SDL_GetTicksNS();
      std::lock_guard<std::mutex> lock(ring_mutex);
      const Slot* pick = nullptr;
      if (picture_ns) {
        for (;;) {   // plan the fields that arrived, in order
          const Slot& sl = ring[(planned_to + 1) & 7];
          if (sl.field != planned_to + 1) break;
          const uint64_t ready = sl.due + field_ns;
          uint64_t at;
          if (!plan_any || sl.pic == plan_pic) at = plan_any ? std::max(ready, plan_show) : ready;
          else if (ready > plan_show + picture_ns + field_ns / 2) at = ready;   // the game fell behind: restart
          else at = std::max(ready, plan_show + picture_ns);
          if (!plan_any || sl.pic != plan_pic) plan_show = at;
          plan_any = true;
          plan_pic = sl.pic;
          plan.push_back({sl.field, at});
          planned_to = sl.field;
        }
        size_t take = SIZE_MAX;
        for (size_t j = 0; j < plan.size(); ++j)
          if (plan[j].show_at <= now) take = j;
        if (take != SIZE_MAX) {
          const Slot& sl = ring[plan[take].field & 7];
          if (sl.field == plan[take].field && sl.field > shown) pick = &sl;
          plan.erase(plan.begin(), plan.begin() + take + 1);
        }
        if (plan.size() > 64) plan.erase(plan.begin(), plan.end() - 8);   // (stalls)
      } else {
        for (const Slot& sl : ring) {
          if (sl.field < 0 || sl.field <= shown) continue;
          if (emulated && sl.due + field_ns > now) continue;
          if (!pick || sl.field > pick->field) pick = &sl;
        }
      }
      if (pick && pick->frame.width) {
        const nuon::Frame& f = pick->frame;
        if (!tex || tw != f.width || th != f.height) {
          if (tex) SDL_DestroyTexture(tex);
          tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, int(f.width), int(f.height));
          SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
          tw = f.width;
          th = f.height;
        }
        SDL_UpdateTexture(tex, nullptr, f.pixels.data(), int(f.width * 4));
        if (!picture_ns || pick->pic != shown_pic || shown < 0) {   // a new picture (or field without pacing)
          if (shown >= 0) {
            hold_hist[std::min(presents_of_shown, 7)]++;
            if (odd_holds.size() < 4000 && presents_of_shown != (picture_ns ? int(std::lround(2 * flip_cap)) : 2))
              odd_holds.push_back(shown);
            if (!picture_ns) skipped += pick->field - shown - 1;
          }
          presents_of_shown = 0;
          shown_pic = pick->pic;
        }
        shown = pick->field;
      }
    }
    int ww = 0, wh = 0;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (tex) {   // 4:3, as big as fits
      float h = float(wh), w = h * 4.f / 3.f;
      if (w > ww) { w = float(ww); h = w * 3.f / 4.f; }
      SDL_FRect dst{(ww - w) / 2, (wh - h) / 2, w, h};
      SDL_RenderTexture(ren, tex, nullptr, &dst);
    }
    SDL_RenderPresent(ren);
    ++presents_of_shown;
    // Keep the field grid away from the vblanks: a field becomes showable at due + one field; if that moment falls
    // within 1.5 ms of when this loop wakes after a vblank, timer jitter picks the refresh (1-then-3 holds). When
    // most of the last 16 presents were that close, shift the grid by half a refresh.
    if (emulated && vsync_on && refresh_ns) {
      const uint64_t tp = SDL_GetTicksNS();
      const uint64_t edge = grid_start.load() + field_ns;   // one grid edge; the others are field_ns apart
      const uint64_t ph = (tp >= edge ? tp - edge : refresh_ns * 1000 - (edge - tp) % (refresh_ns * 1000)) % refresh_ns;
      const bool close = ph < 1500000 || ph > refresh_ns - 1500000;
      near_edge = (near_edge << 1 | (close ? 1u : 0u)) & 0xFFFF;
      if (std::popcount(near_edge) > 8) {
        grid_start += refresh_ns / 2;
        near_edge = 0;
        ++grid_shifts;
      }
    }
    if (!vsync_on) SDL_DelayNS(1000000);
    if (frames >= 0 && fields_done >= frames) running = false;
  }
  emu_quit = true;
  emu.join();
  std::fprintf(stderr, picture_ns ? "pictures shown for 0..7+ refreshes:" : "fields shown for 0..7+ refreshes:");
  for (auto n : hold_hist) std::fprintf(stderr, " %lld", (long long)n);
  std::fprintf(stderr, "; fields never shown: %lld; grid shifts: %d\n", (long long)skipped, grid_shifts);
  if (const char* hf = std::getenv("T3K_HOLDS")) {
    if (FILE* f = std::fopen(hf, "w")) {
      for (auto k : odd_holds) std::fprintf(f, "%lld\n", (long long)k);
      std::fclose(f);
    }
  }
  nuon::MakeContextCurrent();
  if (perf.csv) std::fclose(perf.csv);
  if (audio) SDL_DestroyAudioStream(audio);
  if (tex) SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  nuon::Unload();
  SDL_Quit();
  return 0;
}
