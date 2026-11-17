// t3k_compress.h - the launcher's Tempest 3000 disc compression: "Tempest 3000.iso" (743 MB) -> "Tempest 3000.nuz"
// (~196 MB), the same as tools/t3k_compress.py (the format: third_party/NuanceResurrection src/disc_image.h). The disc's
// nuon.dat is the 19 music tracks (16-bit big-endian stereo, 32 kHz; a block table in nuon.run at +0x104340): each
// becomes one Ogg Vorbis stream (libsndfile_x64.dll, LGPL, loaded at run time; level 0.2, ~260 kbps), every other
// sector is copied as is. Runs on its own threads; the launcher shows the progress and asks before deleting the .iso.
#pragma once
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace t3kc {

struct Job {
  std::atomic<int> tracks_done{0};
  std::atomic<int> tracks{19};
  std::atomic<int> phase{0};       // 0 encoding, 1 writing, 2 checking, 3 finished
  std::atomic<bool> cancel{false};
  std::atomic<bool> ok{false};     // (valid once phase is 3)
  std::mutex m;
  std::string error;               // (under m) why it failed
  std::thread worker;
  ~Job() { if (worker.joinable()) { cancel = true; worker.join(); } }
};

// Starts compressing iso into nuz (written as nuz + ".tmp", renamed when checked). sndfile_dll: libsndfile_x64.dll.
void Start(Job* job, const std::filesystem::path& iso, const std::filesystem::path& nuz,
           const std::filesystem::path& sndfile_dll);

// Hard links to the file (1 = only this name: deleting it frees its space).
int LinkCount(const std::filesystem::path& file);

}  // namespace t3kc
