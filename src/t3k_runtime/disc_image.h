// Disc image access (llamasoft-nuon): a plain 2048-byte-sector ISO, or a compressed NUZ image.
//
// NUZ ("LLNUONZ1", written by the frontend's tools/t3k_compress.py): the disc's sectors as a list of segments,
// each either raw sectors or one Ogg Vorbis stream that decodes to those sectors as 16-bit big-endian stereo PCM
// (512 frames a sector) - for discs whose bulk is streamed music (Tempest 3000's nuon.dat). Layout, little-endian:
//   header  : char magic[8] "LLNUONZ1", u32 version (1), u32 total sectors, u32 segment count, u32 reserved[3]
//   segments: u32 first sector, u32 sector count, u32 kind (0 raw, 1 vorbis), u32 reserved, u64 offset, u64 size
// The sectors read back from a vorbis segment are the decoded music, not the original bytes (lossy).
#ifndef DISC_IMAGE_H
#define DISC_IMAGE_H

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class DiscImage final {
public:
  DiscImage();
  ~DiscImage();
  DiscImage(const DiscImage&) = delete;
  DiscImage& operator=(const DiscImage&) = delete;

  bool open(const char* path);
  void close();
  bool isOpen() const { return fp != nullptr; }
  bool isNuz() const { return nuz; }
  uint64_t size() const { return totalBytes; }
  // Reads len bytes at byte offset off; returns the bytes read (short at the end of the disc).
  size_t read(uint64_t off, void* buf, size_t len);

  // True if the file starts with the NUZ magic.
  static bool IsNuzFile(const char* path);

private:
  struct Segment;
  size_t readSegment(Segment& s, uint64_t off, uint8_t* out, size_t len);
  bool decodeFrames(Segment& s, uint64_t frame, uint32_t frames, uint8_t* out);
  bool decodeDirect(Segment& s, uint64_t frame, uint32_t frames, uint8_t* out);
  void waitPrefetch();

  FILE* fp = nullptr;
  std::string path;
  bool nuz = false;
  uint64_t totalBytes = 0;
  std::vector<std::unique_ptr<Segment>> segs;
  // background prefetch: after a read, a worker decodes the chunk that follows it (the game streams a track in
  // sequential 1024-sector reads), so the next read is a copy instead of ~15-40 ms of decoding on the emulation thread.
  // The bytes are the same either way (the same decoder decoding the same frames in order).
  std::thread worker;
  Segment* pfSeg = nullptr;
  uint64_t pfFrame = 0;
  uint32_t pfFrames = 0;
  bool pfOk = false;
  std::vector<uint8_t> pfBuf;
};

#endif
