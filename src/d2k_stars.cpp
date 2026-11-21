// Defender 2000's sky stars, generated as the GPU overlay $0ADB98 does (see d2k_stars.h).
//
// The overlay (logs/d2k/pf: run at $F031C0) first cycles CLUT entries $20..$3F and builds a table at $B751C; then,
// only while word $1AF146 has bit 15 set, word $B6EDE is not 0 and word $180FFC is 0:
//   first row = (word $180FF8 as signed) / 2 - $80 (nothing when the halved word is negative);
//   one LFSR (seed $2D, taps $110) through 4 layers x 65 stars; layer k (0 first) scrolls by (camera >> 9) >> k, the
//   camera being the long at $183690; star j of a layer: x = (lfsr - scroll) & $1FF on row first + 2 j, plotted with
//   colour 2 when 0 <= x < $180 and the row is below the buffer's first (row > 0).
#include "d2k_stars.h"

#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
extern uint8_t jagMemSpace[];
}

namespace d2k_stars {
namespace {

constexpr uint32_t kOverlay = 0x0ADB98;
constexpr int kW = 384, kH = 279;

uint32_t Be32(uint32_t a) {
  const uint8_t* p = jagMemSpace + (a & 0x1FFFFF);
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
int Be16(uint32_t a) {
  const uint8_t* p = jagMemSpace + (a & 0x1FFFFF);
  return p[0] << 8 | p[1];
}

struct Snap {
  int frame = -1;
  bool on = false;
  uint32_t camera = 0, buf = 0;
  int first_row = 0;
};
constexpr int kRing = 6;
Snap g_snap[kRing];
int g_next = 0;

const Snap* Prev(int i) {
  const Snap* best = nullptr;
  for (const Snap& s : g_snap)
    if (s.frame >= 0 && s.frame < g_snap[i].frame && (!best || s.frame > best->frame)) best = &s;
  return best;
}

}  // namespace

bool Signature() {
  // movei #$F00440,r3 / movei #$B76A0,r4 (the CLUT cycling set-up)
  static const uint8_t kHead[] = {0x98, 0x03, 0x04, 0x40, 0x00, 0xF0, 0x98, 0x04, 0x76, 0xA0, 0x00, 0x0B};
  return std::memcmp(jagMemSpace + kOverlay, kHead, sizeof kHead) == 0;
}

void OnOverlayStart(int frame) {
  Snap& s = g_snap[g_next];
  g_next = (g_next + 1) % kRing;
  s.frame = frame;
  s.camera = Be32(0x183690);
  s.buf = Be32(0x18F208);
  const int half = int(int16_t(Be16(0x180FF8))) >> 1;
  s.first_row = half - 0x80;
  s.on = (Be16(0x1AF146) & 0x8000) && Be16(0x0B6EDE) != 0 && Be16(0x180FFC) == 0 && half >= 0;
}

int ShownSnapshot(uint32_t data, int frame) {
  int best = -1;
  for (int i = 0; i < kRing; ++i) {
    const Snap& s = g_snap[i];
    if (s.frame < 0 || frame - s.frame > 4 || s.frame >= frame) continue;
    if (data < s.buf || data >= s.buf + uint32_t(kW * kH)) continue;
    if (best < 0 || s.frame > g_snap[best].frame) best = i;
  }
  return best;
}

uint32_t Buffer(int i) { return g_snap[i].buf; }

int Latest(int frame) {
  for (int i = 0; i < kRing; ++i)
    if (g_snap[i].frame == frame) return i;
  return -1;
}

void Stars(int i, float t, bool exact, std::vector<Star>& out) {
  out.clear();
  if (i < 0 || i >= kRing || g_snap[i].frame < 0) return;
  const Snap& c = g_snap[i];
  if (!c.on) return;
  // The camera between the snapshot before and this one (smooth; a jump - a new level, hyperspace - is not blended).
  double cam = double(c.camera);
  if (!exact) {
    if (const Snap* p = Prev(i)) {
      const double d = double(int32_t(c.camera - p->camera));
      if (std::fabs(d) < double(1 << 18)) cam -= d * (1.0 - double(t));
    }
  }
  uint32_t lfsr = 0x2D;
  for (int layer = 0; layer < 4; ++layer) {
    const uint32_t scroll_i = ((c.camera >> 9) >> layer) & 0x1FF;
    const double scroll_f = std::fmod(cam / double(1u << (9 + layer)), 512.0);
    for (int j = 0; j < 65; ++j) {
      lfsr = (lfsr >> 1) ^ ((lfsr & 1) ? 0x110u : 0u);
      const int row = c.first_row + 2 * j;
      if (row <= 0) continue;   // (the overlay: the row's address must be past the buffer's start)
      if (exact) {
        const uint32_t x = (lfsr - scroll_i) & 0x1FF;
        if (x < 0x180) out.push_back(Star{float(x), row, layer});
      } else {
        double x = std::fmod(double(lfsr) - scroll_f, 512.0);
        if (x < 0) x += 512.0;
        out.push_back(Star{float(x), row, layer});
      }
    }
  }
}

void Check(int i, int frame) {
  std::vector<Star> st;
  Stars(i, 1.f, true, st);
  int hit = 0, miss = 0;
  for (const Star& s : st) {
    if (s.y >= kH) continue;
    const uint8_t v = jagMemSpace[(g_snap[i].buf + uint32_t(s.y) * kW + uint32_t(s.x)) & 0x1FFFFF];
    (v == 2 ? hit : miss)++;
  }
  std::fprintf(stderr, "stars check %d: snapshot frame %d, %zu stars, %d hold colour 2, %d not (on %d)\n", frame,
               g_snap[i].frame, st.size(), hit, miss, int(g_snap[i].on));
}

}  // namespace d2k_stars
