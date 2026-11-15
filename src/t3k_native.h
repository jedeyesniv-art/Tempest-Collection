// t3k_native.h - Tempest 3000 native renderer (see PLAN.md "Native renderer research"): redraws the pictures the game
// draws in software on its MPEs from their display lists (nuon::Capture), at the window's resolution.
// So far: entry type 00710001, the vector shapes (module 806A3F50): the shape streams, the bit-exact segment transform
// (as tools/t3k_vecmodel.py) and the line look (tools/t3k_linefit.py: a linear tent 1.94 game pixels wide each side,
// opacity 0.00888 x the colours' 4th byte, blended toward the line colour).
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

// One drawn entry: which segments it made, and what identifies it for interpolation.
struct Item {
  uint32_t shape;        // w7: the shape stream
  float x, y;            // w0: screen position
  uint32_t first, count; // its segments in Vectors::segs
};

// A picture's vector layer, in list order.
struct Vectors {
  uint32_t flip = 0;
  std::vector<Seg> segs;
  std::vector<Item> items;
};

// Build the vector layer of a captured picture.
void BuildVectors(const nuon::Capture& cap, Vectors* out);

// The layer between two consecutive pictures (t = 0: prev, 1: cur), drawn in cur's order. Entries of cur are matched
// to prev's by shape, segment count and nearest position (each used once); unmatched ones, or moves of more than
// max_move game pixels, are drawn as in cur.
void Interpolate(const Vectors& prev, const Vectors& cur, float t, std::vector<Seg>* out, float max_move = 30.f);

// Draw segments into dst (the window rectangle the 360 x 240 picture fills).
void DrawVectors(SDL_Renderer* ren, const std::vector<Seg>& segs, const SDL_FRect& dst);

}  // namespace t3knative
