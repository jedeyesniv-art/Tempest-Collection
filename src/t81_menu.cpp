// t81_menu.cpp - see t81_menu.h.
#include "t81_menu.h"

#include "controls.h"
#include "lr_host.h"
#include "settings.h"

namespace t81_menu {
namespace {

// controls:: functions (index 0 = none) and the RetroPad button each one presses (MAME's libretro input: B = button
// 1 = Fire, A = button 2 = Superzapper, SELECT = coin, START = 1 player start).
enum Fn { kNone, kFire, kZap, kStart, kCoin, kFnCount };
const int kBit[kFnCount] = {-1, lr::B, lr::A, lr::START, lr::SELECT};

}  // namespace

void Install(bool from_launcher) {
  // A / RT fire, B / RB / X Superzapper, Start = 1 player start, Y = coin (Back opens this menu).
  //                                             a      b     x     y      lb     rb    lt     rt     start   ls     rs
  const int defaults[controls::kButtonCount] = {kFire, kZap, kZap, kCoin, kNone, kZap, kNone, kFire, kStart, kNone, kNone};
  controls::Setup({{"fire", "Fire"}, {"zap", "Superzapper"}, {"start", "Start (1 player)"}, {"coin", "Coin"}}, defaults);

  menu::Page p;
  p.Button("Resume", menu::kClose, true);
  p.Header("GAME");
  p.Button("Reset the machine", kReset, true);
  p.Choice("Save slot", "state_slot", {"1", "2", "3", "4", "5"}, {"1", "2", "3", "4", "5"});
  p.Button("  Save state", menu::kSaveState)->slot_info = true;
  p.Button("  Load state", menu::kLoadState, true)->slot_info = true;
  p.Header("SPINNER (left stick / d-pad)");
  p.Number("Spinner speed", "spinner_speed", 0.2, 3.0, 0.1, "%.1f", 1.0);
  p.Binds();
  p.Header("DISPLAY");
  p.Button("Fullscreen on / off", menu::kToggleFullscreen);
  p.Choice("Vector resolution", "mame_altres", {"1440x1080", "1920x1440", "2880x2160"},
           {"1440x1080", "1920x1440", "2880x2160"});
  p.Choice("Picture scaling", "picture_filter", {"smooth", "sharp"}, {"Smooth (bilinear)", "Sharp pixels"});
  p.Header("");
  p.Button(from_launcher ? "Return to launcher" : "Quit", menu::kQuit, true);
  menu::SetPage("TEMPEST (1981) - SETTINGS", std::move(p));
}

uint16_t PadButtons(uint32_t held) {
  uint16_t b = 0;
  for (int f = kFire; f < kFnCount; ++f)
    if (controls::FnHeld(held, f)) b |= uint16_t(1u << kBit[f]);
  return b;
}

}  // namespace t81_menu
