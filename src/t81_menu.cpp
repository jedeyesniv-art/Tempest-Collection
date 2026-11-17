// t81_menu.cpp - see t81_menu.h.
#include "t81_menu.h"

#include "controls.h"
#include "crt_menu.h"
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

  menu::SetAccent(90 / 255.f, 200 / 255.f, 1.f);   // the launcher's colour for this game (launcher_view.cpp kStyles)
  menu::Page p;
  p.Button("Resume", menu::kClose, true);
  p.Header("GAME");
  p.Button("Restart the game", kReset, true);
  p.Choice("Save slot", "state_slot", {"1", "2", "3", "4", "5"}, {"1", "2", "3", "4", "5"});
  p.Button("  Save state", menu::kSaveState)->slot_info = true;
  p.Button("  Load state", menu::kLoadState, true)->slot_info = true;
  p.Header("CONTROLS");
  p.Number("Spinner speed (left stick / d-pad)", "spinner_speed", 0.2, 3.0, 0.1, "%.1f", 1.0);
  p.Binds();
  p.Header("PICTURE");
  p.Button("Fullscreen", menu::kToggleFullscreen);
  // our beams (Remaster) or MAME's rasterised picture (Emulation), named as in T2K / T3K
  auto ours = [] { return settings::Num("vector_renderer", 1) != 0; };
  auto mame = [] { return settings::Num("vector_renderer", 1) == 0; };
  p.Choice("Renderer", "vector_renderer", {"1", "0"}, {"Remaster (our beams)", "Emulation (MAME's picture)"});
  p.Choice("  Framing", "framing", {"fill", "wide", "arcade"}, {"Fill", "Wide (big web)", "Whole monitor"})->when = ours;
  p.Choice("  Shape", "aspect", {"round", "arcade"}, {"Round (square units)", "Arcade 3:4 tube"})->when = ours;
  p.Choice("  Vector resolution", "mame_altres", {"1440x1080", "1920x1440", "2880x2160"},
           {"1440x1080", "1920x1440", "2880x2160"})->when = mame;
  p.Choice("  Picture scaling", "picture_filter", {"smooth", "sharp"}, {"Smooth (bilinear)", "Sharp pixels"})->when = mame;
  p.Sub("Fine-tune look");
  p.Number("Line width", "line_width", 1.0, 8.0, 0.25, "%.2f", 2.5)->when = ours;
  p.Number("Width by brightness", "width_by_intensity", 0.0, 2.0, 0.25, "%.2f", 1.0)->when = ours;
  p.Number("Glow", "glow", 0.0, 2.0, 0.1, "%.1f", 1.0)->when = ours;
  p.Number("Glow size", "glow_size", 2.0, 20.0, 1.0, "%.0f", 6.0)->when = ours;
  p.Number("Brightness", "brightness", 0.5, 2.0, 0.05, "%.2f", 1.2)->when = ours;
  p.Number("White-hot core", "hot_core", 0.0, 1.0, 0.05, "%.2f", 0.2)->when = ours;
  crt_menu::Add(p, 2, 0);   // T81: CRT+ and CRT++, off by default
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
