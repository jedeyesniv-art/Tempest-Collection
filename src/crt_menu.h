// crt_menu.h - the CRT section of the settings menu, the same in every game that has the CRT looks (crt.h).
#pragma once
#include "menu.h"

namespace crt_menu {

// Adds the CRT section: the look (Off / CRT+, and CRT++ when max_mode is 2) and its sliders (the CRT++ ones only
// with CRT++). def = the look when the setting is missing; max_mode / def as given to crt::SetModeDefault.
void Add(menu::Page& p, int max_mode, int def);

}  // namespace crt_menu
