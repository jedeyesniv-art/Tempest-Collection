// volume.h - the collection's sound level: every game at the same loudness (each game's gain measured once with
// tools/loudness.py from headless --wav recordings), times the launcher's master volume, times the game's own
// "volume" setting (its menu). Applied where the window's sound is queued, so --wav recordings stay as the core made
// them.
#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace volume {

// game: t2k t3k t81 txk. Reads <udir>\launcher_settings.txt (master_volume 0..1.5, even_loudness 0 / 1) once; the
// launcher can only change them while no game runs.
void Init(const std::filesystem::path& udir, const char* game);

// The gain now: master x (even_loudness ? the game's measured gain : 1) x settings "volume" (the game's own, 0..1.5).
float Gain();

// Queues n interleaved 16-bit samples on the stream at Gain() (ramped from the last buffer's gain: no clicks when it
// changes), through a soft limiter above -3 dBFS (a raised game can't clip on its loudest moments).
void Put(SDL_AudioStream* s, const int16_t* d, size_t n);

// The measured gain that brings a game to the collection's loudness (1 for an unknown game).
float Normalise(const char* game);

}  // namespace volume
