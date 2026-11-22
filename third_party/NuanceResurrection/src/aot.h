// aot.h - ahead-of-time compiled MPE code modules (llamasoft-nuon).
//
// A module is a fixed piece of MPE code that a game loads into an MPE's local memory (e.g. Tempest 3000's renderer
// modules). tools/t3k_aot.py turns a decoded dump of it (nuance_aot_dump) into C++ that runs its packets with the
// interpreter's own Execute_* handlers, called directly with the decoded fields as constants: the same semantics
// as Nuance's interpreter, without FetchDecodeExecute's per-call overhead, and with the handlers inlinable.
// Generated sources hold a translation of game code: they live outside the repositories (NUANCE_AOT_DIR).
#pragma once
#include "basetypes.h"

class MPE;

struct AotModule
{
  uint32 base;            // load address in MPE local memory
  uint32 length;          // bytes
  const uint8* code;      // the expected bytes at base (validated before use)
  uint32 (*run)(MPE& m, uint32 maxPackets);   // packets run (0 = pc isn't a packet start of the module)
  void (*bind)(MPE& m);   // per-MPE set-up (local-memory pointers in the decoded fields)
  const char* name;
};

void AotRegister(const AotModule* module);
// If MPE m is about to run code of a registered module that is really loaded there, run it (up to the next taken
// jump or g_aot_max_packets packets) and return true; else false (FetchDecodeExecute runs as before).
bool AotTry(MPE& m);
void AotPrepare();      // builds the module index (call before AotTry runs on several threads)
void AotPrintStats();
// NUANCE_AOT_COLLECT=<dir>: dump every new code module loaded into an MPE (aot.cpp); flushed each field.
void AotCollectCodeLoad(uint32 target, uint32 src, uint32 dst, uint32 bytes);
void AotCollectFlush();   // per module and MPE: validations, runs, packets (stderr)

extern bool g_aot_enabled;          // NUANCE_NOAOT=1: off
extern uint32 g_aot_max_packets;    // NUANCE_AOT_MAX (default 64; 1 = interpreter-identical scheduling)
extern uint32 g_iramVersion[4];     // mpe.cpp: bumped on every code-region write
extern uint64 g_aot_packets[4];     // statistics: packets run through AOT code

#ifdef AOT_NEED_INTERRUPT_TEST
// Whether FetchDecodeExecute would take an interrupt now (its exact test: level 2, then enabled level 1).
static inline bool AotInterruptTaken(const MPE& m)
{
  if(!m.intsrc || (m.ecuSkipCounter != 0) || (m.intctl & (1U << 5)))
    return false;
  if(m.intsrc & (1U << m.inten2sel))
    return (m.intctl & (1U << 7)) == 0;
  return ((m.intctl & ((1U << 3) | (1U << 1))) == 0) && ((m.intsrc & m.inten1) != 0);
}
#endif
