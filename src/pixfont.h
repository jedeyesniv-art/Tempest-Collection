// pixfont.h - the collection's glowing 5x7 pixel font (system text, T3K style) and glowing lines, shared by the
// launcher (launcher_view.cpp) and the in-game settings menu (menu.cpp). Drawn with SDL's 2D renderer: each font
// pixel a filled square, the glow additive gaussian dots; lines a beam profile across.
#pragma once
#include <SDL3/SDL.h>

#include <string>

namespace pixfont {

struct Col { float r, g, b; };

// Characters beyond ASCII in the font: section chevrons.
constexpr char kFolded = '\x01';   // a small triangle pointing right
constexpr char kOpen = '\x02';     // pointing down

bool HasGlyph(char c);
// Width of s in pixels; unit = one font pixel (a character is 6 units wide with its gap, 7 tall; g p q y reach
// 2 units lower).
float TextWidth(const std::string& s, float unit);
// align: 0 centred on x, > 0 right edge at x, < 0 left edge at x. glow 0 = none, 1 = the launcher's.
// Characters the font lacks are drawn as their capitals (if it has those), else left blank.
void Text(SDL_Renderer* ren, const std::string& s, float x, float y, float unit, Col c, float a, int align = 0,
          float glow = 1.f);
// A soft additive line, the beam profile across, `width` wide.
void Beam(SDL_Renderer* ren, float x0, float y0, float x1, float y1, float width, Col c, float a);
// A glowing line: a wide faint beam and a thin bright core.
void Glow(SDL_Renderer* ren, float x0, float y0, float x1, float y1, float width, Col c, float a, float glow = 4.f);

// Frees the textures (made per renderer on first use). Call before destroying the renderer.
void Shutdown();

}  // namespace pixfont
