// t2k.h - Tempest 2000 specifics: ROM patches and the stick controls.
#pragma once
#include <string>

namespace t2k {

struct Pad {
  float lx = 0, ly = 0;   // left stick, -1..1 (y down)
  bool dpad_lr = false;   // a physical d-pad left/right is held: the game's own pad handling takes over
};

// Verifies the ROM in memory is the known Tempest 2000 and installs the claw hook. Call after host::Load.
bool Install(std::string* err);
// Before each emulated frame: computes the claw velocity and hands it to the hook.
void BeforeFrame(const Pad& pad);
// After each emulated frame: bookkeeping (claw ticks seen).
void AfterFrame();
// Run-ahead: keep / restore the host-side state (stick ramp, tick count, the port area at $DFFC00) around frames
// that are emulated and then rewound.
void Mark();
void Rewind();
bool InPlay();
// The attract-mode demo is running.
bool Demo();
// The game screen proper: the main loop draws with draw_objects (not level select, title or menu screens).
bool PlayScreen();
std::string Status();   // one line for the log / overlay
// The game's own pause screen is up (Start in play).
bool Paused();
// A line printed under "paused" in the game's small font (empty = none). The game's charset: letters, digits,
// space and a little punctuation; upper / lower case pick different styles.
void SetPauseLine(const std::string& text);
// Ends the current game and goes back to the title, the way the game's own * + # reset does.
void ReturnToTitle();

}  // namespace t2k
