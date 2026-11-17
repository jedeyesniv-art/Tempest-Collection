// crt_menu.cpp - see crt_menu.h. The sliders and defaults are T2K's (the owner's tuning on the ST); crt.cpp reads the
// same keys with the same defaults.
#include "crt_menu.h"

#include "settings.h"

namespace crt_menu {

void Add(menu::Page& p, int max_mode, int def) {
  auto mode = [def] { return int(settings::Num("crt", def)); };
  auto on = [mode] { return mode() >= 1; };
  auto pp_on = [mode] { return mode() == 2; };
  p.Header("CRT");
  menu::Item* look = max_mode >= 2
      ? p.Choice("CRT look", "crt", {"0", "1", "2"}, {"Off", "CRT+", "CRT++ (trails, bleed, glow)"})
      : p.Choice("CRT look", "crt", {"0", "1"}, {"Off", "CRT+"});
  look->def_value = std::to_string(def);
  p.Sub("Fine-tune CRT");
  auto c = [&](const char* l, const char* k, double lo, double hi, double st, const char* f, double d) {
    p.Number(l, k, lo, hi, st, f, d)->when = on;
  };
  c("Scanlines", "crt_scanlines", 0, 1, 0.05, "%.2f", 0.45);
  c("Beam, dark colours", "crt_beam_min", 0, 1, 0.05, "%.2f", 0.35);
  c("Beam, bright colours", "crt_beam_max", 0, 1, 0.05, "%.2f", 1.0);
  c("Softness", "crt_blur", 0, 1, 0.05, "%.2f", 0.4);
  c("Phosphor mask", "crt_mask", 0, 1, 0.05, "%.2f", 0.35);
  c("Mask bleed", "crt_mask_bleed", 0, 1, 0.05, "%.2f", 0.5);
  c("Glow", "crt_halation", 0, 0.2, 0.01, "%.2f", 0.04);
  c("Glow size", "crt_halation_size", 0, 1, 0.02, "%.2f", 0.98);
  c("Curvature", "crt_curvature", 0, 0.15, 0.01, "%.2f", 0.02);
  c("Brightness", "crt_brightness", -0.3, 0.3, 0.01, "%.2f", 0.01);
  c("Contrast", "crt_contrast", 0, 1, 0.05, "%.2f", 0.35);
  c("Saturation", "crt_saturation", 0, 1.5, 0.05, "%.2f", 0.95);
  c("Warmth", "crt_temperature", -1, 1, 0.1, "%.1f", 0.3);
  c("Bloom", "crt_overflow", 0, 1, 0.05, "%.2f", 0.85);
  if (max_mode < 2) return;
  auto pp = [&](const char* l, const char* k, double lo, double hi, double st, const char* f, double d) {
    p.Number(l, k, lo, hi, st, f, d)->when = pp_on;
  };
  pp("CRT++ trails", "crt_psy_trails", 0, 2, 0.05, "%.2f", 0.9);
  pp("CRT++ trail length (s)", "crt_psy_length", 0, 1, 0.01, "%.2f", 0.4);
  pp("CRT++ trail zoom", "crt_psy_zoom", -2, 2, 0.05, "%.2f", 0.25);
  pp("CRT++ trail twist", "crt_psy_twist", -2, 2, 0.05, "%.2f", 0);
  pp("CRT++ trail colour", "crt_psy_hue", 0, 1, 0.02, "%.2f", 0.66);
  pp("CRT++ trail tint", "crt_psy_tint", 0, 1, 0.05, "%.2f", 0.3);
  pp("CRT++ colour cycle", "crt_psy_cycle", 0, 3, 0.05, "%.2f", 0);
  pp("CRT++ trail threshold", "crt_psy_threshold", 0, 1, 0.05, "%.2f", 0.1);
  pp("CRT++ trail feed", "crt_psy_gain", 0, 3, 0.1, "%.1f", 1);
  pp("CRT++ glow boost", "crt_psy_glow", 1, 15, 0.5, "%.1f", 4);
  pp("CRT++ colour bleed", "crt_psy_bleed", 0, 3, 0.1, "%.1f", 0.5);
  pp("CRT++ saturation boost", "crt_psy_saturation", 0.5, 2.5, 0.05, "%.2f", 1.3);
  pp("CRT++ keep colours", "crt_psy_keep", 0, 1, 0.05, "%.2f", 1);
}

}  // namespace crt_menu
