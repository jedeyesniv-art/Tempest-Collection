// Defender 2000's bonus tunnel, generated as the game's GPU overlay does (see d2k_bonus.h).
//
// The overlay ($0AB008, run at $F031C0; disassembly from logs/d2k/bonus/ovl.dis) draws, per update:
//   A  64 stars: three LFSRs (seeds $23 / $2B / $7B, taps $110 / $110 / $240) give x, y and a depth; z = (depth -
//      travel) & $3FF, colour $B7 - z / 128, one pixel, no depth offset;
//   B  512 stars: LFSR -> an angle in the sine table, z as A, + the depth offset ($AB714);
//   C  32 (128 when $ABD28 >= 1) streaks: as B, then up to 8 pixels stepping z by 10, colours $A0, $A2 ...;
//   D  pairs of dots per entry of the list at $ABD2C (colour, amplitude, step; to a 0 colour), angles bent by the
//      wave table $1AED6C, z from $3FF down by 8;
//   E  32 pods ($AB920, 16 bytes: flag, angle, arc, type = colour, z): arcs of 2 x 2 blobs (type 14 a full circle);
//      game logic sits in the middle (a pod reached at z $101..$11F inside its arc becomes type 5, score ...): the
//      GPU still runs it, only the type change is mirrored here;
//   F  the tunnel: $ABD2A + 1 ring points from the table at $AB720 (angle, z), 2 x 2 blobs coloured $10 upwards,
//      wobbled by the wave table (amplitude + 2 a point).
// Projection: x = (x + camera x) / z + 192, y = (y + camera y + $1000) / z + 139, the camera from the sine table at
// the angle $AB700; dots outside 384 x 279 are not drawn (exact) - the smooth version keeps them (wide screens).
#include "d2k_bonus.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
extern uint8_t jagMemSpace[];
}

namespace d2k_bonus {
namespace {

constexpr uint32_t kOverlay = 0x0AB008;
constexpr int kW = 384, kH = 279;

struct Region {
  uint32_t base;
  std::vector<uint8_t> b;
};
struct Snap {
  int frame = -1;
  std::vector<Region> r;
  uint32_t rd(uint32_t a, int n) const {
    for (const Region& g : r)
      if (a >= g.base && a + uint32_t(n) <= g.base + g.b.size()) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v = v << 8 | g.b[a - g.base + uint32_t(i)];
        return v;
      }
    return 0;
  }
  int u8(uint32_t a) const { return int(rd(a, 1)); }
  int u16(uint32_t a) const { return int(rd(a, 2)); }
  int s16(uint32_t a) const { return int(int16_t(rd(a, 2))); }
  uint32_t u32(uint32_t a) const { return rd(a, 4); }
};
constexpr int kRing = 6;
Snap g_snap[kRing];
int g_next = 0;

const Snap* Prev(int i) {   // the snapshot taken before i (or null)
  const Snap* best = nullptr;
  for (const Snap& s : g_snap)
    if (s.frame >= 0 && s.frame < g_snap[i].frame && (!best || s.frame > best->frame)) best = &s;
  return best;
}

uint32_t Lfsr(uint32_t v, uint32_t taps) { return (v >> 1) ^ ((v & 1) ? taps : 0); }
double Sext16(double v) {   // a 16-bit word as signed (v may be fractional)
  double w = std::fmod(v, 65536.0);
  if (w < 0) w += 65536.0;
  return w >= 32768.0 ? w - 65536.0 : w;
}
double Wrap(double v, double m) {
  double w = std::fmod(v, m);
  return w < 0 ? w + m : w;
}

struct Gen {
  const Snap& c;
  const Snap* p;   // smooth: the snapshot before (null: no interpolation)
  double t;
  bool exact;
  std::vector<Pt>& out;
  // inputs (interpolated in smooth mode)
  double travel = 0, angle = 0, zoff = 0, cd7a = 0;
  int r7 = 0;
  double camx = 0, camy = 0;

  static constexpr uint32_t kTab = 0x1AE032, kWave = 0x1AED6C;

  double Lerp16(uint32_t a, double range) {   // a word interpolated between p and c (wrapping in range)
    const double cv = c.u16(a);
    if (exact || !p) return cv;
    double d = std::fmod(cv - p->u16(a), range);
    if (d > range / 2) d -= range;
    if (d < -range / 2) d += range;
    if (std::fabs(d) > range / 8) return cv;   // a jump: no interpolation
    return cv - d * (1.0 - t);
  }
  // Sine table entry for an index (fractional: interpolated between entries).
  void Entry(double idx, double* x, double* y) const {
    if (exact) idx = std::floor(idx);
    const double i0 = std::floor(idx), f = idx - i0;
    const uint32_t a = kTab + uint32_t(int(i0)) * 4u;
    *x = c.s16(a), *y = c.s16(a + 2);
    if (f > 0) *x += f * (c.s16(a + 4) - *x), *y += f * (c.s16(a + 6) - *y);
  }
  // The overlay's "mult r7 / shrq #9": an index into the table from v (low 16 bits, unsigned).
  double Index(double v) const {
    const double w = Wrap(v, 65536.0);
    const double i = w * r7 / 512.0;
    return exact ? std::floor(std::floor(w) * r7 / 512.0) : i;
  }
  double Wave(double i) const {   // the wave table byte at i (wrapping at 256; fractional: interpolated)
    i = Wrap(i, 256.0);
    if (exact) return c.u8(kWave + uint32_t(int(i)));
    const double i0 = std::floor(i), f = i - i0;
    const double b0 = c.u8(kWave + uint32_t(int(i0))), b1 = c.u8(kWave + uint32_t((int(i0) + 1) & 255));
    return b0 + f * (b1 - b0);
  }
  double Scale(double b, double amp, int shift) const {   // (b - $80) * amp, sign kept, arithmetic shift
    const double v = (b - 128.0) * amp / double(1 << shift);
    return exact ? std::floor(v) : v;
  }
  // Project and keep a dot; x, y in the overlay's units (before the camera), z without the offset.
  void Dot(double x, double y, double z, bool off, int col, int size) {
    if (off) z += zoff;
    x += camx, y += camy + 4096.0;
    if (z <= 0) return;
    double X, Y;
    if (exact) {
      X = std::trunc(x / std::floor(z)), Y = std::trunc(y / std::floor(z));
    } else {
      X = x / z, Y = y / z;
    }
    X += 192.0, Y += 139.0;
    if (exact ? (X < 0 || Y < 0 || X >= kW || Y >= kH) : (X < -400 || X > kW + 400 || Y < -100 || Y > kH + 100)) return;
    out.push_back(Pt{float(X), float(Y), uint8_t(col), uint8_t(size)});
  }

  void Run() {
    r7 = c.u16(0x1AE030);
    if (r7 <= 0 || r7 > 0x800) return;
    travel = Lerp16(0x0AB704, 65536.0);
    angle = Wrap(Lerp16(0x0AB700, 512.0), 512.0);   // (the angle wraps at 512)
    zoff = double(int32_t(c.u32(0x0AB714)));
    if (!exact && p) zoff += (1.0 - t) * (double(int32_t(p->u32(0x0AB714))) - zoff);
    cd7a = Lerp16(0x18CD7A, 65536.0);
    double cx, cy;
    Entry(Index(exact ? std::floor(angle) : angle), &cx, &cy);
    camx = cx * 64.0, camy = cy * 64.0;
    static const bool trace = std::getenv("D2K_BONUS_TRACE") != nullptr;
    if (trace && !exact)
      std::fprintf(stderr, "bonus trace: snap %d prev %d t %.3f travel %.2f angle %.2f cam %.1f %.1f zoff %.1f\n", c.frame,
                   p ? p->frame : -1, t, travel, angle, camx, camy, zoff);
    const double tr = exact ? std::floor(travel) : travel;
    auto depth = [&](uint32_t r14) { return exact ? double((r14 - uint32_t(tr)) & 0x3FF) : Wrap(double(r14) - tr, 1024.0); };
    auto shade = [&](double z) { return 0xB7 - (int(std::floor(z)) >> 7); };

    uint32_t r13 = 0x23, r22 = 0x2B, r14 = 0x7B;
    // A
    for (int i = 0; i < 64; ++i) {
      r13 = Lfsr(r13, 0x110), r22 = Lfsr(r22, 0x110), r14 = Lfsr(r14, 0x240);
      const double z = depth(r14);
      Dot((double(r13) - 255) * 256, (double(r22) - 255) * 256, z, false, shade(z), 1);
    }
    // B
    for (int i = 0; i < 512; ++i) {
      r13 = Lfsr(r13, 0x110), r14 = Lfsr(r14, 0x240);
      double x, y;
      Entry(std::floor(double(r13 & 0xFFFF) * r7 / 512.0), &x, &y);
      const double z = depth(r14);
      Dot(x * 256, y * 256, z, true, shade(z), 1);
    }
    // C
    const int streaks = c.s16(0x0ABD28) >= 1 ? 128 : 32;
    for (int i = 0; i < streaks; ++i) {
      r13 = Lfsr(r13, 0x110), r14 = Lfsr(r14, 0x240);
      double x, y;
      Entry(std::floor(double(r13 & 0xFFFF) * r7 / 512.0), &x, &y);
      double z = depth(r14) + zoff;
      for (int col = 0xA0;;) {
        Dot(x * 256, y * 256, z, false, col, 1);
        col += 2, z -= 10;
        if (z < 1 || col >= 0xB0) break;
      }
    }
    // D
    {
      const int flip = c.u16(0x0ABD26);
      for (uint32_t e = 0x0ABD2C; e < 0x0ABE00; e += 6) {
        const int col = c.u16(e), amp = c.u16(e + 2), step = c.u16(e + 4);
        if (col == 0) break;
        double r21 = -tr;
        for (int z = 0x3FF; z >= 0; z -= 8, r21 += step) {
          const double r1 = Scale(Wave(r21), amp & 0xFFFF, 8) + 0xFF;
          const double i1 = Index(r1);
          double x, y;
          Entry(i1, &x, &y);
          Dot(x * 256, y * 256, z, true, col, 1);
          double i2;
          if (flip == 0) {
            i2 = r7 - i1;
          } else {
            i2 = std::floor(r7 / 2.0) + i1;
            if (i2 >= r7) i2 -= r7;
          }
          Entry(i2, &x, &y);
          Dot(x * 256, y * 256, z, true, col, 1);
        }
      }
    }
    // E
    {
      const int wrap = c.u16(0x0ABD26);
      const int cam_angle = c.u16(0x0AB700) & 0x1FF;
      for (int k = 0; k < 32; ++k) {
        const uint32_t e = 0x0AB920 + 16u * uint32_t(k);
        if (c.u16(e) == 0) continue;
        double z = c.u16(e + 8);
        int type = c.u16(e + 6), arc = c.u16(e + 4);
        double a0 = c.u16(e + 2);
        // (the overlay's collision test: a pod reached inside its arc turns into type 5 before it is drawn)
        if (z > 0x100 && z - 0x100 < 0x1F && type != 5 && type != 14 && (arc >> 1) >= std::abs(cam_angle - int(a0)))
          type = 5;
        if (!exact && p && p->u16(e) != 0 && p->u16(e + 6) == c.u16(e + 6)) {
          const double pz = p->u16(e + 8), pa = p->u16(e + 2);
          if (std::fabs(z - pz) < 96) z = pz + t * (z - pz);
          if (std::fabs(a0 - pa) < 64) a0 = pa + t * (a0 - pa);
        }
        double r14a = a0 - (arc >> 1);
        int r13a = (arc >> 1) << 1;
        if (type == 14) r14a = 0, r13a = 0x1FF;
        for (; r13a >= 0; r13a -= 4, r14a += 4) {
          if (r14a < 0) {
            if (wrap == 0) continue;
            r14a += 0x1FF;
          } else if (r14a > 0x1FF) {
            if (wrap == 0) continue;
            r14a -= 0x1FF;
          }
          double x, y;
          Entry(Index(r14a), &x, &y);
          Dot(x * 256, y * 256, z, true, type, 2);
        }
      }
    }
    // F
    {
      const int count = c.u16(0x0ABD2A);
      if (count & 0x8000) return;
      int r25 = c.u16(0x0AB71E);
      double r21 = cd7a * 4;
      int amp = 0, col = 0x10;
      for (int r22n = int(int16_t(count)); r22n >= 0; --r22n) {
        const uint32_t e = 0x0AB720 + 4u * uint32_t(r25 & 0x7F);
        const int ang = c.u16(e), ez = c.u16(e + 2);
        const double z = 0x100 - Sext16(tr - ez);
        if (z >= 0 && z < 0x1FF8) {
          r21 = Wrap(r21, 256.0);
          const double b1 = Wave(r21);
          r21 = Wrap(r21 + 0x40, 256.0);
          const double b2 = Wave(r21);
          const double dx = Scale(b1, amp, 10), dy = Scale(b2, amp, 10);
          double x, y;
          // (turning rotates the rings: their angles in the table change each update - the same slot holding the
          // same ring (its z) in the snapshot before is interpolated, by the short way round the 1024 steps)
          double a = ang & 0x3FF;
          if (!exact && p && p->u16(e + 2) == ez) {
            double d = std::fmod(a - (p->u16(e) & 0x3FF) + 1536.0, 1024.0) - 512.0;
            if (std::fabs(d) < 128) a = Wrap(a - d * (1.0 - t), 1024.0);
          }
          Entry(Index(a), &x, &y);
          Dot(Sext16(x + dx) * 256, Sext16(y + dy) * 256, exact ? std::floor(z) : z, true, col, 2);
        }
        if ((r22n & 3) == 0) --r25;
        amp += 2, ++col, r21 += 2;
      }
    }
  }
};

}  // namespace

bool Signature() {
  static const uint8_t kHead[] = {0x98, 0x10, 0x01, 0x10, 0x00, 0x00, 0x98, 0x14, 0x02, 0x40, 0x00, 0x00};
  return std::memcmp(jagMemSpace + kOverlay, kHead, sizeof kHead) == 0;
}

void OnOverlayLoad(int frame) {
  Snap& s = g_snap[g_next];
  g_next = (g_next + 1) % kRing;
  s.frame = frame;
  s.r.clear();
  auto grab = [&](uint32_t lo, uint32_t hi) {
    s.r.push_back(Region{lo, std::vector<uint8_t>(jagMemSpace + lo, jagMemSpace + hi)});
  };
  grab(0x0AB700, 0x0ABF00);
  grab(0x1AE030, 0x1AF040);
  grab(0x18CD70, 0x18CD80);
  grab(0x18F200, 0x18F210);
  static const bool trace = std::getenv("D2K_BONUS_TRACE") != nullptr;
  if (trace)
    std::fprintf(stderr, "bonus load: frame %d travel %d angle %d buffer %06X cd7a %d\n", frame, s.u16(0x0AB704),
                 s.u16(0x0AB700), s.u32(0x18F208), s.u16(0x18CD7A));
}

int ShownSnapshot(uint32_t data, int frame) {
  int best = -1;
  for (int i = 0; i < kRing; ++i) {
    const Snap& s = g_snap[i];
    if (s.frame < 0 || frame - s.frame > 4 || s.frame >= frame) continue;   // (drawn by the GPU after the load)
    const uint32_t buf = s.u32(0x18F208);
    if (data < buf || data >= buf + uint32_t(kW * kH)) continue;
    if (best < 0 || s.frame > g_snap[best].frame) best = i;
  }
  return best;
}

int Latest(int frame) {
  for (int i = 0; i < kRing; ++i)
    if (g_snap[i].frame == frame) return i;
  return -1;
}

uint32_t Buffer(int i) { return g_snap[i].u32(0x18F208); }

void Points(int i, float t, bool exact, std::vector<Pt>& out) {
  out.clear();
  if (i < 0 || i >= kRing || g_snap[i].frame < 0) return;
  Gen g{g_snap[i], exact ? nullptr : Prev(i), t, exact, out};
  g.Run();
}

int Check(int i, const char* tag) {
  std::vector<Pt> pts;
  Points(i, 1.f, true, pts);
  std::vector<uint8_t> mine(size_t(kW) * kH, 0);
  for (const Pt& p : pts) {
    const int x = int(p.x), y = int(p.y);
    auto put = [&](int xx, int yy) {
      const long o = long(yy) * kW + xx;   // (the GPU's own addressing: x + 1 at the right edge runs into the next row)
      if (o >= 0 && o < long(mine.size())) mine[size_t(o)] = p.col;
    };
    put(x, y);
    if (p.size == 2) put(x + 1, y), put(x + 1, y + 1), put(x, y + 1);
  }
  const uint32_t buf = Buffer(i);
  int bad = 0, shown = 0;
  for (size_t o = 0; o < mine.size(); ++o) {
    const uint8_t g = jagMemSpace[(buf + o) & 0x1FFFFF];
    if (g != mine[o]) {
      if (shown++ < 6)
        std::fprintf(stderr, "  bonus %s: (%zu, %zu) game %02X ours %02X\n", tag, o % kW, o / kW, g, mine[o]);
      ++bad;
    }
  }
  std::fprintf(stderr, "bonus check %s: snapshot frame %d, %zu dots, %d of %d bytes differ\n", tag, g_snap[i].frame,
               pts.size(), bad, kW * kH);
  return bad;
}

}  // namespace d2k_bonus
