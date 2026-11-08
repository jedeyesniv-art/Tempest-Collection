// remaster.cpp - see remaster.h.
//
// llama.gas vect3d, as the GPU does it (in_buf = $F03F60, longs):
//   [1..3] position (16.16, camera-relative)   [4] rotate mode (0 = XY only)   [5] horizontal scale
//   [7..9] shape centre                         [10..12] angles xy, xz, yz (0-255)
//   [13] vertex list (12-byte x,y,z, numbered from 1)   [14] connection list   [15] vertex count   [16] colour
//   GPU RAM: _sysflags $FD0 (bit 1 clear = thick lines), scaler $FD4, z_max $FD8, z_top $FDC.
// Per vertex: v - centre; x *= hscale (imult); <<8; rotate xy (then xz, yz if rotate mode) with the signed sine
// table (p_sines $30000 - $80, /128 per step, imult = low 16 bits); sha (scaler - 8); + position;
// zi = Z >> 14 (<= 0: behind - clipped); screen x = 192 + (X / zi) >> 7, y = 120 + (Y / zi) >> 7;
// intensity = (z_max - zi) * z_top / z_max (<< 8 into the blitter's I3: byte = bits 16-23).
// Connection list: words; a vertex number, then the vertices it joins (high byte != 0: new colour), 0; ...; 0.
// Lines: CRY pixels (colour << 8 | intensity), Gouraud between the ends; colour 1 = not drawn.
// We keep the integer maths up to the rotation (the exact shape) and project in float.
#include "remaster.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "traps.h"

extern "C" uint32_t CRY16ToRGB32[0x10000];

namespace remaster {
namespace {

constexpr uint32_t kGpurun = 0x81A4F0;
constexpr uint32_t kLlama = 0x81A526;
constexpr uint32_t kInBuf = 0xF03F60;
constexpr uint32_t kGpuMode = 0xF03FFC;
constexpr uint32_t kGpuScreen = 0xF03FF8;
constexpr uint32_t kCscreen = 0x7FAC;     // the buffer the Object Processor shows
constexpr uint32_t kDscreen = 0x7FB0;     // the one being drawn
constexpr uint32_t kSines = 0x30000;

struct Vec3i { int32_t x, y, z; };

struct VecCall {
  uint32_t caller, obj;
  int32_t pos[3];
  int32_t rmode, hscale, centre[3], ang[3], col;
  int32_t scaler, zmax, ztop, sysflags;
  std::vector<Vec3i> verts;
  std::vector<uint16_t> conns;
};

std::map<uint32_t, std::vector<VecCall>> g_lists;   // per gpu_screen, built while the game draws
std::vector<Line> g_lines;                          // the displayed frame
float g_ox = -23.75f, g_oy = 5.25f;   // GPU screen -> emulated picture (fit_origin.py, 2026-11-08)
int g_sines[256];
uint64_t g_calls = 0, g_frames = 0;

int32_t S32(uint32_t a) { return int32_t(traps::Read32(a)); }
int32_t In(int i) { return S32(kInBuf + 4 * i); }
int32_t Low16(int32_t v) { return int16_t(uint16_t(v)); }

VecCall g_pending;            // the last captured call (RM_CHECK)
bool g_has_pending = false;

void Capture() {
  VecCall c;
  c.caller = traps::Read32(traps::Reg(15)) - 6;
  c.obj = traps::Reg(14);   // a6
  for (int i = 0; i < 3; ++i) c.pos[i] = In(1 + i);
  c.rmode = In(4);
  c.hscale = In(5);
  for (int i = 0; i < 3; ++i) c.centre[i] = In(7 + i), c.ang[i] = In(10 + i) & 0xFF;
  c.col = In(16) & 0xFF;
  c.sysflags = S32(0xF03FD0);
  c.scaler = S32(0xF03FD4);
  c.zmax = S32(0xF03FD8);
  c.ztop = S32(0xF03FDC);
  uint32_t vb = uint32_t(In(13)) & 0xFFFFFF, cb = uint32_t(In(14)) & 0xFFFFFF;
  int n = std::clamp(In(15), 0, 4096);
  c.verts.resize(n);
  for (int k = 0; k < n; ++k)
    c.verts[k] = {S32(vb + 12 * k), S32(vb + 12 * k + 4), S32(vb + 12 * k + 8)};
  // Connection list: vertex, joined..., 0 ... 0. Bounded in case of junk.
  for (int guard = 0; guard < 8192; ++guard) {
    uint16_t v = traps::Read16(cb);
    cb += 2;
    c.conns.push_back(v);
    if (v == 0) break;
    for (int g2 = 0; g2 < 8192; ++g2) {
      uint16_t w = traps::Read16(cb);
      cb += 2;
      c.conns.push_back(w);
      if (w == 0) break;
    }
  }
  g_pending = c;
  g_has_pending = true;
  g_lists[traps::Read32(kGpuScreen)].push_back(std::move(c));
  ++g_calls;
}

struct Pt { double x, y, z; bool ok; };   // camera space (16.16 units), ok = in front
Pt Transform(const VecCall& c, const Vec3i& v);
void Project(const VecCall& c, const Pt& p, float* sx, float* sy, double* inten);

// Debug (RM_CHECK=1): when the next GPU call starts, the previous vect3d call's results are in the GPU's vertex
// cache (vertex_buffer $F03B40, 24 bytes per vertex: screen x, y, intensity, X, Y, Z): compare with ours.

void CheckPending() {
  static const bool on = std::getenv("RM_CHECK") != nullptr;
  if (!on || !g_has_pending) return;
  g_has_pending = false;
  const VecCall& c = g_pending;
  int bad = 0;
  for (size_t k = 0; k < c.verts.size(); ++k) {
    uint32_t e = 0xF03B40 + 24 * uint32_t(k);
    int32_t gx = S32(e), gy = S32(e + 4), gi = S32(e + 8);
    if (gi < 0) continue;   // not used / z-clipped
    Pt p = Transform(c, c.verts[k]);
    float sx, sy;
    double in;
    Project(c, p, &sx, &sy, &in);
    if (std::fabs(sx - gx) > 1.5f || std::fabs(sy - gy) > 1.5f) {
      if (bad++ < 3)
        std::fprintf(stderr, "check %06X v%zu: gpu %d,%d ours %.1f,%.1f  (gpu X %08X Y %08X Z %08X)\n", c.caller,
                     k + 1, gx, gy, sx, sy, S32(e + 12), S32(e + 16), S32(e + 20));
    }
  }
  static int calls = 0, badcalls = 0;
  ++calls;
  badcalls += bad > 0;
  if (calls % 200 == 0) std::fprintf(stderr, "check: %d / %d calls differ\n", badcalls, calls);
}

bool g_enabled = true;
float g_width = 1.5f;   // line width in game pixels (setting remaster_line_width)

// The game's two play buffers (cscreen / dscreen): only draws into these are taken over.
bool PlayBuffer(uint32_t scr) { return scr == traps::Read32(kCscreen) || scr == traps::Read32(kDscreen); }

bool OnGpurun() {
  CheckPending();
  uint32_t mod = traps::Reg(8);
  if (mod != kLlama) return false;
  uint32_t mode = traps::Read32(kGpuMode);
  uint32_t scr = traps::Read32(kGpuScreen);
  if (mode == 0) g_lists[scr].clear();   // clearscreen: a new frame in this buffer
  else if (mode == 2) {
    Capture();
    return g_enabled && PlayBuffer(scr);   // we draw it: the GPU doesn't
  }
  return false;
}


Pt Transform(const VecCall& c, const Vec3i& v) {
  int32_t x = v.x - c.centre[0], y = v.y - c.centre[1], z = v.z - c.centre[2];
  x = Low16(x) * Low16(c.hscale);
  x <<= 8; y <<= 8; z <<= 8;
  auto rot = [&](int32_t& a, int32_t& b, int ang) {
    int32_t s = g_sines[ang & 0xFF], co = g_sines[(ang + 0x40) & 0xFF];
    int32_t na = ((co * Low16(a)) >> 7) - ((s * Low16(b)) >> 7);
    int32_t nb = ((s * Low16(a)) >> 7) + ((co * Low16(b)) >> 7);
    a = na; b = nb;
  };
  rot(x, y, c.ang[0]);
  if (c.rmode != 0) {
    rot(x, z, c.ang[1]);
    rot(y, z, c.ang[2]);
  }
  int sh = c.scaler - 8;
  double scale = std::ldexp(1.0, -sh);
  Pt p;
  p.x = x * scale + c.pos[0];
  p.y = y * scale + c.pos[1];
  p.z = z * scale + c.pos[2];
  p.ok = std::floor(p.z / 16384.0) > 0;
  return p;
}

uint32_t Rgb(int col, double inten) {
  int i = std::clamp(int(inten), 0, 255);
  return CRY16ToRGB32[(col << 8) | i] & 0xFFFFFF;
}

void Project(const VecCall& c, const Pt& p, float* sx, float* sy, double* inten) {
  double zi = p.z / 16384.0;
  *sx = float(192.0 + p.x / zi / 128.0);
  *sy = float(((c.sysflags >> 5) & 1 ? 140.0 : 120.0) + p.y / zi / 128.0);
  double zr = c.zmax - zi;
  // ((zmax - z) * ztop / zmax) << 8 gives the blitter's 8.16 intensity: the pixel byte is that >> 16.
  *inten = zr <= 0 || c.zmax == 0 ? 0 : (zr * c.ztop / c.zmax) * 256.0 / 65536.0;
}

void BuildLines(const VecCall& c, std::vector<Line>& out) {
  int n = int(c.verts.size());
  std::vector<Pt> pts(n);
  for (int k = 0; k < n; ++k) pts[k] = Transform(c, c.verts[k]);
  int col = c.col;
  bool thick = !(c.sysflags & 2);
  size_t i = 0;
  while (i < c.conns.size()) {
    int a = c.conns[i++];
    if (a == 0) break;
    while (i < c.conns.size()) {
      uint16_t w = c.conns[i++];
      if (w == 0) break;
      if (w >> 8) col = w >> 8;
      int b = w & 0xFF;
      if (a < 1 || a > n || b < 1 || b > n || col == 1) continue;
      Pt p0 = pts[a - 1], p1 = pts[b - 1];
      if (!p0.ok && !p1.ok) continue;
      // Clip against the near plane (zi = 1) in camera space.
      const double zn = 16384.0;
      auto clip = [&](Pt& bad, const Pt& good) {
        double t = (good.z - zn) / (good.z - bad.z);
        bad.x = good.x + (bad.x - good.x) * t;
        bad.y = good.y + (bad.y - good.y) * t;
        bad.z = zn;
      };
      if (!p0.ok) clip(p0, p1);
      if (!p1.ok) clip(p1, p0);
      Line l;
      double i0, i1;
      Project(c, p0, &l.x0, &l.y0, &i0);
      Project(c, p1, &l.x1, &l.y1, &i1);
      l.c0 = Rgb(col, i0);
      l.c1 = Rgb(col, i1);
      l.thick = thick;
      out.push_back(l);
    }
  }
}

}  // namespace

bool Install() {
  for (int i = 0; i < 256; ++i) g_sines[i] = int(traps::Read8(kSines + i)) - 0x80;
  return traps::Install(kGpurun, 0x6100, OnGpurun);
}

void AfterFrame() {
  // The sine table is built by the game at boot: refresh it until it looks set.
  if (g_sines[0x40] <= 0)
    for (int i = 0; i < 256; ++i) g_sines[i] = int(traps::Read8(kSines + i)) - 0x80;
  // The picture the core hands us for a frame is scanned out during that frame, so it shows the buffer that was
  // displayed when the frame began: build from the list captured for cscreen at the end of the frame before.
  static std::vector<VecCall> prev;
  static const bool same = std::getenv("RM_SAME") != nullptr;   // debug: the end-of-frame cscreen instead
  g_lines.clear();
  auto it = g_lists.find(traps::Read32(kCscreen));
  std::vector<VecCall> now = it != g_lists.end() ? it->second : std::vector<VecCall>{};
  for (auto& c : same ? now : prev) BuildLines(c, g_lines);
  prev = std::move(now);
  ++g_frames;
}

const std::vector<Line>& Lines() { return g_lines; }
void SetOrigin(float x, float y) { g_ox = x; g_oy = y; }
void SetEnabled(bool on) { g_enabled = on; }
void SetLineWidth(float w) { g_width = w; }
bool Enabled() { return g_enabled; }

host::Frame Overlay(const host::Frame& f, int scale) {
  host::Frame o;
  o.width = f.width * scale;
  o.height = f.height * scale;
  o.pixels.resize(size_t(o.width) * o.height);
  for (unsigned y = 0; y < o.height; ++y)
    for (unsigned x = 0; x < o.width; ++x) {
      uint32_t p = f.pixels[size_t(y / scale) * f.width + x / scale];
      o.pixels[size_t(y) * o.width + x] = p;
    }
  auto plot = [&](int x, int y, uint32_t c) {
    if (x >= 0 && y >= 0 && x < int(o.width) && y < int(o.height)) o.pixels[size_t(y) * o.width + x] = c;
  };
  int brush = std::max(1, scale * 3 / 4);
  for (auto& l : g_lines) {
    for (int pass = 0; pass < (l.thick ? 2 : 1); ++pass) {
      double x0 = (l.x0 + g_ox + pass) * scale, y0 = (l.y0 + g_oy) * scale;
      double x1 = (l.x1 + g_ox + pass) * scale, y1 = (l.y1 + g_oy) * scale;
      int steps = int(std::max(std::fabs(x1 - x0), std::fabs(y1 - y0))) + 1;
      for (int st = 0; st <= steps; ++st) {
        double t = double(st) / steps;
        auto lerp = [&](int sh) { return uint32_t((((l.c0 >> sh) & 255) * (1 - t) + ((l.c1 >> sh) & 255) * t)) << sh; };
        uint32_t c = lerp(16) | lerp(8) | lerp(0);
        int px = int(x0 + (x1 - x0) * t), py = int(y0 + (y1 - y0) * t);
        for (int by = 0; by < brush; ++by)
          for (int bx = 0; bx < brush; ++bx) plot(px + bx, py + by, c);
      }
    }
  }
  return o;
}

void DumpLines(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) return;
  for (auto& l : g_lines) std::fprintf(f, "%.3f %.3f %.3f %.3f\n", l.x0, l.y0, l.x1, l.y1);
  std::fclose(f);
}

std::string Stats() {
  char b[128];
  std::snprintf(b, sizeof b, "remaster: %zu lines shown, %llu vect3d calls captured, sin 0/40/80/C0 = %d %d %d %d",
                g_lines.size(), (unsigned long long)g_calls, g_sines[0], g_sines[0x40], g_sines[0x80], g_sines[0xC0]);
  return b;
}

void Draw(SDL_Renderer* ren, const SDL_FRect& dst, unsigned fw, unsigned fh) {
  if (g_lines.empty() || !fw || !fh) return;
  float sx = dst.w / fw, sy = dst.h / fh;
  std::vector<SDL_Vertex> v;
  v.reserve(g_lines.size() * 18);
  // Core width in game pixels (the original's thick vectors are 2 px wide), plus a 1-px feathered edge on each
  // side for anti-aliasing.
  const float feather = 1.0f;
  auto col = [](uint32_t c, float a) {
    return SDL_FColor{((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, a};
  };
  for (auto& l : g_lines) {
    float x0 = dst.x + (l.x0 + g_ox + 0.5f) * sx, y0 = dst.y + (l.y0 + g_oy + 0.5f) * sy;
    float x1 = dst.x + (l.x1 + g_ox + 0.5f) * sx, y1 = dst.y + (l.y1 + g_oy + 0.5f) * sy;
    if (l.thick) { x0 += sx * 0.5f; x1 += sx * 0.5f; }
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) continue;
    float w = std::max(0.5f, g_width * (l.thick ? 1.0f : 0.6f) * sx * 0.5f - feather * 0.5f);
    float ux = -dy / len, uy = dx / len;
    SDL_FColor c0 = col(l.c0, 1), c1 = col(l.c1, 1), e0 = col(l.c0, 0), e1 = col(l.c1, 0);
    auto V = [](float x, float y, SDL_FColor c) { return SDL_Vertex{{x, y}, c, {0, 0}}; };
    float o = w + feather;
    SDL_Vertex a0 = V(x0 + ux * o, y0 + uy * o, e0), a1 = V(x0 + ux * w, y0 + uy * w, c0);
    SDL_Vertex a2 = V(x0 - ux * w, y0 - uy * w, c0), a3 = V(x0 - ux * o, y0 - uy * o, e0);
    SDL_Vertex b0 = V(x1 + ux * o, y1 + uy * o, e1), b1 = V(x1 + ux * w, y1 + uy * w, c1);
    SDL_Vertex b2 = V(x1 - ux * w, y1 - uy * w, c1), b3 = V(x1 - ux * o, y1 - uy * o, e1);
    v.insert(v.end(), {a0, a1, b0, a1, b1, b0, a1, a2, b1, a2, b2, b1, a2, a3, b2, a3, b3, b2});
  }
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_RenderGeometry(ren, nullptr, v.data(), int(v.size()), nullptr, 0);
}

}  // namespace remaster
