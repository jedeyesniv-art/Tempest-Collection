// volume.cpp - see volume.h.
#include "volume.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "settings.h"

namespace volume {
namespace {

// Each game's loudness in play (ITU BS.1770 integrated, LUFS: tools/loudness.py over headless --wav recordings of about
// a minute of play with the music on, logs/loud/, 2026-11-18). The gain brings it to kTarget (at most kMaxGainDb).
// Measured spread: TxK -11.0, T81 -19.2, T3K -21.5, T2K -27.6 (16.6 dB). -16 is near the modern games (TxK; Space
// Giraffe and Tempest 4000 can't be changed on the Deck), so the old ones are raised more than TxK is lowered.
struct Measured {
  const char* game;
  float lufs;
};
const Measured kMeasured[] = {
    {"t2k", -27.59f},   // frames 2200-6600 (level 1 onwards), peak -12.0 dBFS
    {"t3k", -21.54f},   // fields 3000-7000, peak -7.5
    {"t81", -19.18f},   // frames 720-5400 (MAME, from the first credit), peak -8.0
    {"txk", -10.96f},   // frames 2800-5400 (level 1), peak 0.0
};
constexpr float kTarget = -16.f;     // LUFS
constexpr float kMaxGainDb = 14.f;

float g_master = 1.f;
bool g_even = true;
float g_game = 1.f;
float g_last = -1.f;   // the gain the last buffer ended at (-1: none yet)
std::vector<int16_t> g_buf;

std::string Trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Soft limiter: straight up to the knee, then a tanh curve that approaches full scale (never reaches it).
inline float Limit(float y) {
  constexpr float kKnee = 0.708f;   // -3 dBFS
  const float a = std::fabs(y);
  if (a <= kKnee) return y;
  const float over = kKnee + (1.f - kKnee) * std::tanh((a - kKnee) / (1.f - kKnee));
  return y < 0 ? -over : over;
}

}  // namespace

float Normalise(const char* game) {
  for (const Measured& m : kMeasured)
    if (std::strcmp(m.game, game) == 0) return std::pow(10.f, std::fmin(kTarget - m.lufs, kMaxGainDb) / 20.f);
  return 1.f;
}

void Init(const std::filesystem::path& udir, const char* game) {
  // the launcher's settings file (key = value, '#' comments), read directly: settings:: holds this game's own file
  std::ifstream f(udir / "launcher_settings.txt");
  std::string line;
  while (std::getline(f, line)) {
    if (const size_t h = line.find('#'); h != std::string::npos) line.resize(h);
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = Trim(line.substr(0, eq)), val = Trim(line.substr(eq + 1));
    if (key == "master_volume") g_master = std::fmax(0.f, std::fmin(1.f, std::strtof(val.c_str(), nullptr)));
    if (key == "even_loudness") g_even = std::atoi(val.c_str()) != 0;
  }
  g_game = Normalise(game);
  g_last = -1.f;
  std::fprintf(stderr, "volume: %s master %.2f, loudness %s (x%.3f), own %.2f -> gain %.3f\n", game, g_master,
               g_even ? "evened" : "as made", g_game, settings::Num("volume", 1.0), Gain());
}

float Gain() {
  return g_master * (g_even ? g_game : 1.f) * float(std::fmax(0.0, settings::Num("volume", 1.0)));
}

void Put(SDL_AudioStream* s, const int16_t* d, size_t n) {
  if (!s || n == 0) return;
  const float g1 = Gain(), g0 = g_last < 0 ? g1 : g_last;
  g_last = g1;
  if (g0 == 1.f && g1 == 1.f) {   // as made: untouched
    SDL_PutAudioStreamData(s, d, int(n * sizeof(int16_t)));
    return;
  }
  g_buf.resize(n);
  const float step = (g1 - g0) / float(n);
  for (size_t i = 0; i < n; ++i) {
    const float y = Limit(float(d[i]) / 32768.f * (g0 + step * float(i)));
    g_buf[i] = int16_t(std::lrintf(y * 32767.f));
  }
  SDL_PutAudioStreamData(s, g_buf.data(), int(n * sizeof(int16_t)));
}

}  // namespace volume
