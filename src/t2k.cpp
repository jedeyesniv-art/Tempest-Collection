// t2k.cpp - Tempest 2000: the claw hook and the stick controls.
//
// The game moves the claw in clawcon ($80CCCA, yak.s "clawcon"): velocity 20(a6) and position 16(a6), both
// 16.16 lanes, then domove ($80CDAC) adds the velocity and wraps / clamps the position on the web. With the
// d-pad the velocity grows by 24(a6) a tick up to $6000 (0.375 lanes) and is cleared the moment the pad is
// let go. We replace clawcon's first two instructions with a jsr to a stub in the cart window ($DFFD00):
// for player 1 in real play (whichclaw = 1, not the attract demo) it records the claw object pointer and a
// tick count in the host port ($DFFE00) and, if the host enabled it, sets the velocity from the port and
// jumps straight to domove. Otherwise it runs the two original instructions and returns.
#include "t2k.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "settings.h"

extern "C" uint8_t jagMemSpace[];

namespace t2k {
namespace {

// Addresses (ROM MD5 602bc9953d3737b1ba52b2a0d9932f7c == mwenge/tempest2k).
constexpr uint32_t kClawcon = 0x80CCCA;
constexpr uint32_t kDomove = 0x80CDAC;
constexpr uint32_t kWhichclaw = 0xEA84;
constexpr uint32_t kAuto = 0xE9B6;
constexpr uint32_t kWebX = 0xC986;
constexpr uint32_t kWebPtab = 0xC988;
constexpr uint32_t kWebMax = 0xC990;
constexpr uint32_t kConnect = 0xDB3E;
constexpr uint32_t kMainloopRoutine = 0xE374;
constexpr uint32_t kDrawObjects = 0x80F05E;
constexpr uint32_t kDrawOo = 0x802E26;       // level select's main loop routine
constexpr uint32_t kHudObject = 0xDB7A + 64;   // beasties+64: 14(a0) ObType, 16(a0) screen3
// Cheats (menu CHEATS; RAM flags held before each frame).
constexpr uint32_t kChenable = 0xEAA4;  // keypad cheats (normally keypad 1+4+7+A on game select)
constexpr uint32_t kInfZap = 0xE9C6;    // infinite Superzapper (cleared at every game start)
constexpr uint32_t kSysflags = 0x80B0;  // byte: bit 2 = Beastly mode earned
constexpr uint32_t kT2kMax = 0xE9EE;    // highest start level reached, Tempest 2000 / Traditional
constexpr uint32_t kTradMax = 0xE9F2;
constexpr uint32_t kWarpy = 0xE8C4;     // power-ups still needed to warp; -1 = warp at the end of this level
constexpr uint32_t kView = 0xE350;      // camera view 0..2 (keypad 1/4/7, 2/5/8, 3/6/9)

constexpr uint32_t kZ = 0xE9B0;         // "stop whatever you are doing and reset" (checkpause)
constexpr uint32_t kPawsed = 0xE854;    // game paused
constexpr uint32_t kNpspri = 0x80DE3C;  // paustuff: "tst vadj" after the "paused" text
constexpr uint32_t kVadj = 0xE9F8;
constexpr uint32_t kCentext = 0x80408A; // centred text: a0 string, a1 font, d0 y
constexpr uint32_t kCfont = 0x7B58;     // the small regular font

constexpr uint32_t kStub = 0xDFFD00;
constexpr uint32_t kPauseStub = 0xDFFD80;
constexpr uint32_t kPauseText = 0xDFFC00;   // up to 0x7F chars, 0-terminated
constexpr uint32_t kPort = 0xDFFE00;
constexpr uint32_t kPortEnable = kPort + 0x00;  // w: host -> game, 1 = use the port's velocity
constexpr uint32_t kPortVel = kPort + 0x04;     // l: host -> game, 16.16 lanes per tick
constexpr uint32_t kPortClaw = kPort + 0x10;    // l: game -> host, player 1 claw object
constexpr uint32_t kPortTicks = kPort + 0x14;   // w: game -> host, claw ticks
constexpr uint32_t kPortPauseLine = kPort + 0x20;  // w: host -> game, 1 = print kPauseText on the pause screen
constexpr uint32_t kPortHold = kPort + 0x30;       // w: host -> game, frames gpuwait still waits (render pacing)

// Render pacing. The emulated GPU / blitter finish the menu screens' full-screen effects (antelope scar: the melt
// between screens and the Top Guns background; ripplewarp: the mode select background) well inside a frame;
// on hardware they take several, and the game paces those screens on render completion: the melt (32 steps of
// -8 intensity inside a fixed 150-frame transition) went black early and sat black for ~2 s, the ripple ran too
// fast. gpuwait ($81A50E) jumps to a stub that first spins while kPortHold != 0; the gpurun trap sets the hold
// for those callers, AfterFrame counts it down.
constexpr uint32_t kGpuwait = 0x81A50E;
constexpr uint32_t kGpuwaitStub = 0xDFFDC0;
constexpr uint32_t kAntelope = 0x81BBFA;
constexpr uint32_t kScarCaller = 0x806114;     // failfade: the melt (ffade) and the Top Guns background
constexpr uint32_t kRippleCaller = 0x804980;   // optiondraw: the mode select background
constexpr uint32_t kGameOverCaller = 0x803C04; // gofeed: the Game Over feedback
constexpr uint32_t kClearFeedCaller = 0x803D7E;  // clearfeed: Game Over fading out after a button
int g_frames_since_fx = 1000;                  // frames since an antelope (full-screen effect) call

uint16_t R16(uint32_t a) { return uint16_t(jagMemSpace[a] << 8 | jagMemSpace[a + 1]); }
uint32_t R32(uint32_t a) { return uint32_t(R16(a)) << 16 | R16(a + 2); }
void W16(uint32_t a, uint16_t v) { jagMemSpace[a] = uint8_t(v >> 8); jagMemSpace[a + 1] = uint8_t(v); }
void W32(uint32_t a, uint32_t v) { W16(a, uint16_t(v >> 16)); W16(a + 2, uint16_t(v)); }

bool g_installed = false;
uint16_t g_last_ticks = 0;
int g_ticks_this_frame = 0;
int g_frames_since_tick = 1000;

// lr mode state
int g_dir = 0, g_held = 0, g_release_left = 0;
double g_release_v0 = 0, g_v = 0;
// aim mode state
int g_target = -1;
uint32_t g_target_web = 0;

struct Saved {
  uint16_t last_ticks;
  int ticks_this_frame, frames_since_tick, dir, held, release_left, target;
  double release_v0, v;
  uint32_t target_web;
  uint8_t port[0x400];   // $DFFC00-$DFFFFF: pause text, stubs, the port (not in the core's save state)
} g_saved;

double Smoothstep(double x) {
  x = std::clamp(x, 0.0, 1.0);
  return x * x * (3 - 2 * x);
}

double LrVelocity(const Pad& p) {
  double dz = settings::Num("lr_deadzone", 0.2);
  double d = (std::fabs(p.lx) - dz) / (1 - dz);
  if (d > 0) {
    d = std::min(d, 1.0);
    int sgn = p.lx < 0 ? -1 : 1;
    if (sgn != g_dir) { g_dir = sgn; g_held = 0; }
    ++g_held;
    double frames = std::max(1.0, settings::Num("lr_ramp_frames", 20));
    double r0 = std::clamp(settings::Num("lr_ramp_start", 0.05), 0.0, 1.0);
    double ramp = r0 + (1 - r0) * Smoothstep(g_held / frames);
    double v = sgn * settings::Num("lr_max_speed", 0.41) * std::pow(d, settings::Num("lr_curve", 1.5)) * ramp;
    g_release_left = int(settings::Num("lr_release_frames", 3));
    g_release_v0 = v;
    return v;
  }
  g_dir = 0;
  g_held = 0;
  if (g_release_left > 0) {
    int n = int(settings::Num("lr_release_frames", 3));
    int k = n - g_release_left + 1;
    --g_release_left;
    return g_release_v0 * (1.0 - double(k) / (n + 1));
  }
  return 0;
}

struct Lane { double x, y, angle; };

std::vector<Lane> Lanes(int n) {
  std::vector<Lane> out;
  uint32_t p = R32(kWebPtab) & 0xFFFFFF;
  double wx = int16_t(R16(kWebX));
  for (int i = 0; i < n; ++i) {
    double x0 = int16_t(R16(p + 4 * i)) - wx, y0 = int16_t(R16(p + 4 * i + 2)) - 8;
    double x1 = int16_t(R16(p + 4 * i + 4)) - wx, y1 = int16_t(R16(p + 4 * i + 6)) - 8;
    Lane l{(x0 + x1) / 2, (y0 + y1) / 2, 0};
    l.angle = std::atan2(l.y, l.x);
    out.push_back(l);
  }
  return out;
}

double AngleDist(double a, double b) {
  double d = std::fabs(a - b);
  while (d > 2 * M_PI) d -= 2 * M_PI;
  return std::min(d, 2 * M_PI - d);
}

double AimVelocity(const Pad& p, uint32_t claw) {
  int n = R16(kWebMax);
  if (n < 2 || n > 64) return 0;
  bool closed = R16(kConnect) != 0;
  uint32_t web = R32(kWebPtab);
  if (web != g_target_web) { g_target = -1; g_target_web = web; }
  auto lanes = Lanes(n);

  double mag = std::hypot(p.lx, p.ly);
  if (mag >= settings::Num("aim_threshold", 0.55)) {
    std::vector<double> cost(n);
    double width;
    if (closed) {
      double a = std::atan2(p.ly, p.lx);
      for (int i = 0; i < n; ++i) cost[i] = AngleDist(a, lanes[i].angle);
      width = 2 * M_PI / n;
    } else {
      double lo = 1e9, hi = -1e9;
      for (auto& l : lanes) { lo = std::min(lo, l.x); hi = std::max(hi, l.x); }
      double tx = lo + (std::clamp(double(p.lx), -1.0, 1.0) + 1) / 2 * (hi - lo);
      for (int i = 0; i < n; ++i) cost[i] = std::fabs(tx - lanes[i].x);
      width = (hi - lo) / std::max(1, n - 1);
    }
    int best = int(std::min_element(cost.begin(), cost.end()) - cost.begin());
    if (g_target < 0 || g_target >= n ||
        cost[best] < cost[g_target] - settings::Num("aim_hysteresis", 0.3) * width)
      g_target = best;
  }
  if (g_target < 0) return 0;

  double pos = int32_t(R32(claw + 16)) / 65536.0;
  double diff = (g_target + 0.5) - pos;
  if (closed) {
    while (diff > n / 2.0) diff -= n;
    while (diff < -n / 2.0) diff += n;
  }
  if (std::fabs(diff) < 0.02) return 0;
  double vmax = settings::Num("aim_max_speed", 0.6);
  double v = std::min(vmax, std::fabs(diff) * settings::Num("aim_gain", 0.45));
  v = std::min(std::fabs(diff), std::max(v, 0.06));
  return diff < 0 ? -v : v;
}

}  // namespace

bool Install(std::string* err) {
  static const uint8_t kOrig[] = {0x22, 0x2E, 0x00, 0x14, 0x24, 0x2E, 0x00, 0x18,
                                  0x26, 0x3C, 0x00, 0x00, 0x60, 0x00};
  for (size_t i = 0; i < sizeof kOrig; ++i)
    if (jagMemSpace[kClawcon + i] != kOrig[i]) {
      *err = "not the expected Tempest 2000 ROM (claw code differs)";
      return false;
    }
  // The stub (see the top of this file). Branches: bne/bne/beq to 'orig' at +54.
  static const uint16_t kStubCode[] = {
      0x0C79, 0x0001, 0x0000, uint16_t(kWhichclaw),          // +0  cmp.w #1,whichclaw
      0x662C,                                                // +8  bne.s orig
      0x4A79, 0x0000, uint16_t(kAuto),                       // +10 tst.w auto
      0x6624,                                                // +16 bne.s orig
      0x23CE, 0x00DF, uint16_t(kPortClaw),                   // +18 move.l a6,PortClaw
      0x5279, 0x00DF, uint16_t(kPortTicks),                  // +24 addq.w #1,PortTicks
      0x4A79, 0x00DF, uint16_t(kPortEnable),                 // +30 tst.w PortEnable
      0x6710,                                                // +36 beq.s orig
      0x2D79, 0x00DF, uint16_t(kPortVel), 0x0014,            // +38 move.l PortVel,20(a6)
      0x2EBC, uint16_t(kDomove >> 16), uint16_t(kDomove),    // +46 move.l #domove,(sp)
      0x4E75,                                                // +52 rts
      0x222E, 0x0014,                                        // +54 orig: move.l 20(a6),d1
      0x242E, 0x0018,                                        // +58 move.l 24(a6),d2
      0x4E75,                                                // +62 rts
  };
  for (size_t i = 0; i < sizeof kStubCode / 2; ++i) W16(kStub + 2 * uint32_t(i), kStubCode[i]);
  for (uint32_t a = kPort; a < kPort + 0x100; ++a) jagMemSpace[a] = 0;
  // Pause screen: npspri's "tst vadj" (6 bytes) -> jsr kPauseStub, which prints our line with the game's
  // centext + cfont, then does the tst itself (movem and rts leave the flags alone).
  static const uint8_t kOrigNp[] = {0x4A, 0x79, 0x00, 0x00, 0xE9, 0xF8, 0x6B, 0x00};
  for (size_t i = 0; i < sizeof kOrigNp; ++i)
    if (jagMemSpace[kNpspri + i] != kOrigNp[i]) {
      *err = "not the expected Tempest 2000 ROM (pause code differs)";
      return false;
    }
  static const uint16_t kPauseCode[] = {
      0x48E7, 0xFFFE,                                       // +0  movem.l d0-d7/a0-a6,-(sp)
      0x4A79, 0x00DF, uint16_t(kPortPauseLine),             // +4  tst.w PortPauseLine
      0x6716,                                               // +10 beq.s skip (+34)
      0x41F9, 0x00DF, uint16_t(kPauseText),                 // +12 lea text,a0
      0x43F9, 0x0000, uint16_t(kCfont),                     // +18 lea cfont,a1
      0x303C, 0x00C8,                                       // +24 move.w #200,d0
      0x4EB9, uint16_t(kCentext >> 16), uint16_t(kCentext), // +28 jsr centext
      0x4CDF, 0x7FFF,                                       // +34 skip: movem.l (sp)+,d0-d7/a0-a6
      0x4A79, 0x0000, uint16_t(kVadj),                      // +38 tst.w vadj
      0x4E75,                                               // +44 rts
  };
  for (size_t i = 0; i < sizeof kPauseCode / 2; ++i) W16(kPauseStub + 2 * uint32_t(i), kPauseCode[i]);
  W16(kNpspri, 0x4EB9);
  W32(kNpspri + 2, kPauseStub);

  // gpuwait: movem.l a0/d0,-(sp); lea G_CTRL,a0 (10 bytes) -> jmp kGpuwaitStub; nop; nop. The stub waits out the
  // host's hold, then is the original routine.
  static const uint8_t kOrigGw[] = {0x48, 0xE7, 0x80, 0x80, 0x41, 0xF9, 0x00, 0xF0, 0x21, 0x14, 0x20, 0x10};
  for (size_t i = 0; i < sizeof kOrigGw; ++i)
    if (jagMemSpace[kGpuwait + i] != kOrigGw[i]) {
      *err = "not the expected Tempest 2000 ROM (gpuwait differs)";
      return false;
    }
  static const uint16_t kGwCode[] = {
      0x4A79, 0x00DF, uint16_t(kPortHold),                  // +0  hold: tst.w PortHold
      0x66F8,                                               // +6  bne.s hold
      0x48E7, 0x8080,                                       // +8  movem.l a0/d0,-(sp)
      0x41F9, 0x00F0, 0x2114,                               // +12 lea G_CTRL,a0
      0x2010,                                               // +18 wt: move.l (a0),d0
      0x0800, 0x0000,                                       // +20 btst #0,d0
      0x66F8,                                               // +24 bne.s wt
      0x4CDF, 0x0101,                                       // +26 movem.l (sp)+,a0/d0
      0x4E75,                                               // +30 rts
  };
  for (size_t i = 0; i < sizeof kGwCode / 2; ++i) W16(kGpuwaitStub + 2 * uint32_t(i), kGwCode[i]);
  W16(kGpuwait + 0, 0x4EF9);
  W32(kGpuwait + 2, kGpuwaitStub);
  W16(kGpuwait + 6, 0x4E71);
  W16(kGpuwait + 8, 0x4E71);

  // clawcon: jsr kStub; nop (replaces the 8 bytes of the two moves).
  W16(kClawcon + 0, 0x4EB9);
  W32(kClawcon + 2, kStub);
  W16(kClawcon + 6, 0x4E71);
  g_installed = true;
  return true;
}

void Mark() {
  g_saved = {g_last_ticks, g_ticks_this_frame, g_frames_since_tick, g_dir, g_held, g_release_left, g_target,
             g_release_v0, g_v, g_target_web, {}};
  std::copy(jagMemSpace + 0xDFFC00, jagMemSpace + 0xE00000, g_saved.port);
}

void Rewind() {
  const Saved& s = g_saved;
  g_last_ticks = s.last_ticks;
  g_ticks_this_frame = s.ticks_this_frame;
  g_frames_since_tick = s.frames_since_tick;
  g_dir = s.dir;
  g_held = s.held;
  g_release_left = s.release_left;
  g_target = s.target;
  g_release_v0 = s.release_v0;
  g_v = s.v;
  g_target_web = s.target_web;
  std::copy(s.port, s.port + 0x400, jagMemSpace + 0xDFFC00);
}

bool Demo() { return g_installed && R16(kAuto) != 0; }
// (A full-screen antelope effect lately = a screen like Game Over, which keeps the play HUD object: not play.)
bool LatencyMatters() {
  return (HudShown() && g_frames_since_fx > 8) || (g_installed && R32(kMainloopRoutine) == kDrawOo);
}

int HudRows() { return g_installed && R16(kHudObject + 14) == 7 ? 32 : 48; }

void CheatWarp() { if (g_installed) W16(kWarpy, 0xFFFF); }

bool HudShown() {
  if (!g_installed || R32(kMainloopRoutine) != kDrawObjects) return false;
  uint16_t type = R16(kHudObject + 14);
  return type == 7 || type == 8;
}

bool InPlay() { return g_installed && g_frames_since_tick < 4; }

bool Paused() { return g_installed && R16(kPawsed) != 0; }

void SetPauseLine(const std::string& text) {
  if (!g_installed) return;
  size_t n = std::min<size_t>(text.size(), 0x7F);
  for (size_t i = 0; i < n; ++i) jagMemSpace[kPauseText + i] = uint8_t(text[i]);
  jagMemSpace[kPauseText + n] = 0;
  W16(kPortPauseLine, n ? 1 : 0);
}

void ReturnToTitle() {
  if (!g_installed) return;
  W16(kZ, 1);
  W16(kAuto, 0);
}

// --- Back in menus ---
constexpr uint32_t kRoutine = 0xE326;
constexpr uint32_t kTheOption = 0x83E6;
constexpr uint32_t kSelected = 0x7FD2;
constexpr uint32_t kMisstit = 0xEA46;
constexpr uint32_t kOselector = 0x80476A, kSeldb = 0x8048CA;   // do_choose's routines
constexpr uint32_t kZoomto = 0x802F3A, kWaitfor = 0x802F68, kZprev = 0x8030F6, kZnext = 0x803176, kZshow = 0x8031EA;   // level select
constexpr uint32_t kOption1 = 0x8141CC, kOption2 = 0x7E02, kOption3 = 0x7FDE, kOption4 = 0x814590, kOption5 = 0x8026,
                   kOption6 = 0x814356, kOption7 = 0x81436E, kOption8 = 0x83C6, kOption9 = 0x814386,
                   kOption10 = 0x81439A, kOption11 = 0x8143B2;
int g_back_fire = 0;          // frames Jag B is still held to confirm an exit item
bool g_back_restore = false;  // put the level unlocks back once the reset has cleared z
bool g_back_saw_z = false;
uint16_t g_back_levels[4] = {};

// What B does on the current screen: -2 none, -1 the reset, else the exit item to select.
int BackAction() {
  if (!g_installed || g_back_fire > 0 || g_back_restore || R16(kZ) != 0) return -2;
  const uint32_t routine = R32(kRoutine);
  if (R32(kMainloopRoutine) == kDrawOo &&
      (routine == kZoomto || routine == kWaitfor || routine == kZprev || routine == kZnext || routine == kZshow))
    return -1;
  if (routine != kOselector && routine != kSeldb) return -2;
  switch (R32(kTheOption)) {
    case kOption2: return 3;    // past the end: gameopt falls through to game select
    case kOption3: return 2;    // exit
    case kOption4: return 3;    // exit
    case kOption5: return 2;    // exit
    case kOption6: return 2;    // exit
    case kOption11: return 0;   // no! no! no!
    case kOption1: case kOption7: case kOption8: case kOption9: case kOption10: return -1;
    default: return -2;
  }
}

bool MenuScreen() { return BackAction() != -2 || g_back_fire > 0; }

void MenuBack() {
  const int a = BackAction();
  if (a == -2) return;
  if (a >= 0) {
    W16(kSelected, uint16_t(a));
    g_back_fire = 4;
    return;
  }
  const uint32_t routine = R32(kRoutine);
  const bool to_title = (routine == kOselector || routine == kSeldb) && R32(kTheOption) == kOption1;
  for (int k = 0; k < 4; ++k) g_back_levels[k] = R16(kT2kMax + 2 * uint32_t(k));   // t2k_max / high, trad_max / high
  g_back_restore = true;
  g_back_saw_z = false;
  W16(kMisstit, to_title ? 0 : 1);
  W16(kAuto, 0);
  W16(kZ, 1);
}

bool MenuTick() {
  if (!g_installed) return false;
  if (g_back_restore) {   // rreset resets the levels while z is set, then clears z
    if (R16(kZ) != 0) {
      g_back_saw_z = true;
    } else if (g_back_saw_z) {
      for (int k = 0; k < 4; ++k) W16(kT2kMax + 2 * uint32_t(k), g_back_levels[k]);
      g_back_restore = false;
    }
  }
  if (g_back_fire > 0) {
    --g_back_fire;
    return true;
  }
  return false;
}

void BeforeFrame(const Pad& pad) {
  if (!g_installed) return;
  if (settings::Num("cheat_keys", 0) != 0) W16(kChenable, 1);
  if (settings::Num("cheat_inf_zap", 0) != 0) W16(kInfZap, 1);
  if (settings::Num("cheat_beastly", 0) != 0) jagMemSpace[kSysflags] |= 4;
  if (settings::Num("cheat_levels", 0) != 0) {
    if (R16(kT2kMax) < 98) W16(kT2kMax, 98);
    if (R16(kTradMax) < 98) W16(kTradMax, 98);
  }
  uint32_t claw = R32(kPortClaw);
  bool aim = settings::Str("stick_mode", "lr") == "aim";
  double v = 0;
  if (InPlay() && claw && claw < 0x200000) v = aim ? AimVelocity(pad, claw) : LrVelocity(pad);
  else { g_dir = g_held = g_release_left = 0; g_target = -1; }
  g_v = v;
  if (InPlay()) W16(kView, uint16_t(std::clamp(int(settings::Num("camera_view", 0)), 0, 2)));
  W16(kPortEnable, pad.dpad_lr ? 0 : 1);
  W32(kPortVel, uint32_t(int32_t(std::lround(v * 65536.0))));
}

void OnGpurun(uint32_t caller, uint32_t module, uint32_t mode) {
  if (!g_installed || module != kAntelope) return;
  g_frames_since_fx = 0;
  if (settings::Num("native", 0) != 0) return;
  int frames = 0;
  if (caller == kScarCaller && mode == 2) frames = int(settings::Num("pace_melt", 4));
  else if (caller == kRippleCaller && mode == 1) frames = int(settings::Num("pace_ripple", 2));
  else if ((caller == kGameOverCaller || caller == kClearFeedCaller) && mode == 2)
    frames = int(settings::Num("pace_gameover", 4));
  if (frames > 1) W16(kPortHold, uint16_t(std::min(frames - 1, 30)));
}

void AfterFrame() {
  if (!g_installed) return;
  if (g_frames_since_fx < 1000) ++g_frames_since_fx;
  if (uint16_t h = R16(kPortHold)) W16(kPortHold, uint16_t(h - 1));
  uint16_t t = R16(kPortTicks);
  g_ticks_this_frame = uint16_t(t - g_last_ticks);
  g_last_ticks = t;
  g_frames_since_tick = g_ticks_this_frame ? 0 : g_frames_since_tick + 1;
}

std::string Status() {
  char buf[200];
  uint32_t claw = R32(kPortClaw);
  double pos = claw && claw < 0x200000 ? int32_t(R32(claw + 16)) / 65536.0 : -1;
  std::snprintf(buf, sizeof buf, "play=%d ticks=%d pos=%.3f v=%.3f accel=%08X lanes=%d closed=%d target=%d",
                InPlay(), g_ticks_this_frame, pos, g_v, claw && claw < 0x200000 ? R32(claw + 24) : 0,
                R16(kWebMax), R16(kConnect) != 0, g_target);
  return buf;
}

}  // namespace t2k
