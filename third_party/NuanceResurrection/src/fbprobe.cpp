// Frame buffer probe (llamasoft-nuon, investigations for a native Tempest 3000 renderer). Original resolution only.
//
// NUANCE_FBPROBE=<abs file>: per field, how many pixels the renderers wrote to / read from each of the game's three
// frame buffers (bilinear pixel DMA; linear DMA in words), how often the render target changed, and once per
// (mpe, pc, buffer) the code that READ a frame buffer - feedback effects and read-modify-write blending.
// NUANCE_CLEARTARGET=<hex pixel>: when the renderers start writing into a different frame buffer than the last one,
// fill it with that value first. Compared with a plain run (deterministic), every changed pixel of a picture
// depends on the buffer's earlier contents. Run with NUANCE_THREADS=0 (the hooks are not thread safe).
#include "basetypes.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <algorithm>
#include "byteswap.h"
#include "fbprobe.h"
#include "hires.h"
#include "mpe.h"
#include "NuonEnvironment.h"
#include "NuonMemoryMap.h"
#include "Bios.h"

extern NuonEnvironment nuonEnv;

namespace {

constexpr uint32 kSize = 360U * 240U * 4U;
constexpr uint32 kBuf[3] = {0x40010000U, 0x40064600U, 0x400B8C00U};

FILE* const g_log = getenv("NUANCE_FBPROBE") ? fopen(getenv("NUANCE_FBPROBE"), "w") : nullptr;
const char* const g_clear = getenv("NUANCE_CLEARTARGET");
const bool g_on = g_log != nullptr || g_clear != nullptr;

struct FieldStats
{
  uint32 field = 0xFFFFFFFFU;
  uint64 wr[3] = {}, rd[3] = {}, lwr[3] = {}, lrd[3] = {};
  uint32 switches = 0;
};
FieldStats g_f;
int g_target = -1;

uint32 Field()
{
  return SwapBytes(*((uint32*)&nuonEnv.systemBusDRAM[VIDEO_FIELD_COUNTER_ADDRESS & SYSTEM_BUS_VALID_MEMORY_MASK]));
}

int BufferOf(uint32 addr)
{
  for(int i = 0; i < 3; ++i)
    if(addr >= kBuf[i] && addr < kBuf[i] + kSize)
      return i;
  return -1;
}

void Flush()
{
  if(!g_log || g_f.field == 0xFFFFFFFFU)
    return;
  fprintf(g_log, "f%u tgt %d sw %u wr %llu %llu %llu rd %llu %llu %llu lwr %llu %llu %llu lrd %llu %llu %llu\n",
    g_f.field, g_target, g_f.switches,
    (unsigned long long)g_f.wr[0], (unsigned long long)g_f.wr[1], (unsigned long long)g_f.wr[2],
    (unsigned long long)g_f.rd[0], (unsigned long long)g_f.rd[1], (unsigned long long)g_f.rd[2],
    (unsigned long long)g_f.lwr[0], (unsigned long long)g_f.lwr[1], (unsigned long long)g_f.lwr[2],
    (unsigned long long)g_f.lrd[0], (unsigned long long)g_f.lrd[1], (unsigned long long)g_f.lrd[2]);
  fflush(g_log);
}

void Touch()
{
  const uint32 f = Field();
  if(f != g_f.field)
  {
    Flush();
    g_f = FieldStats();
    g_f.field = f;
  }
}

void Write(int b)
{
  if(b == g_target)
    return;
  ++g_f.switches;
  if(g_clear)
  {
    const uint32 v = SwapBytes((uint32)strtoul(g_clear, nullptr, 16));
    uint32* p = (uint32*)nuonEnv.GetPointerToMemory((kBuf[b] >> 23) & 0x1FU, kBuf[b], false);
    for(uint32 i = 0; i < kSize / 4U; ++i)
      p[i] = v;
  }
  g_target = b;
}

void ReadBy(const MPE& mpe, int b)
{
  if(!g_log)
    return;
  static std::set<uint64> seen;
  const int rel = b == g_target ? 0 : 1;   // 0 = the buffer being drawn (read-modify-write), 1 = another (feedback)
  const uint64 key = (uint64)mpe.mpeIndex << 40 | (uint64)rel << 36 | (uint64)b << 32 | mpe.pcexec;
  if(seen.insert(key).second)
    fprintf(g_log, "rdpc f%u mpe%u pc %08X buf %d %s\n", Field(), mpe.mpeIndex, mpe.pcexec, b,
      rel ? "other" : "target");
}

}  // namespace

// NUANCE_SPANLOG=<type hex> (+ NUANCE_JOBLOG; best with NUANCE_NOJIT / NUANCE_NOAOT / NUANCE_THREADS=0 and NUANCE_PCLOG,
// so spans follow their segment in one file): every pixel row MPE0 reads from / writes to a frame buffer while the
// entry at $20100030 has that type: "span rd|wr x y len" + the pixels (read: the frame buffer's, write: MPE0's).
static void SpanLog(const MPE& mpe, uint32 flags, uint32 base, uint32 xinfo, uint32 yinfo, uint32 intaddr)
{
  static const uint32 type = getenv("NUANCE_SPANLOG") ? (uint32)strtoul(getenv("NUANCE_SPANLOG"), nullptr, 16) : 0;
  extern FILE* g_joblog;
  // NUANCE_SPANLOG_MPES=<mask> (default 7: MPE0-2, each draws its own bands; 1 = MPE0 only, as before)
  static const uint32 mpes = getenv("NUANCE_SPANLOG_MPES") ? (uint32)strtoul(getenv("NUANCE_SPANLOG_MPES"), nullptr, 0) : 7U;
  if(!type || !g_joblog || mpe.mpeIndex > 2 || !((mpes >> mpe.mpeIndex) & 1) || BufferOf(base) < 0)
    return;
  const uint8* local = (const uint8*)nuonEnv.GetPointerToMemory(mpe.mpeIndex, 0x20100030U, false);
  if(SwapBytes(*(const uint32*)(local + 60)) != type)
    return;
  {   // the entry's words whenever they or the picture change ("spanentry w0 .. w15 p<flip count>")
    extern uint32_t g_vid_flips;   // video.cpp
    // (repeated whenever another MPE logs: each span line belongs to the spanentry line before it)
    static uint32 last[16] = {}, last_flip = 0xFFFFFFFFU, last_mpe = 0xFFFFFFFFU;
    uint32 now[16];
    for(int i = 0; i < 16; ++i)
      now[i] = SwapBytes(*(const uint32*)(local + 4 * i));
    if(memcmp(now, last, sizeof now) != 0 || g_vid_flips != last_flip || mpe.mpeIndex != last_mpe)
    {
      memcpy(last, now, sizeof now);
      last_flip = g_vid_flips;
      last_mpe = mpe.mpeIndex;
      fprintf(g_joblog, "spanentry");
      for(int i = 0; i < 16; ++i)
        fprintf(g_joblog, " %08X", now[i]);
      // + the job word at local $20100000 (806AD2B0 adds it << 15 to the texture v: the web surface's scroll)
      fprintf(g_joblog, " p%u j%08X\n", g_vid_flips, SwapBytes(*(const uint32*)(local - 0x30)));
    }
  }
  const uint32 x = xinfo & 0x7FFU, len = (xinfo >> 16) & 0x3FFU, y = yinfo & 0x7FFU, h = (yinfo >> 16) & 0x3FFU;
  const bool rd = (flags & (1U << 13)) != 0;
  if(len == 0 || h == 0 || len * h > 4096)
    return;
  // one row: "span rd|wr x y len" (as the vector analysis reads it); a block: "spanb rd|wr x y len rows", row by row
  if(h == 1)
    fprintf(g_joblog, "span %s %u %u %u", rd ? "rd" : "wr", x, y, len);
  else
    fprintf(g_joblog, "spanb %s %u %u %u %u", rd ? "rd" : "wr", x, y, len, h);
  const uint32* fb = (const uint32*)nuonEnv.GetPointerToMemory((base >> 23) & 0x1FU, base, false) + y * 360U + x;
  const uint32* lp = (const uint32*)nuonEnv.GetPointerToMemory(mpe.mpeIndex, intaddr & 0x207FFFFCU, false);
  for(uint32 r = 0; r < h; ++r)
    for(uint32 i = 0; i < len; ++i)
      fprintf(g_joblog, " %08X", SwapBytes(rd ? fb[r * 360U + i] : lp[r * len + i]));
  fprintf(g_joblog, "\n");
}

void FbProbeBiLinear(const MPE& mpe, uint32 flags, uint32 base, uint32 xinfo, uint32 yinfo, uint32 intaddr)
{
  SpanLog(mpe, flags, base, xinfo, yinfo, intaddr);
  if(!g_on || g_hires)
    return;
  const int b = BufferOf(base);
  if(b < 0)
    return;
  Touch();
  const uint64 px = (uint64)((xinfo >> 16) & 0x3FFU) * ((yinfo >> 16) & 0x3FFU);
  if(flags & (1U << 13))
  {
    g_f.rd[b] += px;
    ReadBy(mpe, b);
  }
  else
  {
    Write(b);
    g_f.wr[b] += px;
  }
}

void FbProbeLinear(const MPE& mpe, uint32 flags, uint32 base)
{
  if(!g_on || g_hires || (flags & (1U << 28)))
    return;
  const int b = BufferOf(base);
  if(b < 0)
    return;
  Touch();
  const uint32 words = (flags >> 16) & 0xFFU;
  if(flags & (1U << 13))
  {
    g_f.lrd[b] += words;
    ReadBy(mpe, b);
  }
  else
  {
    Write(b);
    g_f.lwr[b] += words;
  }
}

// NUANCE_ENTRYLOG=<abs file> (display list census): every display list entry the renderers fetch (16 words from system
// RAM into local $20100030), once per field and address, in first-fetch order: readers (MPE mask), the code modules
// (library source @ local offset of each load into instruction RAM) the readers loaded while working on it, the 16 words as read and,
// if a renderer wrote the entry back, the words as written. Thread safe (a lock; only entry fetches take it).
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

FILE* const g_elog = getenv("NUANCE_ENTRYLOG") ? fopen(getenv("NUANCE_ENTRYLOG"), "w") : nullptr;
std::mutex g_elock;

struct Entry
{
  uint32 src = 0, readers = 0;
  uint32 rd[16] = {}, wb[16] = {};
  bool written = false;
  std::vector<uint64> modules;
  std::vector<std::pair<uint32, std::vector<uint32>>> data;   // MPE0's other reads of system / main bus memory
};
uint32 g_efield = 0xFFFFFFFFU;
std::vector<Entry> g_entries;
std::map<uint32, size_t> g_bySrc;
int g_cur[4] = {-1, -1, -1, -1};

uint32 g_epicture = 0, g_egroup = 0;
}  // namespace
extern uint32_t g_vid_flips;   // video.cpp
namespace {

void EFlush()
{
  if(g_efield == 0xFFFFFFFFU)
    return;
  for(const Entry& e : g_entries)
  {
    fprintf(g_elog, "g%u f%u p%u %08X m%X", g_egroup, g_efield, g_epicture, e.src, e.readers);
    for(int i = 0; i < 16; ++i)
      fprintf(g_elog, " %08X", e.rd[i]);
    if(e.written)
    {
      fprintf(g_elog, " wb");
      for(int i = 0; i < 16; ++i)
        fprintf(g_elog, " %08X", e.wb[i]);
    }
    fprintf(g_elog, " mods");
    for(uint64 m : e.modules)
      fprintf(g_elog, " %08X@%X", (uint32)(m >> 32), (uint32)m & 0x1FFFU);
    if(!e.data.empty())
    {
      fprintf(g_elog, " data");
      for(const auto& d : e.data)
      {
        fprintf(g_elog, " @%08X", d.first);
        for(uint32 w : d.second)
          fprintf(g_elog, ":%08X", w);
      }
    }
    fprintf(g_elog, "\n");
  }
  fflush(g_elog);
}

// A group = one pass over the list: it ends when a renderer fetches an address it already fetched in this group.
// Also a new group at each flip (VidConfig / VidChangeBase: the game showing a picture), so pX = the flip count:
// the entries drawn between two flips are one picture. Labelled with the field and the flip count.
void ENewGroup()
{
  EFlush();
  ++g_egroup;
  {
    g_efield = Field();
    g_epicture = g_vid_flips;
    g_entries.clear();
    g_bySrc.clear();
    for(int& c : g_cur) c = -1;
  }
}

void ReadWords(uint32 mpeIndex, uint32 addr, uint32* out)
{
  const uint8* p = (const uint8*)nuonEnv.GetPointerToMemory(mpeIndex, addr & 0x207FFFFCU, false);
  for(int i = 0; i < 16; ++i)
    out[i] = SwapBytes(*(const uint32*)(p + 4 * i));
}

}  // namespace

// NUANCE_ENTRYDATA=1 (with NUANCE_ENTRYLOG): also every other linear read MPE0 makes from system / main bus memory while
// working on an entry (shape streams, palettes, tables): "data @src:w:w..." - one log holds what a picture needs.
static void EntryData(const MPE& mpe, uint32 flags, uint32 base, uint32 intaddr)
{
  static const bool on = getenv("NUANCE_ENTRYDATA") != nullptr;
  if(!on || mpe.mpeIndex != 0 || !(flags & (1U << 13)) || (flags & (1U << 28)) || (base >> 28) < 4)
    return;
  const uint32 words = (flags >> 16) & 0xFFU;
  if(!words || (intaddr & 0x207FFFFFU) == 0x20100030U)
    return;
  std::lock_guard<std::mutex> lock(g_elock);
  const int c = g_cur[0];
  if(g_efield == 0xFFFFFFFFU || c < 0)
    return;
  const uint8* p = (const uint8*)nuonEnv.GetPointerToMemory(0, intaddr & 0x207FFFFCU, false);
  std::vector<uint32> w(words);
  for(uint32 i = 0; i < words; ++i)
    w[i] = SwapBytes(*(const uint32*)(p + 4 * i));
  g_entries[c].data.emplace_back(base, std::move(w));
}

void EntryLogLinear(const MPE& mpe, uint32 flags, uint32 base, uint32 intaddr)
{
  if(g_elog)
    EntryData(mpe, flags, base, intaddr);
  // NUANCE_SKIPTYPE=<hex type> (identifying what a type draws): MPE0 and MPE2 see entries of that type as 000000FE
  // (an entry the drawing pass passes over), so it disappears from their bands; diff against a plain run.
  static const uint32 skip = getenv("NUANCE_SKIPTYPE") ? (uint32)strtoul(getenv("NUANCE_SKIPTYPE"), nullptr, 16) : 0;
  // NUANCE_NODRAW=1 (cost measurement): MPE0 and MPE2 see every entry as the end of the list (000000FF), and so does
  // MPE1 at the list head $8004E040 (its drawing pass starts there; its compute jobs fetch single other entries), so
  // nothing is drawn - what a native renderer would save. NUANCE_NODRAW=2: MPE0 and MPE2 only.
  static const int nodraw = getenv("NUANCE_NODRAW") ? atoi(getenv("NUANCE_NODRAW")) : 0;
  const bool drawer = mpe.mpeIndex == 0 || mpe.mpeIndex == 2 || (nodraw == 1 && mpe.mpeIndex == 1 && base == 0x8004E040U);
  // NUANCE_ONLYTYPES=<hex type>,... (native renderer checks): the drawing pass draws only these types; the others are
  // passed over as 000000FE. MPE0 and MPE2 only draw; MPE1 is in its drawing pass from its fetch of the list head
  // $8004E040 to the end of the list (its compute jobs fetch single other entries and are left alone).
  static const char* const only = getenv("NUANCE_ONLYTYPES");
  if(only && (flags & (1U << 13)) && !(flags & (1U << 28)) && ((flags >> 16) & 0xFFU) == 16 &&
     (intaddr & 0x207FFFFFU) == 0x20100030U && (base >> 23) == (0x80000000U >> 23) && mpe.mpeIndex < 3)
  {
    static bool mpe1_drawing = false;
    uint8* p = (uint8*)nuonEnv.GetPointerToMemory(mpe.mpeIndex, intaddr & 0x207FFFFCU, false);
    const uint32 type = SwapBytes(*(uint32*)(p + 60));
    if(mpe.mpeIndex == 1 && base == 0x8004E040U)
      mpe1_drawing = true;
    const bool drawing = mpe.mpeIndex != 1 || mpe1_drawing;
    if(mpe.mpeIndex == 1 && type == 0x000000FFU)
      mpe1_drawing = false;
    if(drawing && type != 0x000000FFU && type != 0x000000FEU)
    {
      bool keep = false;
      for(const char* q = only; *q; )
      {
        char* e;
        const uint32 t = (uint32)strtoul(q, &e, 16);
        if(e == q)
          break;
        keep |= t == type;
        q = *e ? e + 1 : e;
      }
      if(!keep)
        *(uint32*)(p + 60) = SwapBytes(0x000000FEU);
    }
  }
  if((skip || nodraw) && drawer && (flags & (1U << 13)) && !(flags & (1U << 28)) &&
     ((flags >> 16) & 0xFFU) == 16 && (intaddr & 0x207FFFFFU) == 0x20100030U && (base >> 23) == (0x80000000U >> 23))
  {
    uint8* p = (uint8*)nuonEnv.GetPointerToMemory(mpe.mpeIndex, intaddr & 0x207FFFFCU, false);
    if(nodraw)
      *(uint32*)(p + 60) = SwapBytes(0x000000FFU);
    else if(SwapBytes(*(uint32*)(p + 60)) == skip)
      *(uint32*)(p + 60) = SwapBytes(0x000000FEU);
  }
  if(!g_elog || mpe.mpeIndex >= 3 || (flags & (1U << 28)) || ((flags >> 16) & 0xFFU) != 16 ||
     (intaddr & 0x207FFFFFU) != 0x20100030U || (base >> 23) != (0x80000000U >> 23))
    return;
  std::lock_guard<std::mutex> lock(g_elock);
  if(g_efield == 0xFFFFFFFFU || g_epicture != g_vid_flips)
    ENewGroup();
  uint32 w[16];
  ReadWords(mpe.mpeIndex, intaddr, w);
  auto it = g_bySrc.find(base);
  size_t idx;
  if(flags & (1U << 13))   // read: a fetch
  {
    if(it != g_bySrc.end() && (g_entries[it->second].readers & (1U << mpe.mpeIndex)))
    {
      ENewGroup();
      it = g_bySrc.end();
    }
    if(it == g_bySrc.end())
    {
      Entry e;
      e.src = base;
      for(int i = 0; i < 16; ++i) e.rd[i] = w[i];
      idx = g_entries.size();
      g_entries.push_back(e);
      g_bySrc[base] = idx;
    }
    else
      idx = it->second;
    g_entries[idx].readers |= 1U << mpe.mpeIndex;
    g_cur[mpe.mpeIndex] = (int)idx;
  }
  else if(it != g_bySrc.end())   // write-back of a fetched entry
  {
    Entry& e = g_entries[it->second];
    e.written = true;
    for(int i = 0; i < 16; ++i) e.wb[i] = w[i];
  }
}

void EntryLogCodeLoad(uint32 target, uint32 src, uint32 dst)
{
  // Any load into instruction RAM ($20300000-$20302000 local): recorded as source@local offset (sub-handlers too).
  if(!g_elog || target >= 3 || (dst & 0x007FFFFFU) < 0x00300000U || (dst & 0x007FFFFFU) >= 0x00302000U)
    return;
  std::lock_guard<std::mutex> lock(g_elock);
  if(g_efield == 0xFFFFFFFFU)
    return;
  const int c = g_cur[target];
  if(c < 0)
    return;
  std::vector<uint64>& m = g_entries[c].modules;
  const uint64 key = (uint64)src << 32 | (dst & 0x1FFFU);
  if(std::find(m.begin(), m.end(), key) == m.end())
    m.push_back(key);
}
