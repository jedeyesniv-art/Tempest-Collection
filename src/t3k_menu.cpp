// t3k_menu.cpp - see t3k_menu.h.
#include "t3k_menu.h"

#include "controls.h"
#include "crt_menu.h"
#include "pads.h"
#include "settings.h"

namespace t3k_menu {
namespace {

// controls:: functions (index 0 = none) and the Nuon button each one presses.
enum Fn { kNone, kA, kB, kCLeft, kCUp, kCDown, kCRight, kL, kR, kStart, kNuon, kFnCount };
const uint16_t kBit[kFnCount] = {0,0x4000,0x0008,0x0004,0x0002,0x8000,0x0001,0x0020,0x0010,0x2000,0x1000};

}  // namespace

void Install(bool from_launcher) {
  // Native NUON layout. Back / Guide / Escape open the collection menu, as in every game (owner 2026-11-25: Back did
  // nothing in play as the Nuon button; Steam owns Guide on the Deck); the Nuon button is on LB (L does nothing in
  // play). Existing bind_* choices retain their meanings.
  const int defaults[controls::kButtonCount] = {kA, kB, kCLeft, kCUp, kNuon, kR, kCDown, kCRight, kStart, kCDown, kCRight};
  controls::Setup({{"a", "A / fire"}, {"b", "B / zapper"}, {"c_left", "C-left"}, {"c_up", "C-up"}, {"c_down", "C-down"},
                   {"c_right", "C-right"}, {"l", "L"}, {"r", "R / jump"}, {"start", "Start"}, {"nuon", "Nuon button"}},
                  defaults);

  menu::SetAccent(1.f, 80 / 255.f, 1.f);   // the launcher's colour for this game (launcher_view.cpp kStyles)
  menu::Page p;
  p.Button("Resume", menu::kClose, true);
  p.Header("GAME");
  p.Choice("Volume", "volume", {"0", "0.25", "0.5", "0.75", "1", "1.25", "1.5"},
           {"Off", "25%", "50%", "75%", "100%", "125%", "150%"})->def_value = "1";
  p.Button("Restart the game", kRestart, true);
  // play / menus / warp each at their own speed (t3k_main.cpp SpeedControl)
  p.Number("Play speed (pictures/s)", "game_speed", 15, 60, 1, "%.0f", 20);
  p.Number("Menu speed (pictures/s)", "menu_speed", 15, 60, 1, "%.0f", 30);
  p.Choice("Warp between levels", "warp_speed", {"60", "0"}, {"60/s (original)", "Play speed"});
  p.Number("  Speed change (s)", "warp_ramp", 0, 2, 0.1, "%.1f", 0.5);
  p.Choice("Save slot", "state_slot", {"1", "2", "3", "4", "5"}, {"1", "2", "3", "4", "5"});
  p.Button("  Save state", menu::kSaveState)->slot_info = true;
  p.Button("  Load state", menu::kLoadState, true)->slot_info = true;
  p.Header("CONTROLS");
  p.Choice("Controller", "controller", {"auto"}, {"Any (last used)"})->refresh = pads::FillChoice;
  p.Choice("Input device", "control_device", {"gamepad", "mouse"},
           {"Gamepad (native NUON)", "Mouse / trackball"});
  p.Binds();
  p.Header("PICTURE");
  p.Button("Fullscreen", menu::kToggleFullscreen);
  // the picture drawn at most this tall, then stretched over the window (lighter for weaker GPUs)
  p.Choice("Resolution", "resolution", {"0", "1440", "1080", "900", "720", "540"},
           {"Window (full)", "1440p", "1080p", "900p", "720p (lighter)", "540p (lightest)"});
  // Both modes use the native renderer; the second retains original resolution.
  auto rem = [] { return settings::Num("native_view", 1) == 1; };
  auto sharp = [] { return settings::Num("native_view", 1) == 1 && settings::Num("sharp_vectors", 1) != 0; };
  p.Choice("Renderer", "native_view", {"1", "0"}, {"Remaster", "Original resolution (native)"});
  p.Choice("  Wide screens", "widescreen", {"1", "0"}, {"Fill the width", "4:3 with bars"})->when = rem;
  p.Choice("  Smooth motion", "native_interpolate", {"1", "0"}, {"On", "Off"})->when = rem;
  p.Choice("  Vector lines", "sharp_vectors", {"1", "0"}, {"Sharp", "Original"})->when = rem;
  p.Number("    Line core width", "sharp_core", 0.1, 1.0, 0.05, "%.2f", 0.35)->when = sharp;
  p.Number("    Glow", "sharp_glow", 0, 1, 0.05, "%.2f", 0.4)->when = sharp;
  p.Choice("Frame-rate counter (F4)", "show_rates", {"0", "1"}, {"Off", "On"});
  crt_menu::Add(p, 1, 0);   // T3K: CRT+ only, off by default
  p.Header("CHEATS");
  p.Choice("Infinite lives", "cheat_inf_lives", {"0", "1"}, {"Off", "On"});
  p.Header("");
  p.Button(from_launcher ? "Return to launcher" : "Quit", menu::kQuit, true);
  menu::SetPage("TEMPEST 3000 - SETTINGS", std::move(p));
}

uint16_t NuonButtons(uint32_t held) {
  uint16_t b = 0;
  for (int f = kA; f < kFnCount; ++f)
    if (controls::FnHeld(held, f)) b |= kBit[f];
  return b;
}

}  // namespace t3k_menu
