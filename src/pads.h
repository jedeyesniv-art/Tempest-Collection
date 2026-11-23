// pads.h - which controller a game program reads, shared by t2k / t3k / t81 / txk. Every connected controller is
// opened (as the launcher does); the game reads one of them:
//   setting controller = auto (default): the one used last (a button press or a stick / trigger pushed past half), so
//                        a wheel, a virtual pad (DS4Windows, vJoy, Steam Input) or another device SDL counts as a
//                        gamepad can't take the game's only slot;
//   controller = <name>: that controller while it is connected (the first of that name), else auto.
// Each game passes every SDL event to HandleEvent and reads Current() after its event loop.
#pragma once
#include <SDL3/SDL.h>

namespace menu { struct Item; }

namespace pads {

void HandleEvent(const SDL_Event& e);
SDL_Gamepad* Current();
// The menu's "Controller" choice: Any (last used) + every connected controller by name (and the saved one when it
// is not connected). Its menu::Item::refresh.
void FillChoice(menu::Item& item);

}  // namespace pads
