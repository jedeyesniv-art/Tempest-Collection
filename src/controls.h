// controls.h - pad button -> game function bindings (settings bind_<button> = <function key>), shared by every game
// program. Each game registers its own function list (Setup) before use; function 0 is always "none".
// Fixed, not remappable: the d-pad and left stick (movement / menus) and Back (settings menu).
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <vector>

namespace controls {

constexpr int kButtonCount = 11;   // a b x y lb rb lt rt start ls rs

struct FnDef {
  const char* key;    // stored in the settings ("fire")
  const char* name;   // shown ("Superzapper")
};

// fns: the game's functions after "none" (index 1 on). defaults: per button (a b x y lb rb lt rt start ls rs) the
// function index, 0 = none.
void Setup(const std::vector<FnDef>& fns, const int (&defaults)[kButtonCount]);
int FnCount();                     // including "none"

const char* FnName(int f);
const char* ButtonName(int b);     // shown name ("RT")
int FnOf(int button);
std::string ButtonsFor(int f);     // "X, LT, RT" or "-"
uint32_t Held(SDL_Gamepad* pad);   // bit per button
bool FnHeld(uint32_t held, int f);
// Binds the button to the function; if it already was, unbinds it (so a second press removes a button).
void Assign(int f, int button);
void ResetDefaults();

}  // namespace controls
