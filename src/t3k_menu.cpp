// t3k_menu.cpp - see t3k_menu.h.
#include "t3k_menu.h"

#include "controls.h"
#include "nuon_host.h"
#include "settings.h"

namespace t3k_menu {
namespace {

// controls:: functions (index 0 = none) and the Nuon button each one presses.
enum Fn { kNone, kA, kB, kCLeft, kCUp, kCDown, kCRight, kL, kR, kStart, kNuon, kFnCount };
const int kBit[kFnCount] = {-1, nuon::A, nuon::B, nuon::Y, nuon::X, nuon::L2, nuon::R2, nuon::L, nuon::R,
                            nuon::START, nuon::SELECT};

}  // namespace

void Install(bool from_launcher) {
  // The owner's layout (from their Nuance setup): A = A, B = B, X = C-left, Y = C-up, LB / RB = L / R, LT / L-stick
  // click = C-down, RT / R-stick click = C-right, Start = Start. Back opens this menu, so the Nuon button starts
  // unbound (bind it here if a screen needs it).
  //                                             a   b   x       y     lb  rb  lt      rt       start   ls      rs
  const int defaults[controls::kButtonCount] = {kA, kB, kCLeft, kCUp, kL, kR, kCDown, kCRight, kStart, kCDown, kCRight};
  controls::Setup({{"a", "A"}, {"b", "B"}, {"c_left", "C-left"}, {"c_up", "C-up"}, {"c_down", "C-down"},
                   {"c_right", "C-right"}, {"l", "L"}, {"r", "R"}, {"start", "Start"}, {"nuon", "Nuon button"}},
                  defaults);

  auto lr = [] { return settings::Str("stick_mode", "lr") == "lr"; };
  menu::Page p;
  p.Button("Resume", menu::kClose, true);
  p.Header("GAME");
  p.Choice("Game speed", "flip_cap", {"3", "2.5", "2", "1"},
           {"20 pictures/s (best)", "24 pictures/s", "30 pictures/s", "60 pictures/s"});
  p.Header("STICK");
  p.Choice("Stick mode", "stick_mode", {"lr", "off"}, {"Left / right", "Off (stick = d-pad)"});
  p.Number("  Top speed (lanes a picture)", "lr_max_speed", 0.30, 1.50, 0.01, "%.2f", 0.94)->when = lr;
  p.Number("  Ramp to top speed (pictures)", "lr_ramp_pictures", 1, 12, 1, "%.0f", 4)->when = lr;
  p.Number("  First-picture speed", "lr_ramp_start", 0, 1, 0.05, "%.2f", 0.30)->when = lr;
  p.Number("  Deflection curve", "lr_curve", 1, 3, 0.1, "%.1f", 1.5)->when = lr;
  p.Number("  Deadzone", "lr_deadzone", 0.05, 0.5, 0.01, "%.2f", 0.20)->when = lr;
  p.Number("  Ease-off (pictures)", "lr_release_pictures", 0, 4, 1, "%.0f", 1)->when = lr;
  p.Binds();
  p.Header("DISPLAY");
  p.Button("Fullscreen on / off", menu::kToggleFullscreen);
  p.Choice("Picture blend (F3)", "picture_blend", {"1", "0"}, {"On", "Off"});
  p.Choice("Frame-rate counter (F4)", "show_rates", {"0", "1"}, {"Off", "On"});
  p.Choice("High resolution 720x480 (experimental, next start)", "high_res", {"0", "1"}, {"Off", "On"});
  p.Header("");
  p.Button(from_launcher ? "Return to launcher" : "Quit", menu::kQuit, true);
  menu::SetPage("TEMPEST 3000 - SETTINGS", std::move(p));
}

uint16_t NuonButtons(uint32_t held) {
  uint16_t b = 0;
  for (int f = kA; f < kFnCount; ++f)
    if (controls::FnHeld(held, f)) b |= uint16_t(1u << kBit[f]);
  return b;
}

}  // namespace t3k_menu
