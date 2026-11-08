// remaster.h - native drawing of what the game draws through its GPU (see REMASTER.md).
// Stage 2 prototype: llama vect3d (3D vector objects / webs) captured at gpurun and projected in float.
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <vector>

#include "host.h"

namespace remaster {

// A projected line in the GPU's 384 x 240 screen space (floats), colour as RGB at each end.
struct Line {
  float x0, y0, x1, y1;
  uint32_t c0, c1;     // 0xRRGGBB
  bool thick;          // the game's thick vectors (drawn twice, 1 px apart)
};

// A filled triangle (Gouraud), same space.
struct Tri {
  float x[3], y[3];
  uint32_t c[3];
};

// A star (a dot), same space.
struct Dot {
  float x, y;
  uint32_t c;
};

bool Install();                       // after t2k::Install
void AfterFrame();                    // picks the item list of the displayed buffer
const std::vector<Line>& Lines();     // the displayed frame's lines

// Where the GPU screen's (0, 0) falls in the emulated picture (pixels); set from the alignment check.
void SetOrigin(float x, float y);
// On: the captured GPU draws are skipped and drawn by us instead (setting remaster, F8).
void SetEnabled(bool on);
void SetLineWidth(float game_pixels);   // setting remaster_line_width (default 1.5)
bool Enabled();

// Headless check: the emulated frame scaled up `scale` times with our lines drawn over it.
host::Frame Overlay(const host::Frame& f, int scale);
std::string Stats();
void DumpLines(const std::string& path);   // x0 y0 x1 y1 per line (GPU screen space)

// Window: our lines over the picture placed at dst (the emulated frame's rectangle).
void Draw(SDL_Renderer* ren, const SDL_FRect& dst, unsigned frame_w, unsigned frame_h);

}  // namespace remaster
