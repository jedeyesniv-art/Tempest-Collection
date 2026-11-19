// d2k_menu.cpp - see d2k_menu.h.
#include "d2k_menu.h"

#include "controls.h"
#include "crt_menu.h"
#include "settings.h"

namespace d2k_menu {

void Install(bool from_launcher) {
  // Defaults: A fire, B / RB smart bomb, X lightning laser (Plus), Y Option, LB / LT reverse, RT thrust, Start pause.
  //                                  a      b      x       y        lb        rb     lt        rt       start   ls     rs
  const int defaults[controls::kButtonCount] = {kFire, kBomb, kLaser, kOption, kReverse, kBomb, kReverse, kThrust, kPause,
                                                kNone, kNone};
  controls::Setup({{"fire", "Fire"}, {"bomb", "Smart bomb"}, {"laser", "Lightning laser (Plus)"},
                   {"thrust", "Thrust (arcade)"}, {"reverse", "Reverse (arcade)"}, {"option", "Option"},
                   {"pause", "Pause"}},
                  defaults);

  menu::SetAccent(1.f, 120 / 255.f, 60 / 255.f);
  menu::Page p;
  p.Button("Resume", menu::kClose, true);
  p.Header("GAME");
  p.Choice("Volume", "volume", {"0", "0.25", "0.5", "0.75", "1", "1.25", "1.5"},
           {"Off", "25%", "50%", "75%", "100%", "125%", "150%"})->def_value = "1";
  p.Choice("Save slot", "state_slot", {"1", "2", "3", "4", "5"}, {"1", "2", "3", "4", "5"});
  p.Button("  Save state", menu::kSaveState)->slot_info = true;
  p.Button("  Load state", menu::kLoadState, true)->slot_info = true;
  p.Header("CONTROLS");
  // Arcade: up / down on the stick, Thrust and Reverse on buttons (the d-pad keeps the game's own turn-and-thrust,
  // and works the game's menus).
  p.Choice("Stick left / right", "d2k_stick_lr", {"game", "off"},
           {"Turn and thrust", "Off (arcade)"});
  p.Binds();
  p.Header("PICTURE");
  p.Button("Fullscreen", menu::kToggleFullscreen);
  p.Choice("Frame rate", "frame_cap", {"0", "60"}, {"Display rate", "60 (lighter: Steam Deck)"});
  auto native = [] { return settings::Num("d2k_native", 1) != 0; };
  p.Choice("Renderer", "d2k_native", {"1", "0"}, {"Native", "Emulation (original)"});
  p.Choice("  Smooth motion", "d2k_interpolate", {"1", "0"}, {"On", "Off"})->when = native;
  p.Choice("  Wide screen", "d2k_widescreen", {"1", "0"}, {"On", "Off (4:3)"})->when = native;
  p.Choice("  Pixels", "d2k_filter", {"sharp", "smooth", "nearest"}, {"Sharp", "Soft (bilinear)", "Blocky"})->when =
      native;
  crt_menu::Add(p, 2, 2);
  p.Header("");
  p.Button(from_launcher ? "Return to launcher" : "Quit", menu::kQuit, true);
  menu::SetPage("DEFENDER 2000 - SETTINGS", std::move(p));
}

}  // namespace d2k_menu
