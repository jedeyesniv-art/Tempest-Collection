// controls.cpp - see controls.h.
#include "controls.h"

#include "settings.h"

namespace controls {
namespace {

struct Button {
  const char* key;    // settings: bind_<key>
  const char* name;
};

const Button kButtons[kButtonCount] = {
    {"a", "A"},   {"b", "B"},   {"x", "X"},   {"y", "Y"},         {"lb", "LB"},           {"rb", "RB"},
    {"lt", "LT"}, {"rt", "RT"}, {"start", "Start"}, {"ls", "L-stick click"}, {"rs", "R-stick click"},
};

std::vector<FnDef> g_fns = {{"none", "None"}};
int g_defaults[kButtonCount] = {};

std::string Key(int b) { return std::string("bind_") + kButtons[b].key; }

}  // namespace

void Setup(const std::vector<FnDef>& fns, const int (&defaults)[kButtonCount]) {
  g_fns = {{"none", "None"}};
  g_fns.insert(g_fns.end(), fns.begin(), fns.end());
  for (int b = 0; b < kButtonCount; ++b) g_defaults[b] = defaults[b] >= 0 && defaults[b] < FnCount() ? defaults[b] : 0;
}

int FnCount() { return int(g_fns.size()); }
const char* FnName(int f) { return f >= 0 && f < FnCount() ? g_fns[f].name : "?"; }
const char* ButtonName(int b) { return kButtons[b].name; }

int FnOf(int b) {
  std::string v = settings::Str(Key(b), g_fns[g_defaults[b]].key);
  for (int f = 0; f < FnCount(); ++f)
    if (v == g_fns[f].key) return f;
  return 0;
}

std::string ButtonsFor(int f) {
  std::string out;
  for (int b = 0; b < kButtonCount; ++b)
    if (FnOf(b) == f) out += (out.empty() ? "" : ", ") + std::string(kButtons[b].name);
  return out.empty() ? "-" : out;
}

uint32_t Held(SDL_Gamepad* pad) {
  if (!pad) return 0;
  auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
  auto trig = [&](SDL_GamepadAxis a) { return SDL_GetGamepadAxis(pad, a) > 12000; };
  bool on[kButtonCount] = {
      btn(SDL_GAMEPAD_BUTTON_SOUTH),          btn(SDL_GAMEPAD_BUTTON_EAST),
      btn(SDL_GAMEPAD_BUTTON_WEST),           btn(SDL_GAMEPAD_BUTTON_NORTH),
      btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),  btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
      trig(SDL_GAMEPAD_AXIS_LEFT_TRIGGER),    trig(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER),
      btn(SDL_GAMEPAD_BUTTON_START),          btn(SDL_GAMEPAD_BUTTON_LEFT_STICK),
      btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK),
  };
  uint32_t m = 0;
  for (int b = 0; b < kButtonCount; ++b)
    if (on[b]) m |= 1u << b;
  return m;
}

bool FnHeld(uint32_t held, int f) {
  for (int b = 0; b < kButtonCount; ++b)
    if ((held >> b & 1) && FnOf(b) == f) return true;
  return false;
}

void Assign(int f, int b) { settings::Save(Key(b), g_fns[FnOf(b) == f ? 0 : f].key); }

void ResetDefaults() {
  for (int b = 0; b < kButtonCount; ++b) settings::Save(Key(b), g_fns[g_defaults[b]].key);
}

}  // namespace controls
