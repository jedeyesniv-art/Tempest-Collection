// pads.cpp - see pads.h.
#include "pads.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "menu.h"
#include "settings.h"

namespace pads {
namespace {

std::vector<SDL_Gamepad*> g_open;            // every connected controller, in the order SDL reported them
SDL_JoystickID g_active = 0;                 // auto: the one used last (0 = none yet: the first)
SDL_JoystickID g_shown = 0;                  // the one Current() returned last (a toast when it changes)
std::map<std::pair<SDL_JoystickID, int>, bool> g_axis_pushed;   // past half last time (only crossings count)

std::string Name(SDL_Gamepad* p) {
  const char* n = SDL_GetGamepadName(p);
  return n && *n ? n : "Controller";
}

SDL_Gamepad* ById(SDL_JoystickID id) {
  for (SDL_Gamepad* p : g_open)
    if (SDL_GetGamepadID(p) == id) return p;
  return nullptr;
}

}  // namespace

void HandleEvent(const SDL_Event& e) {
  switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
      if (!ById(e.gdevice.which))
        if (SDL_Gamepad* p = SDL_OpenGamepad(e.gdevice.which)) {
          g_open.push_back(p);
          std::fprintf(stderr, "controller connected: %s (id %u)\n", Name(p).c_str(), unsigned(e.gdevice.which));
        }
      break;
    case SDL_EVENT_GAMEPAD_REMOVED:
      for (size_t k = 0; k < g_open.size(); ++k)
        if (SDL_GetGamepadID(g_open[k]) == e.gdevice.which) {
          std::fprintf(stderr, "controller disconnected: %s\n", Name(g_open[k]).c_str());
          SDL_CloseGamepad(g_open[k]);
          g_open.erase(g_open.begin() + k);
          break;
        }
      if (g_active == e.gdevice.which) g_active = 0;
      break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      g_active = e.gbutton.which;
      break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
      // A crossing past half, not a held value: a device whose axis rests off centre must not keep taking over.
      const bool pushed = std::abs(int(e.gaxis.value)) > 16384;
      bool& was = g_axis_pushed[{e.gaxis.which, int(e.gaxis.axis)}];
      if (pushed && !was) g_active = e.gaxis.which;
      was = pushed;
      break;
    }
    default:
      break;
  }
}

SDL_Gamepad* Current() {
  SDL_Gamepad* pad = nullptr;
  const std::string want = settings::Str("controller", "auto");
  if (want != "auto")
    for (SDL_Gamepad* p : g_open)
      if (Name(p) == want) { pad = p; break; }
  if (!pad) pad = ById(g_active);
  if (!pad && !g_open.empty()) pad = g_open.front();
  // With more than one connected, say which one the game follows when that changes.
  const SDL_JoystickID id = pad ? SDL_GetGamepadID(pad) : 0;
  if (id != g_shown) {
    if (pad && g_shown != 0 && g_open.size() > 1) menu::Toast("Controller: " + Name(pad), SDL_GetTicksNS());
    g_shown = id;
  }
  return pad;
}

void FillChoice(menu::Item& item) {
  item.values = {"auto"};
  item.names = {"Any (last used)"};
  for (SDL_Gamepad* p : g_open) {
    const std::string n = Name(p);
    bool dup = false;
    for (const std::string& v : item.values) dup |= v == n;
    if (dup) continue;
    item.values.push_back(n);
    item.names.push_back(n);
  }
  const std::string saved = settings::Str("controller", "auto");
  bool listed = false;
  for (const std::string& v : item.values) listed |= v == saved;
  if (!listed) {
    item.values.push_back(saved);
    item.names.push_back(saved + " (not connected)");
  }
}

}  // namespace pads
