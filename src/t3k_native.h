// t3k_native.h - Tempest 3000 native renderer (see PLAN.md "Native renderer research"): redraws the pictures the game
// draws in software on its MPEs from their display lists (nuon::Capture), at the window's resolution.
// Each picture becomes an ordered list of drawing operations, one per display list entry. Entry types done:
//   00710001 vector shapes (module 806A3F50): the shape streams, the bit-exact segment transform (as
//            tools/t3k_vecmodel.py) and the line look (tools/t3k_linefit.py: a linear tent 1.94 game pixels wide each
//            side, opacity 0.00888 x the colours' 4th byte, blended toward the line colour);
//   00000300 fill (806A3108): rectangle w0 (x, y) / w1 (w, h) in colour w2;
//   00300004 zoom feedback (806A5280; tools/t3k_zoomfit.py): the previous picture sampled at
//            centre + R(-angle) (p - centre) / zoom, x the fade (w7 >> 16 as 2.14);
//   0011000B web surface (806AD2B0; tools/t3k_webfit.py: mean error 1.4 grey levels): per lane a quad (records at
//            $800BACF0: 4 x (position y hi / x lo, signed half pixels about (180, 120); u hi / v lo, 1/512 texel))
//            textured with the 16 x 16 texture at w0 (bilinear, wrapping; v scrolled by MPE0's job word / 2), blended
//            out = T + (dest - T) v / 32;
//   0010000D glowing number (806AF1E8; tools/t3k_textfit.py: mean error 0.4, the title's 3000 + the score): the
//            decimal digits of the word at $800DC240 as 16 x 16 YCrCbZ glyphs (w10 + digit x 1 KB) scaled by w2 / w3 at
//            w0 (centred if w14 = 1); per pixel the glyph's Cr / Cb displace a lookup into the 16 x 16 texture at w11
//            (a quadratic mapping from the 20-word block at w12), blended toward it by glyph Z x w6. Drawn as one
//            premultiplied RGBA image per digit, rebuilt per picture;
//   00310309 particles (806A8990 mode 3, 806A8B68; tools/t3k_partfit.py): 4-word records at w12 projected as the point
//            modules (k = 0.75 x 2^44 / (max(depth, $400000) + $800000), camera at $800DAD80), each a soft disc
//            (806A9124): dest + (colour - dest) x I x (1 - d^2 / R^2), R = size / 2 (size <= 30 px);
//   00310209 dot images (806A8990 mode 2, 806A8DB4; tools/t3k_dotfit.py: the emulator's dot calls to 0.003 px): an image
//            (w8 columns / rows, YCrCbZ at w12, or one per copy from a list) as discs of size w9, intensity Z x w6,
//            scaled / rotated / projected, repeated w13 & $7F times deeper by w5;
//   0031020E disc shapes (806AFA80 mode 2, 806AFC52; tools/t3k_flowerfit.py: the emulator's disc calls to 0.001 px):
//            a template at w10 (palette + packed records) as rings of discs, rotated / tilted / projected at w0;
//   00310010 disc grid (806A6E40 mode 0; tools/t3k_gridfit.py: the calls exactly): a 32 x 32 bitmask of 3D grid points
//            (quadratic steps), each drawn with the 0031020E template;
//   00500005 ring (806A7DC0; tools/t3k_ringfit.py: 0.5 a channel): an ellipse band about w0 (radii from w4 x w1 hi,
//            band from w1 lo), strength peaking at w5 >> 2, coloured from the 16 x 8 texture at w12; drawn as a
//            premultiplied image (the glyph path);
//   00310008 point rings (806A5FB0 mode 0, 806A6486; tools/t3k_ringptfit.py: the calls exactly): arcs of discs at
//            depths stepping down by w10, w11 copies each, colours along a 2-texel gradient (warp, level select);
//   x000000FE (any type ending in FE): a disabled entry, nothing drawn;
//   00100200 textured plane (806A3108 mode 2, 806A3794; tools/t3k_planefit.py: 0.7 a channel): the rectangle w0 / w1
//            filled with the 16 x 16 texture at w9 at quadratic (u, v) (bonus / warp backgrounds); a GPU mesh;
//   00310108 (806A5FB0 mode 1) draws nothing unless w4 bit 0 is set: then the jump burst (96 LFSR particles, as
//   00310309's discs; tools/t3k_jumpburstfit.py);
//   000000FF end of list; 000000FE / 003102FE nothing drawn.
// A picture holding any other type is not "complete": the emulated picture is shown instead.
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <vector>

#include "nuon_host.h"

namespace t3knative {

// One line segment in the game's 360 x 240 screen space; colour A at (x0, y0) -> B at (x1, y1), RGBA 0..1 (alpha =
// the opacity on the line's centre).
struct Seg {
  float x0, y0, x1, y1;
  SDL_FColor c0, c1;
  float r = 1.94f;   // game pixels from the centre line to zero coverage: the width byte (w5 low) - 0.06
};

// One drawn vector entry: which segments it made, and what identifies it for interpolation.
struct Item {
  uint32_t shape;        // w7: the shape stream
  float x, y;            // w0: screen position
  uint32_t first, count; // its segments in Picture::segs
};

// The web surface's quads and texture (one per picture).
struct Web {
  struct Vertex { float x, y, u, v; };     // game pixels; texels
  std::vector<Vertex> quads;               // 4 per lane
  uint32_t texture[256] = {};              // 16 x 16 YCrCbZ
};

// One glowing-number digit: the colour it blends toward and how strongly, as an image over its cell.
struct Glyph {
  float x = 0, y = 0, w = 0, h = 0;        // cell (game pixels)
  int pw = 0, ph = 0;                      // image size
  std::vector<uint32_t> rgba;              // premultiplied ARGB8888
};

// One soft disc (particle): centre and radius in game pixels, colour, peak strength.
struct Blob {
  float x = 0, y = 0, r = 0;
  SDL_FColor colour{};   // a = the peak strength I
};

// A textured plane: a grid of vertices (game pixels, texels) over a rectangle, and its 16 x 16 texture.
struct Plane {
  int cols = 0, rows = 0;                  // grid cells
  std::vector<Web::Vertex> grid;           // (cols + 1) x (rows + 1)
  uint32_t texture[256] = {};
};

struct Op {
  enum Kind { kFill, kZoom, kSegs, kWeb, kText, kBlobs, kPlane, kRipple } kind;
  // kRipple: x / y = the displacement's base / slope along x, w / h = along y (16.16 units / 65536), colour, fade
  float x = 0, y = 0, w = 0, h = 0;          // fill rectangle (game pixels)
  SDL_FColor colour{};                         // fill colour
  float cx = 0, cy = 0, zx = 1, zy = 1;        // zoom: centre (game pixels), zoom
  float angle = 0, fade = 1;                   // zoom: rotation (radians), intensity factor
  uint32_t first = 0, count = 0;               // segs: the range in Picture::segs
  uint32_t type = 0;                           // the display list entry type that made it
};

// A picture as drawing operations in list order.
struct Picture {
  uint32_t flip = 0;
  std::vector<Seg> segs;
  std::vector<Item> items;
  std::vector<Op> ops;
  std::vector<Web> webs;          // kWeb ops: Op::first indexes this
  std::vector<Glyph> glyphs;      // kText ops: Op::first / count index this
  std::vector<Blob> blobs;        // kBlobs ops: Op::first / count index this
  std::vector<Plane> planes;      // kPlane ops: Op::first indexes this
  bool complete = true;           // every entry type is one we draw
  uint32_t first_unknown = 0;     // (if not) the first type we don't draw
  uint32_t unknown_entry[16] = {};  // (if not) that entry's words
};
using Vectors = Picture;          // (the vector layer: Picture::segs)

// A YCrCb(A) frame buffer word -> XRGB8888 as Nuance shows it (BT.601, video range).
uint32_t YccToXrgb(uint32_t ycc);

// Build a captured picture.
void BuildPicture(const nuon::Capture& cap, Picture* out);
// Build the picture the renderers are about to draw, from the emulator's memory (the draw gate, step 3: the core asks
// at the first drawing fetch; mpe = the fetching MPE, whose job word the web scroll reads). flip = the flip that will
// show it. False if the memory isn't reachable.
bool BuildPictureLive(int mpe, uint32_t flip, Picture* out);
inline void BuildVectors(const nuon::Capture& cap, Picture* out) { BuildPicture(cap, out); }

// The segments between two consecutive pictures (t = 0: prev, 1: cur), in cur's order (so cur's ops still apply).
// Vector entries of cur are matched to prev's by shape, segment count and nearest position (each used once);
// unmatched ones, or moves of more than max_move game pixels, are drawn as in cur.
void Interpolate(const Picture& prev, const Picture& cur, float t, std::vector<Seg>* out, float max_move = 30.f);
// A whole in-between picture: cur with its segments (Interpolate) and web surface blended toward prev by 1 - t.
void InterpolatePicture(const Picture& prev, const Picture& cur, float t, Picture* out);

// Draw segments into dst (the window rectangle the 360 x 240 picture fills). rgb: onto the window (picture colours
// converted from YCrCb when the YCrCb pipeline is on); false: into a native picture (colours as they are).
void DrawVectors(SDL_Renderer* ren, const std::vector<Seg>& segs, const SDL_FRect& dst, bool rgb = true);

// The GPU renderer (SDL "gpu", DXIL shaders) for windows that show native pictures; the default renderer if not.
SDL_Renderer* CreateRenderer(SDL_Window* window);
// The YCrCb pipeline on ren (call once, before building pictures): the native pictures are kept in YCrCb like the
// hardware (its fades and clipping), converted to RGB for the window. False = not available (no GPU renderer /
// T3K_NATIVE_RGB set): the RGB path.
bool InitPipeline(SDL_Renderer* ren);
// Sharp vectors: vector items up to max_size game pixels across (text, claw, enemies, HUD) drawn as a thin core
// (core x the line's width) plus a glow at the original width (x glow); sharp = false: the original look.
void SetLineStyle(bool sharp, float core, float glow, float max_size);
// Clears the current target to black (YCrCb black in the pipeline).
void ClearBlack(SDL_Renderer* ren);

// Whole native pictures, with the feedback the game builds them on kept in our own textures (window resolution).
class Renderer {
 public:
  ~Renderer();
  // Draw pic (its segments: segs, e.g. interpolated) into dst. step = how much of one game step the zoom feedback
  // advances (1 = a whole picture, as the original; less for in-between frames: zoom^step, angle x step, fade^step).
  // view (wide screens): the window area to fill, around dst - the feedback, full-screen fills and planes carry on
  // past the 4:3 edges, and whatever the game places out there shows. nullptr = dst.
  // step_ends: this draw completes a game picture's feedback step (the zoom fade rounds then, as the hardware does
  // once per picture; in between it stays exact on a float feedback texture).
  void Draw(SDL_Renderer* ren, const Picture& pic, const std::vector<Seg>& segs, const SDL_FRect& dst, float step,
            const SDL_FRect* view = nullptr, bool step_ends = true);
  // Show the last drawn picture again (nothing new to draw).
  void Redraw(SDL_Renderer* ren, const SDL_FRect& dst, const SDL_FRect* view = nullptr);
  // The previous picture was emulated: make it what the next native picture's feedback builds on (in dst; black
  // around it).
  void Seed(SDL_Renderer* ren, SDL_Texture* picture, const SDL_FRect& dst, const SDL_FRect* view = nullptr);

 private:
  void Resize(SDL_Renderer* ren, const SDL_FRect& dst);
  SDL_Texture* tex_[2] = {nullptr, nullptr};   // [cur_]: the last drawn picture; the other: the next one
  SDL_Texture* web_tex_ = nullptr;             // the web surface's 16 x 16 texture
  std::vector<SDL_Texture*> glyph_tex_;        // kText images (uploaded once per picture)
  uint32_t glyph_flip_ = 0xFFFFFFFFu;
  SDL_Texture* disc_tex_ = nullptr;            // kBlobs: alpha = 1 - r^2 over the unit disc
  int cur_ = 0, w_ = 0, h_ = 0;
  bool float_tex_ = false;                     // the feedback textures are 16-bit float (else 8-bit)
};

}  // namespace t3knative
