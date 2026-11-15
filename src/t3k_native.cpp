// t3k_native.cpp - Tempest 3000 native renderer: the vector shapes (see t3k_native.h).
#include "t3k_native.h"

#include <algorithm>
#include <cmath>

namespace t3knative {
namespace {

// --- the module's integer maths (806A3F50), bit-exact as tools/t3k_vecmodel.py ---

int32_t S32(int64_t v) { return int32_t(uint32_t(uint64_t(v))); }
// NUON mul a, b, >>#sh: signed product, arithmetic shift, low 32 bits.
int32_t Mul(int32_t a, int32_t b, int sh) { return S32((int64_t(a) * int64_t(b)) >> sh); }
int32_t Sext(uint32_t v, int bits) {
  v &= (1u << bits) - 1;
  return (v & (1u << (bits - 1))) ? int32_t(v) - (1 << bits) : int32_t(v);
}

// Library 806A4BBE (local $20300F3E): angle (16 bits a turn) -> cos, sin in 2.30.
void SinCos(uint32_t angle, int32_t* c, int32_t* s) {
  const int32_t C1 = int32_t(0xFFB901AF), C2 = 0x051573CA, C3 = int32_t(0xD6A9B10A), C4 = 0x6487D705;
  const int32_t x = int32_t(((angle << 18) & 0xFFFFFFFFu) >> 2);
  const uint32_t q = (angle >> 14) & 3;
  int32_t cc, ss;
  if (x == 0) {
    cc = 0x40000000;
    ss = 0;
  } else {
    const int32_t one_x = S32(int64_t(0x40000000) - x);
    const int32_t u = Mul(x, x, 30), v = Mul(one_x, one_x, 30);
    int32_t pu = S32(int64_t(Mul(u, C1, 30)) + C2), pv = S32(int64_t(Mul(v, C1, 30)) + C2);
    pu = S32(int64_t(Mul(u, pu, 30)) + C3);
    pv = S32(int64_t(Mul(v, pv, 30)) + C3);
    pu = S32(int64_t(Mul(u, pu, 30)) + C4);
    pv = S32(int64_t(Mul(v, pv, 30)) + C4);
    ss = Mul(x, pu, 30);
    cc = Mul(one_x, pv, 30);
  }
  switch (q) {
    case 0: *c = cc; *s = ss; break;
    case 1: *c = S32(-int64_t(ss)); *s = cc; break;
    case 2: *c = S32(-int64_t(cc)); *s = S32(-int64_t(ss)); break;
    default: *c = ss; *s = S32(-int64_t(cc)); break;
  }
}

struct Matrix { int32_t r20, r21, r22, r23; };

Matrix ObjectMatrix(uint32_t w4, uint32_t w6) {
  const int32_t s_lo = Sext(w4, 10) * 256, s_hi = Sext(w4 >> 16, 10) * 256;
  int32_t a, b;
  SinCos(w6 & 0xFFFF, &a, &b);
  return {Mul(s_hi, a, 16), Mul(s_lo, S32(-int64_t(b)), 16), Mul(s_hi, b, 16), Mul(s_lo, a, 16)};
}

// 806A422A..806A43A8, unsorted: (x0, y0) = the current vertex, 16.16.
void Segment(const uint32_t* e, const Matrix& m, uint32_t cur, uint32_t prev, uint32_t lfsr, int32_t cf0,
             int32_t out[4]) {
  int32_t r8 = Sext(cur, 16), r9 = Sext(cur >> 16, 16), r10 = Sext(prev, 16), r11 = Sext(prev >> 16, 16);
  int32_t r28 = r8 + ((r10 - r8) >> 1), r29 = r9 + ((r11 - r9) >> 1);
  r8 -= r28; r9 -= r29; r10 -= r28; r11 -= r29;
  int32_t j16 = Sext(lfsr >> 16, 8), j17 = Sext(lfsr >> 24, 8);
  int32_t t = S32(int64_t(int32_t(e[12])) + cf0);
  t = t <= 0 ? 0 : std::min(t, 0xFFFF);
  const uint32_t w13 = e[13];
  int32_t r0 = Mul(int32_t(lfsr & 0xFFFF), Sext(w13 >> 24, 8), 4);
  j16 = Mul(int32_t((w13 >> 16) & 0xFF), j16, 5);
  j17 = Mul(int32_t((w13 >> 8) & 0xFF), j17, 5);
  j16 = Mul(t, j16, 16);
  j17 = Mul(t, j17, 16);
  r0 = Mul(t, r0, 16);
  int32_t c, s;
  SinCos(uint32_t(r0) & 0xFFFF, &c, &s);
  r28 += j16;
  r29 += j17;
  auto rot = [&](int32_t x, int32_t y, int32_t* ox, int32_t* oy) {
    *ox = S32(int64_t(Mul(x, c, 30)) + Mul(y, S32(-int64_t(s)), 30));
    *oy = S32(int64_t(Mul(x, s, 30)) + Mul(y, c, 30));
  };
  rot(r8, r9, &r8, &r9);
  rot(r10, r11, &r10, &r11);
  r8 += r28; r9 += r29; r10 += r28; r11 += r29;
  auto obj = [&](int32_t x, int32_t y, int32_t* ox, int32_t* oy) {
    *ox = S32(int64_t(Mul(m.r20, x, 14)) + Mul(m.r21, y, 14));
    *oy = S32(int64_t(Mul(m.r22, x, 14)) + Mul(m.r23, y, 14));
  };
  obj(r8, r9, &r8, &r9);
  obj(r10, r11, &r10, &r11);
  const int32_t ox = int32_t((e[0] >> 16) << 16), oy = int32_t((e[0] & 0xFFFF) << 16);
  out[0] = S32(int64_t(r8) + ox);
  out[1] = S32(int64_t(r9) + oy);
  out[2] = S32(int64_t(r10) + ox);
  out[3] = S32(int64_t(r11) + oy);
}

// --- the line look ---

constexpr float kRadius = 1.94f;      // game pixels from the centre line to zero coverage (width byte 2)
constexpr float kZPeak = 0.00888f;    // centre opacity per unit of the colour's 4th byte

// A YCrCbZ colour word -> RGB as Nuance shows it (BT.601, video range), alpha = the line's centre opacity.
SDL_FColor Colour(uint32_t w, float strength) {
  auto cl = [](float v) { return std::clamp(v, 0.f, 1.f); };
  const float y = cl(float((w >> 24) & 0xFF) * (255.f / 219.f) / 255.f - 16.f / 219.f);
  const float cr = cl(float((w >> 16) & 0xFF) * (255.f / 224.f) / 255.f - 16.f / 224.f) - 0.5f;
  const float cb = cl(float((w >> 8) & 0xFF) * (255.f / 224.f) / 255.f - 16.f / 224.f) - 0.5f;
  return {cl(y + 1.402f * cr), cl(y - 0.34413f * cb - 0.714136f * cr), cl(y + 1.772f * cb),
          cl(kZPeak * float(w & 0xFF) * strength)};
}

using Mem = std::unordered_map<uint32_t, uint32_t>;

bool Read(const Mem& mem, uint32_t addr, uint32_t* w) {
  auto it = mem.find(addr);
  if (it == mem.end()) return false;
  *w = it->second;
  return true;
}

void Shape(const uint32_t* entry, const Mem& mem, std::vector<Seg>* segs) {
  uint32_t e[16];
  std::copy(entry, entry + 16, e);
  uint32_t ptr = e[7], pal[16] = {}, prev = 0, w = 0;
  bool have_prev = false;
  int32_t cf0 = 0;
  uint32_t lfsr = 0x31415926;
  Matrix m = ObjectMatrix(e[4], e[6]);
  for (int n = 0; n < 20000; ++n) {
    if (!Read(mem, ptr, &w)) return;
    ptr += 4;
    if ((w >> 16) == 0x8000) {
      uint32_t v = 0;
      switch (w & 0xF) {
        case 1: return;
        case 2: have_prev = false; break;
        case 0: Read(mem, ptr, &v); ptr += 4; e[5] = (e[5] & 0xFFFF0000u) | (v & 0xFFFF); break;
        case 5: Read(mem, ptr, &v); ptr += 4; e[5] = (e[5] & 0xFFFF) | ((v & 0xFFFF) << 16); break;
        case 3: Read(mem, ptr, &e[2]); ptr += 4; break;
        case 4: Read(mem, ptr, &e[3]); Read(mem, ptr + 4, &e[2]); ptr += 8; break;
        case 6: Read(mem, ptr, &v); ptr += 4; for (int i = 0; i < 16; ++i) Read(mem, v + 4 * i, &pal[i]); break;
        case 7: e[2] = pal[(w >> 8) & 0xF]; e[3] = pal[(w >> 12) & 0xF]; break;
        case 8: cf0 += Sext((w & 0xFFFF) >> 4, 12); break;
        case 9: Read(mem, ptr, &v); ptr += 4; cf0 = int32_t(v); break;
        default: break;
      }
      continue;
    }
    if (have_prev) {
      lfsr = (lfsr & 1) ? lfsr >> 1 : (lfsr >> 1) ^ 0xA3000000u;
      int32_t p[4];
      Segment(e, m, w, prev, lfsr, cf0, p);
      int32_t t = S32(int64_t(int32_t(e[12])) + cf0);
      t = t <= 0 ? 0 : std::min(t, 0xFFFF);
      // strength: w5 >> 2 relative to the measured lines (all 4FFF0002 so far), less the jitter share (1 - t)
      const float strength = float((e[5] >> 2) & 0x3FFFFFFF) / float(0x13FFC000) * (1.f - float(t) / 65536.f);
      Seg sg{float(p[0]) / 65536.f, float(p[1]) / 65536.f, float(p[2]) / 65536.f, float(p[3]) / 65536.f,
             Colour(e[2], strength), Colour(e[3], strength)};
      segs->push_back(sg);
    }
    prev = w;
    have_prev = true;
  }
}

}  // namespace

void BuildPicture(const nuon::Capture& cap, Picture* out) {
  out->flip = cap.flip;
  out->segs.clear();
  out->items.clear();
  out->ops.clear();
  out->complete = true;
  out->first_unknown = 0;
  for (size_t i = 0; i + 17 <= cap.entries.size(); i += 17) {
    const uint32_t* e = &cap.entries[i + 1];
    switch (e[15]) {
      case 0x00710001: {   // vector shape
        const uint32_t first = uint32_t(out->segs.size());
        Shape(e, cap.mem, &out->segs);
        const uint32_t n = uint32_t(out->segs.size()) - first;
        out->items.push_back({e[7], float(Sext(e[0] >> 16, 16)), float(Sext(e[0], 16)), first, n});
        Op op{Op::kSegs};
        op.first = first;
        op.count = n;
        if (n) out->ops.push_back(op);
        break;
      }
      case 0x00000300: {   // fill
        Op op{Op::kFill};
        op.x = float(e[0] >> 16);
        op.y = float(e[0] & 0xFFFF);
        op.w = float(e[1] >> 16);
        op.h = float(e[1] & 0xFFFF);
        op.colour = Colour(e[2] | 0xFF, 1.f);
        op.colour.a = 1.f;
        out->ops.push_back(op);
        break;
      }
      case 0x00300004: {   // zoom feedback
        Op op{Op::kZoom};
        op.cx = float(e[0] >> 16);
        op.cy = float(e[0] & 0xFFFF);
        op.zx = float(e[4]) / 65536.f;
        op.zy = float(e[5]) / 65536.f;
        op.angle = float(e[6] & 0xFFFF) * 6.2831853f / 65536.f;
        op.fade = float(e[7] >> 16) / 16384.f;
        out->ops.push_back(op);
        break;
      }
      case 0x000000FF: case 0x000000FE: case 0x003102FE:   // end of list; entries that draw nothing
        break;
      default:
        if (out->complete) out->first_unknown = e[15];
        out->complete = false;
        break;
    }
  }
}

Renderer::~Renderer() {
  for (SDL_Texture* t : tex_)
    if (t) SDL_DestroyTexture(t);
}

void Renderer::Redraw(SDL_Renderer* ren, const SDL_FRect& dst) {
  if (tex_[cur_]) SDL_RenderTexture(ren, tex_[cur_], nullptr, &dst);
}

void Renderer::Seed(SDL_Renderer* ren, SDL_Texture* picture, const SDL_FRect& dst) {
  if (!picture) return;
  Resize(ren, dst);
  SDL_Texture* keep = SDL_GetRenderTarget(ren);
  SDL_SetRenderTarget(ren, tex_[cur_]);
  SDL_BlendMode mode;
  SDL_GetTextureBlendMode(picture, &mode);
  SDL_SetTextureBlendMode(picture, SDL_BLENDMODE_NONE);
  SDL_RenderTexture(ren, picture, nullptr, nullptr);
  SDL_SetTextureBlendMode(picture, mode);
  SDL_SetRenderTarget(ren, keep);
}

void Renderer::Resize(SDL_Renderer* ren, const SDL_FRect& dst) {
  const int w = std::max(1, int(dst.w + 0.5f)), h = std::max(1, int(dst.h + 0.5f));
  if (w == w_ && h == h_ && tex_[0]) return;
  for (SDL_Texture*& t : tex_) {
    if (t) SDL_DestroyTexture(t);
    t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
  }
  w_ = w;
  h_ = h;
  SDL_Texture* keep = SDL_GetRenderTarget(ren);
  for (SDL_Texture* t : tex_) {
    SDL_SetRenderTarget(ren, t);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
  }
  SDL_SetRenderTarget(ren, keep);
}

void Renderer::Draw(SDL_Renderer* ren, const Picture& pic, const std::vector<Seg>& segs, const SDL_FRect& dst,
                    float step) {
  Resize(ren, dst);
  const int w = w_, h = h_;
  SDL_Texture* prev = tex_[cur_];
  SDL_Texture* next = tex_[cur_ ^ 1];
  SDL_Texture* keep = SDL_GetRenderTarget(ren);
  SDL_SetRenderTarget(ren, next);
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
  SDL_RenderClear(ren);
  const SDL_FRect full{0, 0, float(w), float(h)};
  const float sx = float(w) / 360.f, sy = float(h) / 240.f;
  std::vector<Seg> part;
  for (const Op& op : pic.ops) {
    switch (op.kind) {
      case Op::kFill: {
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColorFloat(ren, op.colour.r, op.colour.g, op.colour.b, 1.f);
        const SDL_FRect r{op.x * sx, op.y * sy, op.w * sx, op.h * sy};
        SDL_RenderFillRect(ren, &r);
        break;
      }
      case Op::kZoom: {
        // Each output point p samples the previous picture at c + R(-a) (p - c) / z (game pixels); for an in-between
        // frame the step's share: z^step, a x step, fade^step.
        const float zx = std::pow(op.zx, step), zy = std::pow(op.zy, step), a = -op.angle * step;
        const float fade = std::pow(std::max(op.fade, 0.f), step);
        const float ca = std::cos(a), sa = std::sin(a);
        auto uv = [&](float px, float py) {
          const float dx = px - op.cx, dy = py - op.cy;
          const float rx = (dx * ca - dy * sa) / zx, ry = (dx * sa + dy * ca) / zy;
          // (the module takes the nearest source pixel, floor(S) + 1/2, whose average is S itself)
          return SDL_FPoint{(op.cx + rx) / 360.f, (op.cy + ry) / 240.f};
        };
        const float f1 = std::min(fade, 1.f), f2 = std::max(fade - 1.f, 0.f);
        auto quad = [&](float k, SDL_BlendMode mode) {
          const SDL_FColor c{k, k, k, 1.f};
          const SDL_Vertex v[4] = {{{0, 0}, c, uv(0, 0)}, {{float(w), 0}, c, uv(360, 0)},
                                   {{0, float(h)}, c, uv(0, 240)}, {{float(w), float(h)}, c, uv(360, 240)}};
          const int idx[6] = {0, 1, 2, 1, 3, 2};
          SDL_SetTextureBlendMode(prev, mode);
          SDL_RenderGeometry(ren, prev, v, 4, idx, 6);
        };
        quad(f1, SDL_BLENDMODE_NONE);
        if (f2 > 0.f) quad(std::min(f2, 1.f), SDL_BLENDMODE_ADD);   // brightening (level start): add the excess
        break;
      }
      case Op::kSegs: {
        part.assign(segs.begin() + op.first, segs.begin() + op.first + op.count);
        DrawVectors(ren, part, full);
        break;
      }
    }
  }
  SDL_SetRenderTarget(ren, keep);
  SDL_SetTextureBlendMode(next, SDL_BLENDMODE_NONE);
  SDL_RenderTexture(ren, next, nullptr, &dst);
  cur_ ^= 1;
}

void Interpolate(const Picture& prev, const Picture& cur, float t, std::vector<Seg>* out, float max_move) {
  out->assign(cur.segs.begin(), cur.segs.end());
  if (t >= 1.f) return;
  t = std::max(t, 0.f);
  // Candidate pairs (same shape and segment count), nearest first; each item used once.
  struct Pair { float d; uint32_t c, p; };
  std::vector<Pair> pairs;
  for (uint32_t c = 0; c < cur.items.size(); ++c)
    for (uint32_t p = 0; p < prev.items.size(); ++p) {
      const Item &ic = cur.items[c], &ip = prev.items[p];
      if (ic.shape != ip.shape || ic.count != ip.count || !ic.count) continue;
      // distance: the first segment's start (covers shapes placed by their vertices, e.g. the web)
      const Seg &sc = cur.segs[ic.first], &sp = prev.segs[ip.first];
      pairs.push_back({std::hypot(sc.x0 - sp.x0, sc.y0 - sp.y0) + std::hypot(ic.x - ip.x, ic.y - ip.y), c, p});
    }
  std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.d < b.d; });
  std::vector<char> used_c(cur.items.size(), 0), used_p(prev.items.size(), 0);
  auto lerp = [t](float a, float b) { return a + (b - a) * t; };
  auto lerpc = [&](const SDL_FColor& a, const SDL_FColor& b) {
    return SDL_FColor{lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a)};
  };
  for (const Pair& pr : pairs) {
    if (used_c[pr.c] || used_p[pr.p]) continue;
    used_c[pr.c] = used_p[pr.p] = 1;
    const Item &ic = cur.items[pr.c], &ip = prev.items[pr.p];
    bool ok = true;
    for (uint32_t k = 0; k < ic.count && ok; ++k) {
      const Seg &a = prev.segs[ip.first + k], &b = cur.segs[ic.first + k];
      ok = std::fabs(a.x0 - b.x0) + std::fabs(a.y0 - b.y0) <= max_move &&
           std::fabs(a.x1 - b.x1) + std::fabs(a.y1 - b.y1) <= max_move;
    }
    if (!ok) continue;
    for (uint32_t k = 0; k < ic.count; ++k) {
      const Seg &a = prev.segs[ip.first + k], &b = cur.segs[ic.first + k];
      (*out)[ic.first + k] = {lerp(a.x0, b.x0), lerp(a.y0, b.y0), lerp(a.x1, b.x1), lerp(a.y1, b.y1),
                              lerpc(a.c0, b.c0), lerpc(a.c1, b.c1)};
    }
  }
}

void DrawVectors(SDL_Renderer* ren, const std::vector<Seg>& segs, const SDL_FRect& dst) {
  if (segs.empty()) return;
  const float sx = dst.w / 360.f, sy = dst.h / 240.f;
  // The module samples coverage at integer pixel coordinates (a pixel's corner): shift half a pixel to centres.
  auto X = [&](float x) { return dst.x + (x + 0.5f) * sx; };
  auto Y = [&](float y) { return dst.y + (y + 0.5f) * sy; };
  std::vector<SDL_Vertex> v;
  v.reserve(segs.size() * 30);
  for (const Seg& s : segs) {
    float dx = s.x1 - s.x0, dy = s.y1 - s.y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1e-4f) { dx /= len; dy /= len; } else { dx = 1.f; dy = 0.f; }
    const float nx = -dy * kRadius, ny = dx * kRadius;      // perpendicular, in game pixels
    const float ex = dx * kRadius, ey = dy * kRadius;       // along, for the end caps
    SDL_FColor e0 = s.c0, e1 = s.c1;
    e0.a = 0.f;
    e1.a = 0.f;
    auto V = [&](float x, float y, SDL_FColor c) { return SDL_Vertex{{X(x), Y(y)}, c, {0, 0}}; };
    const SDL_Vertex c0 = V(s.x0, s.y0, s.c0), c1 = V(s.x1, s.y1, s.c1);
    const SDL_Vertex l0 = V(s.x0 + nx, s.y0 + ny, e0), l1 = V(s.x1 + nx, s.y1 + ny, e1);
    const SDL_Vertex r0 = V(s.x0 - nx, s.y0 - ny, e0), r1 = V(s.x1 - nx, s.y1 - ny, e1);
    const SDL_Vertex t0 = V(s.x0 - ex, s.y0 - ey, e0), t1 = V(s.x1 + ex, s.y1 + ey, e1);
    // the tent: centre line opaque, both sides fading to nothing; caps: a fan to a point R beyond each end
    v.insert(v.end(), {c0, l0, c1, l0, l1, c1, c0, c1, r0, r0, c1, r1, c0, r0, t0, c0, t0, l0, c1, l1, t1, c1, t1, r1});
  }
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_RenderGeometry(ren, nullptr, v.data(), int(v.size()), nullptr, 0);
}

}  // namespace t3knative
