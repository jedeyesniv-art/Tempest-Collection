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
#include <set>
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
extern void (*vjHostGpuMem)(uint32_t addr, int load, uint32_t pc);
extern void (*vjHostBlit)(uint32_t cmd, uint32_t who);
extern void (*vjHostM68kWrite)(uint32_t addr, uint32_t value, int size, uint32_t pc);
extern uint32_t vjHostM68kWriteLo, vjHostM68kWriteHi;
const uint8_t* vjBlitterRam(void);
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
  std::vector<uint8_t> idx;      // w * h colour indices (depth <= 8), only filled for the art dump
  int hs = 0;                    // scaled objects: HSCALE (3.5)
  int skip = 0, src_w = 0, src_h = 0, pitch = 1, index = 0;   // the source: first pixel, pixels a row, rows (HEIGHT)
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
uint16_t Pixel(uint32_t line, int k, int bpp, int pitch, int index, bool* zero, int* ci_out = nullptr) {
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
  if (ci_out) *ci_out = ci;
  const uint16_t c = Clut(ci);
  *zero = c == 0;   // (the core tests the CLUT entry, not the index)
  return c;
}

// Test: D2K_ART_DUMP=<dir> - every distinct image the OP draws (see ArtDump).
const char* g_art_dir = std::getenv("D2K_ART_DUMP");

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
    if (type == 1) sp.hs = int(p2 & 0xFF);
    sp.skip = skip, sp.src_w = src_w, sp.src_h = height, sp.pitch = pitch ? pitch : 1, sp.index = index;
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
  const bool want_idx = g_art_dir && bpp <= 8;
  if (want_idx) sp.idx.resize(at + size_t(sp.w), 0);
  const int n = std::min(sp.w, int(cols.size()));
  for (int i = 0; i < n; ++i) {
    bool zero = false;
    int ci = 0;
    const uint16_t v = Pixel(data, cols[i], bpp, pitch ? pitch : 1, index, &zero, want_idx ? &ci : nullptr);
    if (want_idx) sp.idx[at + i] = uint8_t(ci);
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

// ---- the playfield layer's contents -------------------------------------------------------------------------------
// One 8-bit bitmap (384 wide, the whole screen) that the game clears and redraws every update: sky stars (several
// parallax speeds), scanner blips, lasers, explosion particles. Drawn as one image it would move at 30 Hz, so each
// update it is cut into items (8-connected pixel groups; a row of short dashes = one beam) matched against the update
// before's: by shape near a predicted place (still / the item's own last motion / the common displacements of this
// update), then the nearest of the rest; beams by row. The window draws matched items blended like sprites, beams
// with their ends blended, items that vanished fading out over the first half of the update.
struct Item {
  int x = 0, y = 0, w = 0, h = 0, n = 0;   // line-buffer place and size, pixel count
  bool beam = false;
  uint64_t shape = 0;                      // hash of the pixels relative to (x, y)
  uint64_t mask = 0;                       // the same without their colours (particles change colour as they age)
  float vx = 0, vy = 0;                    // motion since the update before (matched)
  int prev = -1;                           // matched item of the update before
  bool moving = false;                     // has a motion (matched, or a burst's new particle)
  int bx0 = 0, bx1 = 0, by = 0;            // beams: the matched beam's span and row
  std::vector<uint32_t> rgb;               // w * h premultiplied XRGB (0 = see-through)
};
std::vector<Item> g_items, g_prev_items;
std::vector<size_t> g_dead;                // items of the update before with no successor
int g_layer = -1;                          // the layer's index in g_cur.sprites (-1: none / not cut)
int g_items_matched = 0, g_items_total = 0;

bool IsLayer(const Sprite& sp) {
  return sp.type == 0 && sp.depth == 3 && sp.w >= 300 && !(sp.flags & 3) && sp.h > 0 &&
         sp.row_y.back() - sp.row_y.front() + 1 == sp.h;
}

void CutItems(const Sprite& sp, uint16_t vmode, std::vector<Item>& out) {
  out.clear();
  const int w = sp.w, h = sp.h;
  std::vector<int> label(size_t(w) * h, -1);
  struct Comp { int x0, y0, x1, y1; std::vector<int> px; };
  std::vector<Comp> comps;
  std::vector<int> stack;
  for (int i = 0; i < w * h; ++i) {
    if (!sp.opaque[size_t(i)] || label[size_t(i)] >= 0) continue;
    Comp c{i % w, i / w, i % w, i / w, {}};
    const int id = int(comps.size());
    label[size_t(i)] = id;
    stack.assign(1, i);
    while (!stack.empty()) {
      const int j = stack.back();
      stack.pop_back();
      c.px.push_back(j);
      const int x = j % w, y = j / w;
      c.x0 = std::min(c.x0, x); c.x1 = std::max(c.x1, x); c.y0 = std::min(c.y0, y); c.y1 = std::max(c.y1, y);
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          const int xx = x + dx, yy = y + dy;
          if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
          const int k = yy * w + xx;
          if (sp.opaque[size_t(k)] && label[size_t(k)] < 0) label[size_t(k)] = id, stack.push_back(k);
        }
    }
    comps.push_back(std::move(c));
  }
  // Beams: one-row pieces at least 2 wide on the same row with gaps of at most 16 pixels, spanning 24 or more.
  std::vector<int> group(comps.size(), -1);
  {
    std::vector<int> flat;
    for (size_t i = 0; i < comps.size(); ++i)
      if (comps[i].y0 == comps[i].y1 && comps[i].x1 > comps[i].x0) flat.push_back(int(i));
    std::sort(flat.begin(), flat.end(), [&](int a, int b) {
      return comps[a].y0 != comps[b].y0 ? comps[a].y0 < comps[b].y0 : comps[a].x0 < comps[b].x0;
    });
    size_t s = 0;
    while (s < flat.size()) {
      size_t e = s + 1;
      while (e < flat.size() && comps[flat[e]].y0 == comps[flat[s]].y0 &&
             comps[flat[e]].x0 - comps[flat[e - 1]].x1 <= 16)
        ++e;
      if (comps[flat[e - 1]].x1 - comps[flat[s]].x0 + 1 >= 24)
        for (size_t k = s; k < e; ++k) group[size_t(flat[k])] = flat[s];
      s = e;
    }
  }
  auto add = [&](const std::vector<int>& members, bool beam) {
    Item it;
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    for (int m : members) {
      x0 = std::min(x0, comps[size_t(m)].x0); x1 = std::max(x1, comps[size_t(m)].x1);
      y0 = std::min(y0, comps[size_t(m)].y0); y1 = std::max(y1, comps[size_t(m)].y1);
    }
    it.w = x1 - x0 + 1;
    it.h = y1 - y0 + 1;
    it.x = sp.x + x0;
    it.y = sp.row_y[size_t(y0)];
    it.beam = beam;
    it.rgb.assign(size_t(it.w) * it.h, 0);
    uint64_t hs = 1469598103934665603ull, hm = hs;
    std::vector<int> all;
    for (int m : members) all.insert(all.end(), comps[size_t(m)].px.begin(), comps[size_t(m)].px.end());
    std::sort(all.begin(), all.end());
    for (int p : all) {
      const int x = p % w - x0, y = p / w - y0;
      const uint16_t v = sp.raw[size_t(p)];
      it.rgb[size_t(y) * it.w + x] = ToRgb(v, vmode) | 0xFF000000u;
      hs = (hs ^ (uint64_t(uint32_t(y * 4096 + x)) << 16 | v)) * 1099511628211ull;
      hm = (hm ^ uint64_t(uint32_t(y * 4096 + x))) * 1099511628211ull;
    }
    it.n = int(all.size());
    it.shape = hs;
    it.mask = hm;
    out.push_back(std::move(it));
  };
  std::map<int, std::vector<int>> beams;
  for (size_t i = 0; i < comps.size(); ++i) {
    if (group[i] >= 0) beams[group[i]].push_back(int(i));
    else add({int(i)}, false);
  }
  for (auto& [k, members] : beams) add(members, true);
}

struct Pair { float score; int i, j; };
struct Burst { float pcx = 0, pcy = 0, ccx = 0, ccy = 0, k = 0, r = 0; };

// One burst: cur cluster ci = prev cluster pj moved by its centroid and scaled about it; the pairs within 3 pixels.
void BurstFit(const std::vector<Item>& cur, const std::vector<Item>& prev, const std::vector<int>& ci,
              const std::vector<int>& pj, int max_move, std::vector<Pair>& bp, Burst* out) {
  auto centre = [](const std::vector<Item>& v, const std::vector<int>& ix, float* cx, float* cy) {
    *cx = *cy = 0;
    for (int k : ix) *cx += float(v[size_t(k)].x), *cy += float(v[size_t(k)].y);
    *cx /= float(ix.size());
    *cy /= float(ix.size());
    float r = 0;
    for (int k : ix) r += std::hypot(float(v[size_t(k)].x) - *cx, float(v[size_t(k)].y) - *cy);
    return r / float(ix.size());
  };
  float pcx, pcy, ccx, ccy;
  const float pr = centre(prev, pj, &pcx, &pcy);
  centre(cur, ci, &ccx, &ccy);
  if (pr < 2.f || std::hypot(ccx - pcx, ccy - pcy) > float(max_move)) return;
  // The scale: the one (1.1 .. 3) that puts the most of the update before's particles within 2 pixels of one now
  // (several rings: the mean radius mixes them).
  float k = 0;
  size_t best = 0;
  for (float kk = 1.1f; kk <= 3.0f; kk += 0.02f) {
    size_t n = 0;
    for (int j : pj) {
      const float qx = ccx + kk * (float(prev[size_t(j)].x) - pcx), qy = ccy + kk * (float(prev[size_t(j)].y) - pcy);
      for (int i : ci)
        if (std::fabs(float(cur[size_t(i)].x) - qx) <= 2.f && std::fabs(float(cur[size_t(i)].y) - qy) <= 2.f) {
          ++n;
          break;
        }
    }
    if (n > best) best = n, k = kk;
  }
  if (k == 0) return;
  out->pcx = pcx; out->pcy = pcy; out->ccx = ccx; out->ccy = ccy; out->k = k;
  for (int i : ci) out->r = std::max(out->r, std::hypot(float(cur[size_t(i)].x) - ccx, float(cur[size_t(i)].y) - ccy));
  const size_t at = bp.size();
  for (int j : pj) {
    const float qx = ccx + k * (float(prev[size_t(j)].x) - pcx), qy = ccy + k * (float(prev[size_t(j)].y) - pcy);
    for (int i : ci) {
      const float d = std::hypot(float(cur[size_t(i)].x) - qx, float(cur[size_t(i)].y) - qy);
      if (d <= 3.f) bp.push_back({d, i, j});
    }
  }
  std::vector<int> hit;
  for (size_t q = at; q < bp.size(); ++q) hit.push_back(bp[q].j);
  std::sort(hit.begin(), hit.end());
  if ((std::unique(hit.begin(), hit.end()) - hit.begin()) * 10 < long(pj.size()) * 6) bp.resize(at);   // (< 60%)
}

void MatchItems(std::vector<Item>& cur, std::vector<Item>& prev) {
  constexpr int kMaxX = 48, kMaxY = 24;
  // The displacements many same-shape pairs share this update (the star layers' scroll, the world's).
  std::map<std::pair<int, int>, int> hist;
  for (const Item& c : cur)
    for (const Item& p : prev)
      if (!c.beam && p.shape == c.shape && std::abs(c.x - p.x) <= kMaxX && std::abs(c.y - p.y) <= kMaxY)
        ++hist[{c.x - p.x, c.y - p.y}];
  std::vector<std::pair<int, std::pair<int, int>>> modes;
  for (auto& [d, n] : hist)
    if (n >= 3) modes.push_back({n, d});
  std::sort(modes.rbegin(), modes.rend());
  if (modes.size() > 8) modes.resize(8);
  std::vector<std::pair<float, float>> preds = {{0.f, 0.f}};
  for (auto& m : modes) preds.push_back({float(m.second.first), float(m.second.second)});
  for (const Motion& m : g_motion)
    if (m.matched) preds.push_back({m.dx, m.dy});
  std::vector<Pair> pairs;
  std::vector<bool> taken(prev.size(), false), done(cur.size(), false);
  auto take = [&](int i, int j) {
    done[size_t(i)] = taken[size_t(j)] = true;
    Item& c = cur[size_t(i)];
    const Item& p = prev[size_t(j)];
    c.prev = j;
    c.moving = true;
    if (c.beam) {
      c.bx0 = p.x; c.bx1 = p.x + p.w; c.by = p.y;
      c.vx = 0; c.vy = float(c.y - p.y);
    } else {
      c.vx = float(c.x - p.x); c.vy = float(c.y - p.y);
    }
  };
  // 1. Bursts (an explosion's particles flying out from a point at once): per outline, the items of both updates as
  // one group that moved by its centroid and grew about it (constant velocities from one point and time: one scale
  // for every ring). First: the prediction pass would pair a young burst's particles with their neighbours.
  {
    std::map<uint64_t, std::pair<std::vector<int>, std::vector<int>>> groups;
    for (size_t i = 0; i < cur.size(); ++i)
      if (!done[i] && !cur[i].beam && cur[i].n >= 2) groups[cur[i].mask].first.push_back(int(i));   // (not stars)
    for (size_t j = 0; j < prev.size(); ++j)
      if (!taken[j] && !prev[j].beam && groups.count(prev[j].mask)) groups[prev[j].mask].second.push_back(int(j));
    // Single-link clusters (24 pixels) of one update's items: one burst each (scanner blips share the outline).
    auto clusters = [](const std::vector<Item>& v, const std::vector<int>& ix) {
      std::vector<std::vector<int>> out;
      std::vector<bool> used(ix.size(), false);
      for (size_t s = 0; s < ix.size(); ++s) {
        if (used[s]) continue;
        std::vector<int> c = {ix[s]};
        used[s] = true;
        for (size_t q = 0; q < c.size(); ++q)
          for (size_t t = 0; t < ix.size(); ++t)
            if (!used[t] && std::abs(v[size_t(ix[t])].x - v[size_t(c[q])].x) <= 24 &&
                std::abs(v[size_t(ix[t])].y - v[size_t(c[q])].y) <= 24)
              used[t] = true, c.push_back(ix[t]);
        if (c.size() >= 4) out.push_back(std::move(c));
      }
      return out;
    };
    for (auto& [shape, g] : groups) {
      if (g.first.size() < 4 || g.second.size() < 4) continue;
      const auto cc_all = clusters(cur, g.first), pc_all = clusters(prev, g.second);
      for (const auto& pj : pc_all) {
        std::vector<Pair> best_bp;
        Burst best_b;
        for (const auto& ci : cc_all) {
          std::vector<Pair> bp;
          Burst b;
          BurstFit(cur, prev, ci, pj, kMaxX, bp, &b);
          if (bp.size() > best_bp.size()) best_bp = std::move(bp), best_b = b;
        }
        std::sort(best_bp.begin(), best_bp.end(), [](const Pair& a, const Pair& b) { return a.score < b.score; });
        for (const Pair& pr2 : best_bp)
          if (!done[size_t(pr2.i)] && !taken[size_t(pr2.j)]) take(pr2.i, pr2.j);
        // The burst's other pieces (merged particles of its dense inner rings, the centre blob breaking up): its
        // motion. New ones fly out from the centre; vanished ones keep flying out while they fade.
        if (!best_bp.empty()) {
          for (size_t j = 0; j < prev.size(); ++j) {
            Item& p = prev[j];
            // (by the piece's middle, in the 2x2 particles' top-left terms the fit used)
            const float mx = float(p.x) + float(p.w - 2) * 0.5f, my = float(p.y) + float(p.h - 2) * 0.5f;
            if (taken[j] || p.beam || p.n < 2 || std::hypot(mx - best_b.pcx, my - best_b.pcy) > best_b.r / best_b.k + 4.f)
              continue;
            p.vx = best_b.ccx + best_b.k * (mx - best_b.pcx) - mx;
            p.vy = best_b.ccy + best_b.k * (my - best_b.pcy) - my;
          }
          for (size_t i = 0; i < cur.size(); ++i) {
            Item& c = cur[i];
            const float mx = float(c.x) + float(c.w - 2) * 0.5f, my = float(c.y) + float(c.h - 2) * 0.5f;
            if (done[i] || c.beam || c.n < 2 || std::hypot(mx - best_b.ccx, my - best_b.ccy) > best_b.r + 4.f) continue;
            c.vx = mx - (best_b.pcx + (mx - best_b.ccx) / best_b.k);
            c.vy = my - (best_b.pcy + (my - best_b.ccy) / best_b.k);
            c.moving = true;
            done[i] = true;
          }
        }
      }
    }
  }
  // 2. Shape (or about the size) where a prediction puts it.
  for (size_t i = 0; i < cur.size(); ++i) {
    const Item& c = cur[i];
    if (c.beam) continue;
    for (size_t j = 0; j < prev.size(); ++j) {
      const Item& p = prev[j];
      if (p.beam || std::abs(p.n - c.n) > std::max(2, c.n / 3)) continue;
      const int dx = c.x - p.x, dy = c.y - p.y;
      if (std::abs(dx) > kMaxX || std::abs(dy) > kMaxY) continue;
      float err = std::fabs(float(dx) - p.vx) + std::fabs(float(dy) - p.vy);
      for (auto& q : preds) err = std::min(err, std::fabs(float(dx) - q.first) + std::fabs(float(dy) - q.second));
      if (err > 3.f) continue;
      pairs.push_back({err + (p.shape == c.shape ? 0.f : 1.5f), int(i), int(j)});
    }
  }
  std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.score < b.score; });
  for (const Pair& pr : pairs)
    if (!done[size_t(pr.i)] && !taken[size_t(pr.j)]) take(pr.i, pr.j);
  // 3. The rest: mutual nearest of about the same size within 14 pixels.
  auto nearest_prev = [&](size_t i) {
    int best = -1;
    float bd = 14.5f;
    for (size_t j = 0; j < prev.size(); ++j) {
      const Item& p = prev[j];
      if (taken[j] || p.beam || std::abs(p.n - cur[i].n) > std::max(2, cur[i].n / 3)) continue;
      const float d = std::hypot(float(cur[i].x - p.x), float(cur[i].y - p.y));
      if (d < bd) bd = d, best = int(j);
    }
    return best;
  };
  for (size_t i = 0; i < cur.size(); ++i) {
    if (done[i] || cur[i].beam) continue;
    const int j = nearest_prev(i);
    if (j < 0) continue;
    int back = -1;   // the nearest unmatched current item to prev j
    float bd = 1e9f;
    for (size_t k = 0; k < cur.size(); ++k) {
      if (done[k] || cur[k].beam || std::abs(prev[size_t(j)].n - cur[k].n) > std::max(2, cur[k].n / 3)) continue;
      const float d = std::hypot(float(cur[k].x - prev[size_t(j)].x), float(cur[k].y - prev[size_t(j)].y));
      if (d < bd) bd = d, back = int(k);
    }
    if (back == int(i)) take(int(i), j);
  }
  // 4. Beams: the nearest beam within 6 rows that overlaps or is within 48 pixels.
  for (size_t i = 0; i < cur.size(); ++i) {
    if (!cur[i].beam) continue;
    const Item& c = cur[i];
    int best = -1;
    int bd = 1 << 30;
    for (size_t j = 0; j < prev.size(); ++j) {
      const Item& p = prev[j];
      if (taken[j] || !p.beam || std::abs(p.y - c.y) > 6) continue;
      const int gap = std::max(0, std::max(p.x - (c.x + c.w), c.x - (p.x + p.w)));
      if (gap > 48) continue;
      const int d = gap * 4 + std::abs(p.y - c.y);
      if (d < bd) bd = d, best = int(j);
    }
    if (best >= 0) take(int(i), best);
  }
  g_dead.clear();
  for (size_t j = 0; j < prev.size(); ++j)
    if (!taken[j]) g_dead.push_back(j);
  for (bool d : done) g_items_matched += d;
  g_items_total += int(cur.size());
}

}  // namespace

void SoftRenderFrom(const std::vector<uint16_t>& lbuf, const std::vector<uint8_t>& written, uint16_t vmode,
                    int x0, int y0, std::vector<uint32_t>& out, unsigned w, unsigned h);

bool IsDefender(const std::string& rom_path) {
  std::ifstream f(rom_path, std::ios::binary);
  std::string head(0x2000, '\0');
  f.read(head.data(), std::streamsize(head.size()));
  return rom_path.find("Defender") != std::string::npos;   // (contents check later: no source to key on yet)
}

// Test: D2K_GPULOG=F0:F1:file - every GPU load / store outside its RAM and every blit, frames F0..F1 (survey).
FILE* g_glog = nullptr;
int g_glog_f0 = 0, g_glog_f1 = -1;
void (*g_blit_chain)(uint32_t, uint32_t) = nullptr;
void OnGpuMem(uint32_t addr, int load, uint32_t pc) {
  if (g_glog && g_frames >= g_glog_f0 && g_frames <= g_glog_f1)
    std::fprintf(g_glog, "%c %06X %06X\n", load ? 'L' : 'S', pc & 0xFFFFFF, addr & 0xFFFFFF);
}
void OnBlit(uint32_t cmd, uint32_t who) {
  if (g_blit_chain) g_blit_chain(cmd, who);
  if (!(g_glog && g_frames >= g_glog_f0 && g_frames <= g_glog_f1)) return;
  const uint8_t* b = vjBlitterRam();
  std::fprintf(g_glog, "B %u cmd=%08X a1=%06X a1f=%08X a1p=%08X a2=%06X a2f=%08X a2p=%08X cnt=%08X pat=%08X%08X\n", who,
               cmd, Be32(b + 0x00), Be32(b + 0x04), Be32(b + 0x0C), Be32(b + 0x24), Be32(b + 0x28), Be32(b + 0x30),
               Be32(b + 0x3C), Be32(b + 0x68), Be32(b + 0x6C));
}

// Test: D2K_M68KLOG=F0:F1:lo:hi:file - 68K writes into [lo, hi] (hex), frames F0..F1: "W <pc> <addr> <size> <value>".
FILE* g_mlog = nullptr;
int g_mlog_f0 = 0, g_mlog_f1 = -1;
void OnM68kWrite(uint32_t addr, uint32_t value, int size, uint32_t pc) {
  if (g_mlog && g_frames >= g_mlog_f0 && g_frames <= g_mlog_f1)
    std::fprintf(g_mlog, "W %06X %06X %d %08X\n", pc & 0xFFFFFF, addr, size, value);
}

bool Install() {
  if (const char* ml = std::getenv("D2K_M68KLOG")) {
    char file[512] = {};
    unsigned lo = 0, hi = 0;
    if (std::sscanf(ml, "%d:%d:%x:%x:%511s", &g_mlog_f0, &g_mlog_f1, &lo, &hi, file) == 5) {
      g_mlog = std::fopen(file, "w");
      vjHostM68kWriteLo = lo;
      vjHostM68kWriteHi = hi;
      vjHostM68kWrite = OnM68kWrite;
    }
  }
  if (const char* gl = std::getenv("D2K_GPULOG")) {
    char file[512] = {};
    if (std::sscanf(gl, "%d:%d:%511s", &g_glog_f0, &g_glog_f1, file) == 3) {
      g_glog = std::fopen(file, "w");
      vjHostGpuMem = OnGpuMem;
      g_blit_chain = vjHostBlit;
      vjHostBlit = OnBlit;
    }
  }
  g_chain = vjHostOpObject;
  vjHostOpObject = OnOp;
  g_active = true;
  return true;
}

bool Active() { return g_active; }

// Arcade controls. The ship's facing is the word at $190630 (+1 right, -1 left; every mode, from boot on); the game
// turns the ship when the d-pad points the other way (read on its updates: 2 frames always did it) and thrusts while
// it points the way the ship faces.
int Arcade(bool thrust, bool reverse) {
  static bool rev_prev = false;
  static int rev_target = 0, rev_frames = 0;
  const int16_t facing = int16_t(jagMemSpace[0x190630] << 8 | jagMemSpace[0x190631]);
  const bool pressed = reverse && !rev_prev;
  rev_prev = reverse;
  if (facing != 1 && facing != -1) return 0;
  if (pressed) rev_target = -facing, rev_frames = 0;
  if (rev_target) {
    if (facing == rev_target || rev_frames >= 6) rev_target = 0;
    else return ++rev_frames, rev_target;
  }
  return thrust ? facing : 0;
}

// ---- wide screens ----------------------------------------------------------------------------------------------
// The object list is built each frame by GPU overlays the 68K copies into $F031C0 from RAM:
// - layers ($0B2B50): walks the parallax / playfield records, cuts each into strips, drops strips outside line-buffer
//   x 0..383 and clips the left one by moving its DATA;
// - sprites ($0A5938): world x - camera (wrapping at 4096, -255..3840 on screen), kept while -w < x < 384 + w, the
//   left one clipped the same way.
// Patched (in their RAM sources, so every copy carries it) to keep things from far left to 511, unclipped (the OP
// draws a negative XPOS itself): what the game has beyond the 4:3 picture reaches the line buffer and our capture.
struct Patch { uint32_t addr; int n; uint8_t orig[4], wide[4]; };
constexpr uint32_t kLayers = 0x0B2B50, kSprites = 0x0A5938, kRun = 0xF031C0;
constexpr Patch kWide[] = {
    {kLayers + (0xF03444 - kRun), 2, {0x18, 0x3A}, {0x01, 0xDA}},   // subq #1,r26 -> add r14,r26: strips up to 300 left
    {kLayers + (0xF0349A - kRun), 2, {0x64, 0x5B}, {0x67, 0xFB}},   // shrq #2,r27 -> shrq #31: no left clip
    {kLayers + (0xF034C2 - kRun), 2, {0x01, 0x80}, {0x02, 0x00}},   // movei #$180 -> #$200: right cull at 512
    {kSprites + (0xF034C8 - kRun), 4, {0x00, 0x00, 0x00, 0x00}, {0xFF, 0xA0, 0xFF, 0xFF}},   // movei #0 -> #-96: left
    {kSprites + (0xF034D0 - kRun), 2, {0x01, 0x80}, {0x02, 0x00}},   // movei #$180 -> #$200: right
    {kSprites + (0xF03726 - kRun), 2, {0xD0, 0x14}, {0xD0, 0x00}},   // jump pl -> jump t: no left clip
};
bool g_wide_on = false;

void ApplyWide(bool on) {
  for (const Patch& p : kWide) {   // only over the original (or our) bytes: another game / version stays untouched
    uint8_t* m = jagMemSpace + p.addr;
    if (std::memcmp(m, p.orig, size_t(p.n)) != 0 && std::memcmp(m, p.wide, size_t(p.n)) != 0) return;
  }
  for (const Patch& p : kWide) std::memcpy(jagMemSpace + p.addr, on ? p.wide : p.orig, size_t(p.n));
  g_wide_on = on;
}

// ---- art dump -----------------------------------------------------------------------------------------------------
// D2K_ART_DUMP=<dir>: the source rectangle of every object drawn (not the playfield layer), once per distinct content:
// where the OP read it in memory (DATA, line pitch DWIDTH, first pixel, pixels x rows) and its pixels as the game
// stores them (colour indices for <= 8 bits, raw words for 16) + their colours. A background strip is a window into
// a bigger picture, so tools/d2k_art_sheet.py joins rectangles that overlap in memory with the same content into one
// picture (a sprite's animation frames don't overlap: they stay apart).
// <dir>/rects.bin: per new rectangle a header of 12 int32 ('D2KR', DATA, DWIDTH, depth, first pixel, w, h, type,
// flags, frame, pitch, indexed) + w * h uint16 stored values + w * h uint32 RGBA (bytes R G B A; A 0 = see-through).
// <dir>/index.txt (rewritten every 300 frames and at exit): one line per rectangle with its uses and colourings.
struct ArtInfo {
  int n = 0, first = 0, last = 0, count = 0;
  uint32_t data = 0, dwidth = 0;
  int depth = 0, type = 0, flags = 0, skip = 0, w = 0, h = 0;
  std::set<uint64_t> colours;
};
std::map<uint64_t, ArtInfo> g_art;
FILE* g_art_bin = nullptr;
// Per memory place (DATA, DWIDTH, depth, first pixel): the content last seen there. A new content whose shared
// pixels differ at all is a conflict; a level loading new art makes one, a clipped sprite (fewer rows) none, a buffer
// the game draws into (the Classic playfield, the radar) one every update. kArtConflicts conflicts within
// kArtWindow frames make the place a buffer: left out from then on, listed in buffers.txt (the sheet tool drops the
// rectangles dumped there before).
constexpr int kArtConflicts = 6, kArtWindow = 300;
struct ArtPlace {
  int w = 0, h = 0;
  std::vector<uint16_t> val;
  std::vector<uint32_t> rgba;
  std::vector<int> conflicts;   // frames
  bool buffer = false;
};
std::map<uint64_t, ArtPlace> g_art_place;

void ArtWriteIndex() {
  FILE* f = std::fopen((std::string(g_art_dir) + "/index.txt").c_str(), "w");
  if (!f) return;
  std::fprintf(f, "# n key data dwidth depth skip w h type flags first last uses colourings\n");
  std::vector<const std::pair<const uint64_t, ArtInfo>*> v;
  for (const auto& e : g_art) v.push_back(&e);
  std::sort(v.begin(), v.end(), [](auto* a, auto* b) { return a->second.n < b->second.n; });
  for (auto* e : v) {
    const ArtInfo& a = e->second;
    std::fprintf(f, "%d %016llx %06X %u %d %d %d %d %d %d %d %d %d %zu\n", a.n, (unsigned long long)e->first, a.data,
                 a.dwidth, a.depth, a.skip, a.w, a.h, a.type, a.flags, a.first, a.last, a.count, a.colours.size());
  }
  std::fclose(f);
}
struct ArtAtExit {
  ~ArtAtExit() {
    if (g_art_dir && !g_art.empty()) ArtWriteIndex();
    if (g_art_bin) std::fclose(g_art_bin);
  }
} g_art_at_exit;

void ArtDump(const Scene& sc) {
  static const int kBpp[5] = {1, 2, 4, 8, 16};
  std::vector<uint16_t> val;
  std::vector<uint32_t> rgba;
  for (const Sprite& sp : sc.sprites) {
    if (IsLayer(sp) || sp.w <= 0 || sp.h <= 0 || sp.depth > 4) continue;
    const int bpp = kBpp[sp.depth];
    const bool indexed = bpp <= 8;
    int w = 0, h = 0;
    val.clear();
    rgba.clear();
    auto put = [&](uint16_t v, int ci, bool opaque) {
      val.push_back(indexed ? uint16_t(ci) : v);
      const uint32_t c = ToRgb(v, sc.vmode);
      rgba.push_back(opaque ? ((c >> 16 & 0xFF) | (c & 0xFF00) | (c & 0xFF) << 16 | 0xFF000000u) : 0u);
    };
    if (sp.type == 0 && sp.pitch == 1 && sp.row_y.back() - sp.row_y.front() + 1 == sp.h &&
        sp.idx.size() == (indexed ? sp.raw.size() : 0)) {
      w = sp.w, h = sp.h;   // as the OP read it
      for (size_t k = 0; k < sp.raw.size(); ++k) put(sp.raw[k], indexed ? sp.idx[k] : 0, sp.opaque[k]);
    } else {   // scaled (or gated rows): the source rows from memory
      w = sp.src_w - sp.skip, h = std::min(sp.src_h, 1023);
      if (w <= 0 || w > 2048 || h <= 0) continue;
      for (int j = 0; j < h; ++j)
        for (int k = 0; k < w; ++k) {
          bool zero = false;
          int ci = 0;
          const uint16_t v =
              Pixel(sp.data + uint32_t(j) * sp.dwidth * 8u, sp.skip + k, bpp, sp.pitch, sp.index, &zero, &ci);
          put(v, ci, !((sp.flags & 4) && zero));
        }
    }
    const uint64_t place_key = uint64_t(sp.data) << 32 | uint64_t(sp.dwidth) << 16 | uint64_t(sp.skip) << 4 |
                               uint64_t(sp.depth);
    ArtPlace& pl = g_art_place[place_key];
    if (pl.buffer) continue;
    if (pl.w) {
      int diff = 0, shared = 0;
      for (int j = 0; j < std::min(h, pl.h); ++j)
        for (int k = 0; k < std::min(w, pl.w); ++k) {
          const size_t a = size_t(j) * w + k, b = size_t(j) * pl.w + k;
          if (!rgba[a] && !pl.rgba[b]) continue;
          ++shared;
          diff += !rgba[a] != !pl.rgba[b] || val[a] != pl.val[b];
        }
      if (diff > 0) {   // (a scrolled or clipped picture agrees on every shared pixel)
        pl.conflicts.push_back(g_frames);
        while (pl.conflicts.front() < g_frames - kArtWindow) pl.conflicts.erase(pl.conflicts.begin());
        if (int(pl.conflicts.size()) >= kArtConflicts) {
          pl.buffer = true;
          if (FILE* f = std::fopen((std::string(g_art_dir) + "/buffers.txt").c_str(), "a")) {
            std::fprintf(f, "%06X %u %d %d %dx%d frame %d\n", sp.data, sp.dwidth, sp.depth, sp.skip, w, h, g_frames);
            std::fclose(f);
          }
          continue;
        }
      }
    }
    pl.w = w, pl.h = h, pl.val = val, pl.rgba = rgba;
    uint64_t key = 1469598103934665603ull, col = key;
    auto mix = [](uint64_t& hh, uint64_t v) { hh = (hh ^ v) * 1099511628211ull; };
    mix(key, uint64_t(sp.data) << 32 | sp.dwidth);
    mix(key, uint64_t(sp.depth) << 48 | uint64_t(sp.skip) << 40 | uint64_t(sp.pitch) << 36 | uint32_t(w) << 16 |
                 uint32_t(h));
    for (size_t k = 0; k < val.size(); ++k) {
      mix(key, rgba[k] ? val[k] + 1u : 0u);
      mix(col, rgba[k]);
    }
    ArtInfo& a = g_art[key];
    if (a.count == 0) {
      a.n = int(g_art.size()), a.first = g_frames, a.data = sp.data, a.dwidth = sp.dwidth, a.depth = sp.depth;
      a.type = sp.type, a.flags = sp.flags, a.skip = sp.skip, a.w = w, a.h = h;
      if (!g_art_bin) g_art_bin = std::fopen((std::string(g_art_dir) + "/rects.bin").c_str(), "wb");
      if (g_art_bin) {
        const int32_t hd[12] = {0x524B3244, int32_t(sp.data), int32_t(sp.dwidth), sp.depth, sp.skip, w, h,
                                sp.type, sp.flags, g_frames, sp.pitch, indexed};
        std::fwrite(hd, 4, 12, g_art_bin);
        std::fwrite(val.data(), 2, val.size(), g_art_bin);
        std::fwrite(rgba.data(), 4, rgba.size(), g_art_bin);
      }
    }
    a.last = g_frames;
    ++a.count;
    if (a.colours.size() < 64) a.colours.insert(col);
  }
  if (g_frames % 300 == 0) {
    ArtWriteIndex();
    if (g_art_bin) std::fflush(g_art_bin);
  }
}

void BeforeFrame() {
  if (!g_active) return;
  ApplyWide(settings::Num("d2k_widescreen", 1) != 0 && settings::Num("d2k_native", 1) != 0);
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
  if (g_art_dir) ArtDump(g_cur);
  if (g_glog && g_frames >= g_glog_f0 && g_frames <= g_glog_f1) std::fprintf(g_glog, "F %d\n", g_frames);
  if (const char* ws = std::getenv("D2K_WIDESHOT")) {   // test: D2K_WIDESHOT=F:margin:file - software picture, wider
    char file[512] = {};
    int f = -1, margin = 0;
    if (std::sscanf(ws, "%d:%d:%511s", &f, &margin, file) == 3 && f == g_frames) {
      static std::vector<uint16_t> lbuf;
      static std::vector<uint8_t> written;
      std::vector<uint32_t> out;
      Compose(g_cur, lbuf, written, kLbufW, kRows);
      SoftRenderFrom(lbuf, written, g_cur.vmode, g_x0 - margin, g_y0, out, unsigned(326 + 2 * margin), 240);
      WriteBmp(file, out, unsigned(326 + 2 * margin), 240);
    }
  }
  if (const char* rd = std::getenv("D2K_RAMDUMP")) {   // test: D2K_RAMDUMP=F:file - main RAM (2 MB) after frame F
    char file[512] = {};
    int f = -1;
    if (std::sscanf(rd, "%d:%511s", &f, file) == 2 && f == g_frames) {
      std::ofstream o(file, std::ios::binary);
      o.write(reinterpret_cast<const char*>(jagMemSpace), 0x200000);
    }
  }
  if (g_mlog && g_frames >= g_mlog_f0 && g_frames <= g_mlog_f1) std::fprintf(g_mlog, "F %d\n", g_frames);
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
  // The playfield layer: cut into items once per update (the shown buffer doesn't change in between).
  {
    int layer = -1;
    for (size_t i = 0; i < g_cur.sprites.size(); ++i)
      if (IsLayer(g_cur.sprites[i])) { layer = int(i); break; }
    if (layer < 0) {
      g_items.clear();
      g_prev_items.clear();
      g_dead.clear();
    } else if (g_upd_frame == g_frames || g_layer < 0) {
      g_prev_items = std::move(g_items);
      CutItems(g_cur.sprites[size_t(layer)], g_cur.vmode, g_items);
      MatchItems(g_items, g_prev_items);
      if (std::getenv("D2K_ITEM_LOG")) {
        int m = 0, beams = 0;
        for (const Item& it : g_items) m += it.moving, beams += it.beam;
        std::fprintf(stderr, "items %d: %zu (%d beams), %d matched, %zu vanished:", g_frames, g_items.size(), beams, m,
                     g_dead.size());
        for (const Item& it : g_items)
          if (!it.moving && it.n > 1) std::fprintf(stderr, " new[%d,%d %dx%d]", it.x, it.y, it.w, it.h);
        std::fprintf(stderr, "\n");
      }
    }
    g_layer = layer;
  }
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
  // Test: D2K_LAYER_DUMP=<prefix>: the 8-bit playfield layer's raw bytes each update (<prefix>_<frame>.bin, a
  // 16-byte header: data address, width, height, frame) for the offline survey of what the game draws into it.
  static const char* dump = std::getenv("D2K_LAYER_DUMP");
  if (dump && g_upd_frame == g_frames)
    for (const Sprite& sp : g_cur.sprites)
      if (sp.depth == 3 && sp.w >= 300) {
        const uint32_t w = sp.dwidth * 8, rows = uint32_t(sp.row_y.back() - sp.row_y.front() + 1);
        const uint32_t top = sp.data;
        std::ofstream f(std::string(dump) + "_" + std::to_string(g_frames) + ".bin", std::ios::binary);
        const uint32_t hd[4] = {top, w, rows, uint32_t(g_frames)};
        f.write(reinterpret_cast<const char*>(hd), 16);
        f.write(reinterpret_cast<const char*>(jagMemSpace + (top & 0xFFFFFF)), std::streamsize(w) * rows);
        std::vector<uint8_t> clut(512);
        std::memcpy(clut.data(), tomRam8 + 0x400, 512);
        f.write(reinterpret_cast<const char*>(clut.data()), 512);
        std::fprintf(stderr, "layer %d: data %06X x %d row %d %ux%u\n", g_frames, top, sp.x, sp.row, w, rows);
      }
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
  std::vector<AtlasRect> item_at, dead_at;        // per layer item / vanished item
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
  for (const Item& it : g_items) sizes.push_back({it.w, it.h});
  for (size_t d : g_dead) sizes.push_back({g_prev_items[d].w, g_prev_items[d].h});
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
  const size_t i0 = g0 + g_ghosts.size();
  g_gpu.item_at.assign(rects.begin() + long(i0), rects.begin() + long(i0 + g_items.size()));
  g_gpu.dead_at.assign(rects.begin() + long(i0 + g_items.size()), rects.end());
  auto put_item = [&](const Item& it, AtlasRect r) {
    for (int j = 0; j < it.h; ++j)
      std::memcpy(&g_gpu.px[size_t(r.y + j) * size + r.x], &it.rgb[size_t(j) * it.w], size_t(it.w) * 4);
  };
  for (size_t k = 0; k < g_items.size(); ++k) put_item(g_items[k], g_gpu.item_at[k]);
  for (size_t k = 0; k < g_dead.size(); ++k) put_item(g_prev_items[g_dead[k]], g_gpu.dead_at[k]);
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
  for (size_t k = 0; k < g_items.size(); ++k) pad(g_gpu.item_at[k], g_items[k].w, g_items[k].h);
  for (size_t k = 0; k < g_dead.size(); ++k) pad(g_gpu.dead_at[k], g_prev_items[g_dead[k]].w, g_prev_items[g_dead[k]].h);
  SDL_UpdateTexture(g_gpu.tex, nullptr, g_gpu.px.data(), size * 4);
  g_gpu.built_frame = g_frames;
}

}  // namespace

void Draw(SDL_Renderer* ren, const SDL_FRect& dst, const SDL_FRect& view_in, float t) {
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
  // Wide screens (setting d2k_widescreen, the list builder patched): the picture carries on into the view around
  // the 4:3 dst with what the game supplies there.
  const SDL_FRect view = g_wide_on ? view_in : dst;
  const SDL_Rect clip{int(std::floor(view.x)), int(std::floor(view.y)), int(std::ceil(view.w)), int(std::ceil(view.h))};
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
      quad(view.x, dst.y + r * sy, view.x + view.w, dst.y + (r + 1) * sy, 0, 0, 0, 0,
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
  // Wide screens: a parallax layer is a row of strips (side by side, same row / height / depth) and the game's record
  // has a fixed number of them, which can end short of a wide window's edge. Its end strips carry on in mirrored
  // copies (a seamless join) as far as the view needs.
  std::vector<int> pad_side;   // per sprite: bit 0 pad leftwards, bit 1 rightwards
  float view_l = 0, view_r = 0;
  if (g_wide_on && (view.x < dst.x || view.x + view.w > dst.x + dst.w)) {
    view_l = float(g_x0) + (view.x - dst.x) / sx;
    view_r = float(g_x0) + (view.x + view.w - dst.x) / sx;
    const size_t n = g_cur.sprites.size();
    pad_side.assign(n, 0);
    auto strip = [](const Sprite& sp) {
      return sp.type == 0 && !(sp.flags & 2) && sp.w >= 64 && sp.h >= 16 &&
             sp.row_y.back() - sp.row_y.front() + 1 == sp.h;
    };
    auto span = [](const Sprite& sp, int* l, int* r) {
      *l = (sp.flags & 1) ? sp.x - sp.w + 1 : sp.x;
      *r = *l + sp.w;
    };
    for (size_t i = 0; i < n; ++i) {
      const Sprite& a = g_cur.sprites[i];
      if (!strip(a)) continue;
      int al, ar;
      span(a, &al, &ar);
      bool left = false, right = false;
      for (size_t k = 0; k < n; ++k) {
        const Sprite& b = g_cur.sprites[k];
        if (k == i || !strip(b) || b.row != a.row || b.h != a.h || b.depth != a.depth) continue;
        int bl, br;
        span(b, &bl, &br);
        if (std::abs(br - al) <= 1) left = true;
        if (std::abs(bl - ar) <= 1) right = true;
      }
      if (left != right) pad_side[i] = left ? 2 : 1;   // (a lone image is no layer: not padded)
    }
  }
  // Mirrored copies of sprite i's drawn pixels from its end towards the view's edge.
  auto draw_pads = [&](size_t i, const Sprite& sp, AtlasRect r, float ox, float oy) {
    if (i >= pad_side.size() || !pad_side[i]) return;
    const bool refl = sp.flags & 1;
    const float l = float(refl ? sp.x - sp.w + 1 : sp.x) + ox, w = float(sp.w);
    const float u_a = float(r.x + (refl ? 0 : sp.lead)) * inv, u_b = float(r.x + (refl ? 0 : sp.lead) + sp.w) * inv;
    for (int k = 1; k < 8; ++k) {
      const bool left = pad_side[i] & 1;
      const float x0 = left ? l - float(k) * w : l + float(k) * w;
      if (left ? x0 + w <= view_l : x0 >= view_r) break;
      // Screen column c of the sprite shows atlas column c (unreflected) or w-1-c (reflected); odd copies mirror it.
      const bool flip = refl != (k % 2 == 1);
      int j0 = 0;
      while (j0 < sp.h) {
        int j1 = j0 + 1;
        while (j1 < sp.h && sp.row_y[j1] == sp.row_y[j1 - 1] + 1) ++j1;
        quad(wx(x0), wy(float(sp.row_y[j0]) + oy), wx(x0 + w), wy(float(sp.row_y[j1 - 1] + 1) + oy), flip ? u_b : u_a,
             float(r.y + j0) * inv, flip ? u_a : u_b, float(r.y + j1) * inv, white);
        j0 = j1;
      }
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
    if (int(i) == g_layer && g_gpu.item_at.size() == g_items.size()) {
      // The layer's items: blended like sprites (beams by their ends); vanished ones for the first half.
      // New items fade in and vanished ones out over the first half of the update (premultiplied: all four).
      auto item_quad = [&](const Item& it, AtlasRect r, float x0, float x1, float y0, float f = 1.f) {
        quad(wx(x0), wy(y0), wx(x1), wy(y0 + float(it.h)), float(r.x) * inv, float(r.y) * inv,
             float(r.x + it.w) * inv, float(r.y + it.h) * inv, SDL_FColor{f, f, f, f});
      };
      for (size_t k = 0; k < g_items.size(); ++k) {
        const Item& it = g_items[k];
        if (it.beam && it.prev >= 0) {
          const float x0 = float(it.bx0) + a * float(it.x - it.bx0), x1 = float(it.bx1) + a * float(it.x + it.w - it.bx1);
          item_quad(it, g_gpu.item_at[k], x0, x1, float(it.by) + a * float(it.y - it.by));
        } else {
          const float mx = it.moving ? -(1.f - a) * it.vx : 0.f, my = it.moving ? -(1.f - a) * it.vy : 0.f;
          item_quad(it, g_gpu.item_at[k], float(it.x) + mx, float(it.x + it.w) + mx, float(it.y) + my,
                    it.moving ? 1.f : std::min(1.f, 2.f * a));
        }
      }
      if (a < 0.5f)
        for (size_t k = 0; k < g_dead.size() && k < g_gpu.dead_at.size(); ++k) {
          const Item& it = g_prev_items[g_dead[k]];
          const float mx = a * it.vx, my = a * it.vy;
          item_quad(it, g_gpu.dead_at[k], float(it.x) + mx, float(it.x + it.w) + mx, float(it.y) + my, 1.f - 2.f * a);
        }
      continue;
    }
    draw_rows(sp, g_gpu.at[i], ox, oy);
    draw_pads(i, sp, g_gpu.at[i], ox, oy);
  }
  flush();
  SDL_SetRenderClipRect(ren, nullptr);
}

std::string Stats() {
  std::string s = "layer items " + std::to_string(g_items_matched) + " of " + std::to_string(g_items_total) +
                  " matched; matched " + std::to_string(g_matched_data) + " by image, " + std::to_string(g_matched_near) +
                  " by size, " + std::to_string(g_unmatched) + " not; ";
  for (auto& [k, v] : g_unhandled) s += k + " x" + std::to_string(v) + "; ";
  return s;
}

}  // namespace d2k
