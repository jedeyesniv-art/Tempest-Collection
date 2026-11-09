// xbr.cpp - 2xBR (Hyllian, level 2 rules with the 2x blend set), CPU, premultiplied ARGB.
//
// Neighbourhood of E (the pixel being scaled):
//        A1 B1 C1
//     A0 PA PB PC C4
//     D0 PD PE PF F4
//     G0 PG PH PI I4
//        G5 H5 I5
// Each of E's 2x2 output pixels starts as E; the filter runs once per corner (rotated neighbourhoods) and, where an
// edge passes that corner, blends output pixels toward the neighbour across the edge. As FFmpeg's vf_xbr: distance
// |dY| + |dU| + |dV| (here + |dA|), equal under 155. Alpha is blended like the colour channels (premultiplied), so
// edges against see-through black fade out instead of darkening.
#include "xbr.h"

#include <cstdlib>

namespace xbr {
namespace {

inline int Y(uint32_t c) {
  int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
  return (r * 299 + g * 587 + b * 114) / 1000;
}
inline int U(uint32_t c) {
  int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
  return (-r * 169 - g * 331 + b * 500) / 1000 + 128;
}
inline int V(uint32_t c) {
  int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
  return (r * 500 - g * 419 - b * 81) / 1000 + 128;
}

struct Px {
  uint32_t c;
  int y, u, v, a;
};

inline int Df(const Px& p, const Px& q) {
  return std::abs(p.y - q.y) + std::abs(p.u - q.u) + std::abs(p.v - q.v) + std::abs(p.a - q.a);
}
inline bool Eq(const Px& p, const Px& q) { return Df(p, q) < 155; }

// dst += (src - dst) * n / 8 on all four channels.
inline void Blend(uint32_t& dst, uint32_t src, int n) {
  uint32_t r = 0;
  for (int sh = 0; sh < 32; sh += 8) {
    int d = (dst >> sh) & 255, s = (src >> sh) & 255;
    r |= uint32_t(d + (s - d) * n / 8) << sh;
  }
  dst = r;
}

void Filter(const Px& PE, const Px& PI, const Px& PH, const Px& PF, const Px& PG, const Px& PC, const Px& PD,
            const Px& PB, const Px& PA, const Px& G5, const Px& C4, const Px& G0, const Px& D0, const Px& C1,
            const Px& B1, const Px& F4, const Px& I4, const Px& H5, const Px& I5, const Px& A0, const Px& A1,
            uint32_t* E, int N0, int N1, int N2, int N3) {
  (void)G5; (void)C4; (void)G0; (void)D0; (void)C1; (void)B1; (void)A0; (void)A1; (void)N0;
  if (PE.c == PH.c || PE.c == PF.c) return;
  int e = Df(PE, PC) + Df(PE, PG) + Df(PI, H5) + Df(PI, F4) + (Df(PH, PF) << 2);
  int i = Df(PH, PD) + Df(PH, I5) + Df(PF, I4) + Df(PF, PB) + (Df(PE, PI) << 2);
  uint32_t px = Df(PE, PF) <= Df(PE, PH) ? PF.c : PH.c;
  if (e > i) return;
  if (e < i && ((!Eq(PF, PB) && !Eq(PH, PD)) || (Eq(PE, PI) && (!Eq(PF, I4) || !Eq(PH, I5))) || Eq(PE, PG) ||
                Eq(PE, PC))) {
    int ke = Df(PF, PG), ki = Df(PH, PC);
    bool ex2 = PE.c != PC.c && PB.c != PC.c, ex3 = PE.c != PG.c && PD.c != PG.c;
    if ((ke << 1) <= ki && ex3 && ke >= (ki << 1) && ex2) {        // left + up
      Blend(E[N3], px, 7);
      Blend(E[N2], px, 2);
      E[N1] = E[N2];
    } else if ((ke << 1) <= ki && ex3) {                             // left
      Blend(E[N3], px, 6);
      Blend(E[N2], px, 2);
    } else if (ke >= (ki << 1) && ex2) {                             // up
      Blend(E[N3], px, 6);
      Blend(E[N1], px, 2);
    } else {                                                          // diagonal
      Blend(E[N3], px, 4);
    }
  } else {
    Blend(E[N3], px, 4);
  }
}

}  // namespace

void Scale2x(const uint32_t* in, int w, int h, std::vector<uint32_t>& out) {
  // Precompute the YUVA form once per source pixel.
  std::vector<Px> p(size_t(w) * h);
  for (size_t k = 0; k < p.size(); ++k) {
    uint32_t c = in[k];
    p[k] = {c, Y(c), U(c), V(c), int(c >> 24)};
  }
  const Px zero{0, 0, 128, 128, 0};
  auto at = [&](int x, int y) -> const Px& {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= w) x = w - 1;
    if (y >= h) y = h - 1;
    return p[size_t(y) * w + x];
  };
  (void)zero;
  int ow = w * 2;
  out.assign(size_t(ow) * h * 2, 0);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const Px &A1 = at(x - 1, y - 2), &B1 = at(x, y - 2), &C1 = at(x + 1, y - 2);
      const Px &A0 = at(x - 2, y - 1), &PA = at(x - 1, y - 1), &PB = at(x, y - 1), &PC = at(x + 1, y - 1),
               &C4 = at(x + 2, y - 1);
      const Px &D0 = at(x - 2, y), &PD = at(x - 1, y), &PE = at(x, y), &PF = at(x + 1, y), &F4 = at(x + 2, y);
      const Px &G0 = at(x - 2, y + 1), &PG = at(x - 1, y + 1), &PH = at(x, y + 1), &PI = at(x + 1, y + 1),
               &I4 = at(x + 2, y + 1);
      const Px &G5 = at(x - 1, y + 2), &H5 = at(x, y + 2), &I5 = at(x + 1, y + 2);
      uint32_t E[4] = {PE.c, PE.c, PE.c, PE.c};
      Filter(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1, E, 0, 1, 2, 3);
      Filter(PE, PC, PF, PB, PI, PA, PH, PD, PG, I4, A1, I5, H5, A0, D0, B1, C1, F4, C4, G5, G0, E, 2, 0, 3, 1);
      Filter(PE, PA, PB, PD, PC, PG, PF, PH, PI, C1, G0, C4, F4, G5, H5, D0, A0, B1, A1, I4, I5, E, 3, 2, 1, 0);
      Filter(PE, PG, PD, PH, PA, PI, PB, PF, PC, A0, I5, A1, B1, I4, F4, H5, G5, D0, G0, C1, C4, E, 1, 3, 0, 2);
      size_t o = size_t(y) * 2 * ow + size_t(x) * 2;
      out[o] = E[0];
      out[o + 1] = E[1];
      out[o + ow] = E[2];
      out[o + ow + 1] = E[3];
    }
}

void Scale4x(const uint32_t* in, int w, int h, std::vector<uint32_t>& out) {
  static thread_local std::vector<uint32_t> mid;
  Scale2x(in, w, h, mid);
  Scale2x(mid.data(), w * 2, h * 2, out);
}

}  // namespace xbr
