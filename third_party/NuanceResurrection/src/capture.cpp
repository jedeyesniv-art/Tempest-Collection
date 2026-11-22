// Picture capture for a native renderer (llamasoft-nuon; Tempest 3000, see the Tempest Collection's PLAN.md
// "Native renderer research"). While MPE0 draws a picture it fetches every display list entry (16 words into local
// $20100030) and the data the entries point at (shape streams, palettes, tables) with linear DMA. We record both;
// at the flip that shows the picture (VidConfig / VidChangeBase) the record is published for the frontend:
// nuance_capture_enable(1), then after each retro_run nuance_capture_get().
// nuance_capture_enable(2) also keeps the ground truth: at the flip, a copy of the frame buffer the renderers drew
// that picture into (360 x 240 YCrCbA words, original resolution only): nuance_capture_frame().
// Runs on the emulation thread only: MPE0 and MPE3 (the flips) both run there, also with the renderer threads on.
#include "basetypes.h"
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "byteswap.h"
#include "capture.h"
#include "mpe.h"
#include "NuonEnvironment.h"
#include "hires.h"

extern NuonEnvironment nuonEnv;
extern uint32_t g_vid_flips;   // video.cpp

namespace {

bool g_on = false;
bool g_truth = false;
uint32_t g_target = 0;                 // the frame buffer MPE0 last drew into
std::vector<uint32_t> g_pub_frame;     // the published picture's frame buffer (ground truth)

struct Picture
{
  std::vector<uint32_t> entries;   // 17 words each: list address, then the 16 words as MPE0 fetched them
  std::vector<uint32_t> mem;       // (address, word) pairs: the other words MPE0 read from bus memory
  std::unordered_map<uint32_t, size_t> entry_at;   // list address -> index into entries / 17
};
Picture g_cur, g_pub;
uint32_t g_pub_flip = 0;
bool g_pub_valid = false;

// The draw gate (nuance_set_picture_callback): once per picture, at its first drawing fetch.
int (*g_pic_cb)(int mpe, uint32_t flips) = nullptr;
std::mutex g_gate_mutex;
int g_gate = 0;               // this picture: 0 undecided, 1 the renderers draw it, 2 skipped (the frontend draws it)
bool g_mpe1_drawing = false;  // MPE1 is in its drawing pass (from the list head to the end; touched by MPE1 only)
uint32_t g_skipped = 0, g_drawn = 0;

}  // namespace

void CaptureLinearRead(const MPE& mpe, uint32 flags, uint32 base, uint32 intaddr)
{
  if(!g_on || mpe.mpeIndex != 0 || !(flags & (1U << 13)) || (flags & (1U << 28)) || (base >> 28) < 4)
    return;
  const uint32 words = (flags >> 16) & 0xFFU;
  if(!words)
    return;
  const uint8* p = (const uint8*)nuonEnv.GetPointerToMemory(0, intaddr & 0x207FFFFCU, false);
  if(words == 16 && (intaddr & 0x207FFFFFU) == 0x20100030U)
  {
    auto it = g_cur.entry_at.find(base);
    size_t at;
    if(it == g_cur.entry_at.end())
    {
      at = g_cur.entries.size();
      g_cur.entries.resize(at + 17);
      g_cur.entry_at[base] = at;
    }
    else
      at = it->second;
    g_cur.entries[at] = base;
    for(uint32 i = 0; i < 16; ++i)
      g_cur.entries[at + 1 + i] = SwapBytes(*(const uint32*)(p + 4 * i));
    // MPE0's job word (local $20100000, from MPE3's comm packet): 806AD2B0 scrolls the web texture by it. Recorded
    // under its local address in the memory pairs.
    g_cur.mem.push_back(0x20100000U);
    g_cur.mem.push_back(SwapBytes(*(const uint32*)(p - 0x30)));
    return;
  }
  for(uint32 i = 0; i < words; ++i)
  {
    g_cur.mem.push_back(base + 4 * i);
    g_cur.mem.push_back(SwapBytes(*(const uint32*)(p + 4 * i)));
  }
}

// The draw gate (llamasoft-nuon; native renderer step 3): with a picture callback set, the first display list entry
// fetch of a picture's drawing pass (MPE0 / MPE2: any entry fetch; MPE1: from its fetch of the list head $8004E040,
// after its pre-pass jobs have rewritten the entries, to the end of the list) calls the frontend once, with the
// fetching MPE and the flip count (the picture will be shown at the next flip). The frontend builds the picture from
// memory there; a non-zero answer means it will draw it itself: then every drawing fetch until the flip sees the end
// of the list (000000FF, as NUANCE_NODRAW), so the renderers skip it. Game logic and MPE1's compute jobs carry on.
void CaptureDrawGate(const MPE& mpe, uint32 flags, uint32 base, uint32 intaddr)
{
  if(!g_pic_cb || mpe.mpeIndex > 2 || !(flags & (1U << 13)) || (flags & (1U << 28)) || ((flags >> 16) & 0xFFU) != 16 ||
     (intaddr & 0x207FFFFFU) != 0x20100030U || (base >> 23) != (0x80000000U >> 23))
    return;
  uint8* const p = (uint8*)nuonEnv.GetPointerToMemory(mpe.mpeIndex, intaddr & 0x207FFFFCU, false);
  const uint32 type = SwapBytes(*(const uint32*)(p + 60));
  if(mpe.mpeIndex == 1)
  {
    if(base == 0x8004E040U)
      g_mpe1_drawing = true;
    const bool drawing = g_mpe1_drawing;
    if(type == 0x000000FFU)
      g_mpe1_drawing = false;
    if(!drawing)
      return;
  }
  int gate;
  {
    std::lock_guard<std::mutex> lock(g_gate_mutex);
    if(g_gate == 0)
    {
      g_gate = g_pic_cb((int)mpe.mpeIndex, g_vid_flips) ? 2 : 1;
      ++(g_gate == 2 ? g_skipped : g_drawn);
    }
    gate = g_gate;
  }
  if(gate == 2)
  {
    *(uint32*)(p + 60) = SwapBytes(0x000000FFU);
    if(mpe.mpeIndex == 1)
      g_mpe1_drawing = false;
  }
}

// Run-ahead (libretro.cpp nuance_ra_save / nuance_ra_load): this module's per-picture state lives outside the save
// state, so the snapshot keeps a copy - hidden fields must not leave the real run a half-collected future picture, a
// future picture as the published one, or the gate's decision for a future picture (which made the real run skip its
// own picture's renderers: blank pictures, 2026-11-18).
void CaptureRaState(bool restore)
{
  static Picture cur, pub;
  static uint32_t pub_flip = 0, target = 0;
  static bool pub_valid = false, mpe1_drawing = false;
  static int gate = 0;
  static std::vector<uint32_t> pub_frame;
  std::lock_guard<std::mutex> lock(g_gate_mutex);
  if(!restore)
  {
    cur = g_cur; pub = g_pub; pub_flip = g_pub_flip; pub_valid = g_pub_valid; target = g_target;
    mpe1_drawing = g_mpe1_drawing; gate = g_gate;
    if(g_truth) pub_frame = g_pub_frame;
  }
  else
  {
    g_cur = cur; g_pub = pub; g_pub_flip = pub_flip; g_pub_valid = pub_valid; g_target = target;
    g_mpe1_drawing = mpe1_drawing; g_gate = gate;
    if(g_truth) g_pub_frame = pub_frame;
  }
}

extern "C" void nuance_set_picture_callback(int (*cb)(int mpe, uint32_t flips))
{
  std::lock_guard<std::mutex> lock(g_gate_mutex);
  g_pic_cb = cb;
  g_gate = 0;
}

// Pictures the gate let the renderers draw / skipped so far.
extern "C" void nuance_gate_stats(uint32_t* drawn, uint32_t* skipped)
{
  if(drawn) *drawn = g_drawn;
  if(skipped) *skipped = g_skipped;
}

void CaptureFlip()
{
  {
    std::lock_guard<std::mutex> lock(g_gate_mutex);   // (the renderers are done: MPE3 waits for them before it flips)
    g_gate = 0;
  }
  if(!g_on)
    return;
  std::swap(g_pub, g_cur);
  g_pub_flip = g_vid_flips;
  // The renderers are done (MPE3 waits for them before it flips): the buffer they drew is the picture.
  if(g_truth && g_target && !g_hires)
  {
    const uint32* fb = (const uint32*)nuonEnv.GetPointerToMemory((g_target >> 23) & 0x1FU, g_target, false);
    g_pub_frame.resize(360U * 240U);
    for(uint32 i = 0; i < 360U * 240U; ++i)
      g_pub_frame[i] = SwapBytes(fb[i]);
  }
  g_pub_valid = true;
  g_cur.entries.clear();
  g_cur.mem.clear();
  g_cur.entry_at.clear();
}

void CaptureBiLinear(const MPE& mpe, uint32 flags, uint32 base)
{
  // MPE0's pixel writes into one of the game's three frame buffers name the picture's buffer.
  if(!g_on || mpe.mpeIndex != 0 || (flags & (1U << 13)))
    return;
  if(base == 0x40010000U || base == 0x40064600U || base == 0x400B8C00U)
    g_target = base;
}

extern "C" void nuance_capture_enable(int on)
{
  g_on = on != 0;
  g_truth = on == 2;
  g_pub_frame.clear();
  g_pub_valid = false;
  g_cur = Picture();
}

// The newest published picture: its flip count (as nuance_get_run_stats' vid_flips after that flip), the entries
// (17 words each: list address + 16 words) and the (address, word) pairs. The pointers stay valid until the next
// retro_run. Returns 0 if nothing was published yet.
extern "C" int nuance_capture_get(uint32_t* flip, const uint32_t** entries, size_t* entry_words, const uint32_t** mem,
                                  size_t* mem_words)
{
  if(!g_pub_valid)
    return 0;
  if(flip) *flip = g_pub_flip;
  if(entries) *entries = g_pub.entries.data();
  if(entry_words) *entry_words = g_pub.entries.size();
  if(mem) *mem = g_pub.mem.data();
  if(mem_words) *mem_words = g_pub.mem.size();
  return 1;
}

// The published picture's ground truth (nuance_capture_enable(2)): 360 x 240 YCrCbA words, or 0 if none.
extern "C" int nuance_capture_frame(const uint32_t** pixels, int* width, int* height)
{
  if(!g_pub_valid || g_pub_frame.empty())
    return 0;
  if(pixels) *pixels = g_pub_frame.data();
  if(width) *width = 360;
  if(height) *height = 240;
  return 1;
}
