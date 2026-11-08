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
constexpr uint32_t kDonky = 0x81F6A2;
constexpr uint32_t kHorse = 0x81EB52;
constexpr uint32_t kXcent = 0xF03EFC;     // xcent (G_RAM + $EFC)
constexpr uint32_t kYcent = 0xF03EF8;     // ycent (G_RAM + $EF8)
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

// donky.gas web3d (the solid web): one quad per lane between the near and far rims (see BuildWeb).
struct WebFace { int32_t x0, y0, x1, y1; int colour, phase; };
struct WebCall {
  int32_t pos[3], glint, rot, angl, ycent;
  std::vector<WebFace> faces;
};

std::map<uint32_t, std::vector<VecCall>> g_lists;   // per gpu_screen, built while the game draws
std::map<uint32_t, std::vector<WebCall>> g_webs;    // solid webs, per gpu_screen

// llama.gas starfield1: in_buf+4/8/12 viewpoint, +16 star data (long count, then 32 bytes a star: x, y, z longs,
// colour word), +20 warp count (extra points a star, streaks), +24 warp separation (z step between them).
struct Star { int32_t x, y, z; int colour; };
struct StarCall {
  int32_t vp[3], warp_count, warp_add, sysflags;
  std::vector<Star> stars;
};
std::map<uint32_t, std::vector<StarCall>> g_stars;  // per gpu_screen

// horse.gas polyo2d (solid claw / enemies, drawn far to near by drawpolyos): in_buf+0 shape (long faces; 16 bytes a
// face: colour word, 3 x (vertex word, intensity word), pad; then x, y words a vertex), +4/8/12 position,
// +16/20 shape centre, +24 rotation. Vertex: v - centre, rotate, << 8, + position, / (z >> 14), >> 7, + xcent / ycent.
struct SolidFace { int colour; int16_t x[3], y[3]; int inten[3]; };
struct SolidCall {
  int32_t pos[3], xcen, ycen, rot, xcent, ycent;
  std::vector<SolidFace> faces;
};
std::map<uint32_t, std::vector<SolidCall>> g_solids;  // per gpu_screen
std::vector<Tri> g_solid_tris;                         // the displayed frame's solids (drawn after the lines)
std::vector<Dot> g_dots;                            // the displayed frame's stars
std::vector<Tri> g_tris;                            // the displayed frame's filled triangles
std::vector<Line> g_streaks;                        // warp streaks of the stars
int g_web_colour = 0;                               // donky's colour register carries over between calls
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

// web3d inputs (donky.gas): in_buf+0 web data, +4/8/12 position, +16 glint, +24 XY rotation, +28 colour phase.
// Web data: word faces, word, then (x, y) word pairs (scaled x8); after them per-lane phase words and, faces*2
// (+2 if connected) further on, the lane colour words; the connect flag is the word before the phase table.
void CaptureWeb() {
  WebCall w;
  uint32_t p = traps::Read32(kInBuf) & 0xFFFFFF;
  for (int i = 0; i < 3; ++i) w.pos[i] = In(1 + i);
  w.glint = In(4);
  w.rot = In(6);
  w.angl = In(7) + 0x40;
  w.ycent = S32(kYcent);
  int faces = int16_t(traps::Read16(p));
  p += 4;
  uint32_t ctptr = p + uint32_t((faces + 2) * 4 + 2);
  bool conn = traps::Read16(ctptr - 2) != 0;
  uint32_t r14 = uint32_t(faces * 2 + (conn ? 2 : 0));
  int f = faces;
  for (int guard = 0; guard < 256; ++guard) {
    WebFace fc;
    fc.x0 = int16_t(traps::Read16(p)) * 8;
    fc.y0 = int16_t(traps::Read16(p + 2)) * 8;
    fc.x1 = int16_t(traps::Read16(p + 4)) * 8;
    fc.y1 = int16_t(traps::Read16(p + 6)) * 8;
    p += 4;
    fc.phase = g_web_colour;                       // (the GPU reads the phase, then overwrites it with this)
    g_web_colour = traps::Read16(ctptr + r14);
    ctptr += 2;
    fc.colour = g_web_colour & 0xFF;
    w.faces.push_back(fc);
    --f;
    if (f < 0) break;
    if (f == 0 && traps::Read16(p + 8) == 0) break;
  }
  g_webs[traps::Read32(kGpuScreen)].push_back(std::move(w));
}

void CaptureSolid() {
  SolidCall c;
  uint32_t p = traps::Read32(kInBuf) & 0xFFFFFF;
  for (int i = 0; i < 3; ++i) c.pos[i] = In(1 + i);
  c.xcen = In(4);
  c.ycen = In(5);
  c.rot = In(6);
  c.xcent = S32(kXcent);
  c.ycent = S32(kYcent);
  int faces = std::clamp(S32(p), 0, 512);
  p += 4;
  uint32_t vt = p + uint32_t(faces) * 16;
  for (int f = 0; f < faces; ++f, p += 16) {
    SolidFace fc;
    fc.colour = traps::Read16(p) & 0xFF;
    for (int k = 0; k < 3; ++k) {
      uint32_t v = vt + uint32_t(traps::Read16(p + 2 + 4 * k)) * 4;
      fc.x[k] = int16_t(traps::Read16(v));
      fc.y[k] = int16_t(traps::Read16(v + 2));
      fc.inten[k] = traps::Read16(p + 4 + 4 * k) >> 8;   // $FF00 = full: the blitter takes word << 16
    }
    c.faces.push_back(fc);
  }
  g_solids[traps::Read32(kGpuScreen)].push_back(std::move(c));
}

void CaptureStars() {
  StarCall c;
  for (int i = 0; i < 3; ++i) c.vp[i] = In(1 + i);
  uint32_t p = traps::Read32(kInBuf + 16) & 0xFFFFFF;
  c.warp_count = std::clamp(In(5), 0, 64);
  c.warp_add = In(6);
  c.sysflags = S32(0xF03FD0);
  int n = std::clamp(S32(p), 0, 4096);
  p += 4;
  c.stars.resize(n);
  for (int i = 0; i < n; ++i, p += 32)
    c.stars[i] = {S32(p), S32(p + 4), S32(p + 8), traps::Read16(p + 12)};
  g_stars[traps::Read32(kGpuScreen)].push_back(std::move(c));
}

bool g_enabled = true;
float g_width = 1.5f;   // line width in game pixels (setting remaster_line_width)

// The game's two play buffers (cscreen / dscreen): only draws into these are taken over.
bool PlayBuffer(uint32_t scr) {
  static const bool noskip = std::getenv("RM_NOSKIP") != nullptr;   // debug: capture, but let the GPU draw too
  return !noskip && (scr == traps::Read32(kCscreen) || scr == traps::Read32(kDscreen));
}

bool OnGpurun() {
  CheckPending();
  uint32_t mod = traps::Reg(8);
  uint32_t mode = traps::Read32(kGpuMode);
  uint32_t scr = traps::Read32(kGpuScreen);
  if (mod == kDonky && mode == 0) {
    CaptureWeb();
    return g_enabled && PlayBuffer(scr);
  }
  if (mod == kHorse && mode == 0) {
    CaptureSolid();
    return g_enabled && PlayBuffer(scr);
  }
  if (mod != kLlama) return false;
  if (mode == 0) {                       // clearscreen: a new frame in this buffer
    g_lists[scr].clear();
    g_webs[scr].clear();
    g_stars[scr].clear();
    g_solids[scr].clear();
  }
  else if (mode == 2) {
    Capture();
    return g_enabled && PlayBuffer(scr);   // we draw it: the GPU doesn't
  } else if (mode == 3) {
    CaptureStars();
    return g_enabled && PlayBuffer(scr);
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

void BuildWeb(const WebCall& w, std::vector<Tri>& out) {
  int64_t zfar = int64_t(w.pos[2]) + 0x500000;
  if (zfar < 0) return;
  int64_t znear = int64_t(w.pos[2]) - 0x500000 + 1;
  if (znear < 1) znear = 0;
  double zf = double(uint32_t(zfar) >> 14), zn = double(uint32_t(znear) >> 14) + 1;
  int s = g_sines[w.rot & 0xFF], c = g_sines[(w.rot + 0x40) & 0xFF];
  auto proj = [&](int32_t px, int32_t py, double z, float* sx, float* sy) {
    px -= 64;
    py -= 64;
    double x = double(c * Low16(px) - s * Low16(py)) * 256.0 + w.pos[0];
    double y = double(s * Low16(px) + c * Low16(py)) * 256.0 + w.pos[1];
    *sx = float(192.0 + x / z / 128.0);
    *sy = float(w.ycent + y / z / 128.0);
  };
  for (auto& f : w.faces) {
    int r0 = ((int(traps::Read8(kSines + ((f.phase + w.angl) & 0xFF))) >> 1) + 64) * w.glint >> 8;
    uint32_t bright = CRY16ToRGB32[(f.colour << 8) | std::clamp(r0, 0, 255)] & 0xFFFFFF;
    uint32_t dim = CRY16ToRGB32[(f.colour << 8) | std::clamp(r0 >> 2, 0, 255)] & 0xFFFFFF;
    Tri a, b;
    float nx0, ny0, nx1, ny1, fx0, fy0, fx1, fy1;
    proj(f.x0, f.y0, zn, &nx0, &ny0);
    proj(f.x1, f.y1, zn, &nx1, &ny1);
    proj(f.x0, f.y0, zf, &fx0, &fy0);
    proj(f.x1, f.y1, zf, &fx1, &fy1);
    out.push_back({{nx0, nx1, fx0}, {ny0, ny1, fy0}, {bright, bright, dim}});
    out.push_back({{fx1, fx0, nx1}, {fy1, fy0, ny1}, {dim, dim, bright}});
  }
}

// starfield1, per point: z = (star.z - vp.z) & $1FFFFFF (+ k * warp_add for streak point k); intensity
// 255 - (z >> 17); zi = (z >> 14) + 1; x = 192 + (X / zi) >> 7, y = 120 (140 PAL) + (Y / zi) >> 7; off screen
// (x < 0, x >= 384, y < 0, y >= 240) not drawn. Pixel = colour word | intensity.
void BuildStars(const StarCall& c, std::vector<Dot>& dots, std::vector<Line>& streaks) {
  double cy = (c.sysflags >> 5) & 1 ? 140.0 : 120.0, h = (c.sysflags >> 5) & 1 ? 280.0 : 240.0;
  for (auto& st : c.stars) {
    double x = double(int32_t(st.x - c.vp[0])), y = double(int32_t(st.y - c.vp[1]));
    uint32_t z0 = uint32_t(st.z - c.vp[2]);
    float px = 0, py = 0;
    uint32_t pc = 0;
    bool have = false;
    for (int k = 0; k <= c.warp_count; ++k) {
      uint32_t z = (z0 + uint32_t(k) * uint32_t(c.warp_add)) & 0x1FFFFFF;
      int inten = 255 - int(z >> 17);
      double zi = double(z) / 16384.0 + 1.0;
      double sx = 192.0 + x / zi / 128.0, sy = cy + y / zi / 128.0;
      if (sx < 0 || sy < 0 || sx >= 384 || sy >= h) { have = false; continue; }
      uint32_t col = CRY16ToRGB32[(st.colour & 0xFF00) | std::clamp(inten, 0, 255)] & 0xFFFFFF;
      if (c.warp_count == 0) dots.push_back({float(sx), float(sy), col});
      else if (have) streaks.push_back({px, py, float(sx), float(sy), pc, col, false});
      else dots.push_back({float(sx), float(sy), col});
      px = float(sx); py = float(sy); pc = col; have = true;
    }
  }
}

void BuildSolid(const SolidCall& c, std::vector<Tri>& out) {
  double z = double(uint32_t(c.pos[2]) >> 14);
  if (z <= 0) return;
  int s = g_sines[c.rot & 0xFF], co = g_sines[(c.rot + 0x40) & 0xFF];
  for (auto& f : c.faces) {
    Tri t;
    for (int k = 0; k < 3; ++k) {
      int32_t x = f.x[k] - c.xcen, y = f.y[k] - c.ycen;
      double rx = double(co * Low16(x) - s * Low16(y)) * 256.0 + c.pos[0];
      double ry = double(s * Low16(x) + co * Low16(y)) * 256.0 + c.pos[1];
      t.x[k] = float(c.xcent + rx / z / 128.0);
      t.y[k] = float(c.ycent + ry / z / 128.0);
      t.c[k] = CRY16ToRGB32[(f.colour << 8) | f.inten[k]] & 0xFFFFFF;
    }
    out.push_back(t);
  }
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
  static std::vector<WebCall> prev_webs;
  static std::vector<StarCall> prev_stars;
  static std::vector<SolidCall> prev_solids;
  static const bool same = std::getenv("RM_SAME") != nullptr;   // debug: the end-of-frame cscreen instead
  g_lines.clear();
  g_tris.clear();
  g_dots.clear();
  g_streaks.clear();
  g_solid_tris.clear();
  uint32_t cs = traps::Read32(kCscreen);
  auto it = g_lists.find(cs);
  std::vector<VecCall> now = it != g_lists.end() ? it->second : std::vector<VecCall>{};
  auto wt = g_webs.find(cs);
  std::vector<WebCall> now_webs = wt != g_webs.end() ? wt->second : std::vector<WebCall>{};
  auto st = g_stars.find(cs);
  std::vector<StarCall> now_stars = st != g_stars.end() ? st->second : std::vector<StarCall>{};
  for (auto& c : same ? now_stars : prev_stars) BuildStars(c, g_dots, g_streaks);
  for (auto& w : same ? now_webs : prev_webs) BuildWeb(w, g_tris);
  auto so = g_solids.find(cs);
  std::vector<SolidCall> now_solids = so != g_solids.end() ? so->second : std::vector<SolidCall>{};
  for (auto& c : same ? now_solids : prev_solids) BuildSolid(c, g_solid_tris);
  for (auto& c : same ? now : prev) BuildLines(c, g_lines);
  prev = std::move(now);
  prev_webs = std::move(now_webs);
  prev_stars = std::move(now_stars);
  prev_solids = std::move(now_solids);
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
  std::fill(o.pixels.begin(), o.pixels.end(), 0u);
  auto plot = [&](int x, int y, uint32_t c) {
    if (x >= 0 && y >= 0 && x < int(o.width) && y < int(o.height)) o.pixels[size_t(y) * o.width + x] = c;
  };
  for (auto& dt : g_dots)
    for (int by = 0; by < std::max(1, scale / 2); ++by)
      for (int bx = 0; bx < std::max(1, scale / 2); ++bx)
        plot(int((dt.x + g_ox) * scale) + bx, int((dt.y + g_oy) * scale) + by, dt.c);
  auto fill = [&](const std::vector<Tri>& tris) {
  for (auto& t : tris) {
    double X[3], Y[3];
    for (int k = 0; k < 3; ++k) X[k] = (t.x[k] + g_ox) * scale, Y[k] = (t.y[k] + g_oy) * scale;
    int y0 = int(std::floor(std::min({Y[0], Y[1], Y[2]}))), y1 = int(std::ceil(std::max({Y[0], Y[1], Y[2]})));
    int x0 = int(std::floor(std::min({X[0], X[1], X[2]}))), x1 = int(std::ceil(std::max({X[0], X[1], X[2]})));
    double area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    if (std::fabs(area) < 1e-9) continue;
    for (int y = std::max(0, y0); y <= std::min(int(o.height) - 1, y1); ++y)
      for (int x = std::max(0, x0); x <= std::min(int(o.width) - 1, x1); ++x) {
        double px = x + 0.5, py = y + 0.5;
        double w0 = ((X[1] - px) * (Y[2] - py) - (X[2] - px) * (Y[1] - py)) / area;
        double w1 = ((X[2] - px) * (Y[0] - py) - (X[0] - px) * (Y[2] - py)) / area;
        double w2 = 1 - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        uint32_t c = 0;
        for (int sh = 0; sh <= 16; sh += 8) {
          double v = ((t.c[0] >> sh) & 255) * w0 + ((t.c[1] >> sh) & 255) * w1 + ((t.c[2] >> sh) & 255) * w2;
          c |= uint32_t(std::clamp(v, 0.0, 255.0)) << sh;
        }
        o.pixels[size_t(y) * o.width + x] = c;
      }
  }
  };
  fill(g_tris);
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
  fill(g_solid_tris);
  // The emulated picture on top, black see-through (RM_OURS: ours alone).
  if (!std::getenv("RM_OURS"))
  for (unsigned y = 0; y < o.height; ++y)
    for (unsigned x = 0; x < o.width; ++x) {
      uint32_t p = f.pixels[size_t(y / scale) * f.width + x / scale] & 0xFFFFFF;
      if (p) o.pixels[size_t(y) * o.width + x] = p;
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
  char b[256];
  std::snprintf(b, sizeof b, "remaster: %zu lines, %zu web triangles shown, %llu vect3d calls captured, "
                "sin 0/40/80/C0 = %d %d %d %d", g_lines.size(), g_tris.size(), (unsigned long long)g_calls, g_sines[0], g_sines[0x40], g_sines[0x80], g_sines[0xC0]);
  return b;
}

void DrawLines(SDL_Renderer* ren, const SDL_FRect& dst, float sx, float sy);

void DrawTris(SDL_Renderer* ren, const std::vector<Tri>& tris, const SDL_FRect& dst, float sx, float sy) {
  if (tris.empty()) return;
  std::vector<SDL_Vertex> t;
  t.reserve(tris.size() * 3);
  for (auto& tr : tris)
    for (int k = 0; k < 3; ++k) {
      uint32_t c = tr.c[k];
      t.push_back({{dst.x + (tr.x[k] + g_ox + 0.5f) * sx, dst.y + (tr.y[k] + g_oy + 0.5f) * sy},
                   {((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1.f}, {0, 0}});
    }
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
  SDL_RenderGeometry(ren, nullptr, t.data(), int(t.size()), nullptr, 0);
}

void Draw(SDL_Renderer* ren, const SDL_FRect& dst, unsigned fw, unsigned fh) {
  if (!fw || !fh) return;
  float sx = dst.w / fw, sy = dst.h / fh;
  if (!g_dots.empty()) {
    // A star: a small disc, full colour in the middle fading out at the edge (8-triangle fan).
    std::vector<SDL_Vertex> d;
    d.reserve(g_dots.size() * 24);
    float r = std::max(1.2f, sx * 0.7f);
    static float cs[9], sn[9];
    if (cs[0] == 0)
      for (int k = 0; k < 9; ++k) cs[k] = std::cos(k * float(M_PI) / 4), sn[k] = std::sin(k * float(M_PI) / 4);
    for (auto& dt : g_dots) {
      float cx = dst.x + (dt.x + g_ox + 0.5f) * sx, cy = dst.y + (dt.y + g_oy + 0.5f) * sy;
      SDL_FColor c{((dt.c >> 16) & 255) / 255.f, ((dt.c >> 8) & 255) / 255.f, (dt.c & 255) / 255.f, 1.f};
      SDL_FColor e = c;
      e.a = 0;
      for (int k = 0; k < 8; ++k) {
        d.push_back({{cx, cy}, c, {0, 0}});
        d.push_back({{cx + cs[k] * r, cy + sn[k] * r}, e, {0, 0}});
        d.push_back({{cx + cs[k + 1] * r, cy + sn[k + 1] * r}, e, {0, 0}});
      }
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(ren, nullptr, d.data(), int(d.size()), nullptr, 0);
  }
  if (!g_streaks.empty()) {
    std::swap(g_lines, g_streaks);   // draw the streaks with the line code (thin), then put things back
    float w = g_width;
    g_width = 0.8f;
    DrawLines(ren, dst, sx, sy);
    g_width = w;
    std::swap(g_lines, g_streaks);
  }
  DrawTris(ren, g_tris, dst, sx, sy);
  DrawLines(ren, dst, sx, sy);
  DrawTris(ren, g_solid_tris, dst, sx, sy);
}

void DrawLines(SDL_Renderer* ren, const SDL_FRect& dst, float sx, float sy) {
  if (g_lines.empty()) return;
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
