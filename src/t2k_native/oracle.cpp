// oracle.cpp - checks a ported routine against the emulated one, call by call (see t2k_native.h).
//
// At the routine's entry (a host trap on its first instruction): snapshot main RAM and the registers, run the port
// on that state (reads from the snapshot, writes kept aside). The emulator then runs the real routine. At the
// instruction after each call site (the return; the stack pointer must be the entry's + 4) compare:
//   - registers d0-d7 / a0-a7 and the Z flag (callers branch on it straight after the bsr),
//   - every byte of main RAM: expected = the port's write if it wrote there, else the entry snapshot.
// The stack below the entry's a7 is skipped (the emulated routine's own bsr return addresses).
// Bytes changed during the call by something else (GPU / blitter / DSP / object processor, another interrupt)
// would also show up; they are counted by region so they can be told apart from real misses.
#include "t2k_native.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../traps.h"
#include "t2k_symbols.h"

extern "C" {
extern uint8_t jagMemSpace[];
unsigned int M68KHostGetCCR(void);
}

namespace t2knative {
namespace {

struct Routine {
  const char* name;
  uint32_t entry;
  void (*port)(Cpu&, Mem&);
  std::vector<uint32_t> returns;   // the instruction after each bsr / jsr (tools/t2k_symbols.py --callers)
};

struct Pending {
  bool active = false;
  uint32_t sp = 0;
  Cpu in, out;
  std::vector<uint8_t> snap;
  Mem mem;
};

struct Stats {
  long calls = 0, ok = 0, reg_bad = 0, z_bad = 0, ram_bad = 0, unsupported = 0, lost = 0;
  long foreign_bytes = 0;   // RAM the emulator changed, the port didn't, outside the game's data / bss
  int printed = 0;
};

constexpr uint32_t kRam = Mem::kRamEnd;
Routine* g_routine = nullptr;
Pending g_p;
Stats g_s;
std::vector<Routine> g_routines;

Cpu ReadCpu() {
  Cpu c;
  for (int i = 0; i < 8; ++i) c.d[i] = traps::Reg(i), c.a[i] = traps::Reg(8 + i);
  c.z = (M68KHostGetCCR() & 4) != 0;
  return c;
}

bool OnEntry() {
  if (g_p.active) ++g_s.lost;   // the previous call never reached a known return
  g_p.active = true;
  g_p.in = ReadCpu();
  g_p.sp = g_p.in.a[7];
  g_p.snap.assign(jagMemSpace, jagMemSpace + kRam);
  g_p.mem = Mem{};
  g_p.mem.base = jagMemSpace;   // the port runs now, inside the trap: the live memory IS the entry state
  g_p.out = g_p.in;
  g_routine->port(g_p.out, g_p.mem);
  g_p.out.a[7] += 4;   // the rts
  return false;        // the emulator runs the real routine
}

const char* Region(uint32_t a) {
  // the Object Processor's lists (zerend .. blist: list1, list2 and their padding): the display hardware writes
  // back into them while it draws, so they change under any routine (seen: list1+31 / +36 during collie)
  if (a >= t2ksym::zerend && a < t2ksym::blist) return "OP list";
  if (a >= t2ksym::USERRAM && a < 0x18D2A) return "data/bss";
  return "other";
}

// What a call did, from the port's own result: which of the routine's paths the test reached.
std::string PathOf(const Cpu& out, const Mem& m) {
  if (m.writes.count(t2ksym::bolt_lock)) return "superzapper";
  if (out.z && m.writes.empty()) return "miss";
  if (m.writes.count(t2ksym::ranptr)) return out.z ? "spatter (super spike, no hit)" : "hit + spatter";
  if (m.writes.count(t2ksym::h2h_sign)) return "hit (head to head)";
  return out.z ? "other, no hit" : "hit, bullet stopped";
}
std::map<std::string, long> g_paths;

bool OnReturn() {
  if (!g_p.active) return false;
  const Cpu now = ReadCpu();
  if (now.a[7] != g_p.sp + 4) return false;   // not our return (another path through this instruction)
  g_p.active = false;
  ++g_s.calls;
  ++g_paths[PathOf(g_p.out, g_p.mem)];
  if (g_p.mem.unsupported) { ++g_s.unsupported; return false; }
  std::string why;
  for (int i = 0; i < 16; ++i) {
    const uint32_t want = i < 8 ? g_p.out.d[i] : g_p.out.a[i - 8], got = i < 8 ? now.d[i] : now.a[i - 8];
    if (want != got) {
      char b[64];
      std::snprintf(b, sizeof b, " %c%d port %08X emu %08X", i < 8 ? 'd' : 'a', i & 7, want, got);
      why += b;
    }
  }
  const bool reg_bad = !why.empty();
  const bool z_bad = now.z != g_p.out.z;
  if (z_bad) why += now.z ? " Z: emu set, port clear" : " Z: emu clear, port set";
  long ram_bad = 0;
  const uint32_t stack_lo = g_p.sp >= 256 ? g_p.sp - 256 : 0;
  for (uint32_t a = 0; a < kRam; ++a) {
    if (a >= stack_lo && a < g_p.sp) continue;
    auto it = g_p.mem.writes.find(a);
    const uint8_t want = it != g_p.mem.writes.end() ? it->second : g_p.snap[a];
    if (jagMemSpace[a] == want) continue;
    if (it == g_p.mem.writes.end() && std::strcmp(Region(a), "data/bss") != 0) { ++g_s.foreign_bytes; continue; }
    if (ram_bad++ < 4) {
      char b[96];
      std::snprintf(b, sizeof b, " [$%06X %s: was %02X port %02X emu %02X]", a, Region(a), g_p.snap[a], want,
                    jagMemSpace[a]);
      why += b;
    }
  }
  if (reg_bad) ++g_s.reg_bad;
  if (z_bad) ++g_s.z_bad;
  if (ram_bad) ++g_s.ram_bad;
  if (!reg_bad && !z_bad && !ram_bad) ++g_s.ok;
  else if (g_s.printed++ < 20)
    std::fprintf(stderr, "oracle %s: call %ld MISMATCH (a6 %06X, %zu bytes written by the port)%s\n", g_routine->name,
                 g_s.calls, g_p.in.a[6], g_p.mem.writes.size(), why.c_str());
  return false;
}

}  // namespace

bool InstallOracle(const char* spec) {
  // entry word, then the return points (tools/t2k_symbols.py --callers collie: address, ROM word there)
  struct Site { uint32_t addr; uint16_t word; };
  static Routine collie_r{"collie", t2ksym::collie, collie, {}};
  static const Site collie_entry{t2ksym::collie, 0x4A79};   // tst _sz
  static const Site collie_ret[] = {
      {0x80A1D2, 0x6700}, {0x80A2B8, 0x6700}, {0x80AC9E, 0x6600}, {0x80ACAC, 0x6600}, {0x80ACBA, 0x6600},
      {0x80AEE4, 0x6600}, {0x80AF12, 0x6600}, {0x80AFD8, 0x6600}, {0x80B14C, 0x6682}, {0x80B55C, 0x6600},
      {0x80BA80, 0x6600}, {0x80BD8E, 0x6600}, {0x80BEF0, 0x6600}, {0x80C088, 0x6600}, {0x80C1C0, 0x6600}};
  if (std::string(spec) != "collie") {
    std::fprintf(stderr, "oracle: unknown routine %s (have: collie)\n", spec);
    return false;
  }
  g_routine = &collie_r;
  bool ok = traps::Install(collie_entry.addr, collie_entry.word, OnEntry);
  for (const Site& s : collie_ret) ok &= traps::Install(s.addr, s.word, OnReturn);
  std::fprintf(stderr, "oracle: %s at $%06X, %zu returns%s\n", spec, collie_entry.addr, std::size(collie_ret),
               ok ? "" : " - INSTALL FAILED");
  return ok;
}

void OracleReport() {
  if (!g_routine) return;
  std::fprintf(stderr,
               "oracle %s: %ld calls checked, %ld identical; mismatches: registers %ld, Z %ld, RAM %ld; unsupported "
               "%ld; lost %ld; foreign RAM bytes (OP lists / outside data+bss, not the port's) %ld\n",
               g_routine->name, g_s.calls, g_s.ok, g_s.reg_bad, g_s.z_bad, g_s.ram_bad, g_s.unsupported, g_s.lost,
               g_s.foreign_bytes);
  for (auto& [path, n] : g_paths) std::fprintf(stderr, "  path %-30s %ld\n", path.c_str(), n);
}

}  // namespace t2knative
