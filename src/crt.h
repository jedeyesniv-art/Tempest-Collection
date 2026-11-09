// crt.h - the CRT+ / CRT++ looks from Llamasoft on the ST (C:\LlamatronRecomp src/crt.cpp, the owner's tuning):
// pixel shaders on SDL's GPU renderer (src/shaders/*.frag.hlsl, DXIL -> D3D12; Tools: tools/build_shaders.py).
// Setting crt = 0 off / 1 CRT+ / 2 CRT++ and the crt_* / crt_psy_* numbers, read live (the Back menu's CRT section).
// Without the GPU renderer the picture is drawn plain.
#pragma once

#include <SDL3/SDL.h>

namespace crt {

// Asks for the GPU renderer that can take our DXIL shaders; falls back to SDL's default renderer.
// (Environment T2K_RENDERER = another renderer name, for tests.)
SDL_Renderer* CreateRenderer(SDL_Window* window);
// After CreateRenderer: builds the shaders (false if this renderer can't run them).
bool Init(SDL_Renderer* renderer);
void Shutdown();

bool Available();
int Mode();          // 0 off (or unavailable), 1 CRT+, 2 CRT++ (trails, colour bleed, more glow)
void LoadSettings(); // rereads the settings (cheap: call once a frame)
// The native scar feedback: `src` (the last frame, window-sized) drawn over the current target so that each window
// corner shows src at corners[k] (window pixels), its intensity lowered by dim (0..1; the shader, else undimmed).
// melt (the screen-transition melt; null = none): a noise warp changing every step (wobble, uv), an uneven downward
// drip (uv) and bright parts spreading (blob, 0..1).
struct Melt { float wobble, drip, blob; uint64_t step; };
void DrawScar(SDL_Renderer* renderer, SDL_Texture* src, const float corners[4][2], int ww, int wh, float dim,
              const Melt* melt = nullptr);

// Draws `texture` into view with the CRT look (Mode() > 0), else as it is. src_w / src_h: the picture's own
// resolution when the texture is bigger (the scanlines and mask follow the game's lines); 0 = the texture's size.
void DrawPicture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_FRect& view, float src_w = 0, float src_h = 0);

}  // namespace crt
