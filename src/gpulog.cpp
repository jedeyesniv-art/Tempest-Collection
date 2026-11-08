// gpulog.cpp - survey tool: logs every gpurun call (module, mode, dest screen, caller, in_buf) in a frame range.
// Headless: --gpulog=F0:F1:<file>.
#include "gpulog.h"

#include <cstdio>

#include "traps.h"

namespace gpulog {
namespace {

constexpr uint32_t kGpurun = 0x81A4F0;     // yakgpu.s gpurun (first word: bsr gpuload)
constexpr uint32_t kInBuf = 0xF03F60;      // in_buf, 39 longs up to gpu_mode
constexpr uint32_t kGpuMode = 0xF03FFC;
constexpr uint32_t kGpuScreen = 0xF03FF8;

struct Mod { uint32_t addr; const char* name; };
const Mod kMods[] = {
    {0x81A526, "llama"}, {0x81AFAE, "goat"}, {0x81BBFA, "antelope"}, {0x81CA4A, "camel"}, {0x81D0DA, "xcamel"},
    {0x81DE12, "stoat"}, {0x81E10E, "ox"},   {0x81EB52, "horse"},    {0x81F6A2, "donky"},
};

FILE* g_out = nullptr;
int g_frame = 0, g_f0 = 0, g_f1 = -1;

bool OnGpurun() {
  if (!g_out || g_frame < g_f0 || g_frame > g_f1) return false;
  uint32_t mod = traps::Reg(8);
  const char* name = "?";
  for (auto& m : kMods)
    if (m.addr == mod) name = m.name;
  uint32_t caller = traps::Read32(traps::Reg(15));
  std::fprintf(g_out, "%d %06X %-8s mode=%u scr=%06X cs=%06X ds=%06X in:", g_frame, caller - 6, name,
               traps::Read32(kGpuMode), traps::Read32(kGpuScreen), traps::Read32(0x7FAC), traps::Read32(0x7FB0));
  for (int i = 0; i < 39; ++i) std::fprintf(g_out, " %08X", traps::Read32(kInBuf + 4 * i));
  std::fprintf(g_out, "\n");
  return false;
}

}  // namespace

bool Install(int f0, int f1, const char* path) {
  g_out = std::fopen(path, "w");
  g_f0 = f0;
  g_f1 = f1;
  return g_out && traps::Install(kGpurun, 0x6100, OnGpurun);
}

void SetFrame(int f) {
  g_frame = f;
  if (g_out && f > g_f1) { std::fclose(g_out); g_out = nullptr; }
}

}  // namespace gpulog
