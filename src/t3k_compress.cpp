// t3k_compress.cpp - see t3k_compress.h. A port of tools/t3k_compress.py (the same steps and checks).
#include "t3k_compress.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"   // (third_party/NuanceResurrection/src; compiled there as its own C file)

namespace fs = std::filesystem;

namespace t3kc {
namespace {

constexpr uint32_t kSector = 2048;
constexpr uint32_t kTableOff = 0x104340;   // in nuon.run: 19 x (start block, end block), big-endian
constexpr int kTracks = 19;
constexpr int kRate = 32000;
constexpr double kLevel = 0.2;             // libsndfile's Vorbis compression level (0 best .. 1): ~260 kbps here

// ---- libsndfile, loaded at run time (only the calls we need) ----
using sf_count_t = int64_t;
struct SF_INFO { sf_count_t frames; int samplerate, channels, format, sections, seekable; };
struct SF_VIRTUAL_IO {
  sf_count_t (*get_filelen)(void*);
  sf_count_t (*seek)(sf_count_t, int, void*);
  sf_count_t (*read)(void*, sf_count_t, void*);
  sf_count_t (*write)(const void*, sf_count_t, void*);
  sf_count_t (*tell)(void*);
};
constexpr int SFM_WRITE = 0x20, SF_FORMAT_OGG = 0x200000, SF_FORMAT_VORBIS = 0x0060, SFC_SET_COMPRESSION_LEVEL = 0x1301;
struct Sndfile {
  HMODULE dll = nullptr;
  void* (*open_virtual)(SF_VIRTUAL_IO*, int, SF_INFO*, void*) = nullptr;
  int (*command)(void*, int, void*, int) = nullptr;
  sf_count_t (*writef_short)(void*, const short*, sf_count_t) = nullptr;
  int (*close)(void*) = nullptr;
  const char* (*strerror)(void*) = nullptr;
  bool Load(const fs::path& p) {
    dll = LoadLibraryW(p.wstring().c_str());
    if (!dll) return false;
    auto get = [&](auto& f, const char* n) { f = reinterpret_cast<std::remove_reference_t<decltype(f)>>(GetProcAddress(dll, n)); return f != nullptr; };
    return get(open_virtual, "sf_open_virtual") && get(command, "sf_command") && get(writef_short, "sf_writef_short") &&
           get(close, "sf_close") && get(strerror, "sf_strerror");
  }
};

// A growable memory file for libsndfile's virtual IO.
struct MemFile {
  std::vector<uint8_t> d;
  sf_count_t pos = 0;
  static sf_count_t Len(void* u) { return sf_count_t(static_cast<MemFile*>(u)->d.size()); }
  static sf_count_t Seek(sf_count_t off, int whence, void* u) {
    auto* m = static_cast<MemFile*>(u);
    m->pos = whence == SEEK_SET ? off : whence == SEEK_CUR ? m->pos + off : sf_count_t(m->d.size()) + off;
    return m->pos;
  }
  static sf_count_t Read(void* p, sf_count_t n, void* u) {
    auto* m = static_cast<MemFile*>(u);
    n = std::max<sf_count_t>(0, std::min<sf_count_t>(n, sf_count_t(m->d.size()) - m->pos));
    std::memcpy(p, m->d.data() + m->pos, size_t(n));
    m->pos += n;
    return n;
  }
  static sf_count_t Write(const void* p, sf_count_t n, void* u) {
    auto* m = static_cast<MemFile*>(u);
    if (size_t(m->pos + n) > m->d.size()) m->d.resize(size_t(m->pos + n));
    std::memcpy(m->d.data() + m->pos, p, size_t(n));
    m->pos += n;
    return n;
  }
  static sf_count_t Tell(void* u) { return static_cast<MemFile*>(u)->pos; }
};

uint32_t Le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

bool ReadAt(std::ifstream& f, uint64_t off, void* buf, size_t n) {
  f.clear();
  f.seekg(std::streamoff(off));
  f.read(static_cast<char*>(buf), std::streamsize(n));
  return size_t(f.gcount()) == n;
}

// The ISO's files by upper-case path: (lba, size). Returns the total sectors (0 = not an ISO9660 image).
struct IsoFile { uint32_t lba = 0, size = 0; };
uint32_t ListIso(std::ifstream& f, std::vector<std::pair<std::string, IsoFile>>* out) {
  uint8_t pvd[kSector];
  if (!ReadAt(f, 16ull * kSector, pvd, kSector) || std::memcmp(pvd + 1, "CD001", 5) != 0) return 0;
  const uint32_t total = Le32(pvd + 80);
  std::vector<std::pair<std::string, IsoFile>> dirs{{"", {Le32(pvd + 156 + 2), Le32(pvd + 156 + 10)}}};
  for (size_t k = 0; k < dirs.size() && k < 64; ++k) {
    const auto [path, dir] = dirs[k];
    std::vector<uint8_t> d(dir.size);
    if (!ReadAt(f, uint64_t(dir.lba) * kSector, d.data(), d.size())) continue;
    for (size_t i = 0; i < d.size();) {
      const uint8_t n = d[i];
      if (n == 0) { i = (i / kSector + 1) * kSector; continue; }
      if (i + 33 > d.size() || i + n > d.size()) break;
      const uint8_t nl = d[i + 32];
      std::string name(reinterpret_cast<const char*>(&d[i + 33]), nl);
      const IsoFile e{Le32(&d[i + 2]), Le32(&d[i + 10])};
      const bool is_dir = d[i + 25] & 2;
      i += n;
      if (nl == 1 && (name[0] == 0 || name[0] == 1)) continue;
      name = name.substr(0, name.find(';'));
      for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
      (is_dir ? dirs : *out).push_back({path + "/" + name, e});
    }
  }
  return total;
}

void Fail(Job* job, const std::string& why) {
  {
    std::lock_guard<std::mutex> l(job->m);
    job->error = why;
  }
  std::fprintf(stderr, "t3k compress: %s\n", why.c_str());
  job->ok = false;
  job->phase = 3;
}

void Run(Job* job, fs::path iso, fs::path nuz, fs::path dll_path) {
  std::ifstream f(iso, std::ios::binary);
  if (!f) return Fail(job, "can't open " + iso.filename().string());
  std::vector<std::pair<std::string, IsoFile>> files;
  const uint32_t total = ListIso(f, &files);
  if (!total) return Fail(job, "not a disc image (ISO9660)");
  IsoFile dat, run;
  for (auto& [p, e] : files) {
    if (p == "/NUON/NUON.DAT") dat = e;
    if (p == "/NUON/NUON.RUN") run = e;
  }
  if (!dat.size || !run.size) return Fail(job, "no NUON/nuon.dat + nuon.run: not the Tempest 3000 disc");
  uint8_t tb[8 * kTracks];
  if (!ReadAt(f, uint64_t(run.lba) * kSector + kTableOff, tb, sizeof tb)) return Fail(job, "can't read nuon.run");
  std::vector<std::pair<uint32_t, uint32_t>> tracks;
  for (int i = 0; i < kTracks; ++i) tracks.push_back({Be32(tb + 8 * i), Be32(tb + 8 * i + 4)});
  std::sort(tracks.begin(), tracks.end());
  const uint32_t dat_sectors = dat.size / kSector;
  bool tiles = dat.size % kSector == 0 && tracks.front().first == 0 && tracks.back().second == dat_sectors;
  for (int i = 0; i + 1 < kTracks; ++i) tiles = tiles && tracks[i].second == tracks[i + 1].first;
  if (!tiles) return Fail(job, "the music table doesn't cover nuon.dat: a different version of the disc?");
  f.close();

  Sndfile sf;
  if (!sf.Load(dll_path)) return Fail(job, "libsndfile_x64.dll missing next to the launcher");

  // encode: each track on the next free worker
  std::vector<std::vector<uint8_t>> ogg(kTracks);
  std::vector<std::string> errs(kTracks);
  std::atomic<int> next{0};
  auto worker = [&] {
    std::ifstream in(iso, std::ios::binary);
    for (int t; !job->cancel && (t = next++) < kTracks;) {
      const uint64_t frames = uint64_t(tracks[t].second - tracks[t].first) * (kSector / 4);
      std::vector<int16_t> pcm(frames * 2);
      if (!ReadAt(in, uint64_t(dat.lba + tracks[t].first) * kSector, pcm.data(), pcm.size() * 2)) {
        errs[t] = "short read";
        continue;
      }
      for (int16_t& s : pcm) s = int16_t(uint16_t(s) >> 8 | uint16_t(s) << 8);   // big-endian on the disc
      MemFile mem;
      SF_VIRTUAL_IO io{MemFile::Len, MemFile::Seek, MemFile::Read, MemFile::Write, MemFile::Tell};
      SF_INFO info{0, kRate, 2, SF_FORMAT_OGG | SF_FORMAT_VORBIS, 0, 0};
      void* h = sf.open_virtual(&io, SFM_WRITE, &info, &mem);
      if (!h) {
        errs[t] = std::string("encoder: ") + sf.strerror(nullptr);
        continue;
      }
      double level = kLevel;
      sf.command(h, SFC_SET_COMPRESSION_LEVEL, &level, sizeof level);
      for (uint64_t i = 0; i < frames && !job->cancel; i += 4096)   // (in chunks: one huge write overflows its stack)
        sf.writef_short(h, pcm.data() + i * 2, sf_count_t(std::min<uint64_t>(4096, frames - i)));
      sf.close(h);
      // check: the stream is exactly the track's length
      int err = 0;
      stb_vorbis* v = stb_vorbis_open_memory(mem.d.data(), int(mem.d.size()), &err, nullptr);
      const uint64_t got = v ? stb_vorbis_stream_length_in_samples(v) : 0;
      if (v) stb_vorbis_close(v);
      if (got != frames) {
        errs[t] = "track " + std::to_string(t + 1) + " came out " + std::to_string(got) + " frames, not " +
                  std::to_string(frames);
        continue;
      }
      ogg[t] = std::move(mem.d);
      ++job->tracks_done;
    }
  };
  // One encoder thread by default (~30 s, a one-off): the first 4-thread run coincided with a hard machine freeze
  // (2026-11-17, probably unrelated, kept gentle to be safe). T3KC_THREADS=N (tests) forces a count; each holds a
  // track (up to 60 MB).
  int n = 1;
  if (const char* env = std::getenv("T3KC_THREADS")) n = std::clamp(std::atoi(env), 1, 16);
  std::vector<std::thread> pool;
  for (int i = 0; i < n; ++i) pool.emplace_back(worker);
  for (auto& t : pool) t.join();
  if (job->cancel) return Fail(job, "cancelled");
  for (auto& e : errs)
    if (!e.empty()) return Fail(job, e);

  // write: header, segment table, the raw sectors before nuon.dat, the streams, the raw sectors after
  job->phase = 1;
  struct Seg { uint32_t first, count, kind; uint64_t size; const std::vector<uint8_t>* data; };
  std::vector<Seg> segs{{0, dat.lba, 0, uint64_t(dat.lba) * kSector, nullptr}};
  for (int t = 0; t < kTracks; ++t)
    segs.push_back({dat.lba + tracks[t].first, tracks[t].second - tracks[t].first, 1, ogg[t].size(), &ogg[t]});
  const uint32_t after = dat.lba + dat_sectors;
  segs.push_back({after, total - after, 0, uint64_t(total - after) * kSector, nullptr});
  const fs::path tmp = nuz.string() + ".tmp";
  {
    std::ifstream in(iso, std::ios::binary);
    std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
    if (!o) return Fail(job, "can't write " + tmp.filename().string());
    auto put32 = [&](uint32_t v) { o.write(reinterpret_cast<const char*>(&v), 4); };
    auto put64 = [&](uint64_t v) { o.write(reinterpret_cast<const char*>(&v), 8); };
    o.write("LLNUONZ1", 8);
    for (uint32_t v : {1u, total, uint32_t(segs.size()), 0u, 0u, 0u}) put32(v);
    uint64_t off = 32 + 32ull * segs.size();
    for (const Seg& s : segs) {
      put32(s.first); put32(s.count); put32(s.kind); put32(0); put64(off); put64(s.size);
      off += s.size;
    }
    std::vector<uint8_t> buf(1 << 20);
    for (const Seg& s : segs) {
      if (s.kind == 1) {
        o.write(reinterpret_cast<const char*>(s.data->data()), std::streamsize(s.data->size()));
        continue;
      }
      for (uint64_t done = 0; done < s.size;) {
        const size_t k = size_t(std::min<uint64_t>(buf.size(), s.size - done));
        if (!ReadAt(in, uint64_t(s.first) * kSector + done, buf.data(), k)) return Fail(job, "short read from the disc");
        o.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(k));
        done += k;
      }
    }
    if (!o.flush()) return Fail(job, "writing failed (disk full?)");
  }
  // check: the raw sectors came through unchanged
  job->phase = 2;
  {
    std::ifstream in(iso, std::ios::binary), back(tmp, std::ios::binary);
    std::vector<uint8_t> a(1 << 20), b(1 << 20);
    uint64_t off = 32 + 32ull * segs.size();
    for (const Seg& s : segs) {
      if (s.kind == 0)
        for (uint64_t done = 0; done < s.size;) {
          const size_t k = size_t(std::min<uint64_t>(a.size(), s.size - done));
          if (!ReadAt(in, uint64_t(s.first) * kSector + done, a.data(), k) || !ReadAt(back, off + done, b.data(), k) ||
              std::memcmp(a.data(), b.data(), k) != 0) {
            back.close();
            std::error_code ec;
            fs::remove(tmp, ec);
            return Fail(job, "check failed: the copied sectors differ");
          }
          done += k;
        }
      off += s.size;
    }
  }
  std::error_code ec;
  fs::rename(tmp, nuz, ec);
  if (ec) return Fail(job, "can't rename the result: " + ec.message());
  std::fprintf(stderr, "t3k compress: wrote %s (%llu bytes)\n", nuz.string().c_str(),
               (unsigned long long)fs::file_size(nuz, ec));
  job->ok = true;
  job->phase = 3;
}

}  // namespace

void Start(Job* job, const fs::path& iso, const fs::path& nuz, const fs::path& sndfile_dll) {
  job->worker = std::thread(Run, job, iso, nuz, sndfile_dll);
}

int LinkCount(const fs::path& file) {
  HANDLE h = CreateFileW(file.wstring().c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return 1;
  BY_HANDLE_FILE_INFORMATION info{};
  const int n = GetFileInformationByHandle(h, &info) ? int(info.nNumberOfLinks) : 1;
  CloseHandle(h);
  return n;
}

}  // namespace t3kc
