// controls.h - pad button -> game function bindings (userdata\settings.txt bind_<button> = <function>).
// Fixed, not remappable: the d-pad and left stick (movement / menus) and Back (settings menu).
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>

namespace controls {

enum Fn { kNone, kFire, kJump, kZap, kOption, kPause, kCamera, kFnCount };

constexpr int kButtonCount = 11;   // a b x y lb rb lt rt start ls rs

const char* FnName(Fn f);          // shown name ("Superzapper")
const char* ButtonName(int b);     // shown name ("RT")
Fn FnOf(int button);
std::string ButtonsFor(Fn f);      // "X, LT, RT" or "-"
uint32_t Held(SDL_Gamepad* pad);   // bit per button
bool FnHeld(uint32_t held, Fn f);
// Binds the button to the function; if it already was, unbinds it (so a second press removes a button).
void Assign(Fn f, int button);
void ResetDefaults();

}  // namespace controls
