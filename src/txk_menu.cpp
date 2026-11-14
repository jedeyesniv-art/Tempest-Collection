// txk_menu.cpp - see txk_menu.h.
#include "txk_menu.h"

#include "controls.h"
#include "settings.h"
#include "vita_host.h"

namespace txk_menu {
namespace {

// controls:: functions (index 0 = none) and the RetroPad button each one presses (the core maps RetroPad B to
// Cross, A to Circle, Y to Square, X to Triangle).
enum Fn { kNone, kCross, kCircle, kSquare, kTriangle, kL, kR, kStart, kSelect, kFnCount };
const int kBit[kFnCount] = {-1, vita::B, vita::A, vita::Y, vita::X, vita::L, vita::R, vita::START, vita::SELECT};

}  // namespace

void Install(bool from_launcher) {
  // Xbox layout by position: A = Cross, B = Circle, X = Square, Y = Triangle, LB / RB = L / R, Start = Start. Back
  // opens this menu, so Select (TxK: options on the title, mode change on level select) moves to LT.
  //                                             a       b        x        y          lb  rb  lt       rt     start   ls     rs
  const int defaults[controls::kButtonCount] = {kCross, kCircle, kSquare, kTriangle, kL, kR, kSelect, kNone, kStart, kNone, kNone};
  controls::Setup({{"cross", "Cross"}, {"circle", "Circle"}, {"square", "Square"}, {"triangle", "Triangle"},
                   {"l", "L"}, {"r", "R"}, {"start", "Start"}, {"select", "Select"}},
                  defaults);

  menu::Page p;
  p.Button("Resume", menu::kClose, true);
  p.Header("GAME");
  p.Button("Restart the game (applies the resolution)", kRestart, true);
  p.Binds();
  p.Header("DISPLAY");
  p.Button("Fullscreen on / off", menu::kToggleFullscreen);
  p.Choice("Internal resolution (restart the game to apply)", "vita3k_resolution_multiplier", {"1", "2", "3", "4"},
           {"1x 960x544", "2x 1920x1088", "3x 2880x1632", "4x 3840x2176"});
  p.Choice("Texture filtering (anisotropic)", "vita3k_anisotropic_filtering", {"1", "2", "4", "8", "16"},
           {"Off", "2x", "4x", "8x", "16x"});
  p.Choice("Picture scaling", "picture_filter", {"smooth", "sharp"}, {"Smooth (bilinear)", "Sharp pixels"});
  p.Header("");
  p.Button(from_launcher ? "Return to launcher" : "Quit", menu::kQuit, true);
  menu::SetPage("TxK - SETTINGS", std::move(p));
}

uint16_t VitaButtons(uint32_t held) {
  uint16_t b = 0;
  for (int f = kCross; f < kFnCount; ++f)
    if (controls::FnHeld(held, f)) b |= uint16_t(1u << kBit[f]);
  return b;
}

}  // namespace txk_menu
