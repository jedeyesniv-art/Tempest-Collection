// menu.h - the in-game settings menu (Back / Esc), shared by every game program (t2k.exe, t3k.exe, ...). The game
// describes its page once (SetPage: title + items reading / writing settings keys) and handles the action ids that
// Update returns; the panel, folding sections, scrolling, button remapping and toasts are common. The game is
// expected to pause its emulation while the menu is open.
#pragma once
#include <SDL3/SDL.h>
#include <functional>
#include <string>
#include <vector>

namespace menu {

struct Nav {            // this frame's menu input (held state; edges are found inside)
  bool up = false, down = false, left = false, right = false;
  bool ok = false, back = false;
  bool cancel = false;  // Back / Esc only (B is bindable, so it can't cancel a remap)
  int new_button = -1;  // controls:: button index pressed this frame (edge), for remapping
};
// The common menu input: d-pad / left stick, A selects, B / Back closes; keyboard arrows, Enter / Z, Esc / Backspace.
Nav ReadNav(SDL_Gamepad* pad, const bool* keys);

// Action ids Update returns. The common ones; a game numbers its own from kGameAction.
enum Action : int {
  kNone = 0, kClose, kQuit, kToggleFullscreen, kReturnToTitle, kSaveState, kLoadState,
  kGameAction = 100
};

enum class Kind { Header, Sub, Choice, Number, Button, Bind, ResetBinds };

struct Item {
  Kind kind = Kind::Header;
  std::string label;
  std::string key;                      // settings key (Choice / Number)
  std::vector<std::string> values;      // Choice: stored values (the first = the value when the key is missing)
  std::vector<std::string> names;       // Choice: shown names
  std::string def_value;                // Choice: the value when the key is missing ("" = values[0])
  double lo = 0, hi = 0, step = 0;      // Number
  double def = 0;                       // Number: the value when the setting is missing
  const char* fmt = "%.2f";
  int action = kNone;                    // Button
  bool closes = false;                  // Button: the menu closes when it is chosen
  bool slot_info = false;               // Button: shows the SetSlotInfo text as its value
  int fn = 0;                           // Bind: controls:: function
  std::function<bool()> when;           // shown only while this is true (unset: always; a Header: its title)
  int section = -1;                     // (set by SetPage) the header it belongs to; -1 = none
  int sub = -1;                         // (set by SetPage) the fold inside the section it belongs to; -1 = none
};

// Builds a page. The returned pointers are valid until the next call (set `when` etc. right away).
struct Page {
  std::vector<Item> items;
  void Header(const std::string& label);      // "" ends the sections (later items are always shown)
  // A fold inside a section ("Fine-tune stick"): the items after it, up to the next Sub / Header, show only while it
  // is open; it shows only while one of them is visible. Sub("") ends it within the section.
  void Sub(const std::string& label);
  Item* Choice(const std::string& label, const std::string& key, std::vector<std::string> values,
               std::vector<std::string> names);
  Item* Number(const std::string& label, const std::string& key, double lo, double hi, double step, const char* fmt,
               double def);
  Item* Button(const std::string& label, int action, bool closes = false);
  // A "Buttons" fold in the current section: one row per controls:: function (A on it, then a button) and "Reset
  // buttons to defaults".
  void Binds(const std::string& label = "Buttons");
};

// title: shown at the top of the panel ("TEMPEST 2000 - SETTINGS": the game big, the rest under it).
void SetPage(const std::string& title, Page page);
// The game's colour (its launcher accent, 0..1): the panel's edge, title, section names, values, selection.
void SetAccent(float r, float g, float b);
// "game paused" next to the subtitle (default on; the launcher has no game to pause).
void SetPausedNote(bool on);

bool IsOpen();
// Drawn: open, or still sliding out after closing.
bool Shown();
void Open(bool expand_all = false);   // expand_all: every section open (tests)
void Close();
int Update(const Nav& nav, uint64_t now_ns);   // an Action (or a game's own id)
void Draw(SDL_Renderer* ren, int out_w, int out_h);
// Text shown next to the slot_info items for the current slot ("empty", "8 Nov 22:14").
void SetSlotInfo(std::function<std::string()> f);

// A short message drawn over the game for a moment (e.g. "Camera: fixed").
void Toast(const std::string& text, uint64_t now_ns);
void DrawToast(SDL_Renderer* ren, int out_w, int out_h, uint64_t now_ns);

}  // namespace menu
