// t3k_menu.cpp - see t3k_menu.h.
#include "t3k_menu.h"

#include "controls.h"
#include "crt_menu.h"
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

  auto lr = [] { return settings::Str("stick_mode", "step") == "lr"; };
  auto st = [] { return settings::Str("stick_mode", "step") == "stiff"; };
  auto sp = [] { return settings::Str("stick_mode", "step") == "step"; };
  menu::SetAccent(1.f, 80 / 255.f, 1.f);   // the launcher's colour for this game (launcher_view.cpp kStyles)
  menu::Page p;
  p.Button("Resume", menu::kClose, true);
  p.Header("GAME");
  p.Choice("Volume", "volume", {"0", "0.25", "0.5", "0.75", "1", "1.25", "1.5"},
           {"Off", "25%", "50%", "75%", "100%", "125%", "150%"})->def_value = "1";
  p.Button("Restart the game", kRestart, true);
  p.Number("Game speed (pictures/s)", "game_speed", 15, 60, 1, "%.0f", 26);
  auto warp = [] { return settings::Num("warp_speed", 60) > 0; };
  p.Choice("Warp between levels", "warp_speed", {"60", "0"}, {"60/s (original)", "Game speed"});
  p.Number("  Speed change (s)", "warp_ramp", 0, 2, 0.1, "%.1f", 0.5)->when = warp;
  p.Choice("Save slot", "state_slot", {"1", "2", "3", "4", "5"}, {"1", "2", "3", "4", "5"});
  p.Button("  Save state", menu::kSaveState)->slot_info = true;
  p.Button("  Load state", menu::kLoadState, true)->slot_info = true;
  p.Header("CONTROLS");
  p.Choice("Stick mode", "stick_mode", {"step", "stiff", "lr"},
           {"Step (nudge = a lane)", "Stiff (T2K feel)", "Left / right"});
  p.Choice("Run-ahead (less input lag, more CPU)", "run_ahead", {"0", "1", "2", "3", "4"},
           {"Off", "1 field", "2 fields", "3 fields", "4 fields"});
  p.Sub("Fine-tune stick");
  // the owner's tuning (2026-11-17) is each slider's default and its middle
  p.Choice("On release after a sweep", "sp_release", {"off", "back", "nearest", "ahead"},
           {"Off (stop where it is)", "Step back a lane (stop short)", "Nearest lane", "On to the next lane"})->when = sp;
  p.Number("Top speed (lanes a second)", "sp_top_speed", 16, 34, 0.5, "%.1f", 25.0)->when = sp;
  p.Number("Hold before sweeping (s)", "sp_repeat_delay", 0.02, 0.18, 0.01, "%.2f", 0.10)->when = sp;
  p.Number("Ramp to top speed (s)", "sp_ramp_time", 0.02, 0.58, 0.02, "%.2f", 0.30)->when = sp;
  p.Number("Sweep start (of top)", "sp_ramp_start", 0.05, 0.55, 0.01, "%.2f", 0.30)->when = sp;
  p.Number("Step time (s a lane)", "sp_step_time", 0.01, 0.17, 0.01, "%.2f", 0.09)->when = sp;
  p.Number("Stick depth curve", "sp_curve", 0.5, 2.5, 0.1, "%.1f", 1.5)->when = sp;
  p.Number("Deadzone", "sp_deadzone", 0.10, 0.30, 0.01, "%.2f", 0.20)->when = sp;
  p.Number("Fine zone (stick push)", "st_fine_threshold", 0.55, 0.95, 0.05, "%.2f", 0.75)->when = st;
  p.Number("Fine speed (lanes a second)", "st_fine_speed", 0.5, 2.5, 0.25, "%.2f", 1.5)->when = st;
  p.Choice("Fine moves", "st_fine_click", {"1", "0"}, {"Click lane to lane", "Glide"})->when = st;
  p.Number("Top speed (lanes a second)", "st_top_speed", 16, 34, 0.5, "%.1f", 25.0)->when = st;
  p.Number("Ramp to top speed (s)", "st_ramp_time", 0.02, 0.78, 0.02, "%.2f", 0.40)->when = st;
  p.Number("First speed (of top)", "st_ramp_start", 0, 0.16, 0.01, "%.2f", 0.08)->when = st;
  p.Number("Sweep curve", "st_curve", 1.0, 3.0, 0.1, "%.1f", 2.0)->when = st;
  p.Number("Deadzone", "st_deadzone", 0.02, 0.18, 0.01, "%.2f", 0.10)->when = st;
  p.Number("Ease-off (s)", "st_ease_time", 0, 0.2, 0.01, "%.2f", 0)->when = st;
  p.Choice("Stop on a lane (after a sweep)", "st_snap", {"0", "1"}, {"Off", "On"})->when = st;
  p.Number("Top speed (lanes a picture)", "lr_max_speed", 0.30, 1.50, 0.01, "%.2f", 0.94)->when = lr;
  p.Number("Ramp to top speed (pictures)", "lr_ramp_pictures", 1, 12, 1, "%.0f", 4)->when = lr;
  p.Number("First-picture speed", "lr_ramp_start", 0, 1, 0.05, "%.2f", 0.30)->when = lr;
  p.Number("Deflection curve", "lr_curve", 1, 3, 0.1, "%.1f", 1.5)->when = lr;
  p.Number("Deadzone", "lr_deadzone", 0.05, 0.5, 0.01, "%.2f", 0.20)->when = lr;
  p.Number("Ease-off (pictures)", "lr_release_pictures", 0, 4, 1, "%.0f", 1)->when = lr;
  p.Binds();
  p.Header("PICTURE");
  p.Button("Fullscreen", menu::kToggleFullscreen);
  // Remaster (the native renderer) or Emulation (the original pictures); each one's options only show with it
  auto rem = [] { return settings::Num("native_view", 1) == 1; };
  auto emu = [] { return settings::Num("native_view", 1) != 1; };
  auto sharp = [] { return settings::Num("native_view", 1) == 1 && settings::Num("sharp_vectors", 1) != 0; };
  p.Choice("Renderer", "native_view", {"1", "0"}, {"Remaster", "Emulation (original)"});
  p.Choice("  Wide screens", "widescreen", {"1", "0"}, {"Fill the width", "4:3 with bars"})->when = rem;
  p.Choice("  Smooth motion", "native_interpolate", {"1", "0"}, {"On", "Off"})->when = rem;
  p.Choice("  Vector lines", "sharp_vectors", {"1", "0"}, {"Sharp", "Original"})->when = rem;
  p.Number("    Line core width", "sharp_core", 0.1, 1.0, 0.05, "%.2f", 0.35)->when = sharp;
  p.Number("    Glow", "sharp_glow", 0, 1, 0.05, "%.2f", 0.4)->when = sharp;
  p.Choice("  Picture blend (F3)", "picture_blend", {"1", "0"}, {"On", "Off"})->when = emu;
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
    if (controls::FnHeld(held, f)) b |= uint16_t(1u << kBit[f]);
  return b;
}

}  // namespace t3k_menu
