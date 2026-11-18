// jagsurvey.cpp - see jagsurvey.h.
//
// Output, one line per event (frame first):
//   <f> GPU pc=<gpu pc> by=<who> m68k=<68K pc> hash=<fnv of GPU RAM pc..pc+512>
//   <f> BLIT by=<who> m68k=<68K pc> gpu=<gpu pc> cmd=<B_CMD> a1=<base> a1f=<flags> a1p=<pixel y:x> a2=<base>
//            a2f=<flags> a2p=<pixel> cnt=<lines:pixels>
//   <f> OP <addr> t=<type> hl=<first halfline> n=<halflines seen> [bitmap: data= x= y= h= d= p= dw= iw= flags=]
// OP lines are written once per object per frame (at the end of the frame), counting the halflines it was read on.
#include "jagsurvey.h"

#include <cstdint>
#include <cstdio>
#include <map>

extern "C" {
extern uint8_t jagMemSpace[];
extern void (*vjHostGpuGo)(uint32_t pc, uint32_t who);
extern void (*vjHostBlit)(uint32_t cmd, uint32_t who);
extern void (*vjHostOpObject)(uint32_t addr, uint64_t p0, int halfline);
const uint8_t* vjBlitterRam(void);
const uint8_t* GPUHostRAM(void);
uint32_t GPUGetPC(void);
unsigned int M68KHostGetPC(void);
}

namespace jagsurvey {
namespace {

FILE* g_out = nullptr;
int g_frame = 0, g_f0 = 0, g_f1 = -1;

struct OpSeen { uint64_t p0, p1, p2; int hl0, n; };
std::map<uint32_t, OpSeen> g_ops;

bool Active() { return g_out && g_frame >= g_f0 && g_frame <= g_f1; }

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint64_t Be64(const uint8_t* p) { return uint64_t(Be32(p)) << 32 | Be32(p + 4); }
uint32_t M68kPc() { return M68KHostGetPC(); }

void OnGpuGo(uint32_t pc, uint32_t who) {
  if (!Active()) return;
  const uint8_t* ram = GPUHostRAM();
  uint32_t off = pc & 0xFFF, h = 2166136261u;
  for (uint32_t i = off; i < off + 512 && i < 0x1000; ++i) h = (h ^ ram[i]) * 16777619u;
  std::fprintf(g_out, "%d GPU pc=%06X by=%u m68k=%06X hash=%08X\n", g_frame, pc, who, M68kPc(), h);
}

void OnBlit(uint32_t cmd, uint32_t who) {
  if (!Active()) return;
  const uint8_t* b = vjBlitterRam();
  std::fprintf(g_out,
               "%d BLIT by=%u m68k=%06X gpu=%06X cmd=%08X a1=%06X a1f=%08X a1p=%08X a2=%06X a2f=%08X a2p=%08X "
               "cnt=%08X\n",
               g_frame, who, M68kPc(), GPUGetPC(), cmd, Be32(b + 0x00), Be32(b + 0x04), Be32(b + 0x0C),
               Be32(b + 0x24), Be32(b + 0x28), Be32(b + 0x30), Be32(b + 0x3C));
}

void OnOp(uint32_t addr, uint64_t p0, int halfline) {
  if (!Active()) return;
  auto it = g_ops.find(addr);
  if (it != g_ops.end()) { ++it->second.n; return; }
  const uint8_t* m = jagMemSpace + (addr & 0xFFFFF8);
  g_ops[addr] = {p0, Be64(m + 8), Be64(m + 16), halfline, 1};
}

void FlushOps() {
  for (auto& [addr, o] : g_ops) {
    int type = int(o.p0 & 7);
    std::fprintf(g_out, "%d OP %06X t=%d hl=%d n=%d", g_frame, addr, type, o.hl0, o.n);
    if (type == 0 || type == 1) {
      uint32_t data = uint32_t((o.p0 >> 43) << 3) & 0xFFFFFF;   // phrase address << 3
      int y = int((o.p0 >> 3) & 0x7FF), h = int((o.p0 >> 14) & 0x3FF);
      int x = int(o.p1 & 0xFFF), d = int((o.p1 >> 12) & 7), pitch = int((o.p1 >> 15) & 7);
      int dw = int((o.p1 >> 18) & 0x3FF), iw = int((o.p1 >> 28) & 0x3FF), flags = int((o.p1 >> 45) & 0xF);
      std::fprintf(g_out, " data=%06X x=%d y=%d h=%d d=%d p=%d dw=%d iw=%d fl=%X", data, x, y, h, d, pitch, dw, iw,
                   flags);
      if (type == 1) std::fprintf(g_out, " scale=%04X", unsigned(o.p2 & 0xFFFFFF));
    }
    std::fprintf(g_out, "\n");
  }
  g_ops.clear();
}

}  // namespace

bool Install(int f0, int f1, const char* path) {
  g_out = std::fopen(path, "w");
  if (!g_out) return false;
  g_f0 = f0;
  g_f1 = f1;
  vjHostGpuGo = OnGpuGo;
  vjHostBlit = OnBlit;
  vjHostOpObject = OnOp;
  return true;
}

void SetFrame(int f) {
  if (!g_out) return;
  if (!g_ops.empty()) FlushOps();
  g_frame = f;
  if (f > g_f1) {
    std::fclose(g_out);
    g_out = nullptr;
    vjHostGpuGo = nullptr;
    vjHostBlit = nullptr;
    vjHostOpObject = nullptr;
  }
}

}  // namespace jagsurvey
