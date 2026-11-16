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
// From the gpurun trap: render pacing for the menu screens' full-screen effects (settings pace_melt, frames a
// melt step takes, default 4; pace_ripple, frames a mode select background frame takes, default 2; 0 / 1 = off).
void OnGpurun(uint32_t caller, uint32_t module, uint32_t mode);
// Run-ahead: keep / restore the host-side state (stick ramp, tick count, the port area at $DFFC00) around frames
// that are emulated and then rewound.
void Mark();
void Rewind();
bool InPlay();
// The attract-mode demo is running.
bool Demo();
// The play HUD is up: the overlay object beasties+64 is the score strip (ObType 8, or 7 head to head; stdinit /
// h2hin) and the main loop draws with draw_objects (level select uses draw_oo with the same strip; the title and
// menu screens set other types; mainloop_routine is never reset after a game, so it can't decide alone).
bool HudShown();
// Cheat: warp (a bonus round) at the end of the current level. The other cheats are settings cheat_* held in
// BeforeFrame: cheat_keys (keypad: Option skips a level, 6 warps), cheat_inf_zap, cheat_beastly, cheat_levels.
void CheatWarp();
// Rows of the HUD strip in use: 48 (one player), 32 (head to head).
int HudRows();
// Screens where input latency matters (play, the demo, level select): run-ahead is used only there. The menu
// screens' GPU effects are heavy to emulate, and N extra frames each shown frame dropped them below 60 fps.
bool LatencyMatters();
std::string Status();   // one line for the log / overlay
// The game's own pause screen is up (Start in play).
bool Paused();
// A line printed under "paused" in the game's small font (empty = none). The game's charset: letters, digits,
// space and a little punctuation; upper / lower case pick different styles.
void SetPauseLine(const std::string& text);
// Ends the current game and goes back to the title, the way the game's own * + # reset does.
void ReturnToTitle();
// Back in the game's own menus (pad B / Backspace). MenuScreen: a menu is up where B means back (the host then keeps
// B away from the game, where it would confirm). MenuBack: the menu's own "exit" item (options, display / control /
// rotary setup, duel; "no! no! no!" on clear cartridge), else the game's skip-the-title reset (z + misstit, as a
// button in the demo does) to game select (Tempest Plus players, keys, rounds, level select) or, from game select,
// the title; the reset's level unlocks (t2k_max ...) are put back afterwards. MenuTick: once per real frame before
// the input is set; true = hold Jaguar B (fire) this frame to confirm the chosen exit item.
bool MenuScreen();
void MenuBack();
bool MenuTick();

}  // namespace t2k
