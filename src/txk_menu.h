// txk_menu.h - TxK's settings page and controls functions for the shared menu (menu.h, controls.h).
#pragma once
#include <cstdint>

#include "menu.h"

namespace txk_menu {

// TxK's own menu actions.
enum Action { kRestart = menu::kGameAction };

// Registers the controls functions (the Vita's buttons) and the menu page.
// from_launcher: the last item says "Return to launcher".
void Install(bool from_launcher);

// The RetroPad button bits (1 << vita::Btn) of the functions held in `held` (controls::Held).
uint16_t VitaButtons(uint32_t held);

}  // namespace txk_menu
