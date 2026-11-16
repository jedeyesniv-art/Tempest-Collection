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
  float size = 1.f;   // game pixels
};

bool Install();                       // after t2k::Install
// The HUD overlay strip (screen3, 384 x 48) as of the shown frame: 0xFFRRGGBB, 0 where see-through.
const host::Frame& Hud();
// Native scar feedback (the melts, the title, Game Over; setting remaster_feedback, default on).
void SetFeedback(bool on);
// Particles drawn as the original's square pixels (while drawing into the native feedback).
void SetHardPixels(bool on);
// The buffer about to be shown came from a skipped scar: the game's play buffers are stale (the core skips them).
bool FeedbackLately();
// The shown scene's scar step: for the window corners (0,0) (w,0) (0,h) (w,h), where they sample the last frame (window
// pixels); dim = the intensity drop (0..1); serial = the scar call (a new step when it changes).
// melt: the step is the melt between screens (not the title / Game Over feedback).
bool ScarMap(const SDL_FRect& dst, unsigned fw, unsigned fh, int ww, int wh, float corners[4][2], float* dim,
             uint64_t* serial, bool* melt = nullptr);
// After each emulated frame: picks the item list of the displayed buffer. publish = make it the newest scene shown
// (false for a frame whose picture isn't shown: a real frame followed by run-ahead frames).
void AfterFrame(bool publish = true);
// Run-ahead: keep / restore the capture state (not the shown scenes) around frames that are rewound.
void Mark();
void Rewind();
// Window: rebuild what Draw shows, blended between the previous and the latest displayed frame (t = 0..1; 1 = the
// latest, as AfterFrame leaves it). Setting remaster_interpolate (default on).
void Interpolate(float t);
void SetInterpolate(bool on);
const std::vector<Line>& Lines();     // the displayed frame's lines

// Where the GPU screen's (0, 0) falls in the emulated picture (pixels); set from the alignment check.
void SetOrigin(float x, float y);
// On: the captured GPU draws are skipped and drawn by us instead (setting remaster, F8).
void SetEnabled(bool on);
void SetLineWidth(float game_pixels);   // setting remaster_line_width (default 1.5)
// Settings remaster_bolt_width (game px, 0 = the original thin line; default 2), remaster_bolt_glow (default on),
// remaster_particle_size (power-up / explosion pixels, x the original; default 1.5), remaster_shot_glow (a halo
// around enemy bullets; 0 = off, default 0.5).
// shot_size: game px each enemy bullet piece is widened by on every side (0 = the exact thin geometry).
void SetEffects(float bolt_width, bool bolt_glow, float particle_size, float shot_glow, float shot_size);
// Settings remaster_pup_floor (power-up ring minimum brightness 0-1, 0 = the original depth fade; default 0.7),
// remaster_pup_glow (glow on the rings, 0 = off; default 1).
void SetPowerUps(float floor, float glow);
bool Enabled();

// Headless check: the emulated frame scaled up `scale` times with our lines drawn over it.
// aspect > 0: a picture of that shape (W / H), the emulated 4:3 frame centred and our layer filling the rest
// (set a wide view first: SetView).
host::Frame Overlay(const host::Frame& f, int scale, double aspect = 0);
// Any aspect ratio: cull to this part of the GPU screen space instead of the original screen (0..384 x 0..240).
void SetView(double x0, double y0, double x1, double y1);
// Window: the view from the window size and the emulated frame's rectangle dst in it.
void SetWindow(const SDL_FRect& dst, int ww, int wh, unsigned frame_w, unsigned frame_h);
std::string Stats();
void DumpLines(const std::string& path);   // x0 y0 x1 y1 per line (GPU screen space)

// Window: our lines over the picture placed at dst (the emulated frame's rectangle).
void Draw(SDL_Renderer* ren, const SDL_FRect& dst, unsigned frame_w, unsigned frame_h);

}  // namespace remaster
