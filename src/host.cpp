// host.cpp - libretro host for the statically linked Virtual Jaguar core.
#include "host.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "libretro.h"

namespace host {
namespace {

bool g_video = true;   // SetVideoEnabled: the picture of this frame is wanted

std::map<std::string, std::string> g_options;
std::string g_save_dir;
std::string g_system_dir;
Frame g_frame;
std::vector<int16_t> g_audio;
Input g_input[2];
retro_system_av_info g_av{};
std::vector<uint8_t> g_rom;
bool g_loaded = false;
bool g_options_dirty = false;

void LogCb(enum retro_log_level level, const char* fmt, ...) {
  if (level < RETRO_LOG_INFO) return;
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
}

bool EnvCb(unsigned cmd, void* data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      static_cast<retro_log_callback*>(data)->log = LogCb;
      return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return *static_cast<const retro_pixel_format*>(data) == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      *static_cast<const char**>(data) = g_system_dir.c_str();
      return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *static_cast<const char**>(data) = g_save_dir.c_str();
      return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      auto* v = static_cast<retro_variable*>(data);
      auto it = g_options.find(v->key ? v->key : "");
      if (it == g_options.end()) { v->value = nullptr; return false; }
      v->value = it->second.c_str();
      return true;
    }
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:   // (the core then skips its final pixel stores; emulation as ever)
      *static_cast<int*>(data) = g_video ? (RETRO_AV_ENABLE_VIDEO | RETRO_AV_ENABLE_AUDIO) : RETRO_AV_ENABLE_AUDIO;
      return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *static_cast<bool*>(data) = g_options_dirty;
      g_options_dirty = false;
      return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      *static_cast<unsigned*>(data) = 2;
      return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
      return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: {
      auto* m = static_cast<const retro_message_ext*>(data);
      if (m && m->msg) std::fprintf(stderr, "[core msg] %s\n", m->msg);
      return true;
    }
    default:
      return false;
  }
}

void VideoCb(const void* data, unsigned w, unsigned h, size_t pitch) {
  if (!data || !g_video) return;  // dupe frame / a frame nobody sees: keep the last one
  g_frame.width = w;
  g_frame.height = h;
  g_frame.pixels.resize(size_t(w) * h);
  for (unsigned y = 0; y < h; ++y)
    std::memcpy(&g_frame.pixels[size_t(y) * w], static_cast<const uint8_t*>(data) + y * pitch, w * 4);
}

void AudioCb(int16_t l, int16_t r) {
  g_audio.push_back(l);
  g_audio.push_back(r);
}

size_t AudioBatchCb(const int16_t* d, size_t frames) {
  g_audio.insert(g_audio.end(), d, d + frames * 2);
  return frames;
}

void InputPollCb() {}

int16_t InputStateCb(unsigned port, unsigned device, unsigned index, unsigned id) {
  if (port > 1) return 0;
  const Input& in = g_input[port];
  unsigned base = device & RETRO_DEVICE_MASK;
  if (base == RETRO_DEVICE_JOYPAD) {
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return int16_t(in.buttons);
    return (in.buttons >> id) & 1;
  }
  if (base == RETRO_DEVICE_KEYBOARD && port == 0) {   // the Jaguar keypad: the core's keyboard keys 0-9, - (*), = (#)
    static const unsigned kKeys[12] = {RETROK_0, RETROK_1, RETROK_2, RETROK_3, RETROK_4, RETROK_5,
                                       RETROK_6, RETROK_7, RETROK_8, RETROK_9, RETROK_MINUS, RETROK_EQUALS};
    for (int k = 0; k < 12; ++k)
      if (id == kKeys[k]) return (g_input[0].keypad >> k) & 1;
    return 0;
  }
  if (base == RETRO_DEVICE_ANALOG) {
    if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
      return id == RETRO_DEVICE_ID_ANALOG_X ? in.lx : in.ly;
    if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
      return id == RETRO_DEVICE_ID_ANALOG_X ? in.rx : in.ry;
  }
  return 0;
}

}  // namespace

bool Load(const std::string& rom_path, const std::string& save_dir, std::string* err) {
  g_save_dir = save_dir;
  g_system_dir = save_dir;
  std::ifstream f(rom_path, std::ios::binary);
  if (!f) { *err = "cannot open " + rom_path; return false; }
  g_rom.assign(std::istreambuf_iterator<char>(f), {});

  retro_set_environment(EnvCb);
  retro_set_video_refresh(VideoCb);
  retro_set_audio_sample(AudioCb);
  retro_set_audio_sample_batch(AudioBatchCb);
  retro_set_input_poll(InputPollCb);
  retro_set_input_state(InputStateCb);
  retro_init();

  retro_game_info gi{};
  gi.path = rom_path.c_str();
  gi.data = g_rom.data();
  gi.size = g_rom.size();
  if (!retro_load_game(&gi)) { *err = "core refused the ROM"; retro_deinit(); return false; }
  retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
  retro_get_system_av_info(&g_av);
  g_loaded = true;
  return true;
}

void Unload() {
  if (!g_loaded) return;
  retro_unload_game();
  retro_deinit();
  g_loaded = false;
}

void SetOption(const std::string& key, const std::string& value) {
  g_options[key] = value;
  g_options_dirty = true;
}

void SetInput(int port, const Input& in) { g_input[port & 1] = in; }
void RunFrame() { retro_run(); }
const Frame& LastFrame() { return g_frame; }
double Fps() { return g_av.timing.fps; }
double SampleRate() { return g_av.timing.sample_rate; }
std::vector<int16_t>& Audio() { return g_audio; }

uint8_t* SaveRam(size_t* size) {
  *size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
  return static_cast<uint8_t*>(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
}

void SetVideoEnabled(bool on) { g_video = on; }

void Reset() { retro_reset(); }

bool SaveStateInto(std::vector<uint8_t>& s) {
  size_t n = retro_serialize_size();
  if (s.size() != n) s.resize(n);
  return retro_serialize(s.data(), s.size());
}

std::vector<uint8_t> SaveState() {
  std::vector<uint8_t> s(retro_serialize_size());
  if (!retro_serialize(s.data(), s.size())) s.clear();
  return s;
}

bool LoadState(const std::vector<uint8_t>& s) {
  return !s.empty() && retro_unserialize(s.data(), s.size());
}

}  // namespace host
