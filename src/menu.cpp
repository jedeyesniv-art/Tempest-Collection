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

std::string g_title = "SETTINGS";
std::vector<Item> g_items;
std::vector<bool> g_open_sections;      // per item index (headers); folded at start

const std::vector<Item>& Items() { return g_items; }

bool g_open = false;
int g_sel = 0;
uint64_t g_changed_at = 0;           // when it last opened / closed (the slide)
constexpr uint64_t kSlideNs = 180000000ull;
float g_scroll = 0;                   // first shown line (in lines)
Nav g_prev;
int g_repeat_dir = 0;      // -1 left / +1 right / -2 up / +2 down
uint64_t g_repeat_at = 0;
int g_capture = 0;                    // the function waiting for a button (0 = none)
std::string g_toast;
uint64_t g_toast_until = 0;
std::function<std::string()> g_slot_info;

bool Visible(const Item& i) { return !i.when || i.when(); }

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

void Page::Header(const std::string& label) {
  Item i;
  i.kind = Kind::Header;
  i.label = label;
  items.push_back(std::move(i));
}

Item* Page::Choice(const std::string& label, const std::string& key, std::vector<std::string> values,
                   std::vector<std::string> names) {
  Item i;
  i.kind = Kind::Choice;
  i.label = label;
  i.key = key;
  i.values = std::move(values);
  i.names = std::move(names);
  items.push_back(std::move(i));
  return &items.back();
}

Item* Page::Number(const std::string& label, const std::string& key, double lo, double hi, double step,
                   const char* fmt, double def) {
  Item i;
  i.kind = Kind::Number;
  i.label = label;
  i.key = key;
  i.lo = lo; i.hi = hi; i.step = step; i.fmt = fmt; i.def = def;
  items.push_back(std::move(i));
  return &items.back();
}

Item* Page::Button(const std::string& label, int action, bool closes) {
  Item i;
  i.kind = Kind::Button;
  i.label = label;
  i.action = action;
  i.closes = closes;
  items.push_back(std::move(i));
  return &items.back();
}

void Page::Binds(const std::string& header) {
  Header(header);
  for (int f = 1; f < controls::FnCount(); ++f) {
    Item i;
    i.kind = Kind::Bind;
    i.label = controls::FnName(f);
    i.fn = f;
    items.push_back(std::move(i));
  }
  Item r;
  r.kind = Kind::ResetBinds;
  r.label = "Reset buttons to defaults";
  items.push_back(std::move(r));
}

void SetPage(const std::string& title, Page page) {
  g_title = title;
  g_items = std::move(page.items);
  int sec = -1;
  for (size_t k = 0; k < g_items.size(); ++k) {
    if (g_items[k].kind == Kind::Header) sec = g_items[k].label.empty() ? -1 : int(k);
    else g_items[k].section = sec;
  }
  g_open_sections.assign(g_items.size(), false);
  g_sel = 0;
}

Nav ReadNav(SDL_Gamepad* pad, const bool* keys) {
  Nav n;
  if (pad) {
    auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
    int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    n.up = btn(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -20000;
    n.down = btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > 20000;
    n.left = btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -20000;
    n.right = btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > 20000;
    n.ok = btn(SDL_GAMEPAD_BUTTON_SOUTH);
    n.back = btn(SDL_GAMEPAD_BUTTON_EAST) || btn(SDL_GAMEPAD_BUTTON_BACK);
    n.cancel = btn(SDL_GAMEPAD_BUTTON_BACK);
  }
  if (keys) {
    n.up |= keys[SDL_SCANCODE_UP];
    n.down |= keys[SDL_SCANCODE_DOWN];
    n.left |= keys[SDL_SCANCODE_LEFT];
    n.right |= keys[SDL_SCANCODE_RIGHT];
    n.ok |= keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_Z];
    n.back |= keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_BACKSPACE];
    n.cancel |= keys[SDL_SCANCODE_ESCAPE];
  }
  return n;
}

bool IsOpen() { return g_open; }
bool Shown() {
  if (g_open) return true;
  return SDL_GetTicksNS() < g_changed_at + kSlideNs;   // sliding out
}
void Open(bool expand_all) {
  if (expand_all) {
    std::fill(g_open_sections.begin(), g_open_sections.end(), true);
  }
  if (!g_open) g_changed_at = SDL_GetTicksNS();
  g_open = true;
  g_prev = Nav{true, true, true, true, true, true, true};  // ignore buttons already held
  g_repeat_dir = 0;
  g_capture = 0;
}
void Close() {
  if (g_open) g_changed_at = SDL_GetTicksNS();
  g_open = false;
  g_capture = 0;
}
void SetSlotInfo(std::function<std::string()> f) { g_slot_info = std::move(f); }

int Update(const Nav& nav, uint64_t now) {
  const auto& items = Items();
  if (items.empty()) return kNone;
  if (!Selectable(items[g_sel])) Move(items, 1);
  Nav p = g_prev;
  g_prev = nav;

  if (g_capture != 0) {            // waiting for a button to bind
    if (nav.cancel && !p.cancel) g_capture = 0;
    else if (nav.new_button >= 0) {
      controls::Assign(g_capture, nav.new_button);
      g_capture = 0;
    }
    return kNone;
  }

  if (nav.back && !p.back) { Close(); return kClose; }
  if (nav.ok && !p.ok) {
    const Item& it = items[g_sel];
    if (it.kind == Kind::Header) { g_open_sections[g_sel] = !g_open_sections[g_sel]; return kNone; }
    if (it.kind == Kind::Bind) { g_capture = it.fn; return kNone; }
    if (it.kind == Kind::ResetBinds) { controls::ResetDefaults(); return kNone; }
    if (it.kind == Kind::Button) {
      if (it.closes) Close();
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
  return kNone;
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
  const auto& items = Items();
  if (items.empty()) return;
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
  SDL_RenderDebugText(ren, x0, pad, g_title.c_str());
  SDL_SetRenderDrawColor(ren, 140, 140, 140, 255);
  SDL_RenderDebugText(ren, x0, pad + lh, "(game paused)");
  const std::string help = g_capture != 0
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
