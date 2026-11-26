// t3k_menu.h - Tempest 3000's settings page and controls functions for the shared menu (menu.h, controls.h).
#pragma once
#include <cstdint>

#include "menu.h"

namespace t3k_menu {

// The page's own action (menu::Update returns it).
enum Action { kRestart = menu::kGameAction };

// Registers the controls functions (the Nuon controller's buttons) and the menu page.
// from_launcher: the last item says "Return to launcher".
void Install(bool from_launcher,bool profile_mode=false);
// T3K adds mouse to the shared gamepad inventory without changing other games.
void FillControllerChoice(menu::Item& item);

// Native hardware button mask of the functions held in `held` (controls::Held).
uint16_t NuonButtons(uint32_t held);

}  // namespace t3k_menu
