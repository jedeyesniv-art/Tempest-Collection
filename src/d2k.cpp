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
uint8_t GPUReadByte(uint32_t offset, uint32_t who);
extern uint32_t gpu_pc, gpu_reg_bank_0[32], gpu_reg_bank_1[32];}

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
  int hs = 0, vs = 0;            // scaled objects: HSCALE, VSCALE (3.5: drawn pixels per source pixel)
  int skip = 0, src_w = 0, src_h = 0, pitch = 1, index = 0;   // the source: first pixel, pixels a row, rows (HEIGHT)
  // GPU only: columns left of the first drawn one that the game clipped off this update but showed the update
  // before (a tile scrolling out at the left edge); drawn while the motion is between the two.
  int lead = 0;
  std::vector<uint32_t> lead_rgb;   // lead * h, premultiplied XRGB (0 = see-through)
  int tail = 0;                     // unreflected only: columns drawn after the last (wide screens: the ground)
  std::vector<uint32_t> tail_rgb;   // tail * h, as lead_rgb
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
  *zero = v == 0;   // (TRANS: the core tests the pixel's own bits, not its CLUT entry - Classic's 2-bpp buffers
                    // have a non-zero colour at index 0, which drew as a big black square over the playfield)
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
    if (type == 1) sp.hs = int(p2 & 0xFF), sp.vs = int((p2 >> 8) & 0xFF);
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
struct Motion { float dx = 0, dy = 0; bool matched = false; int clip_px = 0; int step = 0, prev = -1; };   // step / prev: log
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
  // DATA moved by `lines` lines + px pixels, or by lines + 1 lines and px - a line's pixels: of the readings the
  // sizes allow, the smaller motion. Clipping changes the drawn size, so the same size with DATA moved = another
  // animation frame stored next to it (no reading). (Choosing first and checking after lost narrow images moving
  // more than half their line: identical DATA read as "a line down, a line's pixels back" -> rejected, and Classic's
  // 16 px landers took a neighbour's place instead: the jumping enemies.)
  const int64_t px = inl * 8 / bpp, line_px = pitch * 8 / bpp;
  const float sx = (c.flags & 1) ? -1.f : 1.f;
  auto allowed = [&](int64_t l, int64_t x) { return !((l != 0 && c.h == p.h) || (x != 0 && c.w == p.w)); };
  const bool ok0 = allowed(lines, px), ok1 = allowed(lines + 1, px - line_px);
  if (!ok0 && !ok1) return false;
  const float dx0 = float(c.x - sx * float(px) - p.x), dy0 = float((c.row - lines) - p.row);
  const float dx1 = float(c.x - sx * float(px - line_px) - p.x), dy1 = float((c.row - lines - 1) - p.row);
  int64_t use_lines = lines, use_px = px;
  if (ok0 && (!ok1 || std::fabs(dx0) + std::fabs(dy0) <= std::fabs(dx1) + std::fabs(dy1))) *dx = dx0, *dy = dy0;
  else *dx = dx1, *dy = dy1, use_lines = lines + 1, use_px = px - line_px;
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
  // 1. The same image nearby. Scored by the distance plus half the miss from where the earlier sprite was heading (its
  // own last motion), best pairs first: Classic's landers fly in tight pairs at 9 px a frame, and taken by plain
  // distance in list order each would grab the other's place (the jumping enemies). The distance stays the main term:
  // scored by the prediction alone, one wrong motion confirmed itself (2000's repeating ground strips went +58).
  auto miss = [](const Geo& p, float dx, float dy) {
    return std::fabs(dx) + std::fabs(dy) + 0.5f * (std::fabs(dx - p.mdx) + std::fabs(dy - p.mdy));
  };
  struct Pair { float cost, dx, dy; int clip; size_t i, j; };
  std::vector<Pair> pairs;
  for (size_t i = 0; i < cur.sprites.size(); ++i) {
    const Geo c = GeoOf(cur.sprites[i]);
    for (size_t j = 0; j < g_prev_upd.size(); ++j) {
      float dx, dy;
      int clip = 0;
      if (!SameImage(g_prev_upd[j], c, &dx, &dy, &clip) || std::fabs(dx) > kMax || std::fabs(dy) > kMax) continue;
      pairs.push_back({miss(g_prev_upd[j], dx, dy), dx, dy, clip, i, j});
    }
  }
  std::stable_sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.cost < b.cost; });
  std::vector<bool> later(cur.sprites.size(), false);   // left to step 2
  for (const Pair& pr : pairs) {
    if (g_motion[pr.i].matched || later[pr.i] || taken[pr.j]) continue;
    // Not when a sprite of about its size fits clearly better (itself in another animation frame: the landers flip
    // between two images, and the same image farther off is a neighbour) - step 2 takes those.
    const Geo c = GeoOf(cur.sprites[pr.i]);
    bool nearer = false;
    for (size_t j = 0; j < g_prev_upd.size() && !nearer; ++j) {
      const Geo& p = g_prev_upd[j];
      if (taken[j] || j == pr.j || p.depth != c.depth || (p.flags & 1) != (c.flags & 1) || std::abs(p.w - c.w) > 8 ||
          std::abs(p.h - c.h) > 4)
        continue;
      // (plain distances: with the prediction in it a wrong motion kept itself - 2000's ground pattern shifts 6 px
      // while its images rotate, and once read as +58 px it stayed there)
      nearer = std::fabs(float(c.x - p.x)) + std::fabs(float(c.row - p.row)) + 10.f < std::fabs(pr.dx) + std::fabs(pr.dy);
    }
    if (nearer) {
      later[pr.i] = true;
      continue;
    }
    taken[pr.j] = true;
    g_motion[pr.i] = {pr.dx, pr.dy, true, pr.clip, 1, int(pr.j)};
    ++g_matched_data;
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
      if (taken[j] || p.depth != c.depth || (p.flags & 1) != (c.flags & 1) || std::abs(p.w - c.w) > 8 ||
          std::abs(p.h - c.h) > 4)
        continue;
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
      g_motion[i] = {bdx, bdy, true, 0, 2, int(bj)};
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
      g_motion[i].step = 3, g_motion[i].prev = -1;
      break;
    }
    if (!g_motion[i].matched) ++g_unmatched;
  }
  // 4. A row of strips (a parallax layer: same row, height and depth, 32+ px wide) moves as one: the motion most of
  // its same-image matches agree on overrules its members matched by size or by a sibling. (2000's foreground rocks
  // scroll ~58 px an update; step 2 took another layer's -6 for the left strips and step 3 copied it on, so those
  // slid the other way.)
  for (size_t i = 0; i < cur.sprites.size(); ++i) {
    const Sprite& a = cur.sprites[i];
    if (a.w < 32 || g_motion[i].step == 1 || !g_motion[i].matched) continue;
    std::map<std::pair<float, float>, int> votes;
    for (size_t k = 0; k < cur.sprites.size(); ++k) {
      const Sprite& b = cur.sprites[k];
      if (g_motion[k].step == 1 && b.row == a.row && b.h == a.h && b.depth == a.depth && b.w >= 32)
        ++votes[{g_motion[k].dx, g_motion[k].dy}];
    }
    int best = 1;   // (at least two same-image strips must agree)
    for (auto& [m, n] : votes)
      if (n > best) best = n, g_motion[i].dx = m.first, g_motion[i].dy = m.second, g_motion[i].step = 4;
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
  // Scanner blips (SmoothRadar): the last positions along the item's chain, drawn at their average, blended from the
  // update before's average (sx_prev).
  int hn = 0;
  float hx[4] = {}, hy[4] = {};
  bool radar = false;
  float sx = 0, sy = 0, sx_prev = 0, sy_prev = 0;
  std::vector<uint32_t> rgb;               // w * h premultiplied XRGB (0 = see-through)
};
std::vector<Item> g_items, g_prev_items;
std::vector<size_t> g_dead;                // items of the update before with no successor
int g_layer = -1;                          // the layer's index in g_cur.sprites (-1: none / not cut)
int g_items_matched = 0, g_items_total = 0;
float g_layer_cover = 0;                   // share of the layer's pixels its items cover (this update)
bool g_wide_on = false;                    // wide screens on (setting d2k_widescreen, the list builder patched)

// The scanner (2000 / Plus) is drawn into the playfield layer: its blips step a whole scanner pixel every few updates
// (one pixel is a long way in the world), so blended update by update they go hold - hold - slide. Within the
// scanner's rows each blip is drawn at the average of its last 4 positions instead (1 px in 3 updates becomes a
// steady third of a pixel each update, about 2 updates late) and blended between those averages.
constexpr int kRadarRow0 = 26, kRadarRow1 = 61;   // line-buffer rows of the scanner (its bar: row 30, 32 high)
bool InRadar(const Item& it) { return !it.beam && it.y >= kRadarRow0 && it.y + it.h - 1 <= kRadarRow1; }
// A blip moves at most a few scanner pixels an update (2000 at full thrust: ~3). 2000's humanoid blips (2 x 2) sit in
// one row (54) ~15-40 px apart, and a star layer's displacement (a prediction) paired them with a neighbour: they slid
// across between updates (owner 2026-11-21: "move quickly or swap positions"). Blips (3+ pixels) in the scanner's rows
// only pair within this; single pixels there may be stars passing behind it.
bool BlipMove(const Item& c, const Item& p) { return std::abs(c.x - p.x) <= 4 && std::abs(c.y - p.y) <= 2; }
bool BlipOnly(const Item& c) { return InRadar(c) && c.n >= 3; }
void SmoothRadar(std::vector<Item>& cur, const std::vector<Item>& prev) {
  for (Item& c : cur) {
    c.radar = InRadar(c);
    // (A star crossing the scanner's rows moves further than a blip: drawn as a star, not averaged.)
    if (c.radar && c.prev >= 0 && size_t(c.prev) < prev.size() && !BlipMove(c, prev[size_t(c.prev)])) c.radar = false;
    if (!c.radar) continue;
    const Item* p = c.prev >= 0 && size_t(c.prev) < prev.size() && prev[size_t(c.prev)].radar ? &prev[size_t(c.prev)]
                                                                                               : nullptr;
    c.hn = 0;
    if (p)
      for (int k = std::max(0, p->hn - 3); k < p->hn; ++k) c.hx[c.hn] = p->hx[k], c.hy[c.hn] = p->hy[k], ++c.hn;
    c.hx[c.hn] = float(c.x), c.hy[c.hn] = float(c.y), ++c.hn;
    c.sx = c.sy = 0;
    for (int k = 0; k < c.hn; ++k) c.sx += c.hx[k], c.sy += c.hy[k];
    c.sx /= float(c.hn), c.sy /= float(c.hn);
    c.sx_prev = p ? p->sx : c.sx, c.sy_prev = p ? p->sy : c.sy;
  }
}

bool IsLayer(const Sprite& sp) {
  return sp.type == 0 && sp.depth == 3 && sp.w >= 300 && !(sp.flags & 3) && sp.h >= 200 &&
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
  // A screen full of particles (2000's level end: ~6500 items, against a few hundred in play) is drawn where the game
  // puts it: the burst fit and the nearest searches grow with the square of the count (seconds an update there - the
  // owner's level-2 end ran at a few frames a second and the window stopped responding).
  constexpr size_t kMaxItems = 2000;
  if (cur.size() > kMaxItems || prev.size() > kMaxItems) {
    g_dead.clear();
    g_items_total += int(cur.size());
    return;
  }
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
  // Single pixels in a dense field only follow a motion many pairs share (a star layer: ~80 stars each in Plus):
  // Plus's dust (one pixel coloured by its row, 2 px down and far sideways a frame) has no identity, and paired by
  // any looser prediction it slid across between frames. Unmatched, it is drawn where the game puts it.
  // (Counted by position alone: the stars change colour between frames. Random pairs spread ~1-2 per offset; a star
  // layer's ~80 stars make a peak.)
  // (Dense: over 150 items of 1-2 px - Plus's sky has ~370; 2000's explosions push its item count past 150, but as
  // 2 x 2 particles.)
  const bool dense = std::count_if(cur.begin(), cur.end(), [](const Item& c) { return !c.beam && c.n <= 2; }) > 150;
  std::vector<std::pair<float, float>> strong;
  if (dense) {
    std::map<std::pair<int, int>, int> h1;
    for (const Item& c : cur)
      if (!c.beam && c.n == 1)
        for (const Item& p : prev)
          if (!p.beam && p.n == 1 && std::abs(c.x - p.x) <= kMaxX && std::abs(c.y - p.y) <= kMaxY) ++h1[{c.x - p.x, c.y - p.y}];
    for (auto& [d, n] : h1)
      if (n >= 20) strong.push_back({float(d.first), float(d.second)});
  }
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
      if (!done[i] && !cur[i].beam && cur[i].n >= 2 && !BlipOnly(cur[i]))   // (not stars, not scanner blips)
        groups[cur[i].mask].first.push_back(int(i));
    for (size_t j = 0; j < prev.size(); ++j)
      if (!taken[j] && !prev[j].beam && !BlipOnly(prev[j]) && groups.count(prev[j].mask))
        groups[prev[j].mask].second.push_back(int(j));
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
            if (taken[j] || p.beam || p.n < 2 || BlipOnly(p) || std::hypot(mx - best_b.pcx, my - best_b.pcy) > best_b.r / best_b.k + 4.f)
              continue;
            p.vx = best_b.ccx + best_b.k * (mx - best_b.pcx) - mx;
            p.vy = best_b.ccy + best_b.k * (my - best_b.pcy) - my;
          }
          for (size_t i = 0; i < cur.size(); ++i) {
            Item& c = cur[i];
            const float mx = float(c.x) + float(c.w - 2) * 0.5f, my = float(c.y) + float(c.h - 2) * 0.5f;
            if (done[i] || c.beam || c.n < 2 || BlipOnly(c) || std::hypot(mx - best_b.ccx, my - best_b.ccy) > best_b.r + 4.f) continue;
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
    const bool lone = dense && c.n == 1;
    for (size_t j = 0; j < prev.size(); ++j) {
      const Item& p = prev[j];
      if (p.beam || std::abs(p.n - c.n) > std::max(2, c.n / 3) || (BlipOnly(c) && !BlipMove(c, p))) continue;
      const int dx = c.x - p.x, dy = c.y - p.y;
      if (std::abs(dx) > kMaxX || std::abs(dy) > kMaxY) continue;
      float err = lone ? 1e9f : std::fabs(float(dx) - p.vx) + std::fabs(float(dy) - p.vy);
      for (auto& q : lone ? strong : preds)
        err = std::min(err, std::fabs(float(dx) - q.first) + std::fabs(float(dy) - q.second));
      if (err > (lone ? 1.f : 3.f)) continue;
      // (Ties - two blinking blips 4 px apart, each fitting a 4 px prediction - go to the shorter move: no swap.)
      // (Scanner blips blink colour every update: compared by outline, or a neighbour of the old colour 1-4 px away
      // took a still blip's partner.)
      const bool same = BlipOnly(c) ? p.mask == c.mask : p.shape == c.shape;
      pairs.push_back({err + (same ? 0.f : 1.5f) + 0.01f * float(std::abs(dx) + std::abs(dy)), int(i), int(j)});
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
      if (taken[j] || p.beam || std::abs(p.n - cur[i].n) > std::max(2, cur[i].n / 3) ||
          (BlipOnly(cur[i]) && !BlipMove(cur[i], p)))
        continue;
      const float d = std::hypot(float(cur[i].x - p.x), float(cur[i].y - p.y));
      if (d < bd) bd = d, best = int(j);
    }
    return best;
  };
  // Not single stars (1-2 px) in a dense field (Plus: ~370 items, stars ~13 px apart moving up to 13 px a frame):
  // there the nearest is about random, and a wrong pair slides across between frames.
  for (size_t i = 0; i < cur.size(); ++i) {
    if (done[i] || cur[i].beam || (dense && cur[i].n <= 2)) continue;
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

// ---- wide screens: what scrolled out of the playfield layer -------------------------------------------------------
// Plus's hills (the ground: one item as wide as the layer) and its stars live in the 384 px layer, which ends short of
// a 16:9 window's edges. The ground's scroll is measured each update (the shift that best lines its pixels up with the
// history's) and a history canvas wider than the layer by kEdgeM columns each side is shifted by it and refreshed from
// the layer: past the layer's trailing edge the window shows the ground that scrolled out (the leading edge has never
// been seen: nothing there), coloured from the live ground (below). Stars that leave across an edge carry on at their
// last motion. The ground and these
// pieces blend with the measured scroll (the item matcher took the ground as still or misread its height changes).
constexpr int kEdgeM = 96;
struct EdgeHist {
  int lx = 0, ly = 0, lw = 0, lh = 0;   // the layer's place (line buffer) and size
  std::vector<uint32_t> px;             // (lw + 2 kEdgeM) x lh; column c = line-buffer x lx - kEdgeM + c; 0 = nothing
  std::vector<int> age;                 // per column: updates since it was last in the layer
  bool valid = false;
} g_hist;
std::vector<Item> g_edge;               // drawn past the layer's edges: the history's two sides, stars carrying on

void EdgeHistory(const Sprite& layer, std::vector<Item>& cur, const std::vector<Item>& prev) {
  std::vector<Item> stars;   // last update's carried-on stars (the ground pieces are rebuilt)
  for (Item& e : g_edge)
    if (e.n <= 4) stars.push_back(std::move(e));
  g_edge.clear();
  const int lx = layer.x, ly = layer.row_y.front(), lw = layer.w, lh = layer.h, W = lw + 2 * kEdgeM;
  int gi = -1;
  for (size_t i = 0; i < cur.size(); ++i)
    if (!cur[i].beam && cur[i].w >= lw - 2 && cur[i].n >= 1000 && cur[i].n * 10 < cur[i].w * cur[i].h * 9 &&
        cur[i].h * 2 < lh && (gi < 0 || cur[i].n > cur[size_t(gi)].n))   // (not a full-screen fill: 2000's flashes)
      gi = int(i);
  if (!g_wide_on || gi < 0) {
    g_hist.valid = false;
    return;
  }
  Item& g = cur[size_t(gi)];
  if (!g_hist.valid || g_hist.lx != lx || g_hist.ly != ly || g_hist.lw != lw || g_hist.lh != lh) {
    g_hist = EdgeHist{lx, ly, lw, lh, std::vector<uint32_t>(size_t(W) * lh, 0u), std::vector<int>(size_t(W), 0), false};
  }
  // The ground in layer coordinates.
  std::vector<uint32_t> gp(size_t(lw) * lh, 0u);
  const int gx = g.x - lx, gy = g.y - ly;
  int r0 = lh, r1 = -1;
  for (int j = 0; j < g.h; ++j)
    for (int o = 0; o < g.w; ++o) {
      const uint32_t c = g.rgb[size_t(j) * g.w + o];
      const int x = gx + o, y = gy + j;
      if (!c || x < 0 || x >= lw || y < 0 || y >= lh) continue;
      gp[size_t(y) * lw + x] = c;
      r0 = std::min(r0, y), r1 = std::max(r1, y);
    }
  // The scroll: the shift with the fewest disagreements over the columns the history saw last update - by the outline
  // (opaque or not) only: the texture is an animated surface, redrawn differently each frame (no shift, no palette
  // cycle matches it).
  int dx = 0;
  bool ok = false;
  if (g_hist.valid) {
    constexpr int kMax = 48;
    double score[2 * kMax + 1], best = 1e9;
    int bmm = 0, buni = 0;
    for (int d = -kMax; d <= kMax; ++d) {
      int uni = 0, mm = 0;
      for (int y = r0; y <= r1; ++y)
        for (int x = std::max(0, d); x < std::min(lw, lw + d); ++x) {
          const bool a = gp[size_t(y) * lw + x], b = g_hist.px[size_t(y) * W + x - d + kEdgeM];
          uni += a || b, mm += a != b;
        }
      const double s = score[d + kMax] = uni ? double(mm) / uni : 1.0;
      if (s < best) best = s, dx = d, bmm = mm, buni = uni;
    }
    double other = 1e9;   // the best shift 3+ away: a flat or repeating outline fits several
    for (int d = -kMax; d <= kMax; ++d)
      if (std::abs(d - dx) >= 3) other = std::min(other, score[d + kMax]);
    ok = best < 0.03 && other > best + 0.02;
    static const bool log = std::getenv("D2K_EDGE_LOG") != nullptr;
    if (log)
      std::fprintf(stderr, "edge %d: ground dx %d (%d of %d pixels differ, next %.3f)%s\n", g_frames, dx, bmm, buni,
                   other, ok ? "" : " - lost");
  }
  // Not measured (an explosion over the hills; every ~100 frames on the Plus route the hills also jump in a way no
  // shift fits, and the next update matches the new picture): start the history again. The sides are empty for that
  // update and refill as the hills scroll.
  if (!ok) dx = 0, std::fill(g_hist.px.begin(), g_hist.px.end(), 0u);
  // Shift the history and refresh the layer's columns from the ground now.
  if (dx) {
    std::vector<uint32_t> s(g_hist.px.size(), 0u);
    for (int y = 0; y < lh; ++y)
      for (int c = std::max(0, dx); c < std::min(W, W + dx); ++c) s[size_t(y) * W + c] = g_hist.px[size_t(y) * W + c - dx];
    g_hist.px.swap(s);
    std::vector<int> a(size_t(W), 0);
    for (int c = std::max(0, dx); c < std::min(W, W + dx); ++c) a[size_t(c)] = g_hist.age[size_t(c - dx)];
    g_hist.age.swap(a);
  }
  for (int c = 0; c < W; ++c) g_hist.age[size_t(c)] = c >= kEdgeM && c < kEdgeM + lw ? 0 : g_hist.age[size_t(c)] + 1;
  for (int y = 0; y < lh; ++y)
    std::memcpy(&g_hist.px[size_t(y) * W + kEdgeM], &gp[size_t(y) * lw], size_t(lw) * 4);
  g_hist.valid = true;
  if (ok) g.moving = true, g.vx = float(dx), g.vy = 0;
  // The two sides as items (cropped to what they hold). Their shape is the history's; columns that left the layer up
  // to 3 updates ago keep its colours (flying, the trailing strip is that fresh), older ones (slow, still) take the
  // live ground's mirrored about the layer's edge at the same depth below the outline, so the surface keeps animating
  // (the history's own colour where the mirrored column is too shallow).
  std::vector<int> top(size_t(W), -1), ltop(size_t(lw), -1);   // first opaque rows: history columns, the ground now
  for (int c = 0; c < W; ++c)
    for (int y = 0; y < lh; ++y)
      if (g_hist.px[size_t(y) * W + c]) { top[size_t(c)] = y; break; }
  for (int x = 0; x < lw; ++x)
    for (int y = 0; y < lh; ++y)
      if (gp[size_t(y) * lw + x]) { ltop[size_t(x)] = y; break; }
  auto live = [&](int c, int y) -> uint32_t {
    const int x = c - kEdgeM;                             // layer column
    const int m = x < 0 ? -1 - x : 2 * lw - 1 - x;        // mirrored into the layer
    const int t = m >= 0 && m < lw ? ltop[size_t(m)] : -1;
    if (t < 0 || top[size_t(c)] < 0) return 0;
    const int yy = t + (y - top[size_t(c)]);
    return yy < lh ? gp[size_t(yy) * lw + m] : 0u;
  };
  auto side = [&](int c0, int c1) {
    int x0 = c1, x1 = c0 - 1, y0 = lh, y1 = -1;
    for (int y = 0; y < lh; ++y)
      for (int c = c0; c < c1; ++c)
        if (g_hist.px[size_t(y) * W + c]) x0 = std::min(x0, c), x1 = std::max(x1, c), y0 = std::min(y0, y), y1 = std::max(y1, y);
    if (x1 < x0) return;
    Item it;
    it.x = lx - kEdgeM + x0, it.y = ly + y0, it.w = x1 - x0 + 1, it.h = y1 - y0 + 1, it.n = it.w * it.h;
    it.moving = ok, it.vx = float(dx);
    it.rgb.resize(size_t(it.w) * it.h);
    for (int y = 0; y < it.h; ++y)
      for (int o = 0; o < it.w; ++o) {
        const uint32_t h = g_hist.px[size_t(y0 + y) * W + x0 + o];
        const uint32_t l = h && g_hist.age[size_t(x0 + o)] > 3 ? live(x0 + o, y0 + y) : 0u;
        it.rgb[size_t(y) * it.w + o] = l ? l : h;
      }
    g_edge.push_back(std::move(it));
  };
  side(0, kEdgeM);
  side(kEdgeM + lw, W);
  // Stars: carried-on ones move on (dropped once back over the layer or out of the margin); stars of the update before
  // that vanished on their way out across an edge join them.
  auto outside = [&](const Item& it) {
    return it.x + it.w <= lx || it.x >= lx + lw;
  };
  auto in_margin = [&](const Item& it) { return it.x + it.w > lx - kEdgeM && it.x < lx + lw + kEdgeM; };
  for (Item& s : stars) {
    s.x += int(s.vx), s.y += int(s.vy);
    if (outside(s) && in_margin(s)) g_edge.push_back(std::move(s));
  }
  for (size_t j : g_dead) {
    const Item& p = prev[j];
    if (p.beam || p.n > 4 || !p.moving || p.vx == 0) continue;
    Item s = p;
    s.x += int(s.vx), s.y += int(s.vy);
    const bool left_out = s.vx < 0 && s.x + s.w <= lx && p.x < lx + 8, right_out = s.vx > 0 && s.x >= lx + lw && p.x + p.w > lx + lw - 8;
    if ((left_out || right_out) && in_margin(s)) g_edge.push_back(std::move(s));
  }
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

// In play: the longs at $194E10 / $194E14 read $00140000 / $17000000 from a game's start to the menus after it (deaths,
// wave-complete and game-over screens included) in every mode and in the attract demo, 0 on the title and menus (also
// set for a moment at boot, before the logo). Found by a RAM search over 2000 / Classic / Plus / idle routes
// (logs/d2k/inplay/search3.py; the block $194E04..$194E1F is a record that exists only in play). The first marker,
// $18CD98 = $00760084, was a text position: in Plus play it changes with the messages ($00DD0028, $00B40028 ...) -
// the owner's buttons went to the menu layout after the first death.
bool InPlay() {
  return Be32(jagMemSpace + 0x194E10) == 0x00140000u && Be32(jagMemSpace + 0x194E14) == 0x17000000u;
}

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
struct Patch { uint32_t addr; int n; uint8_t orig[88], wide[88]; };
constexpr uint32_t kLayers = 0x0B2B50, kSprites = 0x0A5938, kRun = 0xF031C0;
constexpr Patch kWide[] = {
    {kLayers + (0xF03444 - kRun), 2, {0x18, 0x3A}, {0x01, 0xDA}},   // subq #1,r26 -> add r14,r26: strips up to 300 left
    {kLayers + (0xF0349A - kRun), 2, {0x64, 0x5B}, {0x67, 0xFB}},   // shrq #2,r27 -> shrq #31: no left clip
    {kLayers + (0xF034C2 - kRun), 2, {0x01, 0x80}, {0x02, 0x00}},   // movei #$180 -> #$200: right cull at 512
    {kSprites + (0xF034C8 - kRun), 4, {0x00, 0x00, 0x00, 0x00}, {0xFF, 0xA0, 0xFF, 0xFF}},   // movei #0 -> #-96: left
    {kSprites + (0xF034D0 - kRun), 2, {0x01, 0x80}, {0x02, 0x00}},   // movei #$180 -> #$200: right
    {kSprites + (0xF03726 - kRun), 2, {0xD0, 0x14}, {0xD0, 0x00}},   // jump pl -> jump t: no left clip
    // The layer builder's first part (per layer: x = -(camera & mask), the strips' map entries from camera >> shift)
    // starts one tile further left: x - (mask + 1), map index - 1 (a mask-0 layer, fixed, stays: bit 0 cleared).
    // Room from a redundant 'or', the jr / jump delay slots and a shorter r13 set-up; the loop moves up 2 bytes.
    // With the counts + 2 (below) a layer covers a tile past each edge: no strip pops in at a wide view's edges.
    {kLayers + (0xF03218 - kRun), 88,
     {0x98, 0x08, 0x32, 0x2A, 0x00, 0xF0, 0x98, 0x09, 0x32, 0x82, 0x00, 0xF0, 0x98, 0x0D, 0x32, 0x86, 0x00, 0xF0, 0xA4, 0x54, 0x08, 0x82, 0xA4, 0x55, 0x08, 0x82, 0x7C, 0x14, 0xD1, 0xA2, 0xE4, 0x00, 0x88, 0x6C, 0xA6, 0xB6, 0x2A, 0xD6, 0x7C, 0x16, 0xD4, 0x61, 0xE4, 0x00, 0xD1, 0x00, 0xE4, 0x00, 0x65, 0x16, 0x42, 0xCC, 0x64, 0xCC, 0x8A, 0x8B, 0x08, 0x95, 0x9E, 0xA7, 0x0B, 0x0B, 0x08, 0x35, 0x9E, 0xA4, 0x08, 0x35, 0x9E, 0xB6, 0x08, 0x35, 0x9E, 0xA5, 0x08, 0x35, 0xA6, 0xA6, 0x89, 0x80, 0x24, 0x80, 0x5E, 0xCC, 0x20, 0x00, 0xBA, 0x80},
     {0x98, 0x08, 0x32, 0x28, 0x00, 0xF0, 0x98, 0x09, 0x32, 0x82, 0x00, 0xF0, 0x89, 0x2D, 0x08, 0x8D, 0xA4, 0x54, 0x08, 0x82, 0xA4, 0x55, 0x08, 0x82, 0x7C, 0x14, 0xD1, 0xA2, 0xE4, 0x00, 0x88, 0x6C, 0xA6, 0xB6, 0x7C, 0x16, 0xD4, 0x41, 0x65, 0x16, 0xD1, 0x00, 0x42, 0xCC, 0x64, 0xCC, 0x8A, 0x8B, 0x08, 0x95, 0x9E, 0xA7, 0x0B, 0x0B, 0x08, 0x35, 0x9E, 0xA4, 0x08, 0x35, 0x9E, 0xB6, 0x08, 0x35, 0x9E, 0xA5, 0x08, 0x35, 0xA6, 0xA6, 0x89, 0x80, 0x24, 0x80, 0x08, 0x24, 0x3C, 0x04, 0x00, 0x80, 0x10, 0x8C, 0x5E, 0xCC, 0x20, 0x00, 0xBA, 0x80}},
};

// The layers' strip counts (descriptor + 4; room for 8 ids, see cap): + 2 with the patch above, for each layer
// with a mask (tiles). The layer list ((record, descriptor) pairs up to a 0 record) is the level's: the 68K at $A4EEE
// puts the long at $B811C + 16 * word $B822E into $B8118 just before it runs the builder ($B8118 holds another list
// the rest of the frame - not to be touched). Remembered per descriptor so a level's own rewrite is the new original.
std::map<uint32_t, std::pair<uint8_t, uint8_t>> g_layer_counts;   // descriptor -> original, ours
void LayerCounts(bool on) {
  auto be32 = [](uint32_t a) { return Be32(jagMemSpace + (a & 0x1FFFFC)); };
  static const uint8_t kPick[] = {0x41, 0xF9, 0x00, 0x0B, 0x81, 0x1C, 0x30, 0x39, 0x00, 0x0B, 0x82, 0x2E,   // lea, move.w
                                  0xE9, 0x48, 0x23, 0xF0, 0x00, 0x00, 0x00, 0x0B, 0x81, 0x18};          // lsl, move.l
  if (std::memcmp(jagMemSpace + 0x0A4EEE, kPick, sizeof kPick) != 0) return;
  const uint32_t level = uint32_t(jagMemSpace[0x0B822E] << 8 | jagMemSpace[0x0B822F]);
  if (level > 64) return;
  const uint32_t list = be32(0x0B811C + 16 * level);
  if (list < 0x1000 || list >= 0x200000) return;
  for (int i = 0; i < 16; ++i) {
    const uint32_t rec = be32(list + 8u * uint32_t(i)), desc = be32(list + 8u * uint32_t(i) + 4);
    if (rec == 0 || desc < 0x1000 || desc >= 0x1FFFF0) break;
    uint8_t& count = jagMemSpace[desc + 4];
    const uint8_t mask = jagMemSpace[desc + 5];
    // Strip ids live at record + 24 up to the descriptor: 8 when it follows the 32-byte record (every level seen),
    // else the 7 the game uses at most. (2026-11-21: with 7, a 7-strip layer of 64 px tiles (level 6's city) got the
    // tile-left shift above and no extra strips, so its right end fell short of even the 4:3 picture's edge and
    // buildings popped in there.)
    const int cap = desc == rec + 32 ? 8 : 7;
    auto it = g_layer_counts.find(desc);
    if (it == g_layer_counts.end() || (count != it->second.first && count != it->second.second))
      it = g_layer_counts.insert_or_assign(desc, std::pair<uint8_t, uint8_t>(
                                                     count, uint8_t(mask ? std::min(count + 2, cap) : count))).first;
    count = on ? it->second.second : it->second.first;
    // The record's own strip count (word + 10, bit 15 a flag), which the second part loops over: the same + 2.
    static std::map<uint32_t, std::pair<uint16_t, uint16_t>> rec_counts;
    uint8_t* rw = jagMemSpace + rec + 10;
    const uint16_t now = uint16_t(rw[0] << 8 | rw[1]);
    auto rt = rec_counts.find(rec);
    if (rt == rec_counts.end() || (now != rt->second.first && now != rt->second.second)) {
      const int n = now & 0x7FFF;
      rt = rec_counts.insert_or_assign(rec, std::pair<uint16_t, uint16_t>(
                                                now, uint16_t((now & 0x8000) | (mask && n ? std::min(n + 2, cap) : n)))).first;
    }
    const uint16_t v = on ? rt->second.second : rt->second.first;
    rw[0] = uint8_t(v >> 8), rw[1] = uint8_t(v);
  }
}

// Classic / Plus enemies: the arcade-logic sprite overlay (GPU code the 68K copies from $0AF988 to $F031C0 each frame)
// keeps an object while r20 = ((world x - camera + offset) & $7FF) + $40 < $1C0, i.e. screen x -64..383, and marks it
// on screen (object flags bit 8, which the game reads: other overlays only act on marked objects, e.g. the smart bomb).
// Wide: x -128..511 (as the 2000 sprites), by adding $40 twice before the mask and moving the subtraction into the two
// branch delay slots (r9 = $40 is also the slot stride, so it stays). The on-screen band beyond a 16:9 view is then
// ~90 / 115 px, about the original's ~80 / 40 px beyond 4:3. (2026-11-21, owner: Plus enemies popped in short of the
// 16:9 edges.) Code at $F032B2:
//   orig: and r22,r20 / add r9,r20 / subq #5,r10 / loadw / bclr #8 / storew / addq #2,r10 / btst #5,r13 /
//         jr ne,+7 / nop / movei #$1C0,r1 / cmp r1,r20 / jump pl,(r0) / nop / sub r9,r20
//   wide: add r9,r20 / add r9,r20 / and r22,r20 / (same 7) / jr ne,+6 / sub r9,r20 / movei #$240,r1 / cmp r1,r20 /
//         jump pl,(r0) / sub r9,r20   (the scaled path, bit 5, branches to the last sub: x = M - 128 either way)
constexpr Patch kWideArcade = {
    0x0AF988 + (0xF032B2 - 0xF031C0), 34,
    {0x26, 0xD4, 0x01, 0x34, 0x18, 0xAA, 0xA1, 0x4D, 0x3D, 0x0D, 0xB9, 0x4D, 0x08, 0x4A, 0x34, 0xAD, 0xD4, 0xE1,
     0xE4, 0x00, 0x98, 0x01, 0x01, 0xC0, 0x00, 0x00, 0x78, 0x34, 0xD0, 0x14, 0xE4, 0x00, 0x11, 0x34},
    {0x01, 0x34, 0x01, 0x34, 0x26, 0xD4, 0x18, 0xAA, 0xA1, 0x4D, 0x3D, 0x0D, 0xB9, 0x4D, 0x08, 0x4A, 0x34, 0xAD,
     0xD4, 0xC1, 0x11, 0x34, 0x98, 0x01, 0x02, 0x40, 0x00, 0x00, 0x78, 0x34, 0xD0, 0x14, 0x11, 0x34}};

void ApplyWide(bool on) {
  for (const Patch& p : kWide) {   // only over the original (or our) bytes: another game / version stays untouched
    uint8_t* m = jagMemSpace + p.addr;
    if (std::memcmp(m, p.orig, size_t(p.n)) != 0 && std::memcmp(m, p.wide, size_t(p.n)) != 0) return;
  }
  for (const Patch& p : kWide) std::memcpy(jagMemSpace + p.addr, on ? p.wide : p.orig, size_t(p.n));
  LayerCounts(on);
  g_wide_on = on;
  // Classic / Plus enemies (their own group: a mismatch here never holds back the patches above).
  const Patch& e = kWideArcade;
  uint8_t* m = jagMemSpace + e.addr;
  if (std::memcmp(m, e.orig, size_t(e.n)) == 0 || std::memcmp(m, e.wide, size_t(e.n)) == 0)
    std::memcpy(m, on ? e.wide : e.orig, size_t(e.n));
}

// Test dumps: <file>.gpu next to a RAM dump = GPU RAM $F03000..$F03FFF, then the GPU pc and both register banks
// (u32 little-endian).
void DumpGpu(const std::string& file) {
  std::ofstream g(file, std::ios::binary);
  for (uint32_t a = 0xF03000; a < 0xF04000; ++a) {
    const char b = char(GPUReadByte(a, 0));
    g.write(&b, 1);
  }
  g.write(reinterpret_cast<const char*>(&gpu_pc), 4);
  g.write(reinterpret_cast<const char*>(gpu_reg_bank_0), 128);
  g.write(reinterpret_cast<const char*>(gpu_reg_bank_1), 128);
}

// ---- wide screens: layer rows carried on from the level's tile map ----------------------------------------------
// A layer record holds at most 7 strips: 64 px tiles (2000 mode's cities, level 6) cover 448 px, a window that slides
// a tile at a time as the layer scrolls, so one edge of a 16:9 view fell up to ~45 px short in turn; the mirrored pads
// stood in there and the real building replaced them a tile later (2026-11-21, owner: "the city background shifts
// between entire buildings"). The layer builder (GPU overlay $0B2B50) makes strip i of a record from its descriptor:
// tile id map[(pos >> shift) + i & mapmask] (signed byte: 0 = empty, negative = reflected), the tile's {DATA long,
// height word, palette word} at record +16's table + 8 * |id|, the strips a record +8 word apart. So each row the
// game drew is matched against its map (the shown strips and gaps must fit one map offset, every candidate offset
// must agree) and the tiles past its ends are added as more strips of the scene, decoded from memory like captured
// ones: matching, smooth motion and the high-res art treat them like the game's. Anything that doesn't fit is left
// to the pads. The level's layer list as in LayerCounts.
void ExtendLayers(Scene& s) {
  static const uint8_t kPick[] = {0x41, 0xF9, 0x00, 0x0B, 0x81, 0x1C, 0x30, 0x39, 0x00, 0x0B, 0x82, 0x2E,
                                  0xE9, 0x48, 0x23, 0xF0, 0x00, 0x00, 0x00, 0x0B, 0x81, 0x18};
  if (std::memcmp(jagMemSpace + 0x0A4EEE, kPick, sizeof kPick) != 0) return;
  auto be32 = [](uint32_t a) { return Be32(jagMemSpace + (a & 0x1FFFFC)); };
  auto be16 = [](uint32_t a) { return int(jagMemSpace[a & 0x1FFFFE] << 8 | jagMemSpace[(a & 0x1FFFFE) + 1]); };
  auto s8 = [](uint32_t a) { return int(int8_t(jagMemSpace[a & 0x1FFFFF])); };
  const uint32_t level = uint32_t(be16(0x0B822E));
  if (level > 64) return;
  const uint32_t list = be32(0x0B811C + 16 * level);
  if (list < 0x1000 || list >= 0x200000) return;
  constexpr int kExt = 136;   // line-buffer px past the 4:3 picture's sides (21:9 needs ~122)
  const int view_l = g_x0 - kExt, view_r = g_x0 + 326 + kExt;
  static const int kBpp[5] = {1, 2, 4, 8, 16};
  for (int li = 0; li < 16; ++li) {
    const uint32_t rec = be32(list + 8u * uint32_t(li)), desc = be32(list + 8u * uint32_t(li) + 4);
    if (rec == 0 || rec >= 0x200000 || desc < 0x1000 || desc >= 0x1FFFF0) break;
    const uint32_t table = be32(rec + 16), map = be32(desc + 8);
    const int tw = be16(rec + 8), n = jagMemSpace[desc + 7] + 1;
    if (table < 0x1000 || table >= 0x200000 || map < 0x1000 || map >= 0x200000 || tw < 16 || tw > 128) continue;
    std::map<uint32_t, int> by_data;   // tile DATA -> id
    for (int k = 0; k < n; ++k)
      if (const int id = std::abs(s8(map + uint32_t(k)))) by_data[be32(table + 8u * uint32_t(id))] = id;
    // The record's strips in this scene: unscaled, unclipped, one tile wide, on one row.
    std::vector<size_t> mem;
    for (size_t i = 0; i < s.sprites.size(); ++i) {
      const Sprite& sp = s.sprites[i];
      if (sp.type == 0 && !(sp.flags & 2) && sp.w == tw && sp.skip == 0 && sp.w == sp.src_w && sp.depth <= 4 &&
          by_data.count(sp.data))
        mem.push_back(i);
    }
    if (mem.size() < 2) continue;
    const Sprite& t = s.sprites[mem[0]];
    auto left_of = [](const Sprite& sp) { return (sp.flags & 1) ? sp.x - sp.w + 1 : sp.x; };
    int l0 = INT32_MAX;
    bool ok = true;
    for (size_t i : mem) {
      const Sprite& sp = s.sprites[i];
      ok = ok && sp.row == t.row && sp.h == t.h && sp.depth == t.depth && sp.dwidth == t.dwidth &&
           sp.pitch == t.pitch && sp.row_y == t.row_y;
      l0 = std::min(l0, left_of(sp));
    }
    if (!ok) continue;
    std::map<int, int> shown;   // slot -> signed id
    for (size_t i : mem) {
      const Sprite& sp = s.sprites[i];
      const int d = left_of(sp) - l0;
      if (d % tw || shown.count(d / tw)) { ok = false; break; }
      shown[d / tw] = by_data[sp.data] * ((sp.flags & 1) ? -1 : 1);
    }
    const int kmax = shown.empty() ? 0 : shown.rbegin()->first;
    if (!ok || kmax > 16) continue;
    // Decoding from memory must give the captured pixels (rows DWIDTH phrases apart).
    const int bpp = kBpp[t.depth];
    auto decode = [&](Sprite& sp) {
      sp.raw.assign(size_t(sp.w) * size_t(sp.h), 0);
      sp.opaque.assign(sp.raw.size(), 0);
      for (int r = 0; r < sp.h; ++r)
        for (int c = 0; c < sp.w; ++c) {
          bool zero = false;
          const size_t o = size_t(r) * size_t(sp.w) + size_t(c);
          sp.raw[o] = Pixel(sp.data + uint32_t(r) * sp.dwidth * 8u, c, bpp, sp.pitch, sp.index, &zero);
          sp.opaque[o] = !((sp.flags & 4) && zero);
        }
    };
    {
      Sprite check = t;
      decode(check);
      if (check.raw != t.raw || check.opaque != t.opaque) continue;
      // (the palette: phrase 2's INDEX = the tile's word << 1; only used below 8 bits a pixel)
      if (bpp < 8 && ((be16(table + 8u * uint32_t(by_data[t.data]) + 6) << 1) & 0xFE) != t.index) continue;
    }
    // Map offsets that explain the shown slots (and their gaps).
    auto at = [&](int j, int k) { return s8(map + uint32_t(((j + k) % n + n) % n)); };
    std::vector<int> cand;
    for (int j = 0; j < n; ++j) {
      bool fit = true;
      for (int k = 0; k <= kmax && fit; ++k) {
        auto it = shown.find(k);
        fit = at(j, k) == (it == shown.end() ? 0 : it->second);
      }
      if (fit) cand.push_back(j);
    }
    if (cand.empty()) continue;
    std::vector<Sprite> add;
    for (int dir : {-1, 1}) {
      for (int step = 1; step <= 8; ++step) {
        const int k = dir < 0 ? -step : kmax + step;
        const int left = l0 + k * tw;
        if (dir < 0 ? left + tw <= view_l : left >= view_r) break;
        const int id = at(cand[0], k);
        bool agree = true;
        for (int j : cand) agree = agree && at(j, k) == id;
        if (!agree) break;
        if (id == 0) continue;
        const uint32_t e = table + 8u * uint32_t(std::abs(id));
        if (be16(e + 4) != be16(table + 8u * uint32_t(std::abs(shown.begin()->second)) + 4)) break;   // tile height
        Sprite sp = t;
        sp.addr = 0;
        sp.data = be32(e);
        sp.flags = (t.flags & ~1) | (id < 0 ? 1 : 0);
        sp.index = (be16(e + 6) << 1) & 0xFE;
        sp.x = id < 0 ? left + tw - 1 : left;
        sp.lead = 0, sp.tail = 0;
        sp.lead_rgb.clear(), sp.tail_rgb.clear(), sp.idx.clear();
        decode(sp);
        add.push_back(std::move(sp));
      }
    }
    if (add.empty()) continue;
    size_t last = 0;
    for (size_t i : mem) last = std::max(last, i);
    s.sprites.insert(s.sprites.begin() + std::ptrdiff_t(last + 1), std::make_move_iterator(add.begin()),
                     std::make_move_iterator(add.end()));
  }
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
  // d2k_widescreen: 0 off, 1 every mode, 2 Defender 2000 only (default; owner 2026-11-21: Classic and Plus at 4:3).
  // The mode being played is the byte at $18CD4B: 0 Classic, 1 Plus, 2 Defender 2000, 255 on the menus (RAM search
  // over the three modes' routes, logs/d2k/inplay/modesearch.py; $18CDDF / $0AD935 follow the mode menu's cursor).
  const int ws = int(settings::Num("d2k_widescreen", 2));
  const bool arcade = InPlay() && jagMemSpace[0x18CD4B] <= 1;
  ApplyWide(settings::Num("d2k_native", 1) != 0 && (ws == 1 || (ws == 2 && !arcade)));
  g_objs.clear();
  g_index.clear();
  for (auto& l : g_line) l = LineState{};
  for (auto& l : g_line_order) l.clear();
}

void AfterFrame() {
  if (!g_active) return;
  std::swap(g_prev_frame, g_cur);
  BuildScene(g_cur);
  if (g_wide_on && !std::getenv("D2K_NO_EXTEND")) ExtendLayers(g_cur);   // (D2K_NO_EXTEND=1: tests)
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
  // Test: D2K_RAMDUMP=F:file - main RAM (2 MB) after frame F; D2K_RAMDUMPS=F0:F1:step:prefix - every step frames
  // (<prefix>_<frame>.bin).
  if (std::getenv("D2K_INPLAY_LOG")) {   // test: in play / not changes
    static int last = -1;
    if (int(InPlay()) != last) last = int(InPlay()), std::fprintf(stderr, "inplay %d: %d\n", g_frames, last);
  }
  if (const char* rs = std::getenv("D2K_RAMDUMPS")) {
    char prefix[480] = {};
    int f0 = 0, f1 = -1, step = 1;
    if (std::sscanf(rs, "%d:%d:%d:%479s", &f0, &f1, &step, prefix) == 4 && step > 0 && g_frames >= f0 &&
        g_frames <= f1 && (g_frames - f0) % step == 0) {
      const std::string file = std::string(prefix) + "_" + std::to_string(g_frames) + ".bin";
      std::ofstream o(file, std::ios::binary);
      o.write(reinterpret_cast<const char*>(jagMemSpace), 0x200000);
      DumpGpu(file + ".gpu");
    }
  }
  if (const char* rd = std::getenv("D2K_RAMDUMP")) {   // test: D2K_RAMDUMP=F:file - main RAM (2 MB) after frame F
    char file[512] = {};
    int f = -1;
    if (std::sscanf(rd, "%d:%511s", &f, file) == 2 && f == g_frames) {
      std::ofstream o(file, std::ios::binary);
      o.write(reinterpret_cast<const char*>(jagMemSpace), 0x200000);
      DumpGpu(std::string(file) + ".gpu");
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
      g_edge.clear();
      g_hist.valid = false;
      g_layer_cover = 0;
    } else if (g_upd_frame == g_frames || g_layer < 0) {
      g_prev_items = std::move(g_items);
      CutItems(g_cur.sprites[size_t(layer)], g_cur.vmode, g_items);
      MatchItems(g_items, g_prev_items);
      SmoothRadar(g_items, g_prev_items);
      EdgeHistory(g_cur.sprites[size_t(layer)], g_items, g_prev_items);
      {
        const Sprite& ls = g_cur.sprites[size_t(layer)];
        long px = 0;
        for (const Item& it : g_items) px += it.n;
        g_layer_cover = float(px) / float(std::max(1, ls.w * ls.h));
      }
      if (std::getenv("D2K_ITEM_LOG")) {
        int m = 0, beams = 0;
        for (const Item& it : g_items) m += it.moving, beams += it.beam;
        std::fprintf(stderr, "items %d: %zu (%d beams), %d matched, %zu vanished, cover %.3f:", g_frames,
                     g_items.size(), beams, m, g_dead.size(), g_layer_cover);
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
  // Wide screens: a full-width 8-bit strip low on the screen (the ground: rows of colour ramps the game cycles, each
  // repeating with its own period, wider down the rows) carries on past both ends, each row repeated at its period
  // (found by the fewest mismatches; mirrored if none fits).
  if (g_wide_on)
    for (Sprite& sp : g_cur.sprites) {
      if (sp.type != 0 || sp.depth > 3 || sp.w < 300 || sp.h > 64 || (sp.flags & 3) || sp.lead || sp.x > 0 ||
          sp.x + sp.w < 384 || sp.row_y.back() - sp.row_y.front() + 1 != sp.h)
        continue;
      constexpr int kExt = 160;
      static std::map<uint32_t, std::vector<int>> periods;   // per strip (DATA): each row's period, kept while it fits
      std::vector<int>& per = periods[sp.data];
      per.resize(size_t(sp.h), 0);
      const int w = sp.w;
      auto px = [&](int j, int o) { return sp.raw[size_t(j) * w + o] | (sp.opaque[size_t(j) * w + o] ? 0 : 0x10000u); };
      auto misses = [&](int j, int pp, int limit) {
        int m = 0;
        for (int o = 0; o + pp < w && m <= limit; ++o) m += px(j, o) != px(j, o + pp);
        return m;
      };
      sp.lead = sp.tail = kExt;
      sp.lead_rgb.assign(size_t(kExt) * sp.h, 0);
      sp.tail_rgb.assign(size_t(kExt) * sp.h, 0);
      for (int j = 0; j < sp.h; ++j) {
        int& pp = per[size_t(j)];
        if (pp == 0 || misses(j, pp, (w - pp) / 5) * 5 > w - pp) {
          pp = 0;
          int best = w;
          for (int c = 4; c <= w / 2; ++c) {
            const int m = misses(j, c, best);
            if (m * 5 <= w - c && m < best) best = m, pp = c;
            if (m == 0) break;
          }
        }
        for (int k = 0; k < kExt; ++k) {
          int ol = k - kExt, orr = w + k;   // offsets left of 0 / right of the last
          if (pp) {
            ol += ((-ol + pp - 1) / pp) * pp;
            orr -= ((orr - w + pp) / pp) * pp;
          } else {
            ol = std::min(-ol - 1, w - 1);
            orr = std::max(2 * w - orr - 1, 0);
          }
          auto rgb = [&](int o) {
            const size_t i = size_t(j) * w + o;
            return sp.opaque[i] ? (ToRgb(sp.raw[i], g_cur.vmode) | 0xFF000000u) : 0u;
          };
          sp.lead_rgb[size_t(j) * kExt + k] = rgb(ol);
          sp.tail_rgb[size_t(j) * kExt + k] = rgb(orr);
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
      std::fprintf(stderr, " [%d,%d %dx%d %06X %s%+.0f,%+.0f L%d s%d#%d f%d]", sp.x, sp.row, sp.w, sp.h, sp.data,
                   g_motion[i].matched ? "" : "?", g_motion[i].dx, g_motion[i].dy, sp.lead, g_motion[i].step,
                   g_motion[i].prev, sp.flags);
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
  std::vector<AtlasRect> item_at, dead_at, edge_at;        // per layer item / vanished item
  std::vector<std::pair<AtlasRect, AtlasRect>> rmw_at;   // per RMW delta: pos, neg
  std::vector<RmwDelta> deltas;
  std::string filter;
  struct Src { SDL_Texture* tex = nullptr; float u0 = 0, v0 = 0, du = 0, dv = 0; };
  std::vector<Src> hi, hi_ghost;   // per sprite / ghost: its high-res art (tex null = none)
} g_gpu;

// ---- high-res art -------------------------------------------------------------------------------------------------
// A pack (tools/d2k_art_pack.py) in <exe dir>/d2k_art: pack.txt = each picture's place in memory (depth, DWIDTH, OP
// pitch, row / column of the grid pixel address // and % the line pitch in pixels, size), src/<n>.png the original,
// hi/<n>.png the replacement (any whole-number scale). A sprite (unscaled, not RMW, consecutive rows) is drawn from a
// replacement when its memory rectangle lies inside the picture and its pixels equal the original's there (the art
// at an address changes between levels; colour changes): else as before. Setting d2k_hires (default 1), F4.
struct HiPic {
  int depth = 0, dwidth = 0, pitch = 0, w = 0, h = 0;
  int64_t row = 0, col = 0;
  std::vector<uint32_t> src;   // ARGB, 0 = see-through
  SDL_Texture* tex = nullptr;
};
struct HiArt {
  bool tried = false;
  std::vector<HiPic> pics;
  std::map<uint64_t, std::vector<int>> by_place;   // depth / DWIDTH / pitch -> pictures
  int matched = 0, missed = 0;                       // sprites this frame (Stats)
} g_hi;

uint64_t HiPlace(int depth, int dwidth, int pitch) { return uint64_t(depth) << 40 | uint64_t(dwidth) << 8 | uint64_t(pitch); }

SDL_Surface* LoadArgb(const std::string& path) {
  SDL_Surface* s = SDL_LoadPNG(path.c_str());
  if (!s) return nullptr;
  SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
  SDL_DestroySurface(s);
  return c;
}

void HiLoad(SDL_Renderer* ren) {
  g_hi.tried = true;
  const char* base = SDL_GetBasePath();
  const std::string dir = std::string(base ? base : "") + "d2k_art/";
  std::ifstream f(dir + "pack.txt");
  if (!f) return;
  std::string line;
  int loaded = 0;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    int n = 0, depth = 0, dwidth = 0, pitch = 0, w = 0, h = 0, buffer = 0;
    long long row = 0, col = 0;
    if (std::sscanf(line.c_str(), "%d %d %d %d %lld %lld %d %d %d", &n, &depth, &dwidth, &pitch, &row, &col, &w, &h,
                    &buffer) != 9 || buffer)
      continue;
    SDL_Surface* src = LoadArgb(dir + "src/" + std::to_string(n) + ".png");
    SDL_Surface* hi = LoadArgb(dir + "hi/" + std::to_string(n) + ".png");
    if (src && hi && src->w == w && src->h == h && hi->w % w == 0 && hi->h % h == 0) {
      HiPic p;
      p.depth = depth, p.dwidth = dwidth, p.pitch = pitch, p.row = row, p.col = col, p.w = w, p.h = h;
      p.src.resize(size_t(w) * h);
      for (int j = 0; j < h; ++j) {
        const uint32_t* r = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(src->pixels) + size_t(j) * src->pitch);
        for (int i = 0; i < w; ++i) p.src[size_t(j) * w + i] = (r[i] >> 24) ? (r[i] | 0xFF000000u) : 0u;
      }
      for (int j = 0; j < hi->h; ++j) {   // premultiplied (the mask is 0 / 255)
        uint32_t* r = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(hi->pixels) + size_t(j) * hi->pitch);
        for (int i = 0; i < hi->w; ++i)
          {
            const uint32_t a = r[i] >> 24;
            auto mul = [a](uint32_t c) { return (c * a + 127) / 255; };
            r[i] = a << 24 | mul(r[i] >> 16 & 0xFF) << 16 | mul(r[i] >> 8 & 0xFF) << 8 | mul(r[i] & 0xFF);
          }
      }
      p.tex = SDL_CreateTextureFromSurface(ren, hi);
      if (p.tex) {
        SDL_SetTextureScaleMode(p.tex, SDL_SCALEMODE_LINEAR);
        g_hi.by_place[HiPlace(depth, dwidth, pitch)].push_back(int(g_hi.pics.size()));
        g_hi.pics.push_back(std::move(p));
        ++loaded;
      }
    }
    if (src) SDL_DestroySurface(src);
    if (hi) SDL_DestroySurface(hi);
  }
  std::fprintf(stderr, "d2k: high-res art: %d pictures from %s\n", loaded, dir.c_str());
}

// The replacement for a sprite as drawn (its atlas columns: [lead][drawn] unreflected, [drawn][lead] reflected).
// Scaled objects: the drawn pixels sample the source (32 / HSCALE source columns a drawn one, 32 / VSCALE rows a drawn
// row), so the match reads the source from memory (every 3rd pixel) and the texture steps by the scale.
Gpu::Src HiFor(const Sprite& sp, uint16_t vmode) {
  static const int kBpp[5] = {1, 2, 4, 8, 16};
  if (sp.type > 1 || (sp.flags & 2) || sp.depth > 4 || sp.h <= 0 || sp.w <= 0 ||
      sp.row_y.back() - sp.row_y.front() + 1 != sp.h)
    return {};
  if (sp.type == 1 && (sp.hs <= 0 || sp.vs <= 0)) return {};
  const auto it = g_hi.by_place.find(HiPlace(sp.depth, int(sp.dwidth), sp.pitch));
  if (it == g_hi.by_place.end()) return {};
  const int bpp = kBpp[sp.depth];
  const int64_t pitch_px = int64_t(sp.dwidth) * 64 / bpp;
  if (pitch_px <= 0) return {};
  const int64_t at = int64_t(sp.data) * 8 / bpp + sp.skip, row = at / pitch_px, col = at % pitch_px;
  const int64_t c0 = (sp.flags & 1) ? col : col - sp.lead;
  if (sp.tail) return {};
  const int wtot = sp.w + sp.lead;
  if (sp.type == 1) {
    const float sx = 32.f / float(sp.hs), sy = 32.f / float(sp.vs);
    const int sw = std::min(sp.src_w - sp.skip, int(std::ceil(float(sp.w) * sx))),
              sh = std::min(sp.src_h, int(std::ceil(float(sp.h) * sy)));
    if (sw <= 0 || sh <= 0) return {};
    for (int pi : it->second) {
      const HiPic& p = g_hi.pics[size_t(pi)];
      if (row < p.row || row + sh > p.row + p.h || col < p.col || col + sw > p.col + p.w) continue;
      int bad = 0, n = 0;
      for (int j = 0; j < sh && bad * 100 <= n + 100; j += 3)
        for (int k = 0; k < sw; k += 3) {
          bool zero = false;
          const uint16_t v = Pixel(sp.data + uint32_t(j) * sp.dwidth * 8u, sp.skip + k, bpp, sp.pitch, sp.index, &zero);
          const uint32_t want = ((sp.flags & 4) && zero) ? 0u : (ToRgb(v, vmode) | 0xFF000000u);
          ++n;
          bad += want != p.src[size_t(row - p.row + j) * p.w + size_t(col - p.col + k)];
        }
      if (bad * 100 > n) continue;
      return {p.tex, float(col - p.col) / float(p.w), float(row - p.row) / float(p.h), sx / float(p.w), sy / float(p.h)};
    }
    return {};
  }
  for (int pi : it->second) {
    const HiPic& p = g_hi.pics[size_t(pi)];
    if (row < p.row || row + sp.h > p.row + p.h || c0 < p.col || c0 + wtot > p.col + p.w) continue;
    int bad = 0;
    const int limit = sp.w * sp.h / 100;
    for (int j = 0; j < sp.h && bad <= limit; ++j) {
      const uint32_t* s = &p.src[size_t(row - p.row + j) * p.w + size_t(col - p.col)];
      for (int o = 0; o < sp.w; ++o) {
        const size_t k = size_t(j) * sp.w + o;
        const uint32_t want = sp.opaque[k] ? (ToRgb(sp.raw[k], vmode) | 0xFF000000u) : 0u;
        if (want != s[o] && ++bad > limit) break;
      }
    }
    if (bad > limit) continue;
    return {p.tex, float(c0 - p.col) / float(p.w), float(row - p.row) / float(p.h), 1.f / float(p.w), 1.f / float(p.h)};
  }
  return {};
}

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
  for (const Sprite& sp : g_cur.sprites) sizes.push_back({sp.lead + sp.w + sp.tail, sp.h});
  for (const RmwDelta& d : g_gpu.deltas) {
    const Sprite& sp = g_cur.sprites[d.sprite];
    sizes.push_back({sp.w, sp.h});
    sizes.push_back({sp.w, sp.h});
  }
  for (const Ghost& gh : g_ghosts) sizes.push_back({g_prev_scene.sprites[gh.prev].w, g_prev_scene.sprites[gh.prev].h});
  for (const Item& it : g_items) sizes.push_back({it.w, it.h});
  for (size_t d : g_dead) sizes.push_back({g_prev_items[d].w, g_prev_items[d].h});
  for (const Item& it : g_edge) sizes.push_back({it.w, it.h});
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
    for (int j = 0; j < sp.h; ++j)
      for (int k = 0; k < sp.tail; ++k)
        g_gpu.px[size_t(r.y + j) * size + r.x + sp.w + k] = sp.tail_rgb[size_t(j) * sp.tail + k];
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
  const size_t e0 = i0 + g_items.size() + g_dead.size();
  g_gpu.dead_at.assign(rects.begin() + long(i0 + g_items.size()), rects.begin() + long(e0));
  g_gpu.edge_at.assign(rects.begin() + long(e0), rects.end());
  auto put_item = [&](const Item& it, AtlasRect r) {
    for (int j = 0; j < it.h; ++j)
      std::memcpy(&g_gpu.px[size_t(r.y + j) * size + r.x], &it.rgb[size_t(j) * it.w], size_t(it.w) * 4);
  };
  for (size_t k = 0; k < g_items.size(); ++k) put_item(g_items[k], g_gpu.item_at[k]);
  for (size_t k = 0; k < g_dead.size(); ++k) put_item(g_prev_items[g_dead[k]], g_gpu.dead_at[k]);
  for (size_t k = 0; k < g_edge.size(); ++k) put_item(g_edge[k], g_gpu.edge_at[k]);
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
  for (size_t i = 0; i < n; ++i)
    pad(rects[i], g_cur.sprites[i].lead + g_cur.sprites[i].w + g_cur.sprites[i].tail, g_cur.sprites[i].h);
  for (size_t d = 0; d < g_gpu.deltas.size(); ++d) {
    const Sprite& sp = g_cur.sprites[g_gpu.deltas[d].sprite];
    pad(rects[n + 2 * d], sp.w, sp.h);
    pad(rects[n + 2 * d + 1], sp.w, sp.h);
  }
  for (size_t gi = 0; gi < g_ghosts.size(); ++gi)
    pad(rects[g0 + gi], g_prev_scene.sprites[g_ghosts[gi].prev].w, g_prev_scene.sprites[g_ghosts[gi].prev].h);
  for (size_t k = 0; k < g_items.size(); ++k) pad(g_gpu.item_at[k], g_items[k].w, g_items[k].h);
  for (size_t k = 0; k < g_dead.size(); ++k) pad(g_gpu.dead_at[k], g_prev_items[g_dead[k]].w, g_prev_items[g_dead[k]].h);
  for (size_t k = 0; k < g_edge.size(); ++k) pad(g_gpu.edge_at[k], g_edge[k].w, g_edge[k].h);
  SDL_UpdateTexture(g_gpu.tex, nullptr, g_gpu.px.data(), size * 4);
  g_gpu.built_frame = g_frames;
  // High-res art: which sprites (and ghosts) have a replacement this frame.
  g_gpu.hi.assign(n, {});
  g_gpu.hi_ghost.assign(g_ghosts.size(), {});
  g_hi.matched = g_hi.missed = 0;
  if (settings::Num("d2k_hires", 1) != 0) {
    if (!g_hi.tried) HiLoad(ren);
    if (!g_hi.pics.empty()) {
      for (size_t i = 0; i < n; ++i) {
        g_gpu.hi[i] = HiFor(g_cur.sprites[i], g_cur.vmode);
        if (int(i) != g_layer && !(g_cur.sprites[i].flags & 2)) ++(g_gpu.hi[i].tex ? g_hi.matched : g_hi.missed);
      }
      static const bool log = std::getenv("D2K_HI_LOG") != nullptr;   // test: the sprites without high-res art
      if (log && (g_upd_frame == g_frames || g_frames % 60 == 0)) {
        std::fprintf(stderr, "hires %d: %d matched, %d not:", g_frames, g_hi.matched, g_hi.missed);
        for (size_t i = 0; i < n; ++i) {
          const Sprite& sp = g_cur.sprites[i];
          if (int(i) != g_layer && !(sp.flags & 2))
            std::fprintf(stderr, " [%s%06X d%d dw%u t%d %dx%d L%d @%d,%d]", g_gpu.hi[i].tex ? "HI " : "", sp.data, sp.depth, sp.dwidth, sp.type, sp.w,
                         sp.h, sp.lead, sp.x, sp.row);
        }
        std::fprintf(stderr, "\n");
      }
      for (size_t gi = 0; gi < g_ghosts.size(); ++gi)
        g_gpu.hi_ghost[gi] = HiFor(g_prev_scene.sprites[g_ghosts[gi].prev], g_prev_scene.vmode);
    }
  }
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
  SDL_Texture* tex = g_gpu.tex;   // the batch's texture: the atlas or a high-res picture
  auto flush = [&]() {
    if (v.empty()) return;
    SDL_SetTextureBlendMode(tex, mode);
    SDL_RenderGeometry(ren, tex, v.data(), int(v.size()), idx.data(), int(idx.size()));
    v.clear();
    idx.clear();
  };
  auto use = [&](SDL_Texture* t) {
    if (t != tex) flush(), tex = t;
  };
  using Src = Gpu::Src;
  auto atlas = [&](AtlasRect r) { return Src{g_gpu.tex, float(r.x) * inv, float(r.y) * inv, inv, inv}; };
  // A sprite's rows (in runs of consecutive rows) from its pixels in a texture, shifted by (ox, oy) line-buffer pixels.
  auto draw_rows = [&](const Sprite& sp, Src r, float ox, float oy) {
    use(r.tex);
    int j0 = 0;
    while (j0 < sp.h) {
      int j1 = j0 + 1;
      while (j1 < sp.h && sp.row_y[j1] == sp.row_y[j1 - 1] + 1) ++j1;
      const float y0 = wy(float(sp.row_y[j0]) + oy), y1 = wy(float(sp.row_y[j1 - 1] + 1) + oy);
      float x0, x1, u0 = r.u0, u1 = r.u0 + float(sp.lead + sp.w + sp.tail) * r.du;
      if (sp.flags & 1) {   // reflected: pixel o at x - o
        x0 = wx(float(sp.x - sp.w - sp.lead + 1) + ox);
        x1 = wx(float(sp.x + 1) + ox);
        std::swap(u0, u1);
      } else {
        x0 = wx(float(sp.x - sp.lead) + ox);
        x1 = wx(float(sp.x + sp.w + sp.tail) + ox);
      }
      quad(x0, y0, x1, y1, u0, r.v0 + float(j0) * r.dv, u1, r.v0 + float(j1) * r.dv, white);
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
      return sp.type <= 1 && !(sp.flags & 2) && sp.w >= 64 && sp.h >= 16 &&
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
      bool left = false, right = false, more_left = false, more_right = false;
      for (size_t k = 0; k < n; ++k) {
        const Sprite& b = g_cur.sprites[k];
        if (k == i || !strip(b) || b.row != a.row || b.h != a.h || b.depth != a.depth) continue;
        int bl, br;
        span(b, &bl, &br);
        if (std::abs(br - al) <= 1) left = true;
        if (std::abs(bl - ar) <= 1) right = true;
        more_left = more_left || bl < al;
        more_right = more_right || bl > al;
      }
      // (a lone image is no layer: not padded; only the row's outermost strips pad outwards - one beside a gap in the
      // middle of the row (an empty tile) would have drawn copies across the gap and on to the edge)
      if (left != right && (left ? !more_right : !more_left)) pad_side[i] = left ? 2 : 1;
    }
  }
  // Mirrored copies of sprite i's drawn pixels from its end towards the view's edge.
  auto draw_pads = [&](size_t i, const Sprite& sp, Src r, float ox, float oy) {
    if (i >= pad_side.size() || !pad_side[i]) return;
    use(r.tex);
    const bool refl = sp.flags & 1;
    const float l = float(refl ? sp.x - sp.w + 1 : sp.x) + ox, w = float(sp.w);
    const float u_a = r.u0 + float(refl ? 0 : sp.lead) * r.du, u_b = u_a + float(sp.w) * r.du;
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
             r.v0 + float(j0) * r.dv, flip ? u_a : u_b, r.v0 + float(j1) * r.dv, white);
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
          draw_rows(g_prev_scene.sprites[g_ghosts[gi].prev],
                    gi < g_gpu.hi_ghost.size() && g_gpu.hi_ghost[gi].tex ? g_gpu.hi_ghost[gi] : atlas(g_gpu.ghost_at[gi]),
                    a * g_ghosts[gi].dx, a * g_ghosts[gi].dy);
    float ox = 0, oy = 0;
    if (i < g_motion.size() && g_motion[i].matched) {
      ox = -(1.f - a) * g_motion[i].dx;
      oy = -(1.f - a) * g_motion[i].dy;
    }
    if ((sp.flags & 2) && next_rmw < g_gpu.deltas.size() && g_gpu.deltas[next_rmw].sprite == i) {
      flush();
      mode = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
      draw_rows(sp, atlas(g_gpu.rmw_at[next_rmw].first), ox, oy);
      flush();
      mode = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
                                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
      draw_rows(sp, atlas(g_gpu.rmw_at[next_rmw].second), ox, oy);
      flush();
      mode = SDL_BLENDMODE_BLEND_PREMULTIPLIED;
      ++next_rmw;
      continue;
    }
    if (int(i) == g_layer && g_gpu.item_at.size() == g_items.size()) {
      use(g_gpu.tex);
      // The layer's items: blended like sprites (beams by their ends); vanished ones for the first half.
      // New items fade in and vanished ones out over the first half of the update (premultiplied: all four).
      auto item_quad = [&](const Item& it, AtlasRect r, float x0, float x1, float y0, float f = 1.f) {
        quad(wx(x0), wy(y0), wx(x1), wy(y0 + float(it.h)), float(r.x) * inv, float(r.y) * inv,
             float(r.x + it.w) * inv, float(r.y + it.h) * inv, SDL_FColor{f, f, f, f});
      };
      for (size_t k = 0; k < g_items.size(); ++k) {
        const Item& it = g_items[k];
        if (it.radar && interp) {   // (SmoothRadar)
          const float x = it.sx_prev + a * (it.sx - it.sx_prev), y = it.sy_prev + a * (it.sy - it.sy_prev);
          item_quad(it, g_gpu.item_at[k], x, x + float(it.w), y, it.moving || g_period == 1 ? 1.f : std::min(1.f, 2.f * a));
        } else if (it.beam && it.prev >= 0) {
          const float x0 = float(it.bx0) + a * float(it.x - it.bx0), x1 = float(it.bx1) + a * float(it.x + it.w - it.bx1);
          item_quad(it, g_gpu.item_at[k], x0, x1, float(it.by) + a * float(it.y - it.by));
        } else {
          const float mx = it.moving ? -(1.f - a) * it.vx : 0.f, my = it.moving ? -(1.f - a) * it.vy : 0.f;
          item_quad(it, g_gpu.item_at[k], float(it.x) + mx, float(it.x + it.w) + mx, float(it.y) + my,
                    it.moving || g_period == 1 ? 1.f : std::min(1.f, 2.f * a));
        }
      }
      // (Updates every frame (Plus, Classic): new items show at once and vanished ones go, as the game draws them - a
      // half-frame fade only flashed them at half brightness.)
      if (a < 0.5f && g_period > 1)
        for (size_t k = 0; k < g_dead.size() && k < g_gpu.dead_at.size(); ++k) {
          const Item& it = g_prev_items[g_dead[k]];
          const float mx = a * it.vx, my = a * it.vy;
          item_quad(it, g_gpu.dead_at[k], float(it.x) + mx, float(it.x + it.w) + mx, float(it.y) + my, 1.f - 2.f * a);
        }
      // Wide screens: what scrolled out past the layer's edges (EdgeHistory).
      for (size_t k = 0; k < g_edge.size() && k < g_gpu.edge_at.size(); ++k) {
        const Item& it = g_edge[k];
        const float mx = it.moving ? -(1.f - a) * it.vx : 0.f, my = it.moving ? -(1.f - a) * it.vy : 0.f;
        item_quad(it, g_gpu.edge_at[k], float(it.x) + mx, float(it.x + it.w) + mx, float(it.y) + my);
      }
      // Wide screens: a layer covered with particles (2000's level end: an explosion of particles, then black dots
      // until the screen is black - the game's fade between levels) carries on past its ends as its mirror image, or
      // the backdrop showed beside the 4:3 picture (2026-11-21, owner). Normal play covers a few percent (stars,
      // shots, explosions).
      constexpr float kCoverMirror = 0.12f;
      if (view_r > view_l && g_layer_cover > kCoverMirror) {
        const float le = float(sp.x), re = float(sp.x + sp.w);
        for (size_t k = 0; k < g_items.size(); ++k) {
          const Item& it = g_items[k];
          if (it.radar) continue;
          const float mx = it.moving ? -(1.f - a) * it.vx : 0.f, my = it.moving ? -(1.f - a) * it.vy : 0.f;
          const float x0 = float(it.x) + mx, x1 = x0 + float(it.w);
          const AtlasRect r = g_gpu.item_at[k];
          const float f = it.moving || g_period == 1 ? 1.f : std::min(1.f, 2.f * a);
          for (const float e : {le, re}) {
            const float m0 = 2.f * e - x1, m1 = 2.f * e - x0;   // mirrored about the edge (u reversed below)
            if (e == le ? m1 <= view_l : m0 >= view_r) continue;
            quad(wx(m0), wy(float(it.y) + my), wx(m1), wy(float(it.y + it.h) + my), float(r.x + it.w) * inv,
                 float(r.y) * inv, float(r.x) * inv, float(r.y + it.h) * inv, SDL_FColor{f, f, f, f});
          }
        }
      }
      continue;
    }
    const Src src = i < g_gpu.hi.size() && g_gpu.hi[i].tex ? g_gpu.hi[i] : atlas(g_gpu.at[i]);
    draw_rows(sp, src, ox, oy);
    draw_pads(i, sp, src, ox, oy);
  }
  flush();
  SDL_SetRenderClipRect(ren, nullptr);
}

std::string Stats() {
  std::string s = "layer items " + std::to_string(g_items_matched) + " of " + std::to_string(g_items_total) +
                  " matched; matched " + std::to_string(g_matched_data) + " by image, " + std::to_string(g_matched_near) +
                  " by size, " + std::to_string(g_unmatched) + " not; ";
  if (!g_hi.pics.empty())
    s += "high-res art: " + std::to_string(g_hi.matched) + " sprites, " + std::to_string(g_hi.missed) + " without; ";
  for (auto& [k, v] : g_unhandled) s += k + " x" + std::to_string(v) + "; ";
  return s;
}

}  // namespace d2k
