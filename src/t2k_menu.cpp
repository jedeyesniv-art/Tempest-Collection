// t2k_menu.cpp - see t2k_menu.h.
#include "t2k_menu.h"

#include "controls.h"
#include "crt_menu.h"
#include "settings.h"

namespace t2k_menu {

void Install(bool from_launcher) {
  // Owner's defaults: A fire, B / RB Superzapper, X / LT / RT jump, Y Option, LB camera, Start pause.
  //                                  a      b     x      y        lb       rb    lt     rt     start   ls     rs
  const int defaults[controls::kButtonCount] = {kFire, kZap, kJump, kOption, kCamera, kZap, kJump, kJump, kPause, kNone, kNone};
  controls::Setup({{"fire", "Fire"}, {"jump", "Jump"}, {"zap", "Superzapper"}, {"option", "Option"},
                   {"pause", "Pause"}, {"camera", "Camera view"}},
                  defaults);

  auto aim = [] { return settings::Str("stick_mode", "lr") == "aim"; };
  auto lr = [aim] { return !aim(); };

  menu::SetAccent(1.f, 200 / 255.f, 40 / 255.f);   // the launcher's colour for this game (launcher_view.cpp kStyles)
  menu::Page p;
  p.Button("Resume", menu::kClose, true);
  p.Header("GAME");
  p.Button("Return to title", menu::kReturnToTitle, true);
  p.Choice("Save slot", "state_slot", {"1", "2", "3", "4", "5"}, {"1", "2", "3", "4", "5"});
  p.Button("  Save state", menu::kSaveState)->slot_info = true;
  p.Button("  Load state", menu::kLoadState, true)->slot_info = true;
  p.Choice("Camera view", "camera_view", {"0", "1", "2"}, {"Standard", "Whole web", "Close-up"});
  p.Button("Music on / off", kToggleMusic);
  p.Header("CONTROLS");
  p.Choice("Stick mode", "stick_mode", {"lr", "aim"}, {"Left / right", "Aim (point at lane)"});
  p.Sub("Fine-tune stick");
  p.Number("Top speed", "lr_max_speed", 0.20, 0.80, 0.01, "%.2f", 0.20)->when = lr;
  p.Number("Ramp to top speed (frames)", "lr_ramp_frames", 1, 40, 1, "%.0f", 1)->when = lr;
  p.Number("First-frame speed", "lr_ramp_start", 0, 1, 0.05, "%.2f", 0)->when = lr;
  p.Number("Deflection curve", "lr_curve", 1, 3, 0.1, "%.1f", 1)->when = lr;
  p.Number("Deadzone", "lr_deadzone", 0.05, 0.5, 0.01, "%.2f", 0.05)->when = lr;
  p.Number("Ease-off (frames)", "lr_release_frames", 0, 8, 1, "%.0f", 0)->when = lr;
  p.Number("Top speed", "aim_max_speed", 0.2, 1.2, 0.05, "%.2f", 0.2)->when = aim;
  p.Number("Closing speed", "aim_gain", 0.1, 1.0, 0.05, "%.2f", 0.1)->when = aim;
  p.Number("Push needed to aim", "aim_threshold", 0.3, 0.9, 0.05, "%.2f", 0.3)->when = aim;
  p.Binds();
  p.Header("PICTURE");
  p.Button("Fullscreen", menu::kToggleFullscreen);
  p.Choice("Frame rate", "frame_cap", {"0", "60"}, {"Display rate", "60 (lighter: Steam Deck)"});
  // Remaster (ours) or Emulation (the emulator's own picture: every picture improvement off, controls stay) - named
  // as in T3K; the remaster's options only show with it
  auto rem = [] { return settings::Num("native", 0) == 0; };
  p.Choice("Renderer", "native", {"0", "1"}, {"Remaster", "Emulation (original)"});
  p.Choice("  Smooth motion", "remaster_interpolate", {"1", "0"}, {"On", "Off"})->when = rem;
  p.Choice("  Native screen effects (melts, title, menus)", "remaster_feedback", {"1", "0"}, {"On", "Off"})->when = rem;
  p.Choice("  Pixel art (text, HUD, menus)", "picture_filter", {"sharp", "xbr", "smooth"},
           {"Sharp pixels", "Smoothed (xBR)", "Soft (bilinear)"})->when = rem;
  p.Sub("Fine-tune look");
  p.Number("Particle size (x original)", "remaster_particle_size", 0.5, 3.0, 0.1, "%.1f", 1.5)->when = rem;
  p.Number("Lightning width (0 = original)", "remaster_bolt_width", 0.0, 5.0, 0.25, "%.2f", 2.0)->when = rem;
  p.Choice("Lightning glow", "remaster_bolt_glow", {"1", "0"}, {"On", "Off"})->when = rem;
  p.Number("Enemy bullet glow (0 = off)", "remaster_shot_glow", 0.0, 3.0, 0.1, "%.1f", 0.5)->when = rem;
  p.Number("Enemy bullet size (0 = exact)", "remaster_shot_size", 0.0, 2.0, 0.1, "%.1f", 0.5)->when = rem;
  p.Number("Power-up min brightness (0 = original)", "remaster_pup_floor", 0.0, 1.0, 0.05, "%.2f", 0.7)->when = rem;
  p.Number("Power-up glow (0 = off)", "remaster_pup_glow", 0.0, 3.0, 0.1, "%.1f", 1.0)->when = rem;
  crt_menu::Add(p, 2, 2);   // T2K: CRT+ and CRT++, CRT++ by default
  p.Header("CHEATS");
  p.Choice("Keypad cheats (Option skips a level, 6 warps)", "cheat_keys", {"0", "1"}, {"Off", "On"});
  p.Choice("Infinite Superzapper", "cheat_inf_zap", {"0", "1"}, {"Off", "On"});
  p.Choice("Unlock Beastly mode", "cheat_beastly", {"0", "1"}, {"Off", "On"});
  p.Choice("Unlock every start level", "cheat_levels", {"0", "1"}, {"Off", "On"});
  p.Button("Warp to a bonus round after this level", kCheatWarp);
  p.Header("");
  p.Button(from_launcher ? "Return to launcher" : "Quit", menu::kQuit, true);
  menu::SetPage("TEMPEST 2000 - SETTINGS", std::move(p));
}

}  // namespace t2k_menu
