// d2k_menu.h - Defender 2000's settings page and controls functions for the shared menu (menu.h, controls.h), used
// when t2k.exe runs as d2k.exe (or with the Defender 2000 cartridge).
#pragma once
#include "menu.h"

namespace d2k_menu {

// controls:: functions (index 0 = none). Fire / Smart bomb / Lightning laser are the Jaguar's B / C / A in Defender
// 2000 and Plus (Classic's three-button pad: B fire, C smart bomb); Thrust / Reverse are the arcade's buttons, done
// on the d-pad (src/d2k.cpp Arcade).
enum Fn { kNone, kFire, kBomb, kLaser, kThrust, kReverse, kOption, kPause };

// Menu actions (numbered past t2k_menu's: main.cpp handles both in one switch).
enum Action { kRestart = menu::kGameAction + 10 };

// Registers the controls functions and the menu page. from_launcher: the last item says "Return to launcher".
void Install(bool from_launcher);

}  // namespace d2k_menu
