// t3k_native.h - Tempest 3000 native renderer (see PLAN.md "Native renderer research"): redraws the pictures the game
// draws in software on its MPEs from their display lists (nuon::Capture), at the window's resolution.
// Each picture becomes an ordered list of drawing operations, one per display list entry. Entry types done:
//   00710001 vector shapes (module 806A3F50): the shape streams, the bit-exact segment transform (as
//            tools/t3k_vecmodel.py) and the line look (tools/t3k_linefit.py: a linear tent 1.94 game pixels wide each
//            side, opacity 0.00888 x the colours' 4th byte, blended toward the line colour);
//   00000300 fill (806A3108): rectangle w0 (x, y) / w1 (w, h) in colour w2;
//   00300004 zoom feedback (806A5280; tools/t3k_zoomfit.py): the previous picture sampled at
//            centre + R(-angle) (p - centre) / zoom, x the fade (w7 >> 16 as 2.14);
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
};

// One drawn vector entry: which segments it made, and what identifies it for interpolation.
struct Item {
  uint32_t shape;        // w7: the shape stream
  float x, y;            // w0: screen position
  uint32_t first, count; // its segments in Picture::segs
};

struct Op {
  enum Kind { kFill, kZoom, kSegs } kind;
  float x = 0, y = 0, w = 0, h = 0;          // fill rectangle (game pixels)
  SDL_FColor colour{};                         // fill colour
  float cx = 0, cy = 0, zx = 1, zy = 1;        // zoom: centre (game pixels), zoom
  float angle = 0, fade = 1;                   // zoom: rotation (radians), intensity factor
  uint32_t first = 0, count = 0;               // segs: the range in Picture::segs
};

// A picture as drawing operations in list order.
struct Picture {
  uint32_t flip = 0;
  std::vector<Seg> segs;
  std::vector<Item> items;
  std::vector<Op> ops;
  bool complete = true;           // every entry type is one we draw
  uint32_t first_unknown = 0;     // (if not) the first type we don't draw
};
using Vectors = Picture;          // (the vector layer: Picture::segs)

// Build a captured picture.
void BuildPicture(const nuon::Capture& cap, Picture* out);
inline void BuildVectors(const nuon::Capture& cap, Picture* out) { BuildPicture(cap, out); }

// The segments between two consecutive pictures (t = 0: prev, 1: cur), in cur's order (so cur's ops still apply).
// Vector entries of cur are matched to prev's by shape, segment count and nearest position (each used once);
// unmatched ones, or moves of more than max_move game pixels, are drawn as in cur.
void Interpolate(const Picture& prev, const Picture& cur, float t, std::vector<Seg>* out, float max_move = 30.f);

// Draw segments into dst (the window rectangle the 360 x 240 picture fills).
void DrawVectors(SDL_Renderer* ren, const std::vector<Seg>& segs, const SDL_FRect& dst);

// Whole native pictures, with the feedback the game builds them on kept in our own textures (window resolution).
class Renderer {
 public:
  ~Renderer();
  // Draw pic (its segments: segs, e.g. interpolated) into dst. step = how much of one game step the zoom feedback
  // advances (1 = a whole picture, as the original; less for in-between frames: zoom^step, angle x step, fade^step).
  void Draw(SDL_Renderer* ren, const Picture& pic, const std::vector<Seg>& segs, const SDL_FRect& dst, float step);
  // Show the last drawn picture again (nothing new to draw).
  void Redraw(SDL_Renderer* ren, const SDL_FRect& dst);
  // The previous picture was emulated: make it what the next native picture's feedback builds on.
  void Seed(SDL_Renderer* ren, SDL_Texture* picture, const SDL_FRect& dst);

 private:
  void Resize(SDL_Renderer* ren, const SDL_FRect& dst);
  SDL_Texture* tex_[2] = {nullptr, nullptr};   // [cur_]: the last drawn picture; the other: the next one
  int cur_ = 0, w_ = 0, h_ = 0;
};

}  // namespace t3knative
