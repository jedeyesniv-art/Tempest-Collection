// Tempest 3000 at 720x480 (llamasoft-nuon); see hires.h.
#include "basetypes.h"
#include <cstdio>
#include <cstdlib>
#include "byteswap.h"
#include "hires.h"
#include "mpe.h"
#include "NuonEnvironment.h"
#include "NuonMemoryMap.h"
#include "Bios.h"

extern NuonEnvironment nuonEnv;

bool g_hires = getenv("NUANCE_HIRES") != nullptr;

namespace {

bool g_templates = getenv("NUANCE_HIRES_TPL") != nullptr;   // NUANCE_HIRES_TPL: also move the render templates' centres

// The game's three 360x240x4 frame buffers and the 720x480x4 buffers that replace them (above 8 MB).
constexpr uint32 kOldSize = 360U * 240U * 4U, kNewSize = 720U * 480U * 4U;
constexpr uint32 kOld[3] = {0x40010000U, 0x40064600U, 0x400B8C00U};
constexpr uint32 kNew[3] = {0x40800000U, 0x40800000U + kNewSize, 0x40800000U + 2U * kNewSize};

struct Patch { uint32 addr, orig, hires; };

// Renderer module library (system RAM): screen centre constants and pixel-DMA flags with the line width.
const Patch kCode[] = {
#include "hires_table.inc"
};

// Game data (system RAM): frame size, clip limits, DMA flags, 16.16 screen size.
const Patch kData[] = {
  {0x800B2E54U, 0x016800F0U, 0x02D001E0U}, {0x800B3094U, 0x016800F0U, 0x02D001E0U},
  {0x800B7B50U, 0x016800F0U, 0x02D001E0U}, {0x800B7C70U, 0x016800F0U, 0x02D001E0U},
  {0x800B7EC4U, 0x016800F0U, 0x02D001E0U}, {0x800B82D4U, 0x016800F0U, 0x02D001E0U},
  {0x800B8500U, 0x016800F0U, 0x02D001E0U},
  {0x800B1730U, 0x002DC840U, 0x005AC840U}, {0x800B1738U, 0x01670000U, 0x02CF0000U}, {0x800B173CU, 0x00EF0000U, 0x01DF0000U},
  {0x8004E3E0U, 0x002DE840U, 0x005AE840U}, {0x800B2F70U, 0x002DE840U, 0x005AE840U}, {0x800B2FB0U, 0x002DE840U, 0x005AE840U},
  {0x800B2FF0U, 0x002DE840U, 0x005AE840U}, {0x800B30B0U, 0x002DE840U, 0x005AE840U},
  {0x800BA3B0U, 0x01680000U, 0x02D00000U}, {0x800BA3B4U, 0x00F00000U, 0x01E00000U},
};

// Render templates' screen centres (180, 120) and (180, 152).
const uint32 kCentre78[] = {0x800B3010U, 0x800B3050U, 0x800B3090U, 0x800B4924U, 0x800B4BDCU, 0x800B6E70U, 0x800B77F0U,
                            0x800B84C4U, 0x800B9E30U, 0x800BA05CU, 0x800BAB90U, 0x800BAB94U};
const uint32 kCentre98[] = {0x800B33A0U, 0x800B56E0U, 0x800B5870U, 0x800B5A20U, 0x800B8B10U, 0x800B9180U, 0x800B9280U,
                            0x800B9610U, 0x800B96E0U, 0x800B9CF0U, 0x800B9F70U};

uint8* Sys(uint32 addr) { return &nuonEnv.systemBusDRAM[addr & SYSTEM_BUS_VALID_MEMORY_MASK]; }
uint32 Get(uint32 addr) { return SwapBytes(*(uint32*)Sys(addr)); }
void Put(uint32 addr, uint32 v) { *(uint32*)Sys(addr) = SwapBytes(v); }

// NUANCE_HIRES_SKIP=<hex address>,...: leave these sites unpatched (investigations).
bool Skipped(uint32 addr)
{
  static const char* const skip = getenv("NUANCE_HIRES_SKIP");
  if(!skip)
    return false;
  for(const char* q = skip; *q; )
  {
    char* e;
    const uint32 a = (uint32)strtoul(q, &e, 16);
    if(e == q)
      break;
    if(a == addr)
      return true;
    q = *e ? e + 1 : e;
  }
  return false;
}

int Apply(const Patch* p, size_t n)
{
  int done = 0;
  for(size_t i = 0; i < n; ++i)
    if(Get(p[i].addr) == p[i].orig && !Skipped(p[i].addr))
    {
      Put(p[i].addr, p[i].hires);
      ++done;
    }
  return done;
}

// Two 16-bit halves (x, y), each doubled when it is a plausible screen value.
uint32 DoublePair(uint32 v)
{
  const uint32 hi = v >> 16, lo = v & 0xFFFFU;
  if(!hi || !lo || hi >= 1024 || lo >= 1024)
    return v;
  return (hi * 2U) << 16 | (lo * 2U);
}

// The other direction: halve both halves (written back entries return to the game's units).
uint32 HalvePair(uint32 v)
{
  const uint32 hi = v >> 16, lo = v & 0xFFFFU;
  if(!hi || !lo || hi >= 2048 || lo >= 2048)
    return v;
  return (hi / 2U) << 16 | (lo / 2U);
}

// A display list entry (16 words, the type in the last) between the game's 360x240 units (MPE3 builds them) and the
// renderers' 720x480 ones: forward when a renderer reads it, back when one writes it to system RAM again.
bool EntryTransform(uint32* e, bool forward)
{
  const uint32 type = e[15];
  // Any word holding exactly the 360-wide pixel-DMA flags of the frame buffers (read / write) gets the 720 width.
  bool changed = false;
  for(int w = 0; w < 15; ++w)
  {
    const uint32 v = e[w] & ~0x2000U;
    if(forward ? v == 0x002DC840U : v == 0x005AC840U)
    {
      e[w] = (e[w] & ~0xFF0000U) | ((forward ? 0x5AU : 0x2DU) << 16);
      changed = true;
    }
  }
  if(e[0] == 0 && e[1] == (forward ? 0x016800F0U : 0x02D001E0U))   // a full-screen rectangle (clears)
  {
    e[1] = forward ? 0x02D001E0U : 0x016800F0U;
    return true;
  }
  switch(type)
  {
    case 0x00300004U:   // zoom feedback (806A5280): centre (word 0), 16.16 zoom (4, 5), source buffer (9)
      // Word 1 (360x240) stays: the source offset works out as word 1 - centre, so doubling it shifted the reads
      // by (360, 240) into unwritten memory (pink). The area covered follows the clip box.
      e[0] = forward ? DoublePair(e[0]) : HalvePair(e[0]);
      return true;
    case 0x00710001U:   // 2D sprites and vector objects: screen position (word 0), size (word 4)
      e[0] = forward ? DoublePair(e[0]) : HalvePair(e[0]);
      // Sizes of 256 and more stay (the menu's TEMPEST logo, 496, vanished when doubled: an overflow somewhere in
      // 806AA120's handlers). On the way back only what was doubled is halved.
      if(forward ? ((e[4] >> 16) < 256 && (e[4] & 0xFFFFU) < 256) : ((e[4] >> 16) < 512 && (e[4] & 0xFFFFU) < 512))
        e[4] = forward ? DoublePair(e[4]) : HalvePair(e[4]);
      return true;
    case 0x0000000FU:   // feedback background (806B0B30): area (width, height x 16) in word 1 - 384 x 3840
    {
      const uint32 w = e[1] >> 16, h = e[1] & 0xFFFFU;
      if(forward ? (w <= 0x200 && h <= 0x1000) : (w <= 0x400 && h <= 0x2000))
        e[1] = forward ? (w * 2U) << 16 | (h * 2U) : (w / 2U) << 16 | (h / 2U);
      return true;
    }
    case 0x0010000DU:   // big glowing text (806AF1E8): position (word 0), 16.16 x / y scale (2, 3), DMA flags (8)
      if(forward)
      {
        e[0] = DoublePair(e[0]);
        for(int w : {2, 3})
          if(e[w] < 0x40000000U) e[w] *= 2U;
        e[8] = HiresDmaFlags(e[8]);
      }
      else
      {
        e[0] = HalvePair(e[0]);
        for(int w : {2, 3})
          e[w] /= 2U;
        if(((e[8] >> 16) & 0xFFU) == 0x5AU)
          e[8] = (e[8] & ~0xFF0000U) | (0x2DU << 16);
      }
      return true;
    default:
      return changed;
  }
}

}  // namespace

void HiresInit()
{
  if(g_hires)
    fprintf(stderr, "hires: 720x480 rendering on%s\n", g_templates ? " (+ template centres)" : "");
}

uint32 HiresRemapBase(uint32 base)
{
  if(!g_hires)
    return base;
  for(int i = 0; i < 3; ++i)
  {
    if(base == kOld[i])
      return kNew[i];
    if(base > kOld[i] && base < kOld[i] + kOldSize)
    {
      static int warned = 0;
      if(warned++ < 8)
        fprintf(stderr, "hires: DMA into frame buffer %d at offset %X (not remapped)\n", i, base - kOld[i]);
    }
  }
  return base;
}

uint32 HiresDmaFlags(uint32 flags)
{
  if(g_hires && ((flags >> 16) & 0xFFU) == 0x2DU)
    flags = (flags & ~0xFF0000U) | (0x5AU << 16);
  return flags;
}

void HiresField()
{
  if(!g_hires)
    return;
  static int reported = -1;
  if(reported < 0)
  {
    HiresInit();
    // The 720x480 buffers start black (YCrCb 10 80 80 00): screens built by feedback effects copy what is there.
    for(uint32 b : kNew)
    {
      uint8* const p = &nuonEnv.mainBusDRAM[b & MAIN_BUS_VALID_MEMORY_MASK];
      for(uint32 i = 0; i < kNewSize; i += 4)
      {
        p[i] = 0x10; p[i + 1] = 0x80; p[i + 2] = 0x80; p[i + 3] = 0x00;
      }
    }
  }
  const int code = Apply(kCode, sizeof(kCode) / sizeof(kCode[0]));
  const int data = Apply(kData, sizeof(kData) / sizeof(kData[0]));
  int tpl = 0;
  if(g_templates)
  {
    for(uint32 a : kCentre78)
      if(Get(a) == 0x00B40078U) { Put(a, 0x016800F0U); ++tpl; }
    for(uint32 a : kCentre98)
      if(Get(a) == 0x00B40098U) { Put(a, 0x01680130U); ++tpl; }
  }
  if(code + data + tpl && reported < 40)
  {
    ++reported;
    fprintf(stderr, "hires: field %u patched %d code, %d data, %d template words\n",
      Get(VIDEO_FIELD_COUNTER_ADDRESS), code, data, tpl);
  }
}

extern FILE* g_joblog;   // comm.cpp
FILE* TraceFile() { return g_joblog ? g_joblog : stderr; }

// NUANCE_HIRES_TRACE=<hex entry address>: log that entry's reads and write-backs (investigations).
const uint32 g_trace = getenv("NUANCE_HIRES_TRACE") ? (uint32)strtoul(getenv("NUANCE_HIRES_TRACE"), nullptr, 16) : 0;

void HiresLinearWrite(MPE& mpe, uint32 dst, uint32 words, uint32 intaddr)
{
  if(g_trace && dst == g_trace)
    fprintf(TraceFile(), "trace f%u mpe%u WRITE %u words from %08X: word0 %08X word4 %08X type %08X\n", Get(VIDEO_FIELD_COUNTER_ADDRESS),
      mpe.mpeIndex, words, intaddr, Get(dst), Get(dst + 16), words == 16 ? Get(dst + 60) : 0);
  // Renderers write display list entries back (16 words from $20100030): return them to the game's units, or the
  // next read would double them again.
  if(!g_hires || words != 16 || (intaddr & 0x207FFFFFU) != 0x20100030U || (dst >> 23) != (0x80000000U >> 23))
    return;
  uint32 e[16];
  for(uint32 i = 0; i < 16; ++i)
    e[i] = Get(dst + 4 * i);
  if(EntryTransform(e, false))
    for(uint32 i = 0; i < 16; ++i)
      Put(dst + 4 * i, e[i]);
}

void HiresLinearRead(MPE& mpe, uint32 src, uint32 words, uint32 intaddr)
{
  if((src >> 23) != (0x80000000U >> 23) || !words)
    return;
  uint8* const local = (uint8*)nuonEnv.GetPointerToMemory(mpe.mpeIndex, intaddr & 0x207FFFFCU);
  if(g_trace && src == g_trace)
    fprintf(TraceFile(), "trace f%u mpe%u read %u words to %08X: word0 %08X\n", Get(VIDEO_FIELD_COUNTER_ADDRESS), mpe.mpeIndex,
      words, intaddr, Get(src));
  auto get = [&](uint32 i) { return SwapBytes(*(uint32*)(local + 4 * i)); };
  auto put = [&](uint32 i, uint32 v) { *(uint32*)(local + 4 * i) = SwapBytes(v); };
  const uint32 end = src + 4 * words;
  // Game data, wherever the game rewrote it since the field started.
  for(const Patch& p : kData)
    if(p.addr >= src && p.addr < end && get((p.addr - src) / 4) == p.orig)
      put((p.addr - src) / 4, p.hires);
  if(g_templates)
  {
    for(uint32 a : kCentre78)
      if(a >= src && a < end && get((a - src) / 4) == 0x00B40078U) put((a - src) / 4, 0x016800F0U);
    for(uint32 a : kCentre98)
      if(a >= src && a < end && get((a - src) / 4) == 0x00B40098U) put((a - src) / 4, 0x01680130U);
  }
  // Display list entries (16 words, read to $20100030 by MPE0-2); the type is the last word.
  static const bool noentry = getenv("NUANCE_HIRES_NOENTRY") != nullptr;   // investigations
  if(words == 16 && (intaddr & 0x207FFFFFU) == 0x20100030U && !noentry)
  {
    uint32 e[16];
    for(uint32 i = 0; i < 16; ++i)
      e[i] = get(i);
    if(EntryTransform(e, true))
      for(uint32 i = 0; i < 16; ++i)
        put(i, e[i]);
  }
}
