// t2k_menu.cpp - see t2k_menu.h.
#include "t2k_menu.h"

#include "controls.h"
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
  auto crt_on = [] { return int(settings::Num("crt", 2)) >= 1; };
  auto crtpp = [] { return int(settings::Num("crt", 2)) == 2; };

  menu::Page p;
  p.Button("Resume", menu::kClose, true);
  p.Header("GAME");
  p.Button("Return to title", menu::kReturnToTitle, true);
  p.Choice("Save slot", "state_slot", {"1", "2", "3", "4", "5"}, {"1", "2", "3", "4", "5"});
  p.Button("  Save state", menu::kSaveState)->slot_info = true;
  p.Button("  Load state", menu::kLoadState, true)->slot_info = true;
  p.Choice("Camera view", "camera_view", {"0", "1", "2"}, {"Standard", "Whole web", "Close-up"});
  p.Button("Music on / off", kToggleMusic);
  p.Header("STICK");
  p.Choice("Stick mode", "stick_mode", {"lr", "aim"}, {"Left / right", "Aim (point at lane)"});
  p.Number("  Top speed", "lr_max_speed", 0.20, 0.80, 0.01, "%.2f", 0.20)->when = lr;
  p.Number("  Ramp to top speed (frames)", "lr_ramp_frames", 1, 40, 1, "%.0f", 1)->when = lr;
  p.Number("  First-frame speed", "lr_ramp_start", 0, 1, 0.05, "%.2f", 0)->when = lr;
  p.Number("  Deflection curve", "lr_curve", 1, 3, 0.1, "%.1f", 1)->when = lr;
  p.Number("  Deadzone", "lr_deadzone", 0.05, 0.5, 0.01, "%.2f", 0.05)->when = lr;
  p.Number("  Ease-off (frames)", "lr_release_frames", 0, 8, 1, "%.0f", 0)->when = lr;
  p.Number("  Top speed", "aim_max_speed", 0.2, 1.2, 0.05, "%.2f", 0.2)->when = aim;
  p.Number("  Closing speed", "aim_gain", 0.1, 1.0, 0.05, "%.2f", 0.1)->when = aim;
  p.Number("  Push needed to aim", "aim_threshold", 0.3, 0.9, 0.05, "%.2f", 0.3)->when = aim;
  p.Binds();
  p.Header("DISPLAY");
  p.Choice("Original mode (all improvements off)", "native", {"0", "1"}, {"Off", "On"});
  p.Button("Fullscreen on / off", menu::kToggleFullscreen);
  p.Choice("Smooth motion (remaster)", "remaster_interpolate", {"1", "0"}, {"On", "Off (60 fps steps)"});
  p.Choice("Frame rate", "frame_cap", {"0", "60"}, {"Display rate", "60 (lighter: Steam Deck)"});
  p.Choice("Native screen effects (melts, title, menus)", "remaster_feedback", {"1", "0"}, {"On", "Off"});
  p.Number("  Melt wobble", "melt_wobble", 0, 0.04, 0.002, "%.3f", 0.010);
  p.Number("  Melt drip", "melt_drip", 0, 0.03, 0.001, "%.3f", 0.006);
  p.Number("  Melt blob", "melt_blob", 0, 1, 0.05, "%.2f", 0.35);
  p.Number("  Melt spin (degrees a step)", "melt_spin", -4, 4, 0.25, "%.2f", 1.0);
  p.Number("  Melt mosaic", "melt_mosaic", 0, 1, 0.05, "%.2f", 0.15);
  p.Number("  Melt mosaic size", "melt_mosaic_size", 2, 40, 1, "%.0f", 12);
  p.Choice("Pixel art (text, HUD, menus)", "picture_filter", {"sharp", "xbr", "smooth"},
           {"Sharp pixels", "Smoothed (xBR)", "Soft (bilinear)"});
  p.Number("Particle size (x original)", "remaster_particle_size", 0.5, 3.0, 0.1, "%.1f", 1.5);
  p.Number("Lightning width (0 = original)", "remaster_bolt_width", 0.0, 5.0, 0.25, "%.2f", 2.0);
  p.Choice("Lightning glow", "remaster_bolt_glow", {"1", "0"}, {"On", "Off"});
  p.Number("Enemy bullet glow (0 = off)", "remaster_shot_glow", 0.0, 3.0, 0.1, "%.1f", 0.5);
  p.Number("Enemy bullet size (0 = exact)", "remaster_shot_size", 0.0, 2.0, 0.1, "%.1f", 0.5);
  p.Number("Power-up min brightness (0 = original)", "remaster_pup_floor", 0.0, 1.0, 0.05, "%.2f", 0.7);
  p.Number("Power-up glow (0 = off)", "remaster_pup_glow", 0.0, 3.0, 0.1, "%.1f", 1.0);
  p.Header("CHEATS");
  p.Choice("Keypad cheats (Option skips a level, 6 warps)", "cheat_keys", {"0", "1"}, {"Off", "On"});
  p.Choice("Infinite Superzapper", "cheat_inf_zap", {"0", "1"}, {"Off", "On"});
  p.Choice("Unlock Beastly mode", "cheat_beastly", {"0", "1"}, {"Off", "On"});
  p.Choice("Unlock every start level", "cheat_levels", {"0", "1"}, {"Off", "On"});
  p.Button("Warp to a bonus round after this level", kCheatWarp);
  p.Header("CRT (F9 cycles the look)");
  p.Choice("CRT look", "crt", {"0", "1", "2"}, {"Off", "CRT+", "CRT++ (trails, bleed, glow)"});
  auto c = [&](const char* l, const char* k, double lo, double hi, double st, const char* f, double d) {
    p.Number(l, k, lo, hi, st, f, d)->when = crt_on;
  };
  c("  Scanlines", "crt_scanlines", 0, 1, 0.05, "%.2f", 0.45);
  c("  Beam, dark colours", "crt_beam_min", 0, 1, 0.05, "%.2f", 0.35);
  c("  Beam, bright colours", "crt_beam_max", 0, 1, 0.05, "%.2f", 1.0);
  c("  Softness", "crt_blur", 0, 1, 0.05, "%.2f", 0.4);
  c("  Phosphor mask", "crt_mask", 0, 1, 0.05, "%.2f", 0.35);
  c("  Mask bleed", "crt_mask_bleed", 0, 1, 0.05, "%.2f", 0.5);
  c("  Glow", "crt_halation", 0, 0.2, 0.01, "%.2f", 0.04);
  c("  Glow size", "crt_halation_size", 0, 1, 0.02, "%.2f", 0.98);
  c("  Curvature", "crt_curvature", 0, 0.15, 0.01, "%.2f", 0.02);
  c("  Brightness", "crt_brightness", -0.3, 0.3, 0.01, "%.2f", 0.01);
  c("  Contrast", "crt_contrast", 0, 1, 0.05, "%.2f", 0.35);
  c("  Saturation", "crt_saturation", 0, 1.5, 0.05, "%.2f", 0.95);
  c("  Warmth", "crt_temperature", -1, 1, 0.1, "%.1f", 0.3);
  c("  Bloom", "crt_overflow", 0, 1, 0.05, "%.2f", 0.85);
  auto pp = [&](const char* l, const char* k, double lo, double hi, double st, const char* f, double d) {
    p.Number(l, k, lo, hi, st, f, d)->when = crtpp;
  };
  pp("  CRT++ trails", "crt_psy_trails", 0, 2, 0.05, "%.2f", 0.9);
  pp("  CRT++ trail length (s)", "crt_psy_length", 0, 1, 0.01, "%.2f", 0.4);
  pp("  CRT++ trail zoom", "crt_psy_zoom", -2, 2, 0.05, "%.2f", 0.25);
  pp("  CRT++ trail twist", "crt_psy_twist", -2, 2, 0.05, "%.2f", 0);
  pp("  CRT++ trail colour", "crt_psy_hue", 0, 1, 0.02, "%.2f", 0.66);
  pp("  CRT++ trail tint", "crt_psy_tint", 0, 1, 0.05, "%.2f", 0.3);
  pp("  CRT++ colour cycle", "crt_psy_cycle", 0, 3, 0.05, "%.2f", 0);
  pp("  CRT++ trail threshold", "crt_psy_threshold", 0, 1, 0.05, "%.2f", 0.1);
  pp("  CRT++ trail feed", "crt_psy_gain", 0, 3, 0.1, "%.1f", 1);
  pp("  CRT++ glow boost", "crt_psy_glow", 1, 15, 0.5, "%.1f", 4);
  pp("  CRT++ colour bleed", "crt_psy_bleed", 0, 3, 0.1, "%.1f", 0.5);
  pp("  CRT++ saturation boost", "crt_psy_saturation", 0.5, 2.5, 0.05, "%.2f", 1.3);
  pp("  CRT++ keep colours", "crt_psy_keep", 0, 1, 0.05, "%.2f", 1);
  p.Header("");
  p.Button(from_launcher ? "Return to launcher" : "Quit", menu::kQuit, true);
  menu::SetPage("TEMPEST 2000 - SETTINGS", std::move(p));
}

}  // namespace t2k_menu
