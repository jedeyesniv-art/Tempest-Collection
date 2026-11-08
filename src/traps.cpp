// traps.cpp - see traps.h.
#include "traps.h"

#include <vector>

extern "C" {
extern uint8_t jagMemSpace[];
extern int (*m68kHostTrap)(unsigned opcode);
uint32_t M68KHostGetReg(int n);
const uint8_t* GPUHostRAM(void);
}

namespace traps {
namespace {

struct Trap {
  uint32_t addr;
  uint16_t orig;
  std::function<void()> handler;
};
std::vector<Trap> g_traps;

int Dispatch(unsigned opcode) {
  unsigned id = opcode & 0xFFF;
  if (id >= g_traps.size()) return -1;
  g_traps[id].handler();
  return g_traps[id].orig;
}

}  // namespace

bool Install(uint32_t addr, uint16_t expected, std::function<void()> handler) {
  if (Read16(addr) != expected) return false;
  uint16_t word = uint16_t(0xA000 + g_traps.size());
  g_traps.push_back({addr, expected, std::move(handler)});
  jagMemSpace[addr] = uint8_t(word >> 8);
  jagMemSpace[addr + 1] = uint8_t(word);
  m68kHostTrap = Dispatch;
  return true;
}

uint32_t Reg(int n) { return M68KHostGetReg(n); }

uint8_t Read8(uint32_t a) {
  a &= 0xFFFFFF;
  if (a >= 0xF03000 && a < 0xF04000) return GPUHostRAM()[a - 0xF03000];
  return jagMemSpace[a];
}
uint16_t Read16(uint32_t a) { return uint16_t(Read8(a) << 8 | Read8(a + 1)); }
uint32_t Read32(uint32_t a) { return uint32_t(Read16(a)) << 16 | Read16(a + 2); }

}  // namespace traps
