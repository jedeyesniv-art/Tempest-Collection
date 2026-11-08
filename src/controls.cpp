// controls.cpp - see controls.h.
#include "controls.h"

#include "settings.h"

namespace controls {
namespace {

struct Button {
  const char* key;    // settings: bind_<key>
  const char* name;
  Fn def;
};

const Button kButtons[kButtonCount] = {
    {"a", "A", kFire},       {"b", "B", kZap},       {"x", "X", kJump},     {"y", "Y", kOption},
    {"lb", "LB", kCamera},   {"rb", "RB", kZap},     {"lt", "LT", kJump},   {"rt", "RT", kJump},
    {"start", "Start", kPause}, {"ls", "L-stick click", kNone}, {"rs", "R-stick click", kNone},
};

const char* kFnKeys[kFnCount] = {"none", "fire", "jump", "zap", "option", "pause", "camera"};
const char* kFnNames[kFnCount] = {"None", "Fire", "Jump", "Superzapper", "Option", "Pause", "Camera view"};

std::string Key(int b) { return std::string("bind_") + kButtons[b].key; }

}  // namespace

const char* FnName(Fn f) { return kFnNames[f]; }
const char* ButtonName(int b) { return kButtons[b].name; }

Fn FnOf(int b) {
  std::string v = settings::Str(Key(b), kFnKeys[kButtons[b].def]);
  for (int f = 0; f < kFnCount; ++f)
    if (v == kFnKeys[f]) return Fn(f);
  return kNone;
}

std::string ButtonsFor(Fn f) {
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

bool FnHeld(uint32_t held, Fn f) {
  for (int b = 0; b < kButtonCount; ++b)
    if ((held >> b & 1) && FnOf(b) == f) return true;
  return false;
}

void Assign(Fn f, int b) { settings::Save(Key(b), kFnKeys[FnOf(b) == f ? kNone : f]); }

void ResetDefaults() {
  for (int b = 0; b < kButtonCount; ++b) settings::Save(Key(b), kFnKeys[kButtons[b].def]);
}

}  // namespace controls
