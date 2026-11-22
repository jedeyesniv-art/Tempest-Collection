// Disc image access (llamasoft-nuon): plain ISO or compressed NUZ - see disc_image.h.
#include "disc_image.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#if defined(_MSC_VER) || defined(_WIN32)
#define DI_FSEEK(f, off) _fseeki64(f, (__int64)(off), SEEK_SET)
#else
#define DI_FSEEK(f, off) fseeko(f, (off_t)(off), SEEK_SET)
#endif

namespace {
constexpr char kMagic[8] = {'L', 'L', 'N', 'U', 'O', 'N', 'Z', '1'};
constexpr uint32_t kSector = 2048;
constexpr uint32_t kFramesPerSector = kSector / 4; // 16-bit stereo
constexpr int kMaxOpenDecoders = 2;

uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint64_t rd64(const uint8_t* p) { return rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

bool LogOn() {
  static const bool on = getenv("NUANCE_NUZLOG") != nullptr;
  return on;
}
} // namespace

struct DiscImage::Segment {
  uint32_t first = 0, count = 0, kind = 0;
  uint64_t offset = 0, size = 0;
  // vorbis decoder state (kind 1)
  FILE* vfp = nullptr;
  stb_vorbis* v = nullptr;
  uint64_t cursor = 0;   // next frame the decoder will produce
  uint64_t lastUse = 0;

  void closeDecoder() {
    if (v) stb_vorbis_close(v);
    if (vfp) fclose(vfp);
    v = nullptr;
    vfp = nullptr;
    cursor = 0;
  }
  ~Segment() { closeDecoder(); }
};

DiscImage::DiscImage() = default;
DiscImage::~DiscImage() { close(); }

bool DiscImage::IsNuzFile(const char* p) {
  FILE* f = fopen(p, "rb");
  if (!f) return false;
  char m[8] = {};
  const size_t n = fread(m, 1, 8, f);
  fclose(f);
  return n == 8 && memcmp(m, kMagic, 8) == 0;
}

bool DiscImage::open(const char* p) {
  close();
  fp = fopen(p, "rb");
  if (!fp) return false;
  path = p;
  uint8_t h[32];
  if (fread(h, 1, 32, fp) == 32 && memcmp(h, kMagic, 8) == 0) {
    const uint32_t version = rd32(h + 8), sectors = rd32(h + 12), nseg = rd32(h + 16);
    if (version != 1 || nseg == 0 || nseg > 4096) {
      fprintf(stderr, "NUZ %s: unsupported version %u / %u segments\n", p, version, nseg);
      close();
      return false;
    }
    std::vector<uint8_t> t((size_t)nseg * 32);
    if (fread(t.data(), 1, t.size(), fp) != t.size()) {
      close();
      return false;
    }
    uint32_t next = 0;
    for (uint32_t i = 0; i < nseg; i++) {
      const uint8_t* e = &t[(size_t)i * 32];
      auto s = std::make_unique<Segment>();
      s->first = rd32(e);
      s->count = rd32(e + 4);
      s->kind = rd32(e + 8);
      s->offset = rd64(e + 16);
      s->size = rd64(e + 24);
      if (s->first != next || s->kind > 1) { // segments must tile the disc in order
        fprintf(stderr, "NUZ %s: bad segment %u\n", p, i);
        close();
        return false;
      }
      next = s->first + s->count;
      segs.push_back(std::move(s));
    }
    if (next != sectors) {
      fprintf(stderr, "NUZ %s: segments cover %u of %u sectors\n", p, next, sectors);
      close();
      return false;
    }
    nuz = true;
    totalBytes = (uint64_t)sectors * kSector;
  } else {
    nuz = false;
    DI_FSEEK(fp, 0);
#if defined(_MSC_VER) || defined(_WIN32)
    _fseeki64(fp, 0, SEEK_END);
    totalBytes = (uint64_t)_ftelli64(fp);
#else
    fseeko(fp, 0, SEEK_END);
    totalBytes = (uint64_t)ftello(fp);
#endif
  }
  return true;
}

void DiscImage::close() {
  waitPrefetch();
  pfSeg = nullptr;
  segs.clear();
  if (fp) fclose(fp);
  fp = nullptr;
  nuz = false;
  totalBytes = 0;
}

size_t DiscImage::read(uint64_t off, void* buf, size_t len) {
  if (!fp || off >= totalBytes) return 0;
  if (len > totalBytes - off) len = (size_t)(totalBytes - off);
  if (!nuz) {
    DI_FSEEK(fp, off);
    return fread(buf, 1, len, fp);
  }
  uint8_t* out = static_cast<uint8_t*>(buf);
  size_t done = 0;
  while (done < len) {
    const uint64_t pos = off + done;
    const uint32_t sector = (uint32_t)(pos / kSector);
    // segments are few (tens): a linear search is fine
    Segment* s = nullptr;
    for (auto& x : segs)
      if (sector >= x->first && sector < x->first + x->count) { s = x.get(); break; }
    if (!s) break;
    const uint64_t segStart = (uint64_t)s->first * kSector, segEnd = segStart + (uint64_t)s->count * kSector;
    const size_t n = (size_t)std::min<uint64_t>(len - done, segEnd - pos);
    const size_t got = readSegment(*s, pos - segStart, out + done, n);
    done += got;
    if (got != n) break;
  }
  return done;
}

size_t DiscImage::readSegment(Segment& s, uint64_t off, uint8_t* out, size_t len) {
  if (s.kind == 0) {
    DI_FSEEK(fp, s.offset + off);
    return fread(out, 1, len, fp);
  }
  // vorbis: whole frames covering [off, off + len)
  const uint64_t f0 = off / 4, f1 = (off + len + 3) / 4;
  const uint32_t frames = (uint32_t)(f1 - f0);
  if (off % 4 == 0 && len % 4 == 0)
    return decodeFrames(s, f0, frames, out) ? len : 0;
  std::vector<uint8_t> tmp((size_t)frames * 4);
  if (!decodeFrames(s, f0, frames, tmp.data())) return 0;
  memcpy(out, tmp.data() + (off - f0 * 4), len);
  return len;
}

void DiscImage::waitPrefetch() {
  if (worker.joinable()) worker.join();
}

bool DiscImage::decodeFrames(Segment& s, uint64_t frame, uint32_t frames, uint8_t* out) {
  constexpr uint32_t kPrefetchFrames = 1024 * kFramesPerSector; // the game's refill size
  const auto t0 = std::chrono::steady_clock::now();
  waitPrefetch(); // the worker may be using a decoder
  bool ok;
  const bool hit = pfSeg == &s && pfOk && pfFrame == frame && frames == pfFrames;
  if (hit) {
    memcpy(out, pfBuf.data(), (size_t)frames * 4);
    ok = true;
  } else {
    ok = decodeDirect(s, frame, frames, out);
  }
  pfSeg = nullptr;
  if (LogOn())
    fprintf(stderr, "NUZ: read segment @%u frames %llu+%u: %s, %.1f ms on the caller\n", s.first,
            (unsigned long long)frame, frames, hit ? "prefetched" : "decoded",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
  // decode the next chunk of this track in the background
  const uint64_t next = frame + frames, end = (uint64_t)s.count * kFramesPerSector;
  if (ok && next < end) {
    pfSeg = &s;
    pfFrame = next;
    pfFrames = (uint32_t)std::min<uint64_t>(kPrefetchFrames, end - next);
    pfOk = false;
    pfBuf.resize((size_t)pfFrames * 4);
    worker = std::thread([this, &s] { pfOk = decodeDirect(s, pfFrame, pfFrames, pfBuf.data()); });
  }
  return ok;
}

bool DiscImage::decodeDirect(Segment& s, uint64_t frame, uint32_t frames, uint8_t* out) {
  static uint64_t useClock = 0;
  s.lastUse = ++useClock;
  const auto t0 = std::chrono::steady_clock::now();
  if (!s.v) {
    // keep only a few decoders open (each holds a file handle and its own buffers)
    int open = 0;
    for (auto& x : segs) open += x->v != nullptr;
    while (open >= kMaxOpenDecoders) { // (runs on the caller only: a prefetch's decoder is already open)
      Segment* oldest = nullptr;
      for (auto& x : segs)
        if (x->v && (!oldest || x->lastUse < oldest->lastUse)) oldest = x.get();
      oldest->closeDecoder();
      open--;
    }
    s.vfp = fopen(path.c_str(), "rb");
    if (!s.vfp) return false;
    DI_FSEEK(s.vfp, s.offset);
    int err = 0;
    s.v = stb_vorbis_open_file_section(s.vfp, 0, &err, nullptr, (unsigned)s.size);
    if (!s.v) {
      fprintf(stderr, "NUZ: vorbis open failed (error %d) for sectors %u..\n", err, s.first);
      fclose(s.vfp);
      s.vfp = nullptr;
      return false;
    }
    s.cursor = 0;
  }
  bool seeked = false;
  if (s.cursor != frame) {
    if (!stb_vorbis_seek(s.v, (unsigned)frame)) {
      fprintf(stderr, "NUZ: vorbis seek to %llu failed\n", (unsigned long long)frame);
      return false;
    }
    s.cursor = frame;
    seeked = true;
  }
  short* pcm = reinterpret_cast<short*>(out);
  uint32_t got = 0;
  while (got < frames) {
    const int n = stb_vorbis_get_samples_short_interleaved(s.v, 2, pcm + (size_t)got * 2, (int)(frames - got) * 2);
    if (n <= 0) break;
    got += (uint32_t)n;
  }
  s.cursor += got;
  if (got < frames) memset(pcm + (size_t)got * 2, 0, (size_t)(frames - got) * 4); // past the stream's end
  // host-endian samples -> the disc's big-endian
  for (uint32_t i = 0; i < frames * 2; i++) {
    const uint16_t x = (uint16_t)pcm[i];
    pcm[i] = (short)(uint16_t)((x >> 8) | (x << 8));
  }
  if (LogOn()) {
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    fprintf(stderr, "NUZ: segment @%u frames %llu+%u%s: %.1f ms\n", s.first, (unsigned long long)frame, frames,
            seeked ? " (seek)" : "", ms);
  }
  return true;
}
