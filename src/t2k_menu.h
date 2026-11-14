// t2k_menu.h - Tempest 2000's settings page and controls functions for the shared menu (menu.h, controls.h).
#pragma once
#include "menu.h"

namespace t2k_menu {

// controls:: functions (index 0 = none).
enum Fn { kNone, kFire, kJump, kZap, kOption, kPause, kCamera };

// Tempest 2000's own menu actions.
enum Action { kToggleMusic = menu::kGameAction, kCheatWarp };

// Registers the controls functions and the menu page. from_launcher: the last item says "Return to launcher".
void Install(bool from_launcher);

}  // namespace t2k_menu
