// menu.h - the in-game settings menu (Back / Esc). Emulation is paused while it is open.
#pragma once
#include <SDL3/SDL.h>
#include <string>

namespace menu {

struct Nav {            // this frame's menu input (held state; edges are found inside)
  bool up = false, down = false, left = false, right = false;
  bool ok = false, back = false;
};

enum class Action { None, Close, Quit, ToggleMusic, ToggleFullscreen };

bool IsOpen();
void Open();
void Close();
Action Update(const Nav& nav, uint64_t now_ns);
void Draw(SDL_Renderer* ren, int out_w, int out_h);

// A short message drawn over the game for a moment (e.g. "Camera: fixed").
void Toast(const std::string& text, uint64_t now_ns);
void DrawToast(SDL_Renderer* ren, int out_w, int out_h, uint64_t now_ns);

}  // namespace menu
