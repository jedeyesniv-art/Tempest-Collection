// t81_vectors.h - arcade Tempest's vectors drawn by t81.exe itself (instead of MAME's rasterised picture): the core
// (third_party/mame-libretro, branch llamasoft-mame) exports each frame's lines (llama_mame_lines, monitor space 0..1),
// and we draw them as beams (a soft additive glow + a sharp core) at window resolution, framed to use the screen:
//   arcade - the whole 3:4 monitor, as MAME shows it
//   fill   - the part of the monitor the game uses (x 0.08..0.885, y 0.13..0.95 over attract + play)
//   wide   - fill, and in play the web alone fills the height with the score / lives and the high score moved to the
//            top corners (play = the score / lives HUD drawn and no attract text at the bottom, with hysteresis).
// Settings (userdata\t81_settings.txt, menu DISPLAY): vector_renderer (1 ours, 0 MAME's picture), framing, aspect
// (arcade 3:4 or round = the game's square units), line_width, glow, glow_size, brightness, width_by_intensity.
#pragma once

#include <SDL3/SDL.h>

namespace t81_vectors {

// One frame's lines as the core gives them: 9 floats each (x0 y0 x1 y1, MAME width, a r g b; a = beam intensity).
struct Frame {
  const float* lines = nullptr;
  int count = 0;
};

// The core's export, or nullptr (an older core: then t81 shows MAME's picture).
using LinesFn = const float* (*)(int* count, float* target_w, float* target_h);

// Call once per emulated frame with its lines (classification + play detection run per game frame).
void NewFrame(const Frame& f);
// Draws the last frame into the window (ww x wh pixels). dt_s: time since the last call (framing transitions).
void Draw(SDL_Renderer* ren, int ww, int wh, float dt_s);
void Shutdown();

}  // namespace t81_vectors
