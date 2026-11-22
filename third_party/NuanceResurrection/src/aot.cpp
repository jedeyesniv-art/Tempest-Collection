// aot.cpp - see aot.h.
#include "mpe.h"
#define AOT_NEED_INTERRUPT_TEST
#include "aot.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <set>
#include <vector>

#include "mpe.h"
#include "NuonMemoryMap.h"

bool g_aot_enabled = getenv("NUANCE_NOAOT") == nullptr;
uint32 g_aot_max_packets = getenv("NUANCE_AOT_MAX") ? (uint32)atoi(getenv("NUANCE_AOT_MAX")) : 64U;
uint64 g_aot_packets[4] = {};

namespace {


struct ModuleState
{
  uint32 version = 0xFFFFFFFFU;   // g_iramVersion when last validated
  bool valid = false;             // the module's bytes were at base then
  bool bound = false;
  uint64 validOk = 0, validFail = 0, runs = 0, packets = 0;   // statistics (AotPrintStats)
};

// Candidate modules by address: modules live in MPE instruction RAM; each 16-byte step of the window lists the
// modules covering it, longest first (a full module also matches its shorter prefix loads and covers more).
constexpr uint32 kWindowBase = 0x20300000U, kWindowSize = 0x2000U, kStep = 16, kSteps = kWindowSize / kStep;
// The registry is built by the generated modules' static initializers, which may run before this file's: a plain
// pointer (zero before any code runs) created on first use avoids the initialization order problem.
struct Registry
{
  std::vector<const AotModule*> modules;
  std::vector<ModuleState> state[4];
  std::vector<uint16> steps[kSteps];
};
Registry* g_reg = nullptr;
#define g_modules (g_reg->modules)
#define g_state (g_reg->state)
#define g_steps (g_reg->steps)
bool g_stepsSorted = false;
// Per MPE and step: the iram version at which no candidate was valid (rejected without looking again), and the
// module that ran last (tried first).
uint32 g_noneValid[4][kSteps];
int g_last[4] = {-1, -1, -1, -1};

bool Validate(const AotModule* mod, ModuleState& st, const MPE& m)
{
  if(st.version != g_iramVersion[m.mpeIndex])
  {
    st.version = g_iramVersion[m.mpeIndex];
    st.valid = memcmp(&m.dtrom[mod->base & MPE_VALID_MEMORY_MASK], mod->code, mod->length) == 0;
    ++(st.valid ? st.validOk : st.validFail);
  }
  return st.valid;
}

void SortSteps()
{
  for(auto& v : g_steps)
    std::stable_sort(v.begin(), v.end(), [](uint16 a, uint16 b) { return g_modules[a]->length > g_modules[b]->length; });
  for(auto& n : g_noneValid)
    for(uint32& x : n)
      x = 0xFFFFFFFFU;
  g_stepsSorted = true;
}

// Runs module k if it is loaded in m and pc is one of its packets; true if it ran.
bool RunModule(MPE& m, const int k, const uint32 pc)
{
  const AotModule* const mod = g_modules[k];
  if(pc - mod->base >= mod->length)
    return false;
  ModuleState& st = g_state[m.mpeIndex][k];
  if(!Validate(mod, st, m))
    return false;
  if(!st.bound)
  {
    mod->bind(m);
    st.bound = true;
  }
  m.pcexec = pc;
  const uint32 n = mod->run(m, g_aot_max_packets);
  if(n == 0)
    return false;   // pc isn't a packet start of this module's decoding
  m.cycleCounter = n;
  g_aot_packets[m.mpeIndex] += n;
  ++st.runs;
  st.packets += n;
  g_last[m.mpeIndex] = k;
  return true;
}

}  // namespace

void AotRegister(const AotModule* module)
{
  if(!g_reg)
    g_reg = new Registry;
  if(g_modules.size() >= 0xFFFF)
    return;
  // NUANCE_AOT_SKIP=<library address, e.g. 806AD2B0> (investigations): leave that module to the interpreter (with
  // NUANCE_NOJIT=1 a NUANCE_PCLOG inside it then fires) while the rest stays compiled.
  // or a range <lo>-<hi> (library addresses): every module whose start lies in it - modules are catalogued by the
  // chunk they start at (806AF1E8, 806AF2E8, ...), and a later chunk's module covers the same code
  if(const char* skip = getenv("NUANCE_AOT_SKIP"); skip && *skip)
  {
    unsigned lo = 0, hi = 0;
    if(sscanf(skip, "%x-%x", &lo, &hi) == 2)
    {
      const unsigned start = (unsigned)strtoul(module->name, nullptr, 16);
      if(start >= lo && start <= hi)
        return;
    }
    else if(strstr(module->name, skip))
      return;
  }
  const uint16 k = (uint16)g_modules.size();
  g_modules.push_back(module);
  for(auto& st : g_state)
    st.emplace_back();
  for(uint32 a = module->base & ~(kStep - 1); a < module->base + module->length; a += kStep)
    if(a - kWindowBase < kWindowSize)
      g_steps[(a - kWindowBase) / kStep].push_back(k);
  g_stepsSorted = false;
}

void AotPrepare()
{
  if(g_reg && !g_stepsSorted)
    SortSteps();
}

bool AotTry(MPE& m)
{
  const uint32 pc = m.pcexec & ~1U;
  const uint32 off = pc - kWindowBase;
  if(off >= kWindowSize || !g_reg || !g_aot_enabled || !(m.mpectl & MPECTRL_MPEGO))
    return false;
  if(!g_stepsSorted)
    SortSteps();
  const uint32 step = off / kStep;
  const std::vector<uint16>& cand = g_steps[step];
  const uint32 version = g_iramVersion[m.mpeIndex];
  if(cand.empty() || g_noneValid[m.mpeIndex][step] == version)
    return false;
  // FetchDecodeExecute would take an interrupt here, or interpret the next packet: let it.
  if(AotInterruptTaken(m) || m.interpretNextPacket)
    return false;

  const int last = g_last[m.mpeIndex];
  if(last >= 0 && RunModule(m, last, pc))
    return true;
  bool anyValid = false;
  for(const uint16 k : cand)
  {
    if(k == last)
      continue;
    if(RunModule(m, k, pc))
      return true;
    anyValid |= g_state[m.mpeIndex][k].valid && (g_state[m.mpeIndex][k].version == version);
  }
  if(!anyValid)
    g_noneValid[m.mpeIndex][step] = version;
  return false;
}

void AotPrintStats()
{
  if(!g_reg)
    return;
  for(size_t k = 0; k < g_modules.size(); ++k)
    for(int i = 0; i < 4; ++i)
    {
      const ModuleState& st = g_state[i][k];
      if(st.validOk | st.validFail)
        fprintf(stderr, "aot %s@%08X/%u mpe%d: validated %llu ok / %llu failed, %llu runs, %llu packets\n",
          g_modules[k]->name, g_modules[k]->base, g_modules[k]->length, i, (unsigned long long)st.validOk,
          (unsigned long long)st.validFail, (unsigned long long)st.runs, (unsigned long long)st.packets);
    }
}

// ---- Module collection (NUANCE_AOT_COLLECT=<dir>) -------------------------------------------------------------
// Code loads into an MPE's local memory arrive as DMA chunks; consecutive chunks (source and destination both
// continuing) form one module load. Each new (source, length, local base) with at least 512 bytes from a bus
// address is dumped with nuance_aot_dump into <dir>/m<src>_<base>_<len>.txt (+ .bin), once.
extern "C" int nuance_aot_dump(int mpe, uint32_t srcAddr, uint32_t length, uint32_t base, const char* path);

namespace {

const char* const g_collectDir = getenv("NUANCE_AOT_COLLECT");
struct Run { uint32 src = 0, dst = 0, len = 0; };
Run g_run[32];
std::set<uint64> g_seen;
uint32 g_collected = 0;

void FinishRun(Run& r)
{
  if(r.len >= 512 && (r.src >> 28) >= 4)   // from main / system bus memory
  {
    const uint32 base = (r.dst >= 0x20000000U && r.dst < 0x22000000U) ? (0x20000000U | (r.dst & 0x007FFFFFU)) : r.dst;
    const uint64 key = ((uint64)r.src << 32) ^ ((uint64)base << 12) ^ r.len;
    if(g_seen.insert(key).second)
    {
      char path[1024];
      snprintf(path, sizeof(path), "%s/m%08X_%08X_%u.txt", g_collectDir, r.src, base, r.len);
      FILE* const exists = fopen(path, "r");
      if(exists)
        fclose(exists);
      else if(nuance_aot_dump(0, r.src, r.len, base, path) > 0)
      {
        ++g_collected;
        fprintf(stderr, "aot collect: %s\n", path);
      }
    }
  }
  r = Run{};
}

}  // namespace

void AotCollectCodeLoad(uint32 target, uint32 src, uint32 dst, uint32 bytes)
{
  if(!g_collectDir || target >= 32)
    return;
  Run& r = g_run[target];
  if(r.len && (r.src + r.len == src) && (r.dst + r.len == dst))
    r.len += bytes;
  else
  {
    FinishRun(r);
    r.src = src;
    r.dst = dst;
    r.len = bytes;
  }
}

void AotCollectFlush()
{
  if(!g_collectDir)
    return;
  for(Run& r : g_run)
    if(r.len)
      FinishRun(r);
}
