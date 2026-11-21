// d2k.h - Defender 2000's native renderer: the game's picture is Object Processor bitmaps (pre-rendered sprites,
// parallax layers, an 8-bit playfield, the scanner). Each frame we capture every object the OP reads, decode its
// pixels from RAM and draw the list ourselves: in software (checked against the emulator's own picture) and on the
// GPU at window resolution, with positions interpolated between the game's 30 Hz updates.
#pragma once
#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace d2k {

bool IsDefender(const std::string& rom_path);   // by the cartridge's contents
bool Install();                                   // hooks the OP; call after host::Load
bool Active();

// Arcade controls (Thrust held, Reverse held): the d-pad direction to add this frame (-1 left, +1 right, 0 none).
// Call once per emulated frame.
int Arcade(bool thrust, bool reverse);

// In play (the game running, the attract demo too) rather than on a menu / title / table / between-level screen: the
// pad's own bindings apply only in play; menus get fixed buttons (A select, B back). See d2k.cpp.
bool InPlay();
int ModePlayed();           // 0 Classic, 1 Plus, 2 Defender 2000; 255 when not in play
bool AutoFire(bool fire);   // Fire as the game should see it this frame (Classic: pulsed while held)

void BeforeFrame();   // before each emulated frame (clears the capture)
void AfterFrame();    // after it: builds the frame's scene, tracks game updates

// The latest frame's scene drawn in software, the size of the core's picture (XRGB8888). For checks.
void SoftRender(std::vector<uint32_t>& out, unsigned w, unsigned h);
// The check: our software picture vs the emulator's; writes <prefix>_ours.bmp / _core.bmp / _diff.bmp, prints the
// mismatch count. Returns the number of differing pixels.
int Check(const std::string& prefix);

// Window drawing: the scene into dst (the 4:3 picture rectangle in window pixels), positions interpolated by t =
// how far real time is into the current emulated frame (0..1). With wide screens on it carries on into view (the
// window) around dst.
void Draw(SDL_Renderer* ren, const SDL_FRect& dst, const SDL_FRect& view, float t);
std::string Stats();

}  // namespace d2k
