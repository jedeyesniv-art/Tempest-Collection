// menu.cpp - see menu.h. A panel sliding in from the left (half the window, semi-transparent, solid text), drawn
// with SDL's built-in 8x8 debug font at a whole-number scale; sections fold ([+] / [-]), all folded at start.
#include "menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "controls.h"
#include "settings.h"

namespace menu {
namespace {

enum class Kind { Header, Choice, Number, Button, Bind };

struct Item {
  Kind kind;
  std::string label;
  std::string key;                      // settings key (Choice / Number)
  std::vector<std::string> values;      // Choice: stored values
  std::vector<std::string> names;       // Choice: shown names
  double lo = 0, hi = 0, step = 0;      // Number
  double def = -1e30;                   // Number: the value when the setting is missing (unset: lo)
  const char* fmt = "%.2f";
  Action action = Action::None;         // Button
  controls::Fn fn = controls::kNone;    // Bind
  bool crt_only = false, crtpp_only = false;   // shown with a CRT look / with CRT++ only
  bool lr_only = false, aim_only = false, slot_info = false, reset_binds = false;
  int section = -1;                     // the header it belongs to (index into the item list; -1 = none)
};

std::vector<bool> g_open_sections;      // per header index; folded at start

std::vector<Item> Items() {
  std::vector<Item> v;
  auto header = [&](const char* t) { v.push_back({Kind::Header, t}); };
  auto num = [&](const char* label, const char* key, double lo, double hi, double step, const char* fmt,
                 double def = -1e30) {
    Item i{Kind::Number, label, key};
    i.lo = lo; i.hi = hi; i.step = step; i.fmt = fmt; i.def = def > -1e29 ? def : lo;
    v.push_back(i);
    return &v.back();
  };
  auto choice = [&](const char* label, const char* key, std::vector<std::string> vals,
                    std::vector<std::string> names) {
    Item i{Kind::Choice, label, key};
    i.values = std::move(vals); i.names = std::move(names);
    v.push_back(i);
  };
  auto button = [&](const char* label, Action a) {
    Item i{Kind::Button, label};
    i.action = a;
    v.push_back(i);
    return &v.back();
  };

  button("Resume", Action::Close);
  header("GAME");
  button("Return to title", Action::ReturnToTitle);
  choice("Save slot", "state_slot", {"1", "2", "3", "4", "5"}, {"1", "2", "3", "4", "5"});
  button("  Save state", Action::SaveState)->slot_info = true;
  button("  Load state", Action::LoadState)->slot_info = true;
  choice("Camera view", "camera_view", {"0", "1", "2"}, {"Standard", "Whole web", "Close-up"});
  button("Music on / off", Action::ToggleMusic);
  header("STICK");
  choice("Stick mode", "stick_mode", {"lr", "aim"}, {"Left / right", "Aim (point at lane)"});
  num("  Top speed", "lr_max_speed", 0.20, 0.80, 0.01, "%.2f")->lr_only = true;
  num("  Ramp to top speed (frames)", "lr_ramp_frames", 1, 40, 1, "%.0f")->lr_only = true;
  num("  First-frame speed", "lr_ramp_start", 0, 1, 0.05, "%.2f")->lr_only = true;
  num("  Deflection curve", "lr_curve", 1, 3, 0.1, "%.1f")->lr_only = true;
  num("  Deadzone", "lr_deadzone", 0.05, 0.5, 0.01, "%.2f")->lr_only = true;
  num("  Ease-off (frames)", "lr_release_frames", 0, 8, 1, "%.0f")->lr_only = true;
  num("  Top speed", "aim_max_speed", 0.2, 1.2, 0.05, "%.2f")->aim_only = true;
  num("  Closing speed", "aim_gain", 0.1, 1.0, 0.05, "%.2f")->aim_only = true;
  num("  Push needed to aim", "aim_threshold", 0.3, 0.9, 0.05, "%.2f")->aim_only = true;
  header("BUTTONS (A on a function, then press a button; again removes it)");
  for (int f = controls::kFire; f < controls::kFnCount; ++f) {
    Item i{Kind::Bind, controls::FnName(controls::Fn(f))};
    i.fn = controls::Fn(f);
    v.push_back(i);
  }
  button("Reset buttons to defaults", Action::None)->reset_binds = true;
  header("DISPLAY");
  choice("Original mode (all improvements off)", "native", {"0", "1"}, {"Off", "On"});
  button("Fullscreen on / off", Action::ToggleFullscreen);
  choice("Smooth motion (remaster)", "remaster_interpolate", {"1", "0"}, {"On", "Off (60 fps steps)"});
  choice("Pixel art (text, HUD, menus)", "picture_filter", {"sharp", "xbr", "smooth"},
         {"Sharp pixels", "Smoothed (xBR)", "Soft (bilinear)"});
  num("Particle size (x original)", "remaster_particle_size", 0.5, 3.0, 0.1, "%.1f", 1.5);
  num("Lightning width (0 = original)", "remaster_bolt_width", 0.0, 5.0, 0.25, "%.2f", 2.0);
  choice("Lightning glow", "remaster_bolt_glow", {"1", "0"}, {"On", "Off"});
  header("CRT (F9 cycles the look)");
  choice("CRT look", "crt", {"0", "1", "2"}, {"Off", "CRT+", "CRT++ (trails, bleed, glow)"});
  auto c = [&](const char* l, const char* k, double lo, double hi, double st, const char* f, double d) {
    num(l, k, lo, hi, st, f, d)->crt_only = true;
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
    num(l, k, lo, hi, st, f, d)->crtpp_only = true;
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
  header("");
  button("Quit", Action::Quit);
  int sec = -1;
  for (size_t k = 0; k < v.size(); ++k) {
    if (v[k].kind == Kind::Header) sec = v[k].label.empty() ? -1 : int(k);
    else v[k].section = sec;
  }
  if (g_open_sections.size() != v.size()) g_open_sections.assign(v.size(), false);
  return v;
}

bool g_open = false;
int g_sel = 0;
uint64_t g_changed_at = 0;           // when it last opened / closed (the slide)
constexpr uint64_t kSlideNs = 180000000ull;
float g_scroll = 0;                   // first shown line (in lines)
Nav g_prev;
int g_repeat_dir = 0;      // -1 left / +1 right / -2 up / +2 down
uint64_t g_repeat_at = 0;
controls::Fn g_capture = controls::kNone;
std::string g_toast;
uint64_t g_toast_until = 0;
std::function<std::string()> g_slot_info;

bool Visible(const Item& i) {
  bool aim = settings::Str("stick_mode", "lr") == "aim";
  int crt = int(settings::Num("crt", 2));
  return !(i.lr_only && aim) && !(i.aim_only && !aim) && !(i.crt_only && crt < 1) && !(i.crtpp_only && crt != 2);
}

bool Open(int section) { return section < 0 || (section < int(g_open_sections.size()) && g_open_sections[section]); }
bool Shown(const Item& i) {
  if (i.kind == Kind::Header) return !i.label.empty();
  return Visible(i) && Open(i.section);
}
bool Selectable(const Item& i) { return Shown(i); }

std::string ValueText(const Item& i) {
  if (i.kind == Kind::Choice) {
    std::string cur = settings::Str(i.key, i.values[0]);
    for (size_t k = 0; k < i.values.size(); ++k)
      if (i.values[k] == cur) return i.names[k];
    return cur;
  }
  if (i.kind == Kind::Number) {
    char buf[32];
    std::snprintf(buf, sizeof buf, i.fmt, settings::Num(i.key, i.def));
    return buf;
  }
  if (i.kind == Kind::Bind) return g_capture == i.fn ? "press a button..." : controls::ButtonsFor(i.fn);
  if (i.slot_info && g_slot_info) return g_slot_info();
  return "";
}

void Change(const Item& i, int dir) {
  if (i.kind == Kind::Choice) {
    std::string cur = settings::Str(i.key, i.values[0]);
    int k = 0;
    for (size_t n = 0; n < i.values.size(); ++n)
      if (i.values[n] == cur) k = int(n);
    k = (k + dir + int(i.values.size())) % int(i.values.size());
    settings::Save(i.key, i.values[k]);
  } else if (i.kind == Kind::Number) {
    double v = settings::Num(i.key, i.def) + dir * i.step;
    v = std::clamp(std::round(v / i.step) * i.step, i.lo, i.hi);
    char buf[32];
    std::snprintf(buf, sizeof buf, i.fmt, v);
    settings::Save(i.key, buf);
  }
}

void Move(const std::vector<Item>& items, int dir) {
  int n = int(items.size());
  for (int k = 1; k <= n; ++k) {
    int s = ((g_sel + dir * k) % n + n) % n;
    if (Selectable(items[s])) { g_sel = s; return; }
  }
}

}  // namespace

bool IsOpen() { return g_open; }
bool Shown() {
  if (g_open) return true;
  return SDL_GetTicksNS() < g_changed_at + kSlideNs;   // sliding out
}
void Open(bool expand_all) {
  if (expand_all) {
    Items();
    std::fill(g_open_sections.begin(), g_open_sections.end(), true);
  }
  if (!g_open) g_changed_at = SDL_GetTicksNS();
  g_open = true;
  g_prev = Nav{true, true, true, true, true, true, true};  // ignore buttons already held
  g_repeat_dir = 0;
  g_capture = controls::kNone;
}
void Close() {
  if (g_open) g_changed_at = SDL_GetTicksNS();
  g_open = false;
  g_capture = controls::kNone;
}
void SetSlotInfo(std::function<std::string()> f) { g_slot_info = std::move(f); }

Action Update(const Nav& nav, uint64_t now) {
  auto items = Items();
  if (!Selectable(items[g_sel])) Move(items, 1);
  Nav p = g_prev;
  g_prev = nav;

  if (g_capture != controls::kNone) {            // waiting for a button to bind
    if (nav.cancel && !p.cancel) g_capture = controls::kNone;
    else if (nav.new_button >= 0) {
      controls::Assign(g_capture, nav.new_button);
      g_capture = controls::kNone;
    }
    return Action::None;
  }

  if (nav.back && !p.back) { Close(); return Action::Close; }
  if (nav.ok && !p.ok) {
    const Item& it = items[g_sel];
    if (it.kind == Kind::Header) { g_open_sections[g_sel] = !g_open_sections[g_sel]; return Action::None; }
    if (it.kind == Kind::Bind) { g_capture = it.fn; return Action::None; }
    if (it.kind == Kind::Button) {
      if (it.reset_binds) { controls::ResetDefaults(); return Action::None; }
      if (it.action == Action::Close || it.action == Action::Quit || it.action == Action::ReturnToTitle ||
          it.action == Action::LoadState)
        Close();
      return it.action;
    }
    Change(it, 1);
  }
  int dir = nav.up ? -2 : nav.down ? 2 : nav.left ? -1 : nav.right ? 1 : 0;
  bool fire = false;
  if (dir != g_repeat_dir) {
    g_repeat_dir = dir;
    // A fresh press only when it was not already held when the menu opened.
    bool was = (dir == -2 && p.up) || (dir == 2 && p.down) || (dir == -1 && p.left) || (dir == 1 && p.right);
    fire = dir != 0 && !was;
    g_repeat_at = now + 350000000ull;
  } else if (dir != 0 && now >= g_repeat_at) {
    fire = true;
    g_repeat_at = now + 70000000ull;
  }
  if (fire) {
    if (dir == -2 || dir == 2) Move(items, dir / 2);
    else if (items[g_sel].kind == Kind::Header) g_open_sections[g_sel] = dir > 0;   // right opens, left folds
    else Change(items[g_sel], dir);
  }
  return Action::None;
}

// Word-wraps text to lines of at most `cols` characters (long words are cut).
std::vector<std::string> Wrap(const std::string& text, int cols) {
  std::vector<std::string> out;
  if (cols < 1) cols = 1;
  std::string line, word;
  auto flush_word = [&]() {
    while (int(word.size()) > cols) {
      if (!line.empty()) out.push_back(line), line.clear();
      out.push_back(word.substr(0, size_t(cols)));
      word.erase(0, size_t(cols));
    }
    if (word.empty()) return;
    if (line.empty()) line = word;
    else if (int(line.size() + 1 + word.size()) <= cols) line += " " + word;
    else out.push_back(line), line = word;
    word.clear();
  };
  for (char c : text) {
    if (c == ' ') flush_word();
    else word.push_back(c);
  }
  flush_word();
  if (!line.empty() || out.empty()) out.push_back(line);
  return out;
}

void Draw(SDL_Renderer* ren, int w, int h) {
  auto items = Items();
  // Slide: eased in over kSlideNs when opening, out when closing.
  uint64_t now = SDL_GetTicksNS();
  float t = std::clamp(float(double(now - g_changed_at) / double(kSlideNs)), 0.f, 1.f);
  float shown = g_open ? t : 1 - t;
  shown = 1 - (1 - shown) * (1 - shown);   // ease out
  if (shown <= 0) return;

  const float s = std::max(1.f, std::floor(h / 360.f));   // text scale: whole pixels
  const float cw = 8, lh = 12;                             // character / line size (unscaled)
  const float pw = std::floor(w * 0.5f / s);               // panel width, unscaled
  const float ph = h / s;
  const float pad = 10;
  const float ox = -pw * (1 - shown);                      // the slide
  const int cols = std::max(10, int((pw - 2 * pad) / cw));

  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderScale(ren, s, s);
  SDL_SetRenderDrawColor(ren, 0, 0, 16, 200);
  SDL_FRect bg{ox, 0, pw, ph};
  SDL_RenderFillRect(ren, &bg);
  SDL_SetRenderDrawColor(ren, 60, 90, 255, 255);
  SDL_FRect edge{ox + pw - 1, 0, 1, ph};
  SDL_RenderFillRect(ren, &edge);

  // Title (fixed) and help (fixed, wrapped, at the bottom).
  float x0 = ox + pad;
  SDL_SetRenderDrawColor(ren, 255, 220, 0, 255);
  SDL_RenderDebugText(ren, x0, pad, "TEMPEST 2000 - SETTINGS");
  SDL_SetRenderDrawColor(ren, 140, 140, 140, 255);
  SDL_RenderDebugText(ren, x0, pad + lh, "(game paused)");
  const std::string help = g_capture != controls::kNone
      ? "Press the button to bind (Back cancels). A bound button pressed again is removed."
      : "Up / Down choose. Left / Right change, or fold / open a section. A selects. B / Back closes.";
  auto help_lines = Wrap(help, cols);
  const float list_top = pad + 3 * lh, list_bottom = ph - pad - (help_lines.size() + 1) * lh;
  for (size_t k = 0; k < help_lines.size(); ++k)
    SDL_RenderDebugText(ren, x0, ph - pad - float(help_lines.size() - k) * lh, help_lines[k].c_str());

  // Lay out the shown items: label wrapped beside a right-aligned value; a long value gets its own line.
  struct Row { int item; std::vector<std::string> label; std::string value; bool value_below; };
  std::vector<Row> rows;
  int sel_line0 = 0, sel_lines = 1, total = 0;
  for (size_t k = 0; k < items.size(); ++k) {
    const Item& it = items[k];
    if (!Shown(it)) continue;
    Row r{int(k), {}, "", false};
    bool sel = int(k) == g_sel;
    if (it.kind == Kind::Header) {
      bool open = g_open_sections[k];
      r.label = Wrap(std::string(open ? "[-] " : "[+] ") + it.label, cols);
    } else {
      r.value = ValueText(it);
      if (sel && !r.value.empty() && (it.kind == Kind::Choice || it.kind == Kind::Number)) r.value = "< " + r.value + " >";
      int vcols = int(r.value.size());
      int lcols = cols - 2 - (vcols ? vcols + 2 : 0);   // (2: the indent)
      if (vcols && lcols < cols / 2) { r.value_below = true; lcols = cols - 2; }
      r.label = Wrap(it.label, lcols);
    }
    int n = int(r.label.size()) + (r.value_below ? 1 : 0);
    if (sel) sel_line0 = total, sel_lines = n;
    total += n;
    rows.push_back(std::move(r));
  }
  // Scroll so the selection is in view.
  const int view_lines = std::max(1, int((list_bottom - list_top) / lh));
  if (sel_line0 < g_scroll) g_scroll = float(sel_line0);
  if (sel_line0 + sel_lines > g_scroll + view_lines) g_scroll = float(sel_line0 + sel_lines - view_lines);
  g_scroll = std::clamp(g_scroll, 0.f, float(std::max(0, total - view_lines)));

  float y = list_top - g_scroll * lh;
  for (const Row& r : rows) {
    const Item& it = items[r.item];
    int n = int(r.label.size()) + (r.value_below ? 1 : 0);
    bool sel = r.item == g_sel;
    auto in_view = [&](float ly) { return ly >= list_top - 0.5f && ly + lh <= list_bottom + 0.5f; };
    if (sel) {
      SDL_SetRenderDrawColor(ren, 60, 60, 160, 255);
      float y0 = std::max(y - 2, list_top - 2), y1 = std::min(y - 2 + n * lh, list_bottom);
      SDL_FRect hl{ox + 2, y0, pw - 4, y1 - y0};
      if (y1 > y0) SDL_RenderFillRect(ren, &hl);
    }
    float lx = x0 + (it.kind == Kind::Header || it.section < 0 ? 0 : 2 * cw);
    if (it.kind == Kind::Header) SDL_SetRenderDrawColor(ren, 110, 160, 255, 255);
    else SDL_SetRenderDrawColor(ren, sel ? 255 : 210, sel ? 255 : 210, sel ? 255 : 210, 255);
    for (size_t k = 0; k < r.label.size(); ++k)
      if (in_view(y + k * lh)) SDL_RenderDebugText(ren, lx, y + k * lh, r.label[k].c_str());
    if (!r.value.empty()) {
      SDL_SetRenderDrawColor(ren, 255, 220, 0, 255);
      float vy = r.value_below ? y + r.label.size() * lh : y;
      if (in_view(vy)) SDL_RenderDebugText(ren, ox + pw - pad - r.value.size() * cw, vy, r.value.c_str());
    }
    y += n * lh;
  }
  // Scroll hints.
  SDL_SetRenderDrawColor(ren, 110, 160, 255, 255);
  if (g_scroll > 0) SDL_RenderDebugText(ren, ox + pw - pad - cw, list_top - lh, "^");
  if (g_scroll + view_lines < total) SDL_RenderDebugText(ren, ox + pw - pad - cw, list_bottom, "v");
  SDL_SetRenderScale(ren, 1, 1);
}

void Toast(const std::string& text, uint64_t now) {
  g_toast = text;
  g_toast_until = now + 1500000000ull;
}

void DrawToast(SDL_Renderer* ren, int w, int h, uint64_t now) {
  if (now >= g_toast_until || g_toast.empty()) return;
  float scale = std::max(1.f, std::floor(h / 360.f));
  float tw = g_toast.size() * 8.f;
  float x = (w / scale - tw) / 2, y = h / scale - 40;
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderScale(ren, scale, scale);
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
  SDL_FRect r{x - 8, y - 6, tw + 16, 20};
  SDL_RenderFillRect(ren, &r);
  SDL_SetRenderDrawColor(ren, 255, 220, 0, 255);
  SDL_RenderDebugText(ren, x, y, g_toast.c_str());
  SDL_SetRenderScale(ren, 1, 1);
}

}  // namespace menu
