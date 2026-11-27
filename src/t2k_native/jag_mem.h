// jag_mem.h - what a ported Tempest 2000 routine sees: the Jaguar's memory at the original addresses, and the 68K
// registers with their width rules. (Frogacuda's method from native T3K: keep the game's memory layout so ported
// and emulated code share one memory image, and a port can be checked byte for byte against the emulator.)
//
// Reads come from `base` (the machine as it was when the routine was entered); writes go to `writes`, so the
// oracle can compare them with what the emulated routine really wrote. Only main RAM ($000000-$1FFFFF) and the
// cartridge ($800000-$DFFFFF) are modelled: anything else (hardware registers, GPU RAM) marks the run unsupported.
#pragma once
#include <cstdint>
#include <map>

namespace t2knative {

struct Mem {
  const uint8_t* base = nullptr;             // jagMemSpace at entry (or a snapshot of it)
  std::map<uint32_t, uint8_t> writes;        // byte address -> value written by the port
  bool unsupported = false;                  // touched something not modelled
  uint32_t unsupported_addr = 0;

  static constexpr uint32_t kRamEnd = 0x200000;
  bool Modelled(uint32_t a, bool write) {
    if (a < kRamEnd || (!write && a >= 0x800000 && a < 0xE00000)) return true;
    if (!unsupported) unsupported = true, unsupported_addr = a;
    return false;
  }
  uint8_t r8(uint32_t a) {
    a &= 0xFFFFFF;
    if (!Modelled(a, false)) return 0;
    auto it = writes.find(a);
    return it != writes.end() ? it->second : base[a];
  }
  uint16_t r16(uint32_t a) { return uint16_t(r8(a) << 8 | r8(a + 1)); }
  uint32_t r32(uint32_t a) { return uint32_t(r16(a)) << 16 | r16(a + 2); }
  void w8(uint32_t a, uint8_t v) {
    a &= 0xFFFFFF;
    if (Modelled(a, true)) writes[a] = v;
  }
  void w16(uint32_t a, uint16_t v) { w8(a, uint8_t(v >> 8)), w8(a + 1, uint8_t(v)); }
  void w32(uint32_t a, uint32_t v) { w16(a, uint16_t(v >> 16)), w16(a + 2, uint16_t(v)); }
};

// The 68K registers. Word / byte moves into a data register keep its upper bits, as on the 68K.
struct Cpu {
  uint32_t d[8] = {}, a[8] = {};
  bool z = false;   // the Z flag at return (callers branch on it: beq / bne right after the bsr)
  static void setw(uint32_t& r, uint32_t v) { r = (r & 0xFFFF0000u) | (v & 0xFFFF); }
  static void setb(uint32_t& r, uint32_t v) { r = (r & 0xFFFFFF00u) | (v & 0xFF); }
  static int16_t sw(uint32_t r) { return int16_t(r & 0xFFFF); }
};

}  // namespace t2knative
