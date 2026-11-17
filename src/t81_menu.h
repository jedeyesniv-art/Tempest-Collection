// t81_menu.h - arcade Tempest's (1981) settings page and controls functions for the shared menu (menu.h, controls.h).
#pragma once
#include <cstdint>

#include "menu.h"

namespace t81_menu {

// Arcade Tempest's own menu actions.
enum Action { kReset = menu::kGameAction };

// Registers the controls functions (the cabinet's buttons) and the menu page.
// from_launcher: the last item says "Return to launcher".
void Install(bool from_launcher);

// The RetroPad button bits (1 << lr::Btn) of the functions held in `held` (controls::Held).
uint16_t PadButtons(uint32_t held);

// The menu's name of a version (a MAME set name: tempest, tempest3, ..., temptube).
std::string VersionName(const std::string& set);

}  // namespace t81_menu
