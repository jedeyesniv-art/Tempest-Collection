// host.h - drives the Virtual Jaguar core through the libretro API (statically linked).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace host {

struct Frame {
  std::vector<uint32_t> pixels;  // XRGB8888, width * height
  unsigned width = 0, height = 0;
};

// RetroPad button bits (RETRO_DEVICE_ID_JOYPAD_*).
enum Btn : int {
  B = 0, Y = 1, SELECT = 2, START = 3, UP = 4, DOWN = 5, LEFT = 6, RIGHT = 7,
  A = 8, X = 9, L = 10, R = 11, L2 = 12, R2 = 13, L3 = 14, R3 = 15
};

struct Input {
  uint16_t buttons = 0;      // bit per Btn
  uint16_t keypad = 0;       // Jaguar keypad: bit 0-9 = digits, 10 = *, 11 = # (to the core as numpad keys)
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

bool Load(const std::string& rom_path, const std::string& save_dir, std::string* err);
void Unload();
void SetOption(const std::string& key, const std::string& value);
void SetInput(int port, const Input& in);
void RunFrame();                     // one emulated video frame
const Frame& LastFrame();
double Fps();
double SampleRate();
// Audio produced since the last call (interleaved stereo int16).
std::vector<int16_t>& Audio();

// The cartridge EEPROM (high scores, keys, options): the core's save buffer, empty if none.
uint8_t* SaveRam(size_t* size);

std::vector<uint8_t> SaveState();
bool SaveStateInto(std::vector<uint8_t>& s);   // reuses s (run-ahead: no allocation a frame)
// The next frames' pictures are wanted (off: the core skips building the picture; LastFrame keeps the last one).
void SetVideoEnabled(bool on);
bool LoadState(const std::vector<uint8_t>& s);
// The console's reset button: the cartridge boots again; the EEPROM (high scores, options) is kept.
void Reset();

}  // namespace host
