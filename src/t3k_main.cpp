// t3k_main.cpp - Tempest 3000 (Nuon, 2000) on PC: SDL3 front end for the Nuance libretro core (stage 1).
//
//   t3k.exe [disc]                        play in a window (disc: Tempest 3000 .iso next to the program, in game\,
//                                         or in Nuance_067\)
//   --headless --frames=N                 run N fields without showing a window (as fast as the PC can; with
//                                         --timing=real one takes a real 60th of a second)
//   --timing=emulated|real --rounds=N     override settings timing / nuon_rounds
//   --hires[=0|1]                         override settings high_res (render at 720x480)
//   --shot=F:<file.bmp>                   save the picture after frame F (repeatable)
//   --nativecheck=F0:F1:<dir>              headless: each game picture shown in fields F0..F1 drawn natively at
//                                         360x240 on the emulated previous picture, against that picture's own
//                                         frame buffer (ground truth): mean error per picture, images in <dir>.
//                                         With NUANCE_ONLYTYPES=<types> the emulator draws only those types too.
//   --nativeshot=F:<file.bmp>             headless: the native renderer's picture after frame F, 1440x1080 (one
//                                         feedback step per game picture; emulated pictures where it can't yet)
//   --press=F:<btn>[:len]                 hold a RetroPad button from frame F (default 6 frames)
//   --wav=<file.wav>                      dump the audio
//   --disasm=F:mpe:hexaddr:count          print count packets of MPE code after field F (headless)
//   --aotdump=F:mpe:src:len:base:file      dump a code module for the AOT generator (headless, absolute file)
//   --analog=F:x,y[:len]                  hold the Nuon analog stick at x,y (-128..127) from field F (headless)
//   --poke=F:hexaddr:hexvalue[:len]       write a big-endian word into system RAM before fields F.. (headless)
//   --stick=F:x[:len]                     hold the left stick at x (-1..1) for the claw controller (headless)
//   --clawlog=F0:F1:<file>                headless: after each field F0..F1: field, stick x, claw position (lanes),
//                                         velocity, scale, the game's picture counter (claw feel study)
//   --memdump=F:<prefix>                  dump the Nuon's memory after field F (headless; repeatable)
//   --perflog=<file.csv>                  one line per frame: timing and the core's run statistics
//   --userdata=<dir>                      save folder (default userdata next to the program)
//   (--frames and --press work in the window too: scripted timing tests)
//   --novsync                             present without waiting for the display
//   --from-launcher                       started by tempest.exe (the menu's last item says "Return to launcher")
//   --menu[=all]                          open the settings menu at start (all sections open; tests)
//   --winshot=N:<file.bmp>                save the window after N presents, then quit (tests)
//   T3K_PERF=1                            a timing summary every 5 s on stderr
// Keys: arrows d-pad, Z = A, X = B, Enter = Start, Tab = Nuon, Q / E = L / R, I J K L = C up / left / down / right,
// A / D = the stick (claw), F3 picture blend, F4 frame-rate counter, F11 fullscreen, Esc = settings menu (pad: Back).
// The emulation pauses while the menu is open.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <windows.h>
#undef min
#undef max

#include <algorithm>
#include <atomic>
#include <bit>
#include <map>
#include <memory>
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

#include "controls.h"
#include "menu.h"
#include "nuon_host.h"
#include "settings.h"
#include "t3k_menu.h"
#include "t3k_native.h"

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
# 1 = cross-fade between the game's pictures (smooth-looking motion at any display rate, ~one picture more lag;
# F3 toggles). 0 = each picture shown as it is.
picture_blend = 1
# Native renderer (in development, F7 cycles): 0 = off, 1 = native pictures (where every part is done; others stay
# emulated), 2 = the native vector layer over the emulated picture (an alignment check), 3 = the vector layer alone.
native_view = 0
# With native_view 1: 1 = pictures we draw natively are skipped by the emulated renderers (the CPU saving: about
# 2.4x less emulation time in play), 0 = the emulator still draws them too.
native_skip = 1
# 1 = the native layer moves smoothly between the game's pictures (F6), 0 = it steps with them.
native_interpolate = 1
# 1 = the frame-rate counter (F4): the game's pictures a second, emulated fields a second.
show_rates = 0
# 1 = render at 720x480, twice the game's own 360x240 (experimental: some screens still have flaws; needs about 2.5x
# the CPU time). Read at start-up.
high_res = 0

# 1 = start without the "your controller configuration has changed" screen (the pad counts as connected from
# power-on); 0 = as the core always did.
skip_controller_screen = 1

# --- Stick controls for the claw (left stick; the d-pad keeps the original controls) ---
# stiff = Tempest 2000's lr handling (speed follows the stick, slow start then quick, stops on a lane);
# lr = push left / right: speed follows how far the stick is pushed, per picture; momentum = TxK's feel (below);
# off = stick acts as the d-pad.
stick_mode = stiff
lr_deadzone = 0.20
# Top speed at full deflection, in lanes per picture (the game's own d-pad top speed is 0.94).
lr_max_speed = 0.94
# Deflection curve: 1 = linear, higher = gentler near the centre.
lr_curve = 1.5
# Ramp from a new push to full speed, in pictures, and the fraction of speed on the first picture.
lr_ramp_pictures = 4
lr_ramp_start = 0.30
# Pictures the claw keeps easing after the stick is released (0 = stop dead, like the original).
lr_release_pictures = 1
# momentum = TxK's feel (measured on TxK): the stick accelerates the claw (a force, not a speed) up to a top speed;
# on release it glides and settles on a lane. The mo_ values below.
# Top speed, lanes a second (TxK 17.3).
mo_top_speed = 17.3
# Seconds from rest to top speed at full stick (TxK 0.35).
mo_accel_time = 0.35
# Pushing against the motion: that many times the acceleration (TxK turns round in about 4 frames).
mo_brake = 4
# Glide after release: seconds for the speed to halve (TxK 0.2; 0 = stop at once, then settle).
mo_glide = 0.2
# Pull to the nearest lane centre while gliding (1 = TxK; 0 = none, the claw may stop between lanes).
mo_settle = 1.0
# Stick response: acceleration = deflection ^ this (TxK 0.8), past the deadzone.
mo_curve = 0.8
mo_deadzone = 0.08
# stiff (T2K's lr, the owner's tuning there): top speed in lanes a second (T2K 0.41 a frame = 24.6), seconds of
# smoothstep ramp from st_ramp_start of it, deflection curve, deadzone, seconds of ease-off after release, then the
# claw settles on the nearest lane (st_snap 1) at st_snap_rate x the distance a second.
st_top_speed = 24.6
st_ramp_time = 0.33
st_ramp_start = 0.05
st_curve = 1.5
st_deadzone = 0.2
st_ease_time = 0.05
st_snap = 1
st_snap_rate = 20
# Past the last lane centre by this much (lanes) on release, the claw goes on to the next one (a tap = one lane).
st_snap_ahead = 0.2
)";

struct Shot { int frame; std::string path; };

// The draw gate (native renderer step 3, PLAN.md "Native renderer status"): the core asks once per picture, when the
// renderers start drawing it; we build it from the emulator's memory and, when every entry type is ours, the
// renderers skip it (the CPU saving). Pictures are filed under the flip that will show them.
// What each native_view (F7) setting does, for the toast and the corner label.
const char* NativeViewName(int v, bool skip) {
  switch (v) {
    case 0: return "F7 native: OFF";
    case 1: return skip ? "F7 native: ON, emulator skips" : "F7 native: ON, emulator draws too";
    case 2: return "F7 native: lines over emulated";
    default: return "F7 native: lines alone";
  }
}

namespace gate {
std::mutex mu;
std::map<uint32_t, std::shared_ptr<t3knative::Picture>> built;
int check_same = 0, check_differ = 0, check_missing = 0;   // --gate=1
std::atomic<uint64_t> cb_ns{0}, cb_calls{0};               // time spent building pictures in the callback
std::atomic<int> mode{0};   // 0 off, 1 build live only (the renderers still draw: --gate=1 checks), 2 skip complete ones
int Callback(int mpe, uint32_t flips) {
  if (mode.load() == 0) return 0;
  const uint64_t t0 = SDL_GetTicksNS();
  auto pic = std::make_shared<t3knative::Picture>();
  const bool ok = t3knative::BuildPictureLive(mpe, flips + 1, pic.get());
  cb_ns += SDL_GetTicksNS() - t0;
  ++cb_calls;
  if (!ok) return 0;
  const bool skip = mode.load() == 2 && pic->complete;
  // T3K_GATE_LOG=1: each decision (flip, skipped / drawn and the first type we don't draw)
  static const bool log = std::getenv("T3K_GATE_LOG") != nullptr;
  if (log) std::fprintf(stderr, "gatelog flip %u %s %08X\n", flips + 1, skip ? "skip" : "draw", pic->first_unknown);
  std::lock_guard<std::mutex> lock(mu);
  built[flips + 1] = pic;
  while (built.size() > 8) built.erase(built.begin());
  return skip ? 1 : 0;
}
// The picture built for this flip, if any (and everything older dropped).
std::shared_ptr<t3knative::Picture> Take(uint32_t flip) {
  std::lock_guard<std::mutex> lock(mu);
  std::shared_ptr<t3knative::Picture> p;
  auto it = built.find(flip);
  if (it != built.end()) p = it->second;
  built.erase(built.begin(), built.upper_bound(flip));
  return p;
}
}  // namespace gate
struct Press { int frame; int btn; int len; };
struct AnalogHold { int frame; int x, y, len; };

// Stick control of the claw (Tempest 3000). The claw is a game object at $8003E9E0: position (16.16 lanes), velocity,
// a speed scale (+8, normally 1.0) and flags (+12, 0x80000401: handler 0, a bounded value the game wraps / clamps
// on the web). The game sets the velocity from the d-pad each picture (input 40, 60, then 80, x 768) and adds
// velocity x scale to the position. So the stick presses the d-pad in its direction and sets the scale each field so
// that the speed follows our curve and ramp; the game does the moving, wrapping and clamping itself.
// Only while the object's signature is there (flags, and the renderer's copy at $800DC100 = the position).
// stick_mode momentum: TxK's feel (measured on TxK 2026-11-16, PLAN.md "Claw feel study"): the stick is a force, not
// a speed - it accelerates the claw up to a top speed, pushing against the motion brakes hard, and on release the
// claw glides (friction) and a spring settles it on a lane centre. Our model runs in real time (per field); the game
// claw follows it closed loop: each picture it is asked to move by (model - game position) + one picture ahead.
struct ClawControl {
  // momentum mode
  double mo_v = 0;              // model velocity, lanes a second (+ = position increasing = stick right)
  double mo_err = 0;            // model position - game position, lanes
  double mo_prev = 0;           // game position last field
  double mo_req = 0;            // displacement asked of the game for its next picture
  bool mo_live = false;         // mo_prev valid
  int mo_fields = 3;            // fields per picture, last interval
  int mo_since = 0;             // fields since the last picture
  int mo_stuck = 0;             // pictures in a row asked to move that didn't (the end of an open web)
  int mo_absent = 0;            // fields in a row without the claw signature (it blinks for a field at a wrap)

  int last_dir = 0;            // -1 / +1 while pushing, 0 not
  uint32_t last_picture = 0;   // the game's picture counter ($80110AA0)
  int pictures_pushed = 0;     // pictures since this push started
  int release_left = 0;        // pictures of ease-off left
  double release_speed = 0;    // speed at release (lanes per picture)
  double last_speed = 0;

  static uint32_t Get(const uint8_t* m, uint32_t a) {
    const uint8_t* q = m + (a & 0x7FFFFF);
    return uint32_t(q[0]) << 24 | uint32_t(q[1]) << 16 | uint32_t(q[2]) << 8 | q[3];
  }
  static void Put(uint8_t* m, uint32_t a, uint32_t v) {
    uint8_t* q = m + (a & 0x7FFFFF);
    q[0] = uint8_t(v >> 24); q[1] = uint8_t(v >> 16); q[2] = uint8_t(v >> 8); q[3] = uint8_t(v);
  }

  // The game's speed for its coming picture at d-pad input `input` (40 on a new push, then 60, 80), lanes a picture.
  static double GameSpeed(int input) { return std::abs(input) * 768.0 / 65536.0; }

  // stick_mode stiff: Tempest 2000's lr handling (the owner's T2K tuning, in real time): speed follows the stick with a
  // smoothstep ramp from a slow start (a short push moves about a lane, a held one gets quick), a short ease-off on
  // release, then the claw settles on the nearest lane centre - it never stops between lanes.
  int st_dir = 0;               // -1 / +1 while pushed
  double st_held = 0;           // seconds of the current push
  double st_release = 0;        // seconds since release (ease-off)
  double st_v0 = 0;             // speed at release

  void StiffModel(float stick_x, double pos, double dt) {
    const double dz = settings::Num("st_deadzone", 0.2);
    const double ax = std::fabs(stick_x);
    const double d = ax > dz ? std::min(1.0, (ax - dz) / (1.0 - dz)) : 0.0;
    if (d > 0) {
      const int dir = stick_x > 0 ? 1 : -1;
      if (dir != st_dir) { st_dir = dir; st_held = 0; }
      st_held += dt;
      const double t = std::clamp(st_held / std::max(0.01, settings::Num("st_ramp_time", 0.33)), 0.0, 1.0);
      const double r0 = std::clamp(settings::Num("st_ramp_start", 0.05), 0.0, 1.0);
      const double ramp = r0 + (1 - r0) * t * t * (3 - 2 * t);
      mo_v = dir * settings::Num("st_top_speed", 24.6) * std::pow(d, settings::Num("st_curve", 1.5)) * ramp;
      st_release = 0;
      st_v0 = mo_v;
      return;
    }
    st_dir = 0;
    st_held = 0;
    const double ease = settings::Num("st_ease_time", 0.05);
    st_release += dt;
    if (st_release < ease) {   // ease-off, as T2K's lr_release_frames
      mo_v = st_v0 * (1.0 - st_release / ease);
      return;
    }
    // Settle on a lane centre: the next one in the direction of travel once the claw is st_snap_ahead past the last
    // (a tap = one lane), else back to it.
    const double p = pos + mo_err;
    const double ahead = st_v0 > 0 ? 1 : st_v0 < 0 ? -1 : 0;
    const double centre = std::round(p + ahead * (0.5 - settings::Num("st_snap_ahead", 0.2))), gap = centre - p;
    if (std::fabs(gap) < 0.01 || settings::Num("st_snap", 1) == 0) {
      mo_v = 0;
      if (std::fabs(gap) < 0.01) mo_err = centre - pos;
      return;
    }
    const double v = gap * settings::Num("st_snap_rate", 20.0);   // lanes a second toward the centre
    const double cap = std::fabs(gap) / dt;                          // never past it
    mo_v = std::clamp(v, -cap, cap);
  }

  uint16_t Momentum(uint8_t* m, float stick_x, bool new_picture, bool stiff) {
    const double dt = 1.0 / 60.0;   // one field
    const double pos = int32_t(Get(m, 0x8003E9E0)) / 65536.0;
    if (!mo_live) {
      mo_live = true;
      mo_prev = pos;
      mo_v = mo_err = mo_req = 0;
      mo_stuck = 0;
      st_dir = 0;
      st_held = 0;
      st_release = 1e9;
      st_v0 = 0;
    }
    double moved = pos - mo_prev;
    mo_prev = pos;
    if (new_picture) {
      mo_fields = std::clamp(mo_since, 1, 6);
      mo_since = 0;
      // Round a closed web the game wraps the position; a jump bigger than any move = a wrap: count what was asked.
      if (std::fabs(moved) > 3.0) moved = mo_req;
      mo_err -= moved;
      // Asked to move but didn't: the end of an open web (the game clamps). Stop there.
      if (std::fabs(mo_req) > 0.05 && moved == 0) {
        if (++mo_stuck >= 2) { mo_v = 0; mo_err = 0; }
      } else {
        mo_stuck = 0;
      }
    } else if (moved != 0) {
      mo_err -= std::fabs(moved) > 3.0 ? mo_req : moved;
    }
    ++mo_since;

    // The model, one field.
    if (stiff) {
      StiffModel(stick_x, pos, dt);
    } else {
    const double dz = settings::Num("mo_deadzone", 0.08);
    const double ax = std::fabs(stick_x);
    const double u = ax > dz ? std::pow(std::min(1.0, (ax - dz) / (1.0 - dz)), settings::Num("mo_curve", 0.8)) : 0.0;
    const double top = settings::Num("mo_top_speed", 17.3);
    if (u > 0) {
      const int dir = stick_x > 0 ? 1 : -1;
      double a = top / std::max(0.02, settings::Num("mo_accel_time", 0.35)) * u;
      if (mo_v * dir < 0) a *= settings::Num("mo_brake", 4.0);   // pushing against the motion
      mo_v = std::clamp(mo_v + dir * a * dt, -top, top);
    } else {
      const double half_life = settings::Num("mo_glide", 0.2);
      mo_v *= half_life > 0.001 ? std::pow(0.5, dt / half_life) : 0.0;
      const double p = pos + mo_err, centre = std::round(p);
      mo_v += settings::Num("mo_settle", 1.0) * 27.8 * (centre - p) * dt;
      if (std::fabs(mo_v) < 0.3 && std::fabs(centre - p) < 0.02) {   // settled: exactly on the lane
        mo_v = 0;
        mo_err = centre - pos;
      }
    }
    }
    mo_err += mo_v * dt;

    // Ask the game for the model's position one picture ahead (it moves once a picture, a picture late).
    mo_req = mo_err + mo_v * dt * mo_fields * settings::Num("claw_lead", 0.5);
    const int input = int(Get(m, 0x801133D8));
    if (std::fabs(mo_req) < 1e-4) {
      mo_req = 0;
      // Not pressing; while the game still applies its late input, hold that picture to nothing.
      Put(m, 0x8003E9E8, input != 0 ? 0u : 0x10000u);
      return 0;
    }
    const int press = mo_req > 0 ? 1 : -1;
    const bool same = (input > 0 && press > 0) || (input < 0 && press < 0);
    const int next = same ? std::min(80, std::abs(input) + (new_picture ? 0 : 20)) : 40;
    double scale = std::clamp(std::fabs(mo_req) / GameSpeed(next), 0.0, 4.0);
    // Turning round: the game's next picture still applies its old input (the d-pad is read a picture late), the old
    // way - hold that picture still, the new way starts on the one after.
    if (input != 0 && !same) scale = 0;
    mo_req = press * scale * GameSpeed(next);
    Put(m, 0x8003E9E8, uint32_t(scale * 65536.0 + 0.5));
    return press > 0 ? uint16_t(1u << nuon::RIGHT) : uint16_t(1u << nuon::LEFT);
  }

  // stick_x: -1..1; dpad_lr: the physical d-pad's left / right bits (they win). Returns the d-pad bits to press.
  uint16_t Update(float stick_x, uint16_t dpad_lr) {
    size_t n = 0;
    uint8_t* const m = nuon::Memory(0, &n);
    const bool claw = m && n >= 0x800000 && Get(m, 0x8003E9EC) == 0x80000401U &&
                      Get(m, 0x800DC100) == Get(m, 0x8003E9E0);
    const std::string mode = settings::Str("stick_mode", "stiff");
    const bool ours = mode == "lr" || mode == "momentum" || mode == "stiff";
    if (!claw || !ours || dpad_lr) {
      if (claw && Get(m, 0x8003E9E8) != 0x10000) Put(m, 0x8003E9E8, 0x10000);
      last_dir = 0;
      pictures_pushed = 0;
      release_left = 0;
      if (claw || ++mo_absent > 8) mo_live = false;
      // No claw in play (menus, between lives): the stick is the d-pad, as in stick_mode off.
      if (!claw && !dpad_lr && ours && std::fabs(stick_x) > 0.5f)
        return stick_x > 0 ? uint16_t(1u << nuon::RIGHT) : uint16_t(1u << nuon::LEFT);
      return 0;
    }
    const uint32_t picture = Get(m, 0x80110AA0);
    const bool new_picture = picture != last_picture;
    last_picture = picture;
    mo_absent = 0;
    if (mode == "momentum" || mode == "stiff") return Momentum(m, stick_x, new_picture, mode == "stiff");

    const double dz = settings::Num("lr_deadzone", 0.2);
    const double ax = std::fabs(stick_x);
    const int dir = ax > dz ? (stick_x > 0 ? 1 : -1) : 0;
    double speed = 0;   // lanes per picture wanted
    if (dir) {
      if (dir != last_dir) pictures_pushed = 0;
      else if (new_picture) ++pictures_pushed;
      const double defl = std::pow(std::min(1.0, (ax - dz) / (1.0 - dz)), settings::Num("lr_curve", 1.5));
      const double ramp_n = std::max(1.0, settings::Num("lr_ramp_pictures", 4));
      const double r0 = settings::Num("lr_ramp_start", 0.3);
      const double t = std::min(1.0, pictures_pushed / ramp_n);
      const double ramp = r0 + (1.0 - r0) * (t * t * (3 - 2 * t));
      speed = settings::Num("lr_max_speed", 0.94) * defl * ramp;
      release_left = int(settings::Num("lr_release_pictures", 1));
      release_speed = speed;
    } else if (last_dir && release_left > 0) {
      if (new_picture) --release_left;
    }
    int press = dir;
    if (!dir && last_dir && release_left > 0) {   // ease off: keep moving the same way, slower
      press = last_dir;
      const double total = std::max(1.0, settings::Num("lr_release_pictures", 1));
      speed = release_speed * (release_left / (total + 1));
    }
    if (!press) {
      last_dir = 0;
      // The game applies the d-pad a picture late: while it still has input, hold that picture to the ease-off
      // speed (half the last, or 0 = stop dead) instead of its full d-pad speed; back to 1.0 once the input is 0.
      const int input = int(Get(m, 0x801133D8));
      if (input != 0) {
        const double ease = settings::Num("lr_release_pictures", 1) > 0 ? last_speed * 0.5 : 0.0;
        const double game_speed = std::abs(input) * 768.0 / 65536.0;
        Put(m, 0x8003E9E8, uint32_t(std::clamp(ease / game_speed, 0.0, 4.0) * 65536.0 + 0.5));
        last_speed = ease;
      } else if (Get(m, 0x8003E9E8) != 0x10000) {
        Put(m, 0x8003E9E8, 0x10000);
      }
      return 0;
    }
    if (dir) last_dir = dir;
    // The game's input for the coming picture: 40 on the first, then +20 up to 80.
    const int input = int(Get(m, 0x801133D8));
    const int same = (input > 0 && press > 0) || (input < 0 && press < 0);
    const int next = same ? std::min(80, std::abs(input) + (new_picture ? 0 : 20)) : 40;
    const double game_speed = next * 768.0 / 65536.0;
    const double scale = std::clamp(speed / game_speed, 0.0, 4.0);
    Put(m, 0x8003E9E8, uint32_t(scale * 65536.0 + 0.5));
    last_speed = speed;
    return press > 0 ? uint16_t(1u << nuon::RIGHT) : uint16_t(1u << nuon::LEFT);
  }
};
struct Poke { int frame; uint32_t addr, value; int len; };

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

// Game buttons: the functions bound in the settings menu (t3k_menu.cpp: the owner's Nuance layout by default). Fixed:
// d-pad and left stick = Nuon d-pad (the stick's x drives the claw in stick_mode lr), Back = settings menu.
uint16_t ReadPad(SDL_Gamepad* pad, const bool* keys) {
  uint16_t b = t3k_menu::NuonButtons(controls::Held(pad));
  auto set = [&](int bit, bool on) { if (on) b |= uint16_t(1u << bit); };
  if (pad) {
    auto btn = [&](SDL_GamepadButton x) { return SDL_GetGamepadButton(pad, x); };
    const int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    set(nuon::UP, btn(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -16000);
    set(nuon::DOWN, btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > 16000);
    const std::string sm = settings::Str("stick_mode", "stiff");
    const bool stick_lr = sm == "lr" || sm == "momentum" || sm == "stiff";   // then the claw controller handles x
    set(nuon::LEFT, btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || (!stick_lr && lx < -16000));
    set(nuon::RIGHT, btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || (!stick_lr && lx > 16000));
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
  // For the on-screen counter (F4): the game's flips (pictures) and the field counter, from the emulation thread.
  std::atomic<uint32_t> flips{0}, fields{0};

  void Frame(uint64_t frame, uint64_t t_run0, uint64_t t_run1, uint64_t t_end) {
    nuon::RunStats s{};
    const bool have = nuon::GetRunStats(&s);
    if (have) {
      flips.store(s.vid_flips, std::memory_order_relaxed);
      fields.store(s.field_counter, std::memory_order_relaxed);
    }
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
  std::string disc, wav, perflog, userdata, timing_arg, rounds_arg, flipcap_arg, hires_arg, winshot_file;
  bool headless = false, novsync = false, from_launcher = false;
  int menu_test = 0, winshot_at = -1;
  int frames = -1;   // headless default 600; the window runs until closed unless given
  std::vector<Shot> shots, memdumps, nativeshots, savestates;
  Shot loadstate{-1, ""};   // --loadstate=F:file: continue from field F + 1
  int check_f0 = -1, check_f1 = -1;
  std::string check_dir;   // memdumps: --memdump=F:<prefix>
  std::vector<std::string> disasms;      // --disasm=F:mpe:hexaddr:count
  std::vector<std::string> aotdumps;     // --aotdump=F:mpe:hexsrc:len:hexbase:file
  std::vector<Press> presses;
  std::vector<AnalogHold> analogs;   // --analog=F:x,y[:len] (-128..127)
  std::vector<Poke> pokes;           // --poke=F:hexaddr:hexvalue[:len] (system bus, big-endian 32-bit)
  int clawlog_f0 = -1, clawlog_f1 = -1;
  std::string clawlog_path;
  FILE* clawlog = nullptr;
  struct StickHold { int frame; float x; int len; };
  std::vector<StickHold> sticks;     // --stick=F:x[:len] (-1..1): the left stick for the claw controller
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&](const char* p) { return a.substr(std::strlen(p)); };
    if (a == "--headless") headless = true;
    else if (a == "--novsync") novsync = true;
    else if (a == "--from-launcher") from_launcher = true;
    else if (a == "--menu" || a == "--menu=all") menu_test = a == "--menu" ? 1 : 2;
    else if (a.rfind("--winshot=", 0) == 0) {
      auto v = val("--winshot=");
      winshot_at = std::atoi(v.substr(0, v.find(':')).c_str());
      winshot_file = v.substr(v.find(':') + 1);
    }
    else if (a.rfind("--frames=", 0) == 0) frames = std::atoi(val("--frames=").c_str());
    else if (a.rfind("--wav=", 0) == 0) wav = val("--wav=");
    else if (a.rfind("--timing=", 0) == 0) timing_arg = val("--timing=");
    else if (a.rfind("--rounds=", 0) == 0) rounds_arg = val("--rounds=");
    else if (a.rfind("--flipcap=", 0) == 0) flipcap_arg = val("--flipcap=");
    else if (a == "--hires") hires_arg = "1";
    else if (a.rfind("--hires=", 0) == 0) hires_arg = val("--hires=");
    else if (a.rfind("--perflog=", 0) == 0) perflog = val("--perflog=");
    else if (a.rfind("--disasm=", 0) == 0) disasms.push_back(val("--disasm="));
    else if (a.rfind("--stick=", 0) == 0) {
      StickHold h{0, 0.f, 6};
      std::sscanf(val("--stick=").c_str(), "%d:%f:%d", &h.frame, &h.x, &h.len);
      sticks.push_back(h);
    }
    else if (a.rfind("--clawlog=", 0) == 0) {
      char path[512] = {};
      if (std::sscanf(val("--clawlog=").c_str(), "%d:%d:%511[^\n]", &clawlog_f0, &clawlog_f1, path) == 3)
        clawlog_path = path;
    }
    else if (a.rfind("--poke=", 0) == 0) {
      Poke k{0, 0, 0, 1};
      std::sscanf(val("--poke=").c_str(), "%d:%x:%x:%d", &k.frame, &k.addr, &k.value, &k.len);
      pokes.push_back(k);
    }
    else if (a.rfind("--analog=", 0) == 0) {
      AnalogHold h{0, 0, 0, 6};
      std::sscanf(val("--analog=").c_str(), "%d:%d,%d:%d", &h.frame, &h.x, &h.y, &h.len);
      analogs.push_back(h);
    }
    else if (a.rfind("--aotdump=", 0) == 0) aotdumps.push_back(val("--aotdump="));
    else if (a.rfind("--memdump=", 0) == 0) {
      auto v = val("--memdump=");
      memdumps.push_back({std::atoi(v.substr(0, v.find(':')).c_str()), v.substr(v.find(':') + 1)});
    }
    else if (a.rfind("--userdata=", 0) == 0) userdata = val("--userdata=");
    else if (a.rfind("--gate=", 0) == 0) gate::mode = std::atoi(val("--gate=").c_str());
    else if (a.rfind("--nativecheck=", 0) == 0) {
      auto v = val("--nativecheck=");
      char dir[512] = {};
      std::sscanf(v.c_str(), "%d:%d:%511[^\n]", &check_f0, &check_f1, dir);
      check_dir = fs::absolute(dir).string();
      fs::create_directories(check_dir);
    }
    else if (a.rfind("--savestate=", 0) == 0 || a.rfind("--loadstate=", 0) == 0) {
      const bool save = a.rfind("--savestate=", 0) == 0;
      auto v = a.substr(12);
      Shot s{std::atoi(v.substr(0, v.find(':')).c_str()), v.substr(v.find(':') + 1)};
      if (save) savestates.push_back(s); else loadstate = s;
    }
    else if (a.rfind("--nativeshot=", 0) == 0) {
      auto v = val("--nativeshot="), c = v.substr(0, v.find(':'));
      nativeshots.push_back({std::atoi(c.c_str()), v.substr(v.find(':') + 1)});
    } else if (a.rfind("--shot=", 0) == 0) {
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
  for (auto& s : nativeshots) s.path = fs::absolute(s.path).string();
  if (!winshot_file.empty()) winshot_file = fs::absolute(winshot_file).string();
  for (auto& s : memdumps) s.path = fs::absolute(s.path).string();
  if (!clawlog_path.empty()) clawlog_path = fs::absolute(clawlog_path).string();
  for (auto& s : savestates) s.path = fs::absolute(s.path).string();
  if (!loadstate.path.empty()) loadstate.path = fs::absolute(loadstate.path).string();
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
  t3k_menu::Install(from_launcher);
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
  // High resolution: 720x480 rendering (before the first field).
  if (!hires_arg.empty()) settings::Set("high_res", hires_arg);
  if (settings::Num("high_res", 0) != 0) {
    if (nuon::SetHighRes(true)) std::fprintf(stderr, "high resolution: 720x480\n");
    else std::fprintf(stderr, "high resolution: not supported by this core\n");
  }
  std::fprintf(stderr, "loaded %s: %.3f fps, %.0f Hz audio\n", disc.c_str(), nuon::Fps(), nuon::SampleRate());
  if (!timing_arg.empty()) settings::Set("timing", timing_arg);
  if (!rounds_arg.empty()) settings::Set("nuon_rounds", rounds_arg);
  bool emulated = settings::Str("timing", "emulated") != "real";
  if (!nuon::SetTiming(emulated, settings::Num("nuon_rounds", 200000))) emulated = false;
  if (!flipcap_arg.empty()) settings::Set("flip_cap", flipcap_arg);
  double flip_cap = settings::Num("flip_cap", 3);   // (the settings menu can change it)
  nuon::SetFlipCap(flip_cap);
  nuon::EnableCapture(check_f0 >= 0 ? 2 : 1);   // the native renderer's input (t3k_native); 2: + ground truth
  nuon::SetPictureCallback(gate::Callback);       // the draw gate (gate::mode: 0 = off)
  if (!headless && loadstate.frame >= 0) {         // the window starts from a saved state too (tests, repros)
    if (!nuon::LoadState(loadstate.path)) {
      std::fprintf(stderr, "loadstate %s failed\n", loadstate.path.c_str());
      return 1;
    }
    std::fprintf(stderr, "loadstate %s\n", loadstate.path.c_str());
  }
  // The pad is there from power-on: no "your controller configuration has changed" screen (setting
  // skip_controller_screen, default on).
  nuon::SetPadPresentAtBoot(settings::Num("skip_controller_screen", 1) != 0);
  // T3K_PAD_PROPS=<hex>: extra controller properties reported to the game (tests).
  if (const char* pp = std::getenv("T3K_PAD_PROPS")) nuon::SetPadProperties(uint32_t(std::strtoul(pp, nullptr, 16)));
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
  auto scripted_analog = [&](int f) {
    int x = 0, y = 0;
    for (auto& h : analogs)
      if (f >= h.frame && f < h.frame + h.len) { x = h.x; y = h.y; }
    if (!analogs.empty()) nuon::SetAnalog(x, y, 0, 0);
  };
  ClawControl headless_claw;
  // --nativeshot: the native renderer on a hidden window, one full feedback step per game picture.
  SDL_Window* nwin = nullptr;
  SDL_Renderer* nren = nullptr;
  SDL_Texture *nout = nullptr, *nemu = nullptr;
  t3knative::Renderer nnative;
  uint32_t nflip = 0;
  nuon::Frame nprev_frame;
  int ncomplete = 0, nincomplete = 0;
  const SDL_FRect ndst{0, 0, 1440, 1080};
  if (headless && (!nativeshots.empty() || check_f0 >= 0)) {
    nwin = SDL_CreateWindow("t3k native", 64, 64, SDL_WINDOW_HIDDEN);
    nren = nwin ? t3knative::CreateRenderer(nwin) : nullptr;
    if (nren) {
      t3knative::InitPipeline(nren);
      nout = SDL_CreateTexture(nren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 1440, 1080);
      nemu = SDL_CreateTexture(nren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 360, 240);
      SDL_SetTextureScaleMode(nemu, SDL_SCALEMODE_LINEAR);
    } else {
      std::fprintf(stderr, "nativeshot: no renderer (%s)\n", SDL_GetError());
    }
  }
  if (headless) {
    if (frames < 0) frames = 600;
    int first = 0;
    if (loadstate.frame >= 0) {   // field numbers carry on from where the state was saved
      if (!nuon::LoadState(loadstate.path)) {
        std::fprintf(stderr, "loadstate %s failed\n", loadstate.path.c_str());
        return 1;
      }
      first = loadstate.frame + 1;
      std::fprintf(stderr, "loadstate %s: continuing at field %d\n", loadstate.path.c_str(), first);
    }
    for (int f = first; f < frames; ++f) {
      {
        float sx = 0.f;
        for (auto& h : sticks)
          if (f >= h.frame && f < h.frame + h.len) sx = h.x;
        const uint16_t b = scripted(f);
        const uint16_t lr = uint16_t((1u << nuon::LEFT) | (1u << nuon::RIGHT));
        nuon::SetButtons(b | headless_claw.Update(sx, b & lr));
      }
      scripted_analog(f);
      for (auto& k : pokes)
        if (f >= k.frame && f < k.frame + k.len) {
          size_t n = 0;
          if (uint8_t* m = nuon::Memory(0, &n); m && (k.addr & 0x7FFFFF) + 4 <= n) {
            uint8_t* q = m + (k.addr & 0x7FFFFF);
            q[0] = uint8_t(k.value >> 24); q[1] = uint8_t(k.value >> 16); q[2] = uint8_t(k.value >> 8); q[3] = uint8_t(k.value);
          }
        }
      const uint64_t t0 = SDL_GetTicksNS();
      nuon::RunFrame();
      const uint64_t t1 = SDL_GetTicksNS();
      if (!clawlog_path.empty() && f >= clawlog_f0 && f <= clawlog_f1) {
        if (!clawlog) clawlog = std::fopen(clawlog_path.c_str(), "w");
        size_t n = 0;
        if (const uint8_t* m = nuon::Memory(0, &n); clawlog && m && n >= 0x800000) {
          float sx = 0.f;
          for (auto& h : sticks)
            if (f >= h.frame && f < h.frame + h.len) sx = h.x;
          auto get = [&](uint32_t a) { return int32_t(ClawControl::Get(m, a)); };
          std::fprintf(clawlog, "%d %.3f %.5f %.5f %.4f %d %d %08X\n", f, sx, get(0x8003E9E0) / 65536.0,
                       get(0x8003E9E4) / 65536.0, get(0x8003E9E8) / 65536.0, get(0x80110AA0), get(0x801133D8),
                       uint32_t(get(0x8003E9EC)));
        }
        if (f == clawlog_f1 && clawlog) { std::fclose(clawlog); clawlog = nullptr; }
      }
      auto& au = nuon::Audio();
      if (!wav.empty()) wav_samples.insert(wav_samples.end(), au.begin(), au.end());
      au.clear();
      if (gate::mode.load() == 1) {   // --gate=1: the live-built picture vs the one built from the capture
        static nuon::Capture gcap;
        static uint32_t gflip = 0;
        int& same = gate::check_same;
        int& differ = gate::check_differ;
        int& missing = gate::check_missing;
        if (nuon::GetCapture(&gcap) && gcap.flip != gflip) {
          gflip = gcap.flip;
          t3knative::Picture a;
          t3knative::BuildPicture(gcap, &a);
          auto b = gate::Take(gcap.flip);
          if (!b) {
            ++missing;
          } else {
            bool eq = a.complete == b->complete && a.ops.size() == b->ops.size() && a.segs.size() == b->segs.size() &&
                      a.blobs.size() == b->blobs.size() && a.glyphs.size() == b->glyphs.size() &&
                      a.webs.size() == b->webs.size();
            for (size_t i = 0; eq && i < a.segs.size(); ++i)
              eq = std::fabs(a.segs[i].x0 - b->segs[i].x0) < 1e-3f && std::fabs(a.segs[i].y1 - b->segs[i].y1) < 1e-3f;
            for (size_t i = 0; eq && i < a.blobs.size(); ++i)
              eq = std::fabs(a.blobs[i].x - b->blobs[i].x) < 1e-3f && std::fabs(a.blobs[i].colour.a - b->blobs[i].colour.a) < 1e-4f;
            for (size_t i = 0; eq && i < a.glyphs.size(); ++i) eq = a.glyphs[i].rgba == b->glyphs[i].rgba;
            for (size_t i = 0; eq && i < a.webs.size(); ++i)
              eq = std::equal(std::begin(a.webs[i].texture), std::end(a.webs[i].texture), std::begin(b->webs[i].texture));
            (eq ? same : differ) += 1;
            if (!eq && differ <= 5)
              std::printf("gate: flip %u differs: ops %zu/%zu segs %zu/%zu blobs %zu/%zu glyphs %zu/%zu complete %d/%d\n",
                          gcap.flip, a.ops.size(), b->ops.size(), a.segs.size(), b->segs.size(), a.blobs.size(),
                          b->blobs.size(), a.glyphs.size(), b->glyphs.size(), a.complete, b->complete);
          }
        }
      }
      if (gate::mode.load() && f == frames - 1) {
        uint32_t drawn = 0, skipped = 0;
        nuon::GateStats(&drawn, &skipped);
        std::printf("gate: %u pictures drawn by the renderers, %u skipped; building them took %.2f ms a picture\n",
                    drawn, skipped, gate::cb_calls ? double(gate::cb_ns) / 1e6 / double(gate::cb_calls) : 0.0);
        if (gate::mode.load() == 1)
          std::printf("gate check: %d pictures the same, %d differ, %d not built live\n", gate::check_same,
                      gate::check_differ, gate::check_missing);
      }
      if (nren) {   // --nativeshot: each new game picture, natively where it can be
        static nuon::Capture ncap;
        if (nuon::GetCapture(&ncap) && ncap.flip != nflip) {
          nflip = ncap.flip;
          t3knative::Picture pic;
          // skipped by the renderers (--gate=2): the picture built from memory when they would have started
          auto live = gate::mode.load() == 2 ? gate::Take(ncap.flip) : nullptr;
          if (live) pic = *live;
          else t3knative::BuildPicture(ncap, &pic);
          SDL_SetRenderTarget(nren, nout);
          if (pic.complete) {
            // T3K_NATIVE_RESEED=1 (tests): every native picture builds on the emulated previous one (one-step error)
            static const bool reseed = std::getenv("T3K_NATIVE_RESEED") != nullptr;
            if (reseed && nprev_frame.width) {
              float tw = 0, th = 0;
              SDL_GetTextureSize(nemu, &tw, &th);
              if (int(tw) != int(nprev_frame.width) || int(th) != int(nprev_frame.height)) {
                SDL_DestroyTexture(nemu);
                nemu = SDL_CreateTexture(nren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                         int(nprev_frame.width), int(nprev_frame.height));
                SDL_SetTextureScaleMode(nemu, SDL_SCALEMODE_LINEAR);
              }
              SDL_UpdateTexture(nemu, nullptr, nprev_frame.pixels.data(), int(nprev_frame.width * 4));
              nnative.Seed(nren, nemu, ndst);
            }
            nnative.Draw(nren, pic, pic.segs, ndst, 1.f);
            ++ncomplete;
          } else {   // the emulated picture (and what the next native one's feedback builds on)
            const nuon::Frame& fr = nuon::LastFrame();
            if (fr.width) {
              float tw = 0, th = 0;
              SDL_GetTextureSize(nemu, &tw, &th);
              if (int(tw) != int(fr.width) || int(th) != int(fr.height)) {
                SDL_DestroyTexture(nemu);
                nemu = SDL_CreateTexture(nren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, int(fr.width),
                                         int(fr.height));
              }
              SDL_UpdateTexture(nemu, nullptr, fr.pixels.data(), int(fr.width * 4));
              nnative.Seed(nren, nemu, ndst);
              SDL_SetTextureBlendMode(nemu, SDL_BLENDMODE_NONE);
              SDL_RenderTexture(nren, nemu, nullptr, &ndst);
            }
            ++nincomplete;
          }
          SDL_SetRenderTarget(nren, nullptr);
          nprev_frame = nuon::LastFrame();   // this picture, emulated (T3K_NATIVE_RESEED: the next one builds on it)
        }
        // --nativecheck: this picture natively at 360x240 on the emulated previous one vs its own frame buffer.
        static uint32_t check_flip = 0;
        static std::vector<uint32_t> check_prev;   // the previous picture's ground truth (XRGB)
        static t3knative::Renderer checker;
        static SDL_Texture *ck_out = nullptr, *ck_seed = nullptr;
        static double ck_sum = 0;
        static int ck_n = 0, ck_saved = 0;
        // T3K_CHECK_MASK=<span log> (NUANCE_SPANLOG of one type, same run or a run from the same state): also the error
        // over just the pixels that type wrote in each picture (MPE0's bands), so the rest of the picture can't swamp
        // it. The span log numbers pictures by the flip count while they are drawn: picture = that + T3K_CHECK_MASK_OFS
        // (default 1).
        static std::unordered_map<uint32_t, std::vector<uint8_t>> ck_mask;
        static bool ck_mask_loaded = false;
        static double ck_msum = 0;
        static int ck_mn = 0;
        if (!ck_mask_loaded) {
          ck_mask_loaded = true;
          if (const char* mp = std::getenv("T3K_CHECK_MASK")) {
            const int ofs = std::getenv("T3K_CHECK_MASK_OFS") ? std::atoi(std::getenv("T3K_CHECK_MASK_OFS")) : 1;
            std::ifstream in(mp);
            std::string line;
            uint32_t pic = 0;
            size_t marked = 0;
            while (std::getline(in, line)) {
              if (line.rfind("spanentry", 0) == 0) {
                const size_t k = line.find(" p", 9 * 17);
                if (k != std::string::npos) pic = uint32_t(std::atoi(line.c_str() + k + 2) + ofs);
              } else if (line.rfind("span wr ", 0) == 0 || line.rfind("spanb wr ", 0) == 0) {
                const bool blk = line[4] == 'b';
                int x = 0, y = 0, n = 0, rows = 1;
                if (blk) std::sscanf(line.c_str() + 9, "%d %d %d %d", &x, &y, &n, &rows);
                else std::sscanf(line.c_str() + 8, "%d %d %d", &x, &y, &n);
                auto& m = ck_mask[pic];
                if (m.empty()) m.assign(360 * 240, 0);
                for (int r = 0; r < rows; ++r)
                  for (int i = 0; i < n; ++i)
                    if (x + i < 360 && y + r < 240) m[size_t(y + r) * 360 + size_t(x + i)] = 1, ++marked;
              }
            }
            std::printf("check mask %s: %zu pictures, %zu pixels\n", mp, ck_mask.size(), marked);
          }
        }
        if (check_f0 >= 0 && ncap.flip != check_flip && !ncap.frame.empty()) {
          check_flip = ncap.flip;
          std::vector<uint32_t> truth(ncap.frame.size());
          for (size_t i = 0; i < truth.size(); ++i) truth[i] = t3knative::YccToXrgb(ncap.frame[i]);
          t3knative::Picture pic;
          t3knative::BuildPicture(ncap, &pic);
          if (f >= check_f0 && f <= check_f1 && pic.complete && !check_prev.empty()) {
            if (!ck_out) {
              ck_out = SDL_CreateTexture(nren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 360, 240);
              ck_seed = SDL_CreateTexture(nren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 360, 240);
            }
            const SDL_FRect d360{0, 0, 360, 240};
            SDL_UpdateTexture(ck_seed, nullptr, check_prev.data(), 360 * 4);
            SDL_SetRenderTarget(nren, ck_out);
            checker.Seed(nren, ck_seed, d360);
            checker.Draw(nren, pic, pic.segs, d360, 1.f);
            SDL_Surface* sf = SDL_RenderReadPixels(nren, nullptr);
            SDL_SetRenderTarget(nren, nullptr);
            if (sf) {
              SDL_Surface* cv = SDL_ConvertSurface(sf, SDL_PIXELFORMAT_XRGB8888);
              double sum = 0;
              int big = 0;
              std::vector<uint32_t> diff(360 * 240);
              for (int y = 0; y < 240; ++y)
                for (int x = 0; x < 360; ++x) {
                  const uint32_t a = truth[size_t(y) * 360 + x];
                  const uint32_t b = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(cv->pixels) + y * cv->pitch)[x];
                  int e = 0;
                  for (int k = 0; k < 24; k += 8) e += std::abs(int((a >> k) & 0xFF) - int((b >> k) & 0xFF));
                  sum += e / 3.0;
                  big += e / 3 > 24;
                  const uint32_t g = uint32_t(std::min(255, e));
                  diff[size_t(y) * 360 + x] = g << 16 | g << 8 | g;
                }
              const double mean = sum / (360.0 * 240.0);
              ck_sum += mean;
              ++ck_n;
              std::string masked;
              if (auto it = ck_mask.find(pic.flip); it != ck_mask.end()) {
                double ms = 0;
                int mn = 0, mbig = 0;
                for (size_t i = 0; i < it->second.size(); ++i)
                  if (it->second[i]) {
                    const uint32_t a = truth[i];
                    const uint32_t b = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(cv->pixels) +
                                                                         (i / 360) * cv->pitch)[i % 360];
                    int e = 0;
                    for (int k = 0; k < 24; k += 8) e += std::abs(int((a >> k) & 0xFF) - int((b >> k) & 0xFF));
                    ms += e / 3.0;
                    mbig += e / 3 > 24;
                    ++mn;
                  }
                if (mn) {
                  ck_msum += ms / mn;
                  ++ck_mn;
                  char buf[96];
                  std::snprintf(buf, sizeof buf, "; type's pixels: %d, mean error %.2f, %d off by > 24", mn, ms / mn, mbig);
                  masked = buf;
                }
              }
              std::printf("check field %d flip %u: mean error %.2f, %.1f%% of pixels off by > 24%s\n", f, pic.flip, mean,
                          100.0 * big / (360.0 * 240.0), masked.c_str());
              // images: truth | native | difference, for the first pictures and then every 50th
              if (ck_saved < 6 || ck_n % 50 == 0) {
                ++ck_saved;
                SDL_Surface* row = SDL_CreateSurface(360 * 3 + 16, 240, SDL_PIXELFORMAT_XRGB8888);
                SDL_FillSurfaceRect(row, nullptr, 0);
                for (int y = 0; y < 240; ++y) {
                  uint32_t* r = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(row->pixels) + y * row->pitch);
                  const uint32_t* b = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(cv->pixels) + y * cv->pitch);
                  for (int x = 0; x < 360; ++x) {
                    r[x] = truth[size_t(y) * 360 + x];
                    r[368 + x] = b[x];
                    r[736 + x] = diff[size_t(y) * 360 + x];
                  }
                }
                SDL_SaveBMP(row, (check_dir + "/check_f" + std::to_string(f) + ".bmp").c_str());
                SDL_DestroySurface(row);
              }
              SDL_DestroySurface(cv);
              SDL_DestroySurface(sf);
            }
          } else if (f >= check_f0 && f <= check_f1 && !pic.complete) {
            std::printf("check field %d flip %u: not complete (type %08X)\n", f, pic.flip, pic.first_unknown);
          }
          check_prev = std::move(truth);
        }
        if (check_f0 >= 0 && f == check_f1 && ck_n) {
          std::printf("check: %d pictures, mean error %.2f\n", ck_n, ck_sum / ck_n);
          if (ck_mn) std::printf("check: type's pixels in %d pictures, mean error %.2f\n", ck_mn, ck_msum / ck_mn);
        }
        for (auto& s : nativeshots)
          if (s.frame == f) {
            SDL_SetRenderTarget(nren, nout);
            if (SDL_Surface* sf = SDL_RenderReadPixels(nren, nullptr)) {
              SDL_SaveBMP(sf, s.path.c_str());
              SDL_DestroySurface(sf);
            }
            SDL_SetRenderTarget(nren, nullptr);
            std::fprintf(stderr, "nativeshot %d: %d native pictures, %d emulated so far\n", f, ncomplete, nincomplete);
          }
      }
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
          // T3K_MEMDUMP_REGIONS=sys,main,mpe0,mpe3 (default all four)
          const char* rg = std::getenv("T3K_MEMDUMP_REGIONS");
          for (int w : {0, 1, 2, 5}) {
            if (rg) {   // "_sys.bin" -> "sys"
              std::string nm = names[w] + 1;
              nm = nm.substr(0, nm.find('.'));
              if (!std::strstr(rg, nm.c_str())) continue;
            }
            size_t n = 0;
            const uint8_t* m = nuon::Memory(w, &n);
            if (!m) continue;
            std::ofstream o(d.path + names[w], std::ios::binary);
            o.write(reinterpret_cast<const char*>(m), std::streamsize(n));
          }
        }
      for (auto& s : shots)
        if (s.frame == f && !WriteBmp(s.path, nuon::LastFrame())) std::fprintf(stderr, "shot %s failed\n", s.path.c_str());
      for (auto& s : savestates)
        if (s.frame == f)
          std::fprintf(stderr, "savestate %d %s: %s\n", f, s.path.c_str(), nuon::SaveState(s.path) ? "ok" : "FAILED");
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
  SDL_Renderer* ren = win ? t3knative::CreateRenderer(win) : nullptr;
  if (ren) t3knative::InitPipeline(ren);   // before the emulation thread builds any native picture
  if (!ren) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
  if (settings::Num("fullscreen", 1) != 0) SDL_SetWindowFullscreen(win, true);
  const bool vsync_on = !novsync && settings::Num("vsync", 1) != 0;
  SDL_SetRenderVSync(ren, vsync_on ? 1 : 0);
  SDL_HideCursor();
  SDL_Texture* tex = nullptr;
  SDL_Texture* tex_prev = nullptr;   // picture_blend: the previous picture
  bool picture_blend = settings::Num("picture_blend", 1) != 0;
  uint64_t blend_from = 0, pick_show_at = 0;   // when the current picture's slot began
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
  struct Slot {
    int64_t field = -1; uint64_t due = 0; uint32_t pic = 0; nuon::Frame frame;
    std::shared_ptr<const t3knative::Vectors> vec;   // the shown picture's vector layer (native renderer)
  };
  Slot ring[8];
  std::mutex ring_mutex;
  std::atomic<uint16_t> pad_buttons{0};
  std::atomic<float> stick_x{0.f};
  ClawControl claw;
  std::atomic<bool> emu_quit{false};
  // The settings menu pauses the emulation: emu_pause asks, emu_parked answers once the thread is waiting (from then
  // on the main thread may change settings the thread reads).
  std::atomic<bool> emu_pause{false}, emu_parked{false};
  std::atomic<int64_t> fields_done{0};
  // The field grid's origin, shared: the main thread nudges it away from the vblanks (see below).
  std::atomic<uint64_t> grid_start{SDL_GetTicksNS() + field_ns};
  nuon::ReleaseContext();   // the emulation thread takes the core's OpenGL context
  std::thread emu([&] {
    // (grid_start may move: the main thread shifts it by half a refresh when the grid sits on the vblanks.)
    for (int64_t k = 0; !emu_quit; ++k) {
      if (emu_pause) {
        emu_parked = true;
        while (emu_pause && !emu_quit) SDL_Delay(2);
        emu_parked = false;
        if (emu_quit) break;
        grid_start = SDL_GetTicksNS() - uint64_t(k) * field_ns;   // carry on from now: no catching up
      }
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
      {
        const uint16_t b = pad_buttons.load() | scripted(uint64_t(k));
        const uint16_t lr = uint16_t((1u << nuon::LEFT) | (1u << nuon::RIGHT));
        nuon::SetButtons(b | claw.Update(stick_x.load(), b & lr));
      }
      const uint64_t t0 = SDL_GetTicksNS();
      nuon::RunFrame();
      const uint64_t t1 = SDL_GetTicksNS();
      // The native renderer's input: each new picture's display list, built into its vector layer once.
      static nuon::Capture cap;
      static std::shared_ptr<const t3knative::Vectors> vec;
      if (nuon::GetCapture(&cap) && (!vec || vec->flip != cap.flip)) {
        // the draw gate: a picture the renderers skipped was built from memory when they would have started it
        auto live = gate::mode.load() == 2 ? gate::Take(cap.flip) : nullptr;
        if (live) {
          vec = live;
        } else {
          auto v = std::make_shared<t3knative::Vectors>();
          t3knative::BuildVectors(cap, v.get());
          vec = v;
        }
      }
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
        sl.vec = vec;
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
  uint64_t picture_ns = (emulated && flip_cap > 0) ? uint64_t(flip_cap * double(field_ns)) : 0;
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
  // F4: on-screen counter - the game's pictures a second (its real frame rate; the display refresh is separate) and
  // emulated fields a second (60 = keeping up).
  bool show_rates = settings::Num("show_rates", 0) != 0;
  std::string rates_text;
  uint64_t rates_t = SDL_GetTicksNS();
  uint32_t rates_flips = perf.flips.load(), rates_fields = perf.fields.load();
  const bool high_res_on = settings::Num("high_res", 0) != 0;
  auto set_fullscreen = [&](bool on) {
    SDL_SetWindowFullscreen(win, on);
    settings::Save("fullscreen", on ? "1" : "0");
  };
  bool prev_back = false;
  uint32_t prev_held = 0;
  int native_view = int(settings::Num("native_view", 0));
  const bool native_skip = settings::Num("native_skip", 1) != 0;
  gate::mode = (native_view == 1 && native_skip) ? 2 : 0;   // the draw gate follows the native view
  std::shared_ptr<const t3knative::Vectors> shown_vec, prev_vec;   // the shown picture's layer and the one before
  bool native_interp = settings::Num("native_interpolate", 1) != 0;
  std::vector<t3knative::Seg> native_segs;
  t3knative::Renderer native;
  uint64_t native_last_ns = 0;
  float native_step_left = 0.f;   // the shown picture's zoom-feedback step not yet applied
  std::shared_ptr<const t3knative::Vectors> native_last_pic;
  int presents = 0;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running = false;
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE && !menu::IsOpen()) menu::Open();
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F3) {
        picture_blend = !picture_blend;
        settings::Save("picture_blend", picture_blend ? "1" : "0");
        std::fprintf(stderr, "picture blend %s\n", picture_blend ? "on" : "off");
      }
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F6) {
        native_interp = !native_interp;
        settings::Save("native_interpolate", native_interp ? "1" : "0");
        std::fprintf(stderr, "native interpolation %s\n", native_interp ? "on" : "off");
      }
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F7) {
        native_view = (native_view + 1) % 4;
        gate::mode = (native_view == 1 && native_skip) ? 2 : 0;
        menu::Toast(NativeViewName(native_view, native_skip), SDL_GetTicksNS());
        settings::Save("native_view", std::to_string(native_view));
        std::fprintf(stderr, "native view %d\n", native_view);
      }
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F4) {
        show_rates = !show_rates;
        settings::Save("show_rates", show_rates ? "1" : "0");
      }
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
    // Settings menu (Back / Esc): the emulation waits while it is open; its changes apply here once it is parked.
    {
      const uint64_t now = SDL_GetTicksNS();
      const bool back = pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
      const uint32_t held = controls::Held(pad), pressed = held & ~prev_held;
      prev_held = held;
      if (!menu::IsOpen() && back && !prev_back) menu::Open();
      prev_back = back;
      emu_pause = menu::IsOpen();
      if (menu::IsOpen() && emu_parked) {
        menu::Nav nav = menu::ReadNav(pad, SDL_GetKeyboardState(nullptr));
        for (int b = 0; b < controls::kButtonCount; ++b)
          if (pressed >> b & 1) { nav.new_button = b; break; }
        switch (menu::Update(nav, now)) {
          case menu::kQuit: running = false; break;
          case menu::kToggleFullscreen: set_fullscreen(!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)); break;
          default: break;
        }
        picture_blend = settings::Num("picture_blend", 1) != 0;
        show_rates = settings::Num("show_rates", 0) != 0;
        const double fc = settings::Num("flip_cap", 3);
        if (fc != flip_cap) {   // game speed: re-plan the picture pacing from the next field
          flip_cap = fc;
          nuon::SetFlipCap(flip_cap);
          picture_ns = (emulated && flip_cap > 0) ? uint64_t(flip_cap * double(field_ns)) : 0;
          std::lock_guard<std::mutex> lock(ring_mutex);
          plan.clear();
          plan_any = false;
          planned_to = fields_done.load() - 1;
        }
      }
      emu_pause = menu::IsOpen();
    }
    pad_buttons = ReadPad(pad, SDL_GetKeyboardState(nullptr));
    {
      float x = pad ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f : 0.f;
      const bool* ks = SDL_GetKeyboardState(nullptr);
      if (ks[SDL_SCANCODE_A]) x = -1.f;   // (keyboard A / D: the stick, for testing without a pad)
      if (ks[SDL_SCANCODE_D]) x = 1.f;
      stick_x = std::clamp(x, -1.f, 1.f);
    }

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
          if (sl.field == plan[take].field && sl.field > shown) {
            pick = &sl;
            pick_show_at = plan[take].show_at;
          }
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
        const bool new_picture = !picture_ns || pick->pic != shown_pic || shown < 0;
        if (new_picture && picture_ns) {   // blending: the previous picture stays underneath while this one fades in
          std::swap(tex, tex_prev);
          blend_from = pick_show_at ? pick_show_at : now;
        }
        if (!tex || tw != f.width || th != f.height) {
          if (tex) SDL_DestroyTexture(tex);
          if (tex_prev && (tw != f.width || th != f.height)) { SDL_DestroyTexture(tex_prev); tex_prev = nullptr; }
          tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, int(f.width), int(f.height));
          SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
          tw = f.width;
          th = f.height;
        }
        SDL_UpdateTexture(tex, nullptr, f.pixels.data(), int(f.width * 4));
        if (pick->vec != shown_vec) {
          if (shown_vec && pick->vec && pick->vec->flip != shown_vec->flip) prev_vec = shown_vec;
          shown_vec = pick->vec;
        }
        if (new_picture) {   // a new picture (or field without pacing)
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
      // picture_blend (F3): cross-fade from the previous picture to this one over its time slot - smooth-looking
      // motion between the game's steps, at the price of about one picture more lag.
      // Native renderer (F7): 1 = whole native pictures where every entry type is done (else the emulated picture),
      // 2 = the native vector layer over the emulated picture, 3 = the vector layer alone. native_interpolate (F6):
      // moving things between the previous picture and this one over its time slot (as picture_blend), and the zoom
      // feedback advanced by the time since the last present.
      const uint64_t now_ns = SDL_GetTicksNS();
      const double native_t = (native_interp && prev_vec && picture_ns)
          ? std::clamp(double(int64_t(now_ns + refresh_ns / 2 - blend_from)) / double(picture_ns), 0.0, 1.0) : 1.0;
      auto native_segments = [&]() -> const std::vector<t3knative::Seg>& {
        if (native_t >= 1.0 || !prev_vec) return shown_vec->segs;
        t3knative::Interpolate(*prev_vec, *shown_vec, float(native_t), &native_segs);
        return native_segs;
      };
      const bool native_whole = native_view == 1 && shown_vec && shown_vec->complete;
      if (native_whole) {
        // coming from emulated pictures: their last one is what this picture's feedback builds on
        if (!native_last_ns) {
          native.Seed(ren, (picture_ns && tex_prev) ? tex_prev : tex, dst);
          native_last_ns = std::min(now_ns, blend_from ? blend_from : now_ns);   // the step so far: since this slot began
          native_last_pic = shown_vec;
          native_step_left = 1.f;
        }
        if (native_interp && picture_ns) {
          // Exactly one zoom-feedback step per game picture, spread over its time on screen: a new picture first
          // finishes what is left of the previous one's step; a picture that stays up longer than its slot (the
          // game slowed down) stops advancing once its step is used.
          float carry = 0.f;
          if (shown_vec != native_last_pic) {
            carry = native_last_pic ? native_step_left : 0.f;
            native_step_left = 1.f;
          }
          const float due = float(std::clamp(double(now_ns - native_last_ns) / double(picture_ns), 0.0, 1.0));
          const float take = std::min(native_step_left, due);
          native_step_left -= take;
          native.Draw(ren, *shown_vec, native_segments(), dst, carry + take);
        } else if (shown_vec != native_last_pic) {
          native.Draw(ren, *shown_vec, shown_vec->segs, dst, 1.f);
        } else {
          native.Redraw(ren, dst);
        }
        native_last_ns = now_ns;
        native_last_pic = shown_vec;
      } else if (native_view == 3) {
        // the native vector layer alone (on black)
      } else if (picture_blend && picture_ns && tex_prev) {
        const double t = std::clamp(double(int64_t(SDL_GetTicksNS() + refresh_ns / 2 - blend_from)) / double(picture_ns),
                                    0.0, 1.0);
        SDL_SetTextureBlendMode(tex_prev, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(ren, tex_prev, nullptr, &dst);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaModFloat(tex, float(t));
        SDL_RenderTexture(ren, tex, nullptr, &dst);
        SDL_SetTextureAlphaModFloat(tex, 1.f);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
      } else {
        SDL_RenderTexture(ren, tex, nullptr, &dst);
      }
      if ((native_view == 2 || native_view == 3) && shown_vec) t3knative::DrawVectors(ren, native_segments(), dst);
      if (!native_whole) native_last_ns = 0;   // (the feedback restarts from our last native picture)
    }
    if (show_rates) {
      const uint64_t now = SDL_GetTicksNS();
      if (now - rates_t >= 1000000000ull) {
        const uint32_t fl = perf.flips.load(), fi = perf.fields.load();
        const double secs = (now - rates_t) / 1e9;
        char buf[128];
        std::snprintf(buf, sizeof buf, "%.1f pictures/s  %.1f fields/s  %s", (fl - rates_flips) / secs,
                      (fi - rates_fields) / secs, high_res_on ? "720x480" : "360x240");
        rates_text = buf;
        rates_t = now; rates_flips = fl; rates_fields = fi;
      }
      if (!rates_text.empty()) {
        SDL_SetRenderScale(ren, 2.f, 2.f);
        SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
        SDL_RenderDebugText(ren, 8.f, 8.f, rates_text.c_str());
        SDL_SetRenderScale(ren, 1.f, 1.f);
      }
    }
    if (native_view != 0) {   // which renderer drew this picture (testing aid while the native renderer is in development)
      std::string tag = NativeViewName(native_view, native_skip);
      if (native_view == 1) {
        if (shown_vec && shown_vec->complete) {
          tag += " | NATIVE";
        } else {
          char buf[64];
          std::snprintf(buf, sizeof buf, " | EMULATED (%08X)",
                        shown_vec ? shown_vec->first_unknown : 0u);
          tag += buf;
        }
      }
      SDL_SetRenderScale(ren, 2.f, 2.f);
      const float ty = float(wh) / 2.f - 14.f;
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
      const SDL_FRect bg{4.f, ty - 3.f, 8.f * float(tag.size()) + 8.f, 14.f};
      SDL_RenderFillRect(ren, &bg);
      if (native_view == 1 && shown_vec && shown_vec->complete) SDL_SetRenderDrawColor(ren, 80, 255, 120, 255);
      else SDL_SetRenderDrawColor(ren, 255, 160, 60, 255);
      SDL_RenderDebugText(ren, 8.f, ty, tag.c_str());
      SDL_SetRenderScale(ren, 1.f, 1.f);
    }
    if (menu::Shown()) menu::Draw(ren, ww, wh);
    menu::DrawToast(ren, ww, wh, SDL_GetTicksNS());
    if (menu_test && presents == 0) menu::Open(menu_test == 2);
    // T3K_WINSEQ=<first present>:<count>:<dir> (tests): save each of those presents as <dir>/p<n>.bmp.
    if (static const char* ws = std::getenv("T3K_WINSEQ"); ws) {
      static int w0 = -1, wn = 0;
      static char wdir[512] = {};
      if (w0 < 0) std::sscanf(ws, "%d:%d:%511s", &w0, &wn, wdir);
      if (presents >= w0 && presents < w0 + wn)
        if (SDL_Surface* sf = SDL_RenderReadPixels(ren, nullptr)) {
          SDL_SaveBMP(sf, (std::string(wdir) + "/p" + std::to_string(presents) + ".bmp").c_str());
          SDL_DestroySurface(sf);
        }
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
  if (tex_prev) SDL_DestroyTexture(tex_prev);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  nuon::Unload();
  SDL_Quit();
  return 0;
}
