// launcher_sound.cpp - see launcher_sound.h. SDL3 mixes every audio stream bound to a device: a few voices round-robin,
// so quick scrolling overlaps clicks instead of cutting them off.
#include "launcher_sound.h"

#include <SDL3/SDL.h>

#include <map>

namespace fs = std::filesystem;

namespace lsound {
namespace {

struct Clip {
  SDL_AudioSpec spec{};
  std::vector<Uint8> data;
};

SDL_AudioDeviceID g_dev = 0;
std::map<std::string, Clip> g_clips;   // "<id>_move", "<id>_launch"
constexpr int kVoices = 6;
SDL_AudioStream* g_voice[kVoices] = {};
int g_next = 0;

void Load(const fs::path& file, const std::string& key) {
  SDL_AudioSpec spec{};
  Uint8* buf = nullptr;
  Uint32 len = 0;
  if (!SDL_LoadWAV(file.string().c_str(), &spec, &buf, &len)) return;
  Clip c;
  c.spec = spec;
  c.data.assign(buf, buf + len);
  SDL_free(buf);
  g_clips[key] = std::move(c);
}

void Play(const std::string& key) {
  if (!g_dev) return;
  auto it = g_clips.find(key);
  if (it == g_clips.end()) return;
  SDL_AudioStream*& v = g_voice[g_next];
  g_next = (g_next + 1) % kVoices;
  SDL_AudioSpec dev{};
  SDL_GetAudioDeviceFormat(g_dev, &dev, nullptr);
  if (v) SDL_DestroyAudioStream(v);   // (a voice still playing is cut: the oldest of six)
  v = SDL_CreateAudioStream(&it->second.spec, &dev);
  if (!v) return;
  SDL_BindAudioStream(g_dev, v);
  SDL_PutAudioStreamData(v, it->second.data.data(), int(it->second.data.size()));
  SDL_FlushAudioStream(v);
}

}  // namespace

int Init(const fs::path& dir, const std::vector<std::string>& ids) {
  if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return 0;
  for (const std::string& id : ids) {
    Load(dir / (id + "_move.wav"), id + "_move");
    Load(dir / (id + "_launch.wav"), id + "_launch");
  }
  if (g_clips.empty()) return 0;
  g_dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
  return g_dev ? int(g_clips.size()) : 0;
}

void Shutdown() {
  for (SDL_AudioStream*& v : g_voice)
    if (v) { SDL_DestroyAudioStream(v); v = nullptr; }
  if (g_dev) SDL_CloseAudioDevice(g_dev);
  g_dev = 0;
  g_clips.clear();
}

void Move(const std::string& id) { Play(id + "_move"); }
void Launch(const std::string& id) { Play(id + "_launch"); }

}  // namespace lsound
