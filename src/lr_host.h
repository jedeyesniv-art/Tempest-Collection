// lr_host.h - drives a libretro core loaded at run time from its DLL (every core exports retro_*, so each one lives
// in its own program): Vita3K for TxK (txk.exe), MAME for arcade Tempest (t81.exe).
// Hardware rendering: a core asking for OpenGL core profile (SET_HW_RENDER, OPENGL_CORE; Vita3K wants 4.4) gets a
// hidden SDL OpenGL window and an FBO as big as its largest picture, read back to XRGB8888 for the SDL renderer.
// Software cores (MAME) hand XRGB8888 frames straight over. Core options come from a key -> value map (SetOption);
// the core's own defaults for the rest.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lr {

struct Frame {
  std::vector<uint32_t> pixels;  // XRGB8888, width * height, top row first
  unsigned width = 0, height = 0;
};

// RetroPad button bits (RETRO_DEVICE_ID_JOYPAD_*). Vita3K maps B = Cross, A = Circle, Y = Square, X = Triangle;
// MAME B = button 1, A = button 2, SELECT = coin, START = start.
enum Btn : int {
  B = 0, Y = 1, SELECT = 2, START = 3, UP = 4, DOWN = 5, LEFT = 6, RIGHT = 7,
  A = 8, X = 9, L = 10, R = 11, L2 = 12, R2 = 13, L3 = 14, R3 = 15
};

// A core option (key as the core names it, e.g. vita3k_resolution_multiplier). Before Load, or while running: the
// core is told on its next frame (GET_VARIABLE_UPDATE); some options only apply after Reset.
void SetOption(const std::string& key, const std::string& value);
// Restart the game (Vita3K: unloads and loads it again on a later frame, with the current options).
void Reset();
// A core's own export by name (our core patches, e.g. Vita3K's llama_vita_read); nullptr when it has none.
void* CoreProc(const char* name);

// core_dll: the core; game: the content path (TxK: its folder; MAME: the romset .zip); system_dir / save_dir: the
// core's folders. Needs SDL video initialised.
bool Load(const std::string& core_dll, const std::string& game, const std::string& system_dir,
          const std::string& save_dir, std::string* err);
void Unload();
void SetButtons(uint16_t buttons);
// Analog sticks, -32768..32767 (RETRO_DEVICE_ANALOG): left x / y, right x / y.
void SetSticks(int lx, int ly, int rx, int ry);
// Relative mouse movement for the coming frame (RETRO_DEVICE_MOUSE; MAME's dial / spinner with mame_mouse_enable).
void SetMouse(int dx, int dy);
// A touch on the game's picture for the coming frames (RETRO_DEVICE_POINTER, one point; the Vita3K core's front
// touchscreen): x, y from 0 (left / top) to 1 (right / bottom); down = false lifts it.
void SetTouch(bool down, float x, float y);
void RunFrame();                // one retro_run
const Frame& LastFrame();
uint64_t NewFrames();           // pictures the core has handed over so far (repeats of the last one not counted)
double Fps();
double SampleRate();
double AspectRatio();           // the core's display aspect (0 = width / height of the picture)
unsigned Rotation();            // SET_ROTATION: 0..3 = 0, 90, 180, 270 degrees counter-clockwise
std::vector<int16_t>& Audio();  // interleaved stereo int16 produced since the last clear
// The core's on-screen messages (SET_MESSAGE): the newest, and a counter that changes with each.
const std::string& LastMessage(unsigned* serial = nullptr);
// Save states (retro_serialize): false when the core can't.
bool SaveState(std::vector<uint8_t>& out);
bool LoadState(const std::vector<uint8_t>& in);

}  // namespace lr
