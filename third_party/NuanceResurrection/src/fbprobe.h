#ifndef FBPROBE_H
#define FBPROBE_H

// Frame buffer probe (llamasoft-nuon, investigations): NUANCE_FBPROBE / NUANCE_CLEARTARGET, see fbprobe.cpp.

#include "basetypes.h"

class MPE;

void FbProbeBiLinear(const MPE& mpe, uint32 flags, uint32 base, uint32 xinfo, uint32 yinfo, uint32 intaddr);
void FbProbeLinear(const MPE& mpe, uint32 flags, uint32 base);
// NUANCE_ENTRYLOG (display list census): after a linear DMA; and a code load into MPE `target`.
void EntryLogLinear(const MPE& mpe, uint32 flags, uint32 base, uint32 intaddr);
void EntryLogCodeLoad(uint32 target, uint32 src, uint32 dst);

#endif
