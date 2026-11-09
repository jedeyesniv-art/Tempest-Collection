// menu.cpp - see menu.h. Drawn with SDL's built-in 8x8 debug font for now.
#include "menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
  bool lr_only = false, aim_only = false, slot_info = false, reset_binds = false;
};

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

  header("GAME");
  button("Resume", Action::Close);
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
  header("");
  button("Quit", Action::Quit);
  return v;
}

bool g_open = false;
int g_sel = 1;
Nav g_prev;
int g_repeat_dir = 0;      // -1 left / +1 right / -2 up / +2 down
uint64_t g_repeat_at = 0;
controls::Fn g_capture = controls::kNone;
std::string g_toast;
uint64_t g_toast_until = 0;
std::function<std::string()> g_slot_info;

bool Visible(const Item& i) {
  bool aim = settings::Str("stick_mode", "lr") == "aim";
  return !(i.lr_only && aim) && !(i.aim_only && !aim);
}

bool Selectable(const Item& i) { return i.kind != Kind::Header && Visible(i); }

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
void Open() {
  g_open = true;
  g_prev = Nav{true, true, true, true, true, true, true};  // ignore buttons already held
  g_repeat_dir = 0;
  g_capture = controls::kNone;
}
void Close() { g_open = false; g_capture = controls::kNone; }
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

  if (nav.back && !p.back) { g_open = false; return Action::Close; }
  if (nav.ok && !p.ok) {
    const Item& it = items[g_sel];
    if (it.kind == Kind::Bind) { g_capture = it.fn; return Action::None; }
    if (it.kind == Kind::Button) {
      if (it.reset_binds) { controls::ResetDefaults(); return Action::None; }
      if (it.action == Action::Close || it.action == Action::Quit || it.action == Action::ReturnToTitle ||
          it.action == Action::LoadState)
        g_open = false;
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
    else Change(items[g_sel], dir);
  }
  return Action::None;
}

void Draw(SDL_Renderer* ren, int w, int h) {
  auto items = Items();
  int rows = 0;
  for (auto& i : items) rows += Visible(i);
  float cw = 8, lh = 11;
  float bw = 66 * cw, bh = (rows + 4) * lh;
  float scale = std::max(1.f, std::floor(std::min(h / (bh + 40), w / (bw + 40))));
  float x0 = (w / scale - bw) / 2, y0 = (h / scale - bh) / 2;

  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderScale(ren, scale, scale);
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 210);
  SDL_FRect bg{x0 - 12, y0 - 12, bw + 24, bh + 24};
  SDL_RenderFillRect(ren, &bg);
  SDL_SetRenderDrawColor(ren, 60, 90, 255, 255);
  SDL_RenderRect(ren, &bg);

  SDL_SetRenderDrawColor(ren, 255, 220, 0, 255);
  SDL_RenderDebugText(ren, x0, y0, "TEMPEST 2000 - SETTINGS (game paused)");
  float y = y0 + 2 * lh;
  for (size_t k = 0; k < items.size(); ++k) {
    const Item& i = items[k];
    if (!Visible(i)) continue;
    bool sel = int(k) == g_sel;
    if (i.kind == Kind::Header) {
      SDL_SetRenderDrawColor(ren, 90, 140, 255, 255);
      SDL_RenderDebugText(ren, x0, y, i.label.c_str());
    } else {
      if (sel) {
        SDL_SetRenderDrawColor(ren, 60, 60, 160, 255);
        SDL_FRect r{x0 - 4, y - 2, bw + 8, lh};
        SDL_RenderFillRect(ren, &r);
      }
      SDL_SetRenderDrawColor(ren, sel ? 255 : 200, sel ? 255 : 200, sel ? 255 : 200, 255);
      SDL_RenderDebugText(ren, x0 + 8, y, i.label.c_str());
      std::string v = ValueText(i);
      if (!v.empty()) {
        if (sel && (i.kind == Kind::Choice || i.kind == Kind::Number)) v = "< " + v + " >";
        SDL_SetRenderDrawColor(ren, 255, 220, 0, 255);
        SDL_RenderDebugText(ren, x0 + bw - v.size() * cw, y, v.c_str());
      }
    }
    y += lh;
  }
  SDL_SetRenderDrawColor(ren, 140, 140, 140, 255);
  const char* help = g_capture != controls::kNone
                         ? "Press the button to bind (Back cancels). A bound button pressed again is removed."
                         : "Up/Down choose   Left/Right change   A select   B/Back close";
  SDL_RenderDebugText(ren, x0, y + lh, help);
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
