// d2k.cpp - see d2k.h.
//
// Object Processor model (JTRM + the core's op.c, which is what the picture we check against comes from):
//   phrase 0: type 0-2, YPOS 3-13 (halflines), HEIGHT 14-23, LINK 24-42, DATA 43-63 (both >> 3)
//   phrase 1: XPOS 0-11 (signed), DEPTH 12-14, PITCH 15-17, DWIDTH 18-27, IWIDTH 28-37, INDEX 38-44,
//             REFLECT 45, RMW 46, TRANS 47, RELEASE 48, FIRSTPIX 49-54
//   phrase 2 (scaled, type 1): HSCALE 0-7, VSCALE 8-15, REMAINDER 16-23 (3.5 fixed point)
// The OP draws a bitmap one row per line it processes it on, from the first line at or past YPOS, writing HEIGHT
// and DATA back into the list. So we keep each object's phrases from the first time it is read in a frame and the
// list of halflines it was processed on (branch objects can gate it to part of the screen).
// Line buffer pixels are raw 16-bit words (CRY / RGB16 by VMODE); RMW adds signed CRY deltas; colour tables are
// the core's own.
#include "d2k.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <unordered_map>

#include "host.h"
#include "settings.h"

extern "C" {
extern uint8_t jagMemSpace[];
extern uint8_t tomRam8[];
extern uint32_t RGB16ToRGB32[];
extern uint32_t CRY16ToRGB32[];
extern uint32_t MIX16ToRGB32[];
extern void (*vjHostOpObject)(uint32_t addr, uint64_t p0, int halfline);
}

namespace d2k {
namespace {

bool g_active = false;
void (*g_chain)(uint32_t, uint64_t, int) = nullptr;

// ---- capture ----------------------------------------------------------------------------------------------------
// Each bitmap row is decoded when the OP reads the object for that line, from the phrase as the OP sees it then
// (DATA / HEIGHT / REMAINDER written back line by line) and the CLUT as it is then: the game redraws some buffers
// (stars, scanner dots) during the frame after their lines were shown, and changes the CLUT / BG down the picture.

// One drawn object: its rows as raw 16-bit line-buffer words with an opacity mask, placed in line-buffer space
// (x = line buffer pixel, row = halfline / 2).
struct Sprite {
  uint32_t addr = 0;             // its place in the object list
  int x = 0, row = 0;            // first drawn pixel / row (reflected: x is the RIGHT edge)
  int w = 0, h = 0;              // drawn pixels per row / rows
  int depth = 0, flags = 0;      // flags: 1 reflect, 2 RMW, 4 trans
  int type = 0;
  uint32_t data = 0, dwidth = 0; // DATA of the first drawn row, line pitch in phrases: for matching across updates
  std::vector<int> row_y;        // line-buffer row of each drawn row
  std::vector<uint16_t> raw;     // w * h
  std::vector<uint8_t> opaque;   // w * h
  // GPU only: columns left of the first drawn one that the game clipped off this update but showed the update
  // before (a tile scrolling out at the left edge); drawn while the motion is between the two.
  int lead = 0;
  std::vector<uint32_t> lead_rgb;   // lead * h, premultiplied XRGB (0 = see-through)
};
struct Scene {
  std::vector<Sprite> sprites;
  uint16_t vmode = 0, bg = 0;
  std::vector<uint16_t> row_bg;  // per line-buffer row (the game changes BG down the picture)
  uint64_t key = 0;              // geometry hash: changes when the game updates
};
Scene g_cur;          // this frame's
int g_frames = 0;

std::vector<Sprite> g_objs;                    // this frame's, in order of first drawn row
std::unordered_map<uint32_t, size_t> g_index;  // addr -> g_objs index

// The BG word each line was cleared with (taken at the line's first OP call: the clear comes before the OP pass).
constexpr int kHalflines = 640;
struct LineState { uint16_t bg = 0; bool seen = false; };
LineState g_line[kHalflines];
// The order sprites were drawn in on each line (the OP walks the list per line): sorted into one draw order.
std::vector<std::vector<uint16_t>> g_line_order(kHalflines);

std::map<std::string, int> g_unhandled;   // reasons, for Stats()

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint64_t Be64(const uint8_t* p) { return uint64_t(Be32(p)) << 32 | Be32(p + 4); }
uint16_t Raw16(uint32_t a) { return uint16_t(jagMemSpace[a & 0xFFFFFF] << 8 | jagMemSpace[(a + 1) & 0xFFFFFF]); }
uint16_t Clut(int i) { return uint16_t(tomRam8[0x400 + 2 * i] << 8 | tomRam8[0x401 + 2 * i]); }

// Pixel k of a bitmap line (in source order) as a raw word; *zero for pixels TRANS makes see-through.
uint16_t Pixel(uint32_t line, int k, int bpp, int pitch, int index, bool* zero) {
  const int ppp = 64 / bpp;
  const uint32_t phrase = line + uint32_t(k / ppp) * uint32_t(pitch) * 8u;
  const int bit = (k % ppp) * bpp;   // from the phrase's top
  if (bpp == 16) {
    uint16_t v = Raw16(phrase + uint32_t(bit / 8));
    *zero = v == 0;
    return v;
  }
  const uint8_t byte = jagMemSpace[(phrase + uint32_t(bit / 8)) & 0xFFFFFF];
  const int v = (byte >> (8 - bpp - (bit % 8))) & ((1 << bpp) - 1);
  int ci = v;
  if (bpp == 1) ci = (index & 0xFE) | v;
  else if (bpp == 2) ci = (index & 0xFC) | v;
  else if (bpp == 4) ci = (index & 0xF0) | v;
  const uint16_t c = Clut(ci);
  *zero = c == 0;   // (the core tests the CLUT entry, not the index)
  return c;
}

void OnOp(uint32_t addr, uint64_t p0, int halfline) {
  if (g_chain) g_chain(addr, p0, halfline);
  if (halfline >= 0 && halfline < kHalflines && !g_line[halfline].seen) {
    g_line[halfline].seen = true;
    g_line[halfline].bg = uint16_t(tomRam8[0x58] << 8 | tomRam8[0x59]);
  }
  const int type = int(p0 & 7);
  if (type > 1) return;   // branch / GPU / stop objects: not drawn
  const int ypos = int((p0 >> 3) & 0x7FF), height = int((p0 >> 14) & 0x3FF);
  if (halfline < ypos || height == 0) return;   // (the OP's own test)
  const uint8_t* m = jagMemSpace + (addr & 0xFFFFF8);
  const uint64_t p1 = Be64(m + 8), p2 = Be64(m + 16);
  const uint32_t data = uint32_t(p0 >> 43) << 3;
  int x = int(p1 & 0xFFF);
  if (x & 0x800) x -= 0x1000;
  const int depth = int((p1 >> 12) & 7), pitch = int((p1 >> 15) & 7);
  const int iwidth = int((p1 >> 28) & 0x3FF);
  const int index = int((p1 >> 37) & 0xFE);
  const int flags = int((p1 >> 45) & 7);
  const int firstpix = int((p1 >> 49) & 0x3F);
  if (depth > 4) { ++g_unhandled["24-bit bitmap"]; return; }
  if (iwidth == 0) return;
  static const int kBpp[5] = {1, 2, 4, 8, 16};
  const int bpp = kBpp[depth], src_w = iwidth * (64 / bpp);
  const int skip = firstpix / bpp;   // FIRSTPIX: bits into the first phrase
  if (firstpix) ++g_unhandled["firstpix (approx)"];
  // The source pixel of each drawn pixel: one to one, or the scaled OP's stepping (per drawn pixel the remainder
  // gains 1.0 and each HSCALE taken from it steps a source pixel).
  static std::vector<int> cols;
  cols.clear();
  if (type == 1) {
    const int hs = int(p2 & 0xFF);
    if (hs == 0) return;
    const int out_w = (src_w * hs) >> 5;
    int k = skip, hr = 0;
    for (int px = 0; px < out_w && k < src_w; ++px) {
      cols.push_back(k);
      hr += 0x20;
      while (hr >= hs) hr -= hs, ++k;
    }
  } else {
    for (int k = skip; k < src_w; ++k) cols.push_back(k);
  }
  auto it = g_index.find(addr);
  if (it == g_index.end()) {
    Sprite sp;
    sp.addr = addr;
    sp.type = type;
    sp.x = x;
    sp.row = halfline >> 1;
    sp.w = int(cols.size());
    sp.depth = depth;
    sp.flags = flags;
    sp.data = data;
    sp.dwidth = uint32_t((p1 >> 18) & 0x3FF);
    it = g_index.emplace(addr, g_objs.size()).first;
    g_objs.push_back(std::move(sp));
  }
  Sprite& sp = g_objs[it->second];
  if (!sp.row_y.empty() && sp.row_y.back() == (halfline >> 1)) return;
  if (halfline >= 0 && halfline < kHalflines) g_line_order[halfline].push_back(uint16_t(it->second));
  sp.row_y.push_back(halfline >> 1);
  ++sp.h;
  const size_t at = sp.raw.size();
  sp.raw.resize(at + size_t(sp.w), 0);
  sp.opaque.resize(at + size_t(sp.w), 0);
  const int n = std::min(sp.w, int(cols.size()));
  for (int i = 0; i < n; ++i) {
    bool zero = false;
    const uint16_t v = Pixel(data, cols[i], bpp, pitch ? pitch : 1, index, &zero);
    sp.raw[at + i] = v;
    sp.opaque[at + i] = !((flags & 4) && zero);
  }
}

// ---- scene ------------------------------------------------------------------------------------------------------
// One draw order from the per-line orders (a sprite after every sprite drawn before it on a shared line); ties
// keep first-drawn order.
void SortByLines(std::vector<Sprite>& v) {
  const size_t n = v.size();
  std::vector<std::vector<uint16_t>> after(n);
  std::vector<int> indeg(n, 0);
  for (auto& line : g_line_order)
    for (size_t i = 1; i < line.size(); ++i) {
      after[line[i - 1]].push_back(line[i]);
      ++indeg[line[i]];
    }
  std::vector<size_t> order;
  std::vector<bool> done(n, false);
  while (order.size() < n) {
    size_t pick = n;
    for (size_t i = 0; i < n; ++i)
      if (!done[i] && indeg[i] == 0) { pick = i; break; }
    if (pick == n) {   // a cycle (inconsistent lines): take the first left
      ++g_unhandled["draw order cycle"];
      for (size_t i = 0; i < n; ++i)
        if (!done[i]) { pick = i; break; }
    }
    done[pick] = true;
    order.push_back(pick);
    for (uint16_t a : after[pick]) --indeg[a];
  }
  std::vector<Sprite> out;
  out.reserve(n);
  for (size_t i : order) out.push_back(std::move(v[i]));
  v.swap(out);
}

void BuildScene(Scene& s) {
  SortByLines(g_objs);
  s.sprites.swap(g_objs);
  g_objs.clear();
  g_index.clear();
  s.vmode = uint16_t(tomRam8[0x28] << 8 | tomRam8[0x29]);
  s.bg = uint16_t(tomRam8[0x58] << 8 | tomRam8[0x59]);
  // Rows' BG: from the line state (a row no OP pass touched keeps the one above).
  s.row_bg.assign(kHalflines / 2, s.bg);
  uint16_t bg = s.bg;
  bool any = false;
  for (int r = 0; r < kHalflines / 2; ++r) {
    for (int hl : {2 * r, 2 * r + 1})
      if (g_line[hl].seen) { bg = g_line[hl].bg; any = true; }
    if (any) s.row_bg[r] = bg;
  }
  uint64_t key = 1469598103934665603ull;
  auto mix = [&](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
  for (const Sprite& sp : s.sprites) {
    mix(uint64_t(uint32_t(sp.x)) << 32 | uint32_t(sp.row)); mix(sp.data);
    mix(uint64_t(sp.w) << 32 | uint32_t(sp.h));
  }
  s.key = key;
}

// CRY add (RMW): signed deltas on the C and R nibbles (high byte) and Y (low byte), clamped.
uint16_t BlendRmw(uint16_t dst, uint16_t src) {
  int y = (dst & 0xFF) + int(int8_t(src & 0xFF));
  y = std::clamp(y, 0, 255);
  const int d_hi = dst >> 8, s_hi = src >> 8;
  int c1 = (d_hi & 0x0F) + (int(int8_t(uint8_t(s_hi << 4))) >> 4);
  int c2 = (d_hi >> 4) + (int(int8_t(uint8_t(s_hi & 0xF0))) >> 4);
  c1 = std::clamp(c1, 0, 15);
  c2 = std::clamp(c2, 0, 15);
  return uint16_t((c2 << 4 | c1) << 8 | y);
}

uint32_t ToRgb(uint16_t v, uint16_t vmode) {
  if (vmode & 0x100) return MIX16ToRGB32[v];
  const int mode = (vmode >> 1) & 3;
  return mode == 0 ? CRY16ToRGB32[v] : RGB16ToRGB32[v];
}

// The calibrated mapping from line buffer to the core's picture: frame x = lbuf x - g_x0 (found by Check).
int g_x0 = 16, g_y0 = 16;   // measured (D2K_CALIBRATE=1 searches again): the core starts its picture there
bool g_calibrate = std::getenv("D2K_CALIBRATE") != nullptr;   // frame (x, y) = line buffer (x - g_x0, row - g_y0)

// An RMW object's effect on what was under it, per pixel and channel, split into the part to add and the part to
// subtract (the GPU draws them with additive / reverse-subtract blending).
struct RmwDelta {
  size_t sprite = 0;
  std::vector<uint32_t> pos, neg;   // w * h, XRGB
};

void Compose(const Scene& s, std::vector<uint16_t>& lbuf, std::vector<uint8_t>& written, int lw, int rows,
             std::vector<RmwDelta>* deltas = nullptr) {
  lbuf.assign(size_t(lw) * rows, 0);
  if (s.vmode & 0x80)
    for (int r = 0; r < rows && r < int(s.row_bg.size()); ++r)
      std::fill(lbuf.begin() + size_t(r) * lw, lbuf.begin() + size_t(r + 1) * lw, s.row_bg[r]);
  written.assign(lbuf.size(), 0);
  if (deltas) deltas->clear();
  for (size_t si = 0; si < s.sprites.size(); ++si) {
    const Sprite& sp = s.sprites[si];
    RmwDelta* d = nullptr;
    if (deltas && (sp.flags & 2)) {
      deltas->push_back({si, std::vector<uint32_t>(size_t(sp.w) * sp.h, 0), std::vector<uint32_t>(size_t(sp.w) * sp.h, 0)});
      d = &deltas->back();
    }
    for (int j = 0; j < sp.h; ++j) {
      const int ry = sp.row_y[j];
      if (ry < 0 || ry >= rows) continue;
      for (int o = 0; o < sp.w; ++o) {
        if (!sp.opaque[size_t(j) * sp.w + o]) continue;
        const int lx = (sp.flags & 1) ? sp.x - o : sp.x + o;
        if (lx < 0 || lx >= lw) continue;
        const size_t i = size_t(ry) * lw + lx;
        const uint16_t v = sp.raw[size_t(j) * sp.w + o];
        if (d) {
          const uint32_t before = ToRgb(lbuf[i], s.vmode), after = ToRgb(BlendRmw(lbuf[i], v), s.vmode);
          uint32_t pp = 0, nn = 0;
          for (int c = 0; c < 24; c += 8) {
            const int dv = int(after >> c & 0xFF) - int(before >> c & 0xFF);
            if (dv > 0) pp |= uint32_t(dv) << c;
            else nn |= uint32_t(-dv) << c;
          }
          d->pos[size_t(j) * sp.w + o] = pp | 0xFF000000u;
          d->neg[size_t(j) * sp.w + o] = nn | 0xFF000000u;
        }
        lbuf[i] = (sp.flags & 2) ? BlendRmw(lbuf[i], v) : v;
        written[i] = 1;
      }
    }
  }
}

void WriteBmp(const std::string& path, const std::vector<uint32_t>& px, unsigned w, unsigned h) {
  std::ofstream f(path, std::ios::binary);
  const uint32_t row = w * 3 + (4 - (w * 3) % 4) % 4, size = 54 + row * h;
  uint8_t hd[54] = {'B', 'M'};
  auto put = [&](int at, uint32_t v) { std::memcpy(hd + at, &v, 4); };
  put(2, size); put(10, 54); put(14, 40); put(18, w); put(22, h);
  hd[26] = 1; hd[28] = 24;
  f.write(reinterpret_cast<char*>(hd), 54);
  std::vector<uint8_t> line(row, 0);
  for (unsigned y = h; y-- > 0;) {
    for (unsigned x = 0; x < w; ++x) {
      uint32_t p = px[size_t(y) * w + x];
      line[x * 3] = uint8_t(p); line[x * 3 + 1] = uint8_t(p >> 8); line[x * 3 + 2] = uint8_t(p >> 16);
    }
    f.write(reinterpret_cast<char*>(line.data()), row);
  }
}

constexpr int kLbufW = 720, kRows = 320;

// ---- game updates and motion ------------------------------------------------------------------------------------
// The game moves things every second frame (30 Hz). An update = a frame whose geometry differs from the frame
// before. Each sprite of the latest frame gets the motion since the update before (matched below); the window
// draws it at (position - (1 - a) * motion), a going 0 -> 1 over one update period: one update behind, smooth.
struct Geo {
  int x, row, w, h, depth, flags;
  uint32_t data, dwidth;
  float mdx = 0, mdy = 0;   // the motion it was given in its own update (a prediction for the next)
};
std::vector<Geo> g_prev_upd, g_this_upd;   // the update before the current one / the current one
uint64_t g_last_key = 0;
int g_upd_frame = 0, g_period = 2;
struct Motion { float dx = 0, dy = 0; bool matched = false; int clip_px = 0; };
std::vector<Motion> g_motion;              // per sprite of g_cur
std::vector<bool> g_taken;                 // per sprite of the update before: has a successor
// Ghosts: the update before's sprites with no successor in a moving layer (a tile that scrolled off), drawn moving
// with their layer while the blend runs, so no gap opens where they were.
struct Ghost { size_t prev = 0, before = 0; float dx = 0, dy = 0; };
std::vector<Ghost> g_ghosts;
Scene g_prev_frame, g_prev_scene;          // the frame before / the last frame of the update before
int g_matched_data = 0, g_matched_near = 0, g_unmatched = 0;

Geo GeoOf(const Sprite& sp) { return {sp.x, sp.row, sp.w, sp.h, sp.depth, sp.flags, sp.data, sp.dwidth, 0, 0}; }

// Same image (the same pixel data, maybe clipped differently: the game clips at the left / top edge by moving DATA
// into the image): the position of the image's origin moved by (dx, dy).
bool SameImage(const Geo& p, const Geo& c, float* dx, float* dy, int* clip_px = nullptr) {
  if (p.depth != c.depth || p.dwidth != c.dwidth || p.dwidth == 0 || (p.flags & 1) != (c.flags & 1)) return false;
  static const int kBpp[5] = {1, 2, 4, 8, 16};
  const int bpp = kBpp[std::min(c.depth, 4)];
  const int64_t pitch = int64_t(c.dwidth) * 8;
  const int64_t delta = int64_t(c.data) - int64_t(p.data);
  int64_t lines = delta / pitch, inl = delta - lines * pitch;
  if (inl < 0) inl += pitch, --lines;
  if (lines > 300 || lines < -300) return false;
  // DATA moved by `lines` lines + px pixels, or by lines + 1 lines and px - a line's pixels: the smaller motion.
  const int64_t px = inl * 8 / bpp, line_px = pitch * 8 / bpp;
  const float sx = (c.flags & 1) ? -1.f : 1.f;
  const float dx0 = float(c.x - sx * float(px) - p.x), dy0 = float((c.row - lines) - p.row);
  const float dx1 = float(c.x - sx * float(px - line_px) - p.x), dy1 = float((c.row - lines - 1) - p.row);
  int64_t use_lines = lines, use_px = px;
  if (std::fabs(dx0) + std::fabs(dy0) <= std::fabs(dx1) + std::fabs(dy1)) *dx = dx0, *dy = dy0;
  else *dx = dx1, *dy = dy1, use_lines = lines + 1, use_px = px - line_px;
  // Clipping changes the drawn size; the same size with DATA moved = another animation frame stored next to it.
  if ((use_lines != 0 && c.h == p.h) || (use_px != 0 && c.w == p.w)) return false;
  // The pixels the update before showed on the side clipped now: a reflected image is drawn leftwards from DATA,
  // so its left edge clips by narrowing; an unreflected one by moving DATA in.
  if (clip_px) {
    *clip_px = 0;
    if (use_lines == 0 && (c.flags & 1) && use_px == 0 && c.w < p.w) *clip_px = p.w - c.w;
    if (use_lines == 0 && !(c.flags & 1) && use_px > 0) *clip_px = int(use_px);
  }
  return true;
}

void MatchMotion(const Scene& cur) {
  g_motion.assign(cur.sprites.size(), Motion{});
  std::vector<bool>& taken = g_taken;
  taken.assign(g_prev_upd.size(), false);
  constexpr float kMax = 72.f;
  // 1. The same image nearby.
  for (size_t i = 0; i < cur.sprites.size(); ++i) {
    const Geo c = GeoOf(cur.sprites[i]);
    float best = 1e9f, bdx = 0, bdy = 0;
    int bclip = 0;
    size_t bj = SIZE_MAX;
    for (size_t j = 0; j < g_prev_upd.size(); ++j) {
      if (taken[j]) continue;
      float dx, dy;
      int clip = 0;
      if (!SameImage(g_prev_upd[j], c, &dx, &dy, &clip)) continue;
      const float d = std::fabs(dx) + std::fabs(dy);
      if (d < best && std::fabs(dx) <= kMax && std::fabs(dy) <= kMax) best = d, bdx = dx, bdy = dy, bj = j, bclip = clip;
    }
    if (bj != SIZE_MAX) {
      taken[bj] = true;
      g_motion[i] = {bdx, bdy, true, bclip};
      ++g_matched_data;
    }
  }
  // The motions seen so far this update (the parallax layers' scroll, 0 for the HUD): predictions for the rest.
  std::vector<std::pair<float, float>> layer = {{0.f, 0.f}};
  for (const Motion& m : g_motion)
    if (m.matched && std::find(layer.begin(), layer.end(), std::make_pair(m.dx, m.dy)) == layer.end())
      layer.push_back({m.dx, m.dy});
  // 2. Animated (another frame of pixels): about the same size and depth, where a prediction puts it (no motion,
  // its own last motion, or a layer's), within a few pixels; ties go to the nearer list place.
  constexpr float kTol = 10.f;
  for (size_t i = 0; i < cur.sprites.size(); ++i) {
    if (g_motion[i].matched) continue;
    const Geo c = GeoOf(cur.sprites[i]);
    float best = 1e9f, bdx = 0, bdy = 0;
    size_t bj = SIZE_MAX;
    for (size_t j = 0; j < g_prev_upd.size(); ++j) {
      const Geo& p = g_prev_upd[j];
      if (taken[j] || p.depth != c.depth || std::abs(p.w - c.w) > 8 || std::abs(p.h - c.h) > 4) continue;
      const float dx = float(c.x - p.x), dy = float(c.row - p.row);
      if (std::fabs(dx) > kMax || std::fabs(dy) > kMax) continue;
      float err = std::fabs(dx - p.mdx) + std::fabs(dy - p.mdy);
      for (auto& m : layer) err = std::min(err, std::fabs(dx - m.first) + std::fabs(dy - m.second));
      if (err > kTol) continue;
      const float score = err + 0.01f * float(i > j ? i - j : j - i);
      if (score < best) best = score, bdx = dx, bdy = dy, bj = j;
    }
    if (bj != SIZE_MAX) {
      taken[bj] = true;
      g_motion[i] = {bdx, bdy, true};
      ++g_matched_near;
    }
  }
  // 3. New this update (a tile coming in): the motion of a matched sibling in the same layer (same depth, line
  // pitch and top row).
  for (size_t i = 0; i < cur.sprites.size(); ++i) {
    if (g_motion[i].matched) continue;
    const Sprite& c = cur.sprites[i];
    for (size_t k = 0; k < cur.sprites.size(); ++k) {
      const Sprite& o = cur.sprites[k];
      if (k == i || !g_motion[k].matched || o.depth != c.depth || o.dwidth != c.dwidth ||
          std::abs(o.row - c.row) > 8 || c.dwidth == 0)
        continue;
      g_motion[i] = g_motion[k];
      break;
    }
    if (!g_motion[i].matched) ++g_unmatched;
  }
}

}  // namespace

bool IsDefender(const std::string& rom_path) {
  std::ifstream f(rom_path, std::ios::binary);
  std::string head(0x2000, '\0');
  f.read(head.data(), std::streamsize(head.size()));
  return rom_path.find("Defender") != std::string::npos;   // (contents check later: no source to key on yet)
}

bool Install() {
  g_chain = vjHostOpObject;
  vjHostOpObject = OnOp;
  g_active = true;
  return true;
}

bool Active() { return g_active; }

void BeforeFrame() {
  if (!g_active) return;
  g_objs.clear();
  g_index.clear();
  for (auto& l : g_line) l = LineState{};
  for (auto& l : g_line_order) l.clear();
}

void AfterFrame() {
  if (!g_active) return;
  std::swap(g_prev_frame, g_cur);
  BuildScene(g_cur);
  ++g_frames;
  if (g_cur.key != g_last_key) {
    g_period = std::clamp(g_frames - g_upd_frame, 1, 6);
    g_upd_frame = g_frames;
    g_last_key = g_cur.key;
    g_prev_scene = std::move(g_prev_frame);
    g_prev_frame = Scene{};
    for (Sprite& sp : g_prev_scene.sprites) sp.lead = 0;   // (ghosts are drawn without their lead-ins)
    g_prev_upd.clear();
    for (size_t i = 0; i < g_prev_scene.sprites.size(); ++i) {
      Geo gg = GeoOf(g_prev_scene.sprites[i]);
      if (g_this_upd.size() == g_prev_scene.sprites.size()) gg.mdx = g_this_upd[i].mdx, gg.mdy = g_this_upd[i].mdy;
      g_prev_upd.push_back(gg);
    }
    g_this_upd.clear();
    for (const Sprite& sp : g_cur.sprites) g_this_upd.push_back(GeoOf(sp));
  }
  MatchMotion(g_cur);
  g_ghosts.clear();
  for (size_t j = 0; j < g_prev_upd.size() && j < g_prev_scene.sprites.size(); ++j) {
    if (g_taken[j]) continue;
    const Sprite& p = g_prev_scene.sprites[j];
    if (p.dwidth == 0) continue;
    for (size_t k = 0; k < g_cur.sprites.size(); ++k) {
      const Sprite& c = g_cur.sprites[k];
      if (!g_motion[k].matched || c.depth != p.depth || c.dwidth != p.dwidth || std::abs(c.row - p.row) > 8) continue;
      if (g_motion[k].dx == 0 && g_motion[k].dy == 0) break;   // (a still layer: nothing to cover)
      g_ghosts.push_back({j, k, g_motion[k].dx, g_motion[k].dy});
      break;
    }
  }
  // Lead-ins: unscaled, unreflected sprites with every row consecutive, from the pixels before their DATA.
  for (size_t i = 0; i < g_cur.sprites.size(); ++i) {
    Sprite& sp = g_cur.sprites[i];
    const int lead = std::min(g_motion[i].clip_px, 96);
    if (lead <= 0 || sp.type != 0 || (sp.flags & 2) || sp.row_y.back() - sp.row_y.front() + 1 != sp.h) continue;
    static const int kBpp[5] = {1, 2, 4, 8, 16};
    const int bpp = kBpp[std::min(sp.depth, 4)];
    if (bpp < 8) continue;   // (sub-byte offsets: not worth it)
    sp.lead = lead;
    sp.lead_rgb.assign(size_t(lead) * sp.h, 0);
    for (int j = 0; j < sp.h; ++j) {
      // Unreflected: the pixels before DATA; reflected: the ones after the drawn width.
      const bool refl = sp.flags & 1;
      const uint32_t line = sp.data + uint32_t(j) * sp.dwidth * 8u - (refl ? 0u : uint32_t(lead * bpp / 8));
      for (int k = 0; k < lead; ++k) {
        bool zero = false;
        const uint16_t v = Pixel(line, refl ? sp.w + k : k, bpp, 1, 0, &zero);
        if (!((sp.flags & 4) && zero)) sp.lead_rgb[size_t(j) * lead + k] = ToRgb(v, g_cur.vmode) | 0xFF000000u;
      }
    }
  }
  if (g_upd_frame == g_frames && g_this_upd.size() == g_motion.size())
    for (size_t i = 0; i < g_motion.size(); ++i) g_this_upd[i].mdx = g_motion[i].dx, g_this_upd[i].mdy = g_motion[i].dy;
  static const bool log = std::getenv("D2K_MOTION_LOG") != nullptr;
  if (log && g_upd_frame == g_frames) {
    std::fprintf(stderr, "update %d (period %d):", g_frames, g_period);
    for (size_t i = 0; i < g_cur.sprites.size(); ++i) {
      const Sprite& sp = g_cur.sprites[i];
      std::fprintf(stderr, " [%d,%d %dx%d %06X %s%+.0f,%+.0f L%d]", sp.x, sp.row, sp.w, sp.h, sp.data,
                   g_motion[i].matched ? "" : "?", g_motion[i].dx, g_motion[i].dy, sp.lead);
    }
    for (const Ghost& gh : g_ghosts) {
      const Sprite& p = g_prev_scene.sprites[gh.prev];
      std::fprintf(stderr, " ghost[%d,%d %dx%d %06X %+.0f,%+.0f]", p.x, p.row, p.w, p.h, p.data, gh.dx, gh.dy);
    }
    std::fprintf(stderr, "\n");
  }
}

void SoftRenderFrom(const std::vector<uint16_t>& lbuf, const std::vector<uint8_t>& written, uint16_t vmode,
                    int x0, int y0, std::vector<uint32_t>& out, unsigned w, unsigned h) {
  out.assign(size_t(w) * h, 0);
  for (unsigned y = 0; y < h; ++y) {
    const int ry = int(y) + y0;
    if (ry < 0 || ry >= kRows) continue;
    for (unsigned x = 0; x < w; ++x) {
      const int lx = int(x) + x0;
      if (lx < 0 || lx >= kLbufW) continue;
      const size_t i = size_t(ry) * kLbufW + lx;
      if (written[i] || (vmode & 0x80)) out[size_t(y) * w + x] = ToRgb(lbuf[i], vmode) & 0xFFFFFF;
    }
  }
}

void SoftRender(std::vector<uint32_t>& out, unsigned w, unsigned h) {
  static std::vector<uint16_t> lbuf;
  static std::vector<uint8_t> written;
  Compose(g_cur, lbuf, written, kLbufW, kRows);
  SoftRenderFrom(lbuf, written, g_cur.vmode, g_x0, g_y0, out, w, h);
}

int Check(const std::string& prefix) {
  const host::Frame& fr = host::LastFrame();
  if (!fr.width) return -1;
  std::vector<uint32_t> ours;
  auto count = [&]() {
    SoftRender(ours, fr.width, fr.height);
    int bad = 0;
    for (size_t i = 0; i < ours.size(); ++i) bad += (ours[i] & 0xFFFFFF) != (fr.pixels[i] & 0xFFFFFF);
    return bad;
  };
  if (g_calibrate && host::LastFrame().width) {
    g_calibrate = false;   // calibrate the offsets (coarse: every 4th pixel)
    std::vector<uint16_t> lbuf;
    std::vector<uint8_t> written;
    Compose(g_cur, lbuf, written, kLbufW, kRows);
    int64_t best = int64_t(1) << 40;
    int bx = 0, by = 0;
    for (int y0 = -40; y0 < 60; ++y0)
      for (int x0 = -80; x0 < 120; ++x0) {
        int64_t b = 0;
        for (unsigned y = 0; y < fr.height; y += 2)
          for (unsigned x = 0; x < fr.width; x += 4) {
            const int lx = int(x) + x0, ry = int(y) + y0;
            uint32_t o = 0;
            if (lx >= 0 && ry >= 0 && lx < kLbufW && ry < kRows && written[size_t(ry) * kLbufW + lx])
              o = ToRgb(lbuf[size_t(ry) * kLbufW + lx], g_cur.vmode) & 0xFFFFFF;
            b += o != (fr.pixels[size_t(y) * fr.width + x] & 0xFFFFFF);
          }
        if (b < best) best = b, bx = x0, by = y0;
      }
    g_x0 = bx;
    g_y0 = by;
    std::fprintf(stderr, "d2k: line buffer offset x %d, row %d\n", bx, by);
  }
  if (const char* who = std::getenv("D2K_WHO")) {   // which sprites cover frame pixel x,y (debug)
    int fx = 0, fy = 0;
    std::sscanf(who, "%d,%d", &fx, &fy);
    const int lx = fx + g_x0, ry = fy + g_y0;
    for (const Sprite& sp : g_cur.sprites)
      for (int j = 0; j < sp.h; ++j)
        if (sp.row_y[j] == ry) {
          const int o = (sp.flags & 1) ? sp.x - lx : lx - sp.x;
          if (o >= 0 && o < sp.w)
            std::fprintf(stderr, "  covers %d,%d: [%d,%d %dx%d %06X d%d] opaque %d\n", fx, fy, sp.x, sp.row, sp.w, sp.h,
                         sp.data, sp.depth, sp.opaque[size_t(j) * sp.w + o]);
        }
  }
  int bad = count();
  std::vector<uint32_t> diff(ours.size());
  for (size_t i = 0; i < ours.size(); ++i)
    diff[i] = (ours[i] & 0xFFFFFF) == (fr.pixels[i] & 0xFFFFFF) ? (fr.pixels[i] >> 2 & 0x3F3F3F) : 0xFF00FF;
  if (bad == 0) {
    std::fprintf(stderr, "d2k check %s: exact (%d sprites)\n", prefix.c_str(), int(g_cur.sprites.size()));
    return 0;
  }
  WriteBmp(prefix + "_ours.bmp", ours, fr.width, fr.height);
  WriteBmp(prefix + "_core.bmp", fr.pixels, fr.width, fr.height);
  WriteBmp(prefix + "_diff.bmp", diff, fr.width, fr.height);
  std::fprintf(stderr, "d2k check %s: %d of %zu pixels differ (%d sprites, vmode %04X) %s\n", prefix.c_str(),
               bad, ours.size(), int(g_cur.sprites.size()), g_cur.vmode, Stats().c_str());
  return bad;
}

namespace {

struct AtlasRect { int x = 0, y = 0; };
struct Gpu {
  SDL_Texture* tex = nullptr;
  int size = 0;
  int built_frame = -1;
  std::vector<uint32_t> px;
  std::vector<AtlasRect> at;                      // per sprite
  std::vector<AtlasRect> ghost_at;                // per ghost
  std::vector<std::pair<AtlasRect, AtlasRect>> rmw_at;   // per RMW delta: pos, neg
  std::vector<RmwDelta> deltas;
  std::string filter;
} g_gpu;

// Shelf packing with a kPad-pixel gap around each image, filled with copies of its edge pixels (filtering at an
// edge then reads the edge itself: no seams between tiles that meet, no neighbour bleeding in).
constexpr int kPad = 2;
bool Pack(int size, const std::vector<std::pair<int, int>>& sizes, std::vector<AtlasRect>& out) {
  out.assign(sizes.size(), {});
  int x = kPad, y = kPad, shelf = 0;
  for (size_t i = 0; i < sizes.size(); ++i) {
    const int w = sizes[i].first, h = sizes[i].second;
    if (w + 2 * kPad > size || h + 2 * kPad > size) return false;
    if (x + w + kPad > size) { x = kPad; y += shelf + 2 * kPad; shelf = 0; }
    if (y + h + kPad > size) return false;
    out[i] = {x, y};
    x += w + 2 * kPad;
    shelf = std::max(shelf, h);
  }
  return true;
}

void BuildAtlas(SDL_Renderer* ren) {
  static std::vector<uint16_t> lbuf;
  static std::vector<uint8_t> written;
  Compose(g_cur, lbuf, written, kLbufW, kRows, &g_gpu.deltas);
  std::vector<std::pair<int, int>> sizes;
  for (const Sprite& sp : g_cur.sprites) sizes.push_back({sp.lead + sp.w, sp.h});
  for (const RmwDelta& d : g_gpu.deltas) {
    const Sprite& sp = g_cur.sprites[d.sprite];
    sizes.push_back({sp.w, sp.h});
    sizes.push_back({sp.w, sp.h});
  }
  for (const Ghost& gh : g_ghosts) sizes.push_back({g_prev_scene.sprites[gh.prev].w, g_prev_scene.sprites[gh.prev].h});
  std::vector<AtlasRect> rects;
  int size = std::max(g_gpu.size, 1024);
  while (!Pack(size, sizes, rects) && size < 8192) size *= 2;
  const std::string filter = settings::Str("d2k_filter", "sharp");
  if (!g_gpu.tex || g_gpu.size != size) {
    if (g_gpu.tex) SDL_DestroyTexture(g_gpu.tex);
    g_gpu.tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, size, size);
    g_gpu.size = size;
    g_gpu.px.assign(size_t(size) * size, 0);
    g_gpu.filter.clear();
  }
  if (filter != g_gpu.filter) {
    SDL_SetTextureScaleMode(g_gpu.tex, filter == "smooth" ? SDL_SCALEMODE_LINEAR
                                       : filter == "nearest" ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_PIXELART);
    g_gpu.filter = filter;
  }
  std::fill(g_gpu.px.begin(), g_gpu.px.end(), 0u);
  const size_t n = g_cur.sprites.size();
  g_gpu.at.assign(rects.begin(), rects.begin() + long(n));
  for (size_t i = 0; i < n; ++i) {
    const Sprite& sp = g_cur.sprites[i];
    // Atlas rows: [lead][drawn] unreflected, [drawn][lead] reflected (texture u runs with the image's columns).
    const bool refl = sp.flags & 1;
    for (int j = 0; j < sp.h; ++j)
      for (int k = 0; k < sp.lead; ++k)
        g_gpu.px[size_t(rects[i].y + j) * size + rects[i].x + (refl ? sp.w : 0) + k] = sp.lead_rgb[size_t(j) * sp.lead + k];
    const AtlasRect r{rects[i].x + (refl ? 0 : sp.lead), rects[i].y};
    for (int j = 0; j < sp.h; ++j)
      for (int o = 0; o < sp.w; ++o) {
        const size_t k = size_t(j) * sp.w + o;
        // Premultiplied: see-through pixels are 0.
        g_gpu.px[size_t(r.y + j) * size + r.x + o] =
            sp.opaque[k] ? (ToRgb(sp.raw[k], g_cur.vmode) | 0xFF000000u) : 0u;
      }
  }
  g_gpu.rmw_at.clear();
  for (size_t d = 0; d < g_gpu.deltas.size(); ++d) {
    const Sprite& sp = g_cur.sprites[g_gpu.deltas[d].sprite];
    const AtlasRect rp = rects[n + 2 * d], rn = rects[n + 2 * d + 1];
    for (int j = 0; j < sp.h; ++j)
      for (int o = 0; o < sp.w; ++o) {
        const size_t k = size_t(j) * sp.w + o;
        g_gpu.px[size_t(rp.y + j) * size + rp.x + o] = sp.opaque[k] ? g_gpu.deltas[d].pos[k] : 0u;
        g_gpu.px[size_t(rn.y + j) * size + rn.x + o] = sp.opaque[k] ? g_gpu.deltas[d].neg[k] : 0u;
      }
    g_gpu.rmw_at.push_back({rp, rn});
  }
  const size_t g0 = n + 2 * g_gpu.deltas.size();
  g_gpu.ghost_at.clear();
  for (size_t gi = 0; gi < g_ghosts.size(); ++gi) {
    const Sprite& sp = g_prev_scene.sprites[g_ghosts[gi].prev];
    const AtlasRect r = rects[g0 + gi];
    for (int j = 0; j < sp.h; ++j)
      for (int o = 0; o < sp.w; ++o) {
        const size_t k = size_t(j) * sp.w + o;
        g_gpu.px[size_t(r.y + j) * size + r.x + o] =
            sp.opaque[k] ? (ToRgb(sp.raw[k], g_prev_scene.vmode) | 0xFF000000u) : 0u;
      }
    g_gpu.ghost_at.push_back(r);
  }
  // Edge copies into the gaps.
  auto pad = [&](AtlasRect r, int w, int h) {
    uint32_t* px = g_gpu.px.data();
    for (int j = 0; j < h; ++j)
      for (int k = 1; k <= kPad; ++k) {
        px[size_t(r.y + j) * size + r.x - k] = px[size_t(r.y + j) * size + r.x];
        px[size_t(r.y + j) * size + r.x + w - 1 + k] = px[size_t(r.y + j) * size + r.x + w - 1];
      }
    for (int k = 1; k <= kPad; ++k)
      for (int i = -kPad; i < w + kPad; ++i) {
        px[size_t(r.y - k) * size + r.x + i] = px[size_t(r.y) * size + r.x + i];
        px[size_t(r.y + h - 1 + k) * size + r.x + i] = px[size_t(r.y + h - 1) * size + r.x + i];
      }
  };
  for (size_t i = 0; i < n; ++i) pad(rects[i], g_cur.sprites[i].lead + g_cur.sprites[i].w, g_cur.sprites[i].h);
  for (size_t d = 0; d < g_gpu.deltas.size(); ++d) {
    const Sprite& sp = g_cur.sprites[g_gpu.deltas[d].sprite];
    pad(rects[n + 2 * d], sp.w, sp.h);
    pad(rects[n + 2 * d + 1], sp.w, sp.h);
  }
  for (size_t gi = 0; gi < g_ghosts.size(); ++gi)
    pad(rects[g0 + gi], g_prev_scene.sprites[g_ghosts[gi].prev].w, g_prev_scene.sprites[g_ghosts[gi].prev].h);
  SDL_UpdateTexture(g_gpu.tex, nullptr, g_gpu.px.data(), size * 4);
  g_gpu.built_frame = g_frames;
}

}  // namespace

void Draw(SDL_Renderer* ren, const SDL_FRect& dst, float t) {
  if (!g_active || g_cur.sprites.empty()) return;
  if (g_gpu.built_frame != g_frames || settings::Str("d2k_filter", "sharp") != g_gpu.filter) BuildAtlas(ren);
  const host::Frame& fr = host::LastFrame();
  const float fw = fr.width ? float(fr.width) : 326.f, fh = fr.height ? float(fr.height) : 240.f;
  const float sx = dst.w / fw, sy = dst.h / fh;
  auto wx = [&](float lx) { return dst.x + (lx - float(g_x0)) * sx; };
  auto wy = [&](float row) { return dst.y + (row - float(g_y0)) * sy; };
  const bool interp = settings::Num("d2k_interpolate", 1) != 0;
  float a = interp ? std::clamp((float(g_frames - g_upd_frame) + t) / float(g_period), 0.f, 1.f) : 1.f;
  static const char* force = std::getenv("D2K_FORCE_A");   // test: a fixed blend position
  if (force) a = float(std::atof(force));
  const SDL_Rect clip{int(std::floor(dst.x)), int(std::floor(dst.y)), int(std::ceil(dst.w)), int(std::ceil(dst.h))};
  SDL_SetRenderClipRect(ren, &clip);

  // Background: each row's BG word (the sky gradient).
  std::vector<SDL_Vertex> v;
  std::vector<int> idx;
  auto quad = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, SDL_FColor c) {
    const int b = int(v.size());
    v.push_back({{x0, y0}, c, {u0, v0}});
    v.push_back({{x1, y0}, c, {u1, v0}});
    v.push_back({{x1, y1}, c, {u1, v1}});
    v.push_back({{x0, y1}, c, {u0, v1}});
    for (int k : {0, 1, 2, 0, 2, 3}) idx.push_back(b + k);
  };
  if (g_cur.vmode & 0x80) {
    for (int r = 0; r < int(fh); ++r) {
      const int row = r + g_y0;
      if (row < 0 || row >= int(g_cur.row_bg.size())) continue;
      const uint32_t c = ToRgb(g_cur.row_bg[row], g_cur.vmode);
      quad(dst.x, dst.y + r * sy, dst.x + dst.w, dst.y + (r + 1) * sy, 0, 0, 0, 0,
           {float(c >> 16 & 0xFF) / 255.f, float(c >> 8 & 0xFF) / 255.f, float(c & 0xFF) / 255.f, 1.f});
    }
    SDL_RenderGeometry(ren, nullptr, v.data(), int(v.size()), idx.data(), int(idx.size()));
    v.clear();
    idx.clear();
  }

  const float inv = 1.f / float(g_gpu.size);
  const SDL_FColor white{1, 1, 1, 1};
  SDL_BlendMode mode = SDL_BLENDMODE_BLEND_PREMULTIPLIED;
  auto flush = [&]() {
    if (v.empty()) return;
    SDL_SetTextureBlendMode(g_gpu.tex, mode);
    SDL_RenderGeometry(ren, g_gpu.tex, v.data(), int(v.size()), idx.data(), int(idx.size()));
    v.clear();
    idx.clear();
  };
  // A sprite's rows (in runs of consecutive rows) from an atlas rectangle, shifted by (ox, oy) line-buffer pixels.
  auto draw_rows = [&](const Sprite& sp, AtlasRect r, float ox, float oy) {
    int j0 = 0;
    while (j0 < sp.h) {
      int j1 = j0 + 1;
      while (j1 < sp.h && sp.row_y[j1] == sp.row_y[j1 - 1] + 1) ++j1;
      const float y0 = wy(float(sp.row_y[j0]) + oy), y1 = wy(float(sp.row_y[j1 - 1] + 1) + oy);
      float x0, x1, u0 = float(r.x) * inv, u1 = float(r.x + sp.lead + sp.w) * inv;
      if (sp.flags & 1) {   // reflected: pixel o at x - o
        x0 = wx(float(sp.x - sp.w - sp.lead + 1) + ox);
        x1 = wx(float(sp.x + 1) + ox);
        std::swap(u0, u1);
      } else {
        x0 = wx(float(sp.x - sp.lead) + ox);
        x1 = wx(float(sp.x + sp.w) + ox);
      }
      quad(x0, y0, x1, y1, u0, float(r.y + j0) * inv, u1, float(r.y + j1) * inv, white);
      j0 = j1;
    }
  };
  size_t next_rmw = 0;
  for (size_t i = 0; i < g_cur.sprites.size(); ++i) {
    const Sprite& sp = g_cur.sprites[i];
    if (a < 1.f)
      for (size_t gi = 0; gi < g_ghosts.size() && gi < g_gpu.ghost_at.size(); ++gi)
        if (g_ghosts[gi].before == i)
          draw_rows(g_prev_scene.sprites[g_ghosts[gi].prev], g_gpu.ghost_at[gi], a * g_ghosts[gi].dx,
                    a * g_ghosts[gi].dy);
    float ox = 0, oy = 0;
    if (i < g_motion.size() && g_motion[i].matched) {
      ox = -(1.f - a) * g_motion[i].dx;
      oy = -(1.f - a) * g_motion[i].dy;
    }
    if ((sp.flags & 2) && next_rmw < g_gpu.deltas.size() && g_gpu.deltas[next_rmw].sprite == i) {
      flush();
      mode = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
      draw_rows(sp, g_gpu.rmw_at[next_rmw].first, ox, oy);
      flush();
      mode = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
                                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
      draw_rows(sp, g_gpu.rmw_at[next_rmw].second, ox, oy);
      flush();
      mode = SDL_BLENDMODE_BLEND_PREMULTIPLIED;
      ++next_rmw;
      continue;
    }
    draw_rows(sp, g_gpu.at[i], ox, oy);
  }
  flush();
  SDL_SetRenderClipRect(ren, nullptr);
}

std::string Stats() {
  std::string s = "matched " + std::to_string(g_matched_data) + " by image, " + std::to_string(g_matched_near) +
                  " by size, " + std::to_string(g_unmatched) + " not; ";
  for (auto& [k, v] : g_unhandled) s += k + " x" + std::to_string(v) + "; ";
  return s;
}

}  // namespace d2k
