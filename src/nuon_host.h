// nuon_host.h - drives the Nuance (Nuon) libretro core, loaded at run time from nuance_libretro.dll (both cores
// export retro_*, so it can't be linked into the same program as the Jaguar core).
// The core draws with OpenGL 2.1 (SET_HW_RENDER): we give it a hidden SDL OpenGL window and a 720x480 FBO and read
// each frame back to XRGB8888 for the SDL renderer.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nuon {

struct Frame {
  std::vector<uint32_t> pixels;  // XRGB8888, width * height, top row first
  unsigned width = 0, height = 0;
};

// The core's per-retro_run statistics (our branch llamasoft-nuon, src/libretro.cpp NuanceRunStats).
struct RunStats {
  uint64_t iterations;     // main-loop iterations (each runs MPE 3..0 once)
  uint64_t mpe3_stalled;   // iterations with MPE3 stalled in VidSync (the game waiting for a field)
  uint64_t run_us;         // wall time in the emulation loop
  uint32_t field_fired;    // 1 = ended on a video field, 0 = on the time budget
  uint32_t field_counter;  // video field counter after the run
  uint32_t vidsync_calls;  // VidSync waits started (cumulative)
  uint32_t pad;
  uint64_t mpe_packets[4]; // packets each MPE executed
  uint32_t vid_flips;      // VidChangeBase + VidConfig calls (cumulative): the game's own frame rate
  uint32_t pad2;
};

// RetroPad button bits (RETRO_DEVICE_ID_JOYPAD_*); the core maps A -> Nuon A, B -> B, START -> Start,
// SELECT -> Nuon, L / R -> L / R, X -> C-up, Y -> C-left, L2 -> C-down, R2 -> C-right.
enum Btn : int {
  B = 0, Y = 1, SELECT = 2, START = 3, UP = 4, DOWN = 5, LEFT = 6, RIGHT = 7,
  A = 8, X = 9, L = 10, R = 11, L2 = 12, R2 = 13, L3 = 14, R3 = 15
};

// core_dll: nuance_libretro.dll; system_dir: holds nuance\ (bios.cof, minibios.cof, minibiosX.cof, nuance.cfg).
// Needs SDL video initialised. Note: the core changes the process's working folder to system_dir\nuance.
bool Load(const std::string& core_dll, const std::string& disc, const std::string& system_dir,
          const std::string& save_dir, std::string* err);
void Unload();
void SetButtons(uint16_t buttons);
void RunFrame();                // one retro_run (the core runs until its next video field by the PC clock)
const Frame& LastFrame();
double Fps();
double SampleRate();
std::vector<int16_t>& Audio();  // interleaved stereo int16 produced since the last clear
bool GetRunStats(RunStats* out);

}  // namespace nuon
