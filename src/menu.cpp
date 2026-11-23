// menu.cpp - see menu.h. A panel sliding in from the left (half the window or more), drawn in the launcher's look
// (the glowing pixel font, pixfont.h); sections fold (chevrons), all folded at start.
#include "menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "controls.h"
#include "pixfont.h"
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
  if (i.kind == Kind::Header) return !i.label.empty() && Visible(i);
  if (!Visible(i) || !Open(i.section)) return false;
  if (i.kind == Kind::Sub) {   // only while something in it can show
    if (i.label.empty()) return false;
    const int k = int(&i - g_items.data());
    for (size_t j = size_t(k) + 1; j < g_items.size() && g_items[j].sub == k; ++j)
      if (Visible(g_items[j])) return true;
    return false;
  }
  return i.sub < 0 || Open(i.sub);
}
bool Folds(const Item& i) { return i.kind == Kind::Header || i.kind == Kind::Sub; }
SDL_Window* g_window = nullptr;   // the window drawn into last (Fullscreen's On / Off)
bool IsFullscreenItem(const Item& i) { return i.kind == Kind::Button && i.action == kToggleFullscreen; }
bool Selectable(const Item& i) { return Shown(i); }

const std::string& DefaultValue(const Item& i) { return i.def_value.empty() ? i.values[0] : i.def_value; }

std::string ValueText(const Item& i) {
  if (i.kind == Kind::Choice) {
    std::string cur = settings::Str(i.key, DefaultValue(i));
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
  if (IsFullscreenItem(i) && g_window) return (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) ? "On" : "Off";
  if (i.slot_info && g_slot_info) return g_slot_info();
  return "";
}

void Change(const Item& i, int dir) {
  if (i.kind == Kind::Choice) {
    std::string cur = settings::Str(i.key, DefaultValue(i));
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

void Page::Sub(const std::string& label) {
  Item i;
  i.kind = Kind::Sub;
  i.label = label;
  items.push_back(std::move(i));
}

void Page::Binds(const std::string& label) {
  Sub(label);
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
  int sec = -1, sub = -1;
  for (size_t k = 0; k < g_items.size(); ++k) {
    Item& it = g_items[k];
    if (it.kind == Kind::Header) {
      sec = it.label.empty() ? -1 : int(k);
      sub = -1;
    } else if (it.kind == Kind::Sub) {
      it.section = sec;
      sub = it.label.empty() ? -1 : int(k);
    } else {
      it.section = sec;
      it.sub = sub;
    }
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
  if (expand_all) {   // (tests) every section open; the folds inside too with MENU_ALL_SUBS=1
    const bool subs = std::getenv("MENU_ALL_SUBS") != nullptr;
    for (size_t k = 0; k < g_items.size(); ++k)
      if (g_items[k].kind == Kind::Header || (subs && g_items[k].kind == Kind::Sub)) g_open_sections[k] = true;
  }
  if (!g_open) g_changed_at = SDL_GetTicksNS();
  g_open = true;
  g_prev = Nav{true, true, true, true, true, true, true};  // ignore buttons already held
  g_repeat_dir = 0;
  g_capture = 0;
  // Tests: MENU_SELECT=N starts with the selection N rows down (screenshots of a selected value).
  if (const char* e = std::getenv("MENU_SELECT"))
    for (int k = std::atoi(e); k > 0; --k) Move(g_items, 1);
}
void Close() {
  if (g_open) g_changed_at = SDL_GetTicksNS();
  g_open = false;
  g_capture = 0;
}
void SetSlotInfo(std::function<std::string()> f) { g_slot_info = std::move(f); }

int Update(const Nav& nav, uint64_t now) {
  for (Item& it : g_items)
    if (it.refresh) it.refresh(it);
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
    if (Folds(it)) { g_open_sections[g_sel] = !g_open_sections[g_sel]; return kNone; }
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
    const bool fresh = g_repeat_at == now + 350000000ull;   // (not a held repeat)
    if (dir == -2 || dir == 2) Move(items, dir / 2);
    else if (Folds(items[g_sel])) g_open_sections[g_sel] = dir > 0;   // right opens, left folds
    else if (IsFullscreenItem(items[g_sel])) { if (fresh) return kToggleFullscreen; }
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

// The look (2026-11-17, the launcher's): the glowing 5x7 pixel font (pixfont), a dark panel with the game's launcher
// colour (SetAccent) for its edge, title, section names, values and the selection. Sizes in font units u.
namespace {
pixfont::Col g_accent{0.35f, 0.6f, 1.f};
bool g_paused_note = true;   // "game paused" next to the subtitle

pixfont::Col Mix(pixfont::Col a, pixfont::Col b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
void FillRect(SDL_Renderer* ren, float x, float y, float w, float h, pixfont::Col c, float a) {
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColorFloat(ren, c.r, c.g, c.b, a);
  const SDL_FRect r{x, y, w, h};
  SDL_RenderFillRect(ren, &r);
}
float Unit(int h) { return std::max(2.f, h / 300.f); }
// Characters in from the left: sections' items 2, a fold's items 4.
int Indent(const Item& i) { return i.kind == Kind::Header || i.section < 0 ? 0 : i.sub >= 0 ? 4 : 2; }
}  // namespace

void SetAccent(float r, float g, float b) { g_accent = {r, g, b}; }
void SetPausedNote(bool on) { g_paused_note = on; }

void Draw(SDL_Renderer* ren, int w, int h) {
  for (Item& it : g_items)
    if (it.refresh) it.refresh(it);
  const auto& items = Items();
  if (items.empty()) return;
  g_window = SDL_GetRenderWindow(ren);
  // Slide: eased in over kSlideNs when opening, out when closing.
  uint64_t now = SDL_GetTicksNS();
  float t = std::clamp(float(double(now - g_changed_at) / double(kSlideNs)), 0.f, 1.f);
  float shown = g_open ? t : 1 - t;
  shown = 1 - (1 - shown) * (1 - shown);   // ease out
  if (shown <= 0) return;
  const float secs = float(double(now % 100000000000ull) / 1e9);

  const float u = Unit(h);                    // one font pixel
  const float cw = 6 * u, lh = 11 * u;        // character advance / line height
  const float pad = 7 * u;
  const float pw = std::floor(std::min(float(w), std::max(w * 0.5f, 44 * cw + 2 * pad)));   // panel width
  const float ox = -pw * (1 - shown);         // the slide
  const float x0 = ox + pad;
  const int cols = std::max(10, int((pw - 2 * pad) / cw));
  const pixfont::Col white{1, 1, 1}, label_col{0.80f, 0.83f, 0.90f}, dim{0.55f, 0.58f, 0.66f};
  const pixfont::Col value_col = Mix(g_accent, white, 0.35f);

  // The panel: dark, a faint wash of the accent at the top, a glowing edge.
  FillRect(ren, ox, 0, pw, float(h), {0.012f, 0.014f, 0.035f}, 0.90f);
  const float band = pad + 10.5f * u + lh * 1.6f;
  for (int k = 0; k < 8; ++k)   // the wash, fading downwards
    FillRect(ren, ox, band * k / 8.f, pw, band / 8.f, g_accent, 0.10f * (1 - k / 8.f));
  pixfont::Glow(ren, ox + pw - u * 0.6f, 0, ox + pw - u * 0.6f, float(h), u * 0.5f, g_accent, 0.9f, 6.f);

  // Title: the game (big) and "settings - game paused" under it; a glowing rule.
  std::string name = g_title, sub = "SETTINGS";
  if (size_t d = g_title.find(" - "); d != std::string::npos) name = g_title.substr(0, d), sub = g_title.substr(d + 3);
  float tu = u * 1.5f;
  if (pixfont::TextWidth(name, tu) > pw - 2 * pad) tu = u;
  pixfont::Text(ren, name, x0, pad, tu, g_accent, 1.f, -1, 1.3f);
  const float sub_y = pad + 7 * tu + 3.5f * u;
  pixfont::Text(ren, g_paused_note ? sub + "  -  game paused" : sub, x0, sub_y, u, dim, 0.9f, -1, 0.f);
  const float rule_y = sub_y + 7 * u + 5 * u;
  pixfont::Glow(ren, x0, rule_y, ox + pw - pad, rule_y, u * 0.25f, g_accent, 0.55f, 5.f);

  // Help at the bottom (wrapped), under its own faint rule.
  const Kind sel_kind = items[g_sel].kind;
  const std::string help = g_capture != 0
      ? "Press the button to bind (Back cancels). A bound button pressed again is removed."
      : sel_kind == Kind::Bind
      ? "A, then press a button to bind it to this (pressing a bound one again removes it). B / Back closes."
      : "Up / Down choose. Left / Right change, or fold / open a section. A selects. B / Back closes.";
  auto help_lines = Wrap(help, cols);
  const float help_top = h - pad - float(help_lines.size()) * lh + 2 * u;
  for (size_t k = 0; k < help_lines.size(); ++k)
    pixfont::Text(ren, help_lines[k], x0, help_top + float(k) * lh, u, dim, 0.8f, -1, 0.f);
  const float help_rule = help_top - lh * 0.6f;
  pixfont::Glow(ren, x0, help_rule, ox + pw - pad, help_rule, u * 0.2f, g_accent, 0.25f, 4.f);
  const float list_top = rule_y + lh * 0.9f, list_bottom = help_rule - lh * 0.5f;

  // Lay out the shown items: label wrapped beside a right-aligned value; a long value gets its own line.
  struct Row { int item; std::vector<std::string> label; std::string value; bool value_below; int indent; };
  std::vector<Row> rows;
  int sel_line0 = 0, sel_lines = 1, total = 0;
  for (size_t k = 0; k < items.size(); ++k) {
    const Item& it = items[k];
    if (!Shown(it)) continue;
    Row r{int(k), {}, "", false, 0};
    bool sel = int(k) == g_sel;
    // a label's own leading spaces indent it further (an option that belongs to the one above it)
    const size_t lead = std::min(it.label.find_first_not_of(' '), it.label.size());
    const std::string text = it.label.substr(lead);
    const int indent = Indent(it) + int(lead);
    r.indent = indent;
    if (Folds(it)) {
      r.label = Wrap(std::string(1, g_open_sections[k] ? pixfont::kOpen : pixfont::kFolded) + " " + text,
                     cols - indent);
    } else {
      r.value = ValueText(it);
      if (sel && !r.value.empty() && (it.kind == Kind::Choice || it.kind == Kind::Number || IsFullscreenItem(it)))
        r.value = "< " + r.value + " >";
      int vcols = int(r.value.size());
      int lcols = cols - indent - (vcols ? vcols + 2 : 0);
      if (vcols && int(text.size()) > lcols && lcols < cols / 2) { r.value_below = true; lcols = cols - indent; }
      r.label = Wrap(text, lcols);
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

  const float text_dy = 2 * u;   // the 7-unit glyphs centred in an 11-unit line
  float y = list_top - g_scroll * lh;
  for (const Row& r : rows) {
    const Item& it = items[r.item];
    int n = int(r.label.size()) + (r.value_below ? 1 : 0);
    bool sel = r.item == g_sel;
    auto in_view = [&](float ly) { return ly >= list_top - 0.5f && ly + lh <= list_bottom + 0.5f; };
    if (sel) {   // the selection: a tinted bar with a glowing, gently pulsing marker on its left
      float y0 = std::max(y, list_top), y1 = std::min(y + n * lh, list_bottom);
      if (y1 > y0) {
        FillRect(ren, ox + u * 1.5f, y0, pw - u * 4.f, y1 - y0, g_accent, 0.16f);
        const float pulse = 0.75f + 0.25f * std::sin(secs * 5.f);
        pixfont::Glow(ren, ox + u * 2.5f, y0 + u, ox + u * 2.5f, y1 - u, u * 0.45f, g_accent, pulse, 5.f);
      }
    }
    const bool header = it.kind == Kind::Header, sub = it.kind == Kind::Sub;
    float lx = x0 + r.indent * cw;
    pixfont::Col lc = header ? g_accent : sel ? white : sub ? Mix(g_accent, label_col, 0.45f) : label_col;
    float la = header || sel ? 1.f : 0.85f, lg = header ? 0.9f : sub ? 0.4f : sel ? 0.5f : 0.f;
    for (size_t k = 0; k < r.label.size(); ++k)
      if (in_view(y + k * lh)) pixfont::Text(ren, r.label[k], lx, y + k * lh + text_dy, u, lc, la, -1, lg);
    if (!r.value.empty()) {
      float vy = r.value_below ? y + r.label.size() * lh : y;
      if (in_view(vy))
        pixfont::Text(ren, r.value, ox + pw - pad, vy + text_dy, u, sel ? g_accent : value_col, 1.f, 1,
                      sel ? 1.f : 0.35f);
    }
    y += n * lh;
  }
  // Scroll hints.
  if (g_scroll > 0) pixfont::Text(ren, "^", ox + pw - pad, list_top - lh + text_dy, u, g_accent, 1.f, 1, 1.f);
  if (g_scroll + view_lines < total) pixfont::Text(ren, "v", ox + pw - pad, list_bottom + text_dy - u, u, g_accent, 1.f, 1, 1.f);
}

void Toast(const std::string& text, uint64_t now) {
  g_toast = text;
  g_toast_until = now + 1500000000ull;
}

void DrawToast(SDL_Renderer* ren, int w, int h, uint64_t now) {
  if (now >= g_toast_until || g_toast.empty()) return;
  const float u = Unit(h);
  const float tw = pixfont::TextWidth(g_toast, u);
  const float bw = tw + 16 * u, bh = 17 * u, bx = (w - bw) / 2, by = h - bh - 12 * u;
  FillRect(ren, bx, by, bw, bh, {0.012f, 0.014f, 0.035f}, 0.85f);
  pixfont::Glow(ren, bx, by, bx + bw, by, u * 0.3f, g_accent, 0.6f, 4.f);
  pixfont::Glow(ren, bx, by + bh, bx + bw, by + bh, u * 0.3f, g_accent, 0.6f, 4.f);
  pixfont::Text(ren, g_toast, w / 2.f, by + 5 * u, u, g_accent, 1.f, 0, 1.f);
}

}  // namespace menu
