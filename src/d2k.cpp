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
    mix(sp.addr); mix(uint64_t(uint32_t(sp.x)) << 32 | uint32_t(sp.row)); mix(sp.data);
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

void Compose(const Scene& s, std::vector<uint16_t>& lbuf, std::vector<uint8_t>& written, int lw, int rows) {
  lbuf.assign(size_t(lw) * rows, 0);
  if (s.vmode & 0x80)
    for (int r = 0; r < rows && r < int(s.row_bg.size()); ++r)
      std::fill(lbuf.begin() + size_t(r) * lw, lbuf.begin() + size_t(r + 1) * lw, s.row_bg[r]);
  written.assign(lbuf.size(), 0);
  for (const Sprite& sp : s.sprites) {
    for (int j = 0; j < sp.h; ++j) {
      const int ry = sp.row_y[j];
      if (ry < 0 || ry >= rows) continue;
      for (int o = 0; o < sp.w; ++o) {
        if (!sp.opaque[size_t(j) * sp.w + o]) continue;
        const int lx = (sp.flags & 1) ? sp.x - o : sp.x + o;
        if (lx < 0 || lx >= lw) continue;
        const size_t i = size_t(ry) * lw + lx;
        const uint16_t v = sp.raw[size_t(j) * sp.w + o];
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
  BuildScene(g_cur);
  ++g_frames;
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

void Draw(SDL_Renderer*, const SDL_FRect&, float) {}

std::string Stats() {
  std::string s;
  for (auto& [k, v] : g_unhandled) s += k + " x" + std::to_string(v) + "; ";
  return s;
}

}  // namespace d2k
