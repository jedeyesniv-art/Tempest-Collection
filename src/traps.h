// traps.h - host hooks at 68K addresses. The first opcode word at the address is replaced by a line-A word
// ($A000 + id); when the 68K reaches it the core calls us, then runs the original opcode as if nothing happened
// (core fork: m68kHostTrap in m68kinterface.c). The handler sees the CPU exactly at the hooked instruction.
#pragma once
#include <cstdint>
#include <functional>

namespace traps {

// Returns false if the word at addr isn't `expected` (wrong ROM / already patched). A handler returning true
// skips the hooked routine: an RTS runs instead of the original opcode (only for a routine's first instruction).
bool Install(uint32_t addr, uint16_t expected, std::function<bool()> handler);

uint32_t Reg(int n);          // 0-7 d0-d7, 8-15 a0-a7
uint32_t Read32(uint32_t a);  // main RAM / cart window (jagMemSpace) or GPU RAM; no bus side effects
uint16_t Read16(uint32_t a);
uint8_t Read8(uint32_t a);

}  // namespace traps
