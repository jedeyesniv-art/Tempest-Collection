// vita_host.h - drives the Vita3K libretro core (PS Vita; TxK), loaded at run time from vita3k_libretro.dll (every
// core exports retro_*, so each one lives in its own program and DLL).
// The core renders with OpenGL 4.4 core profile (SET_HW_RENDER, OPENGL_CORE): we give it a hidden SDL OpenGL window
// and an FBO as big as its largest picture (960x544 x4) and read each frame back to XRGB8888 for the SDL renderer.
// Core options come from a key -> value map (SetOption): the core's own defaults for the rest.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vita {

struct Frame {
  std::vector<uint32_t> pixels;  // XRGB8888, width * height, top row first
  unsigned width = 0, height = 0;
};

// RetroPad button bits (RETRO_DEVICE_ID_JOYPAD_*). The core maps them to the Vita's: B = Cross, A = Circle,
// Y = Square, X = Triangle, L / R = L / R, START / SELECT = Start / Select.
enum Btn : int {
  B = 0, Y = 1, SELECT = 2, START = 3, UP = 4, DOWN = 5, LEFT = 6, RIGHT = 7,
  A = 8, X = 9, L = 10, R = 11, L2 = 12, R2 = 13, L3 = 14, R3 = 15
};

// Before Load: a core option (key as the core names it, e.g. vita3k_resolution_multiplier).
void SetOption(const std::string& key, const std::string& value);

// core_dll: vita3k_libretro.dll; game: the game's folder (or its eboot.bin, a .pkg / .vpk / .zip); system_dir:
// holds vita3k\ (the firmware .pup files; the core unpacks its data there); save_dir: the core's saves go in
// save_dir\vita3k\. Needs SDL video initialised. The core prepares the game (firmware install) on a thread of its
// own: RunFrame shows progress until the game starts.
bool Load(const std::string& core_dll, const std::string& game, const std::string& system_dir,
          const std::string& save_dir, std::string* err);
void Unload();
void SetButtons(uint16_t buttons);
// Analog sticks, -32768..32767 (RETRO_DEVICE_ANALOG): left x / y, right x / y.
void SetSticks(int lx, int ly, int rx, int ry);
void RunFrame();                // one retro_run: one 60 Hz frame
const Frame& LastFrame();
uint64_t NewFrames();           // pictures the core has handed over so far (repeats of the last one not counted)
double Fps();
double SampleRate();
std::vector<int16_t>& Audio();  // interleaved stereo int16 produced since the last clear
// The core's on-screen messages (SET_MESSAGE): the newest, and a counter that changes with each.
const std::string& LastMessage(unsigned* serial = nullptr);

}  // namespace vita
