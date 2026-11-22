#ifndef HIRES_H
#define HIRES_H

// Tempest 3000 at 720x480 (llamasoft-nuon): the game renders into 360x240 buffers that the video hardware upscales.
// With g_hires on, the renderers draw at twice the resolution into bigger buffers above the game's 8 MB of main-bus
// memory; the game keeps its own buffer addresses and the core remaps them. See PLAN.md "High resolution".

#include "basetypes.h"

class MPE;

extern bool g_hires;

void HiresInit();                         // reports the mode (NUANCE_HIRES, NUANCE_HIRES_TPL: read at start-up)
uint32 HiresRemapBase(uint32 base);       // a game frame buffer base -> its 720x480 buffer (others unchanged)
uint32 HiresDmaFlags(uint32 flags);       // 360-pixel line width -> 720
void HiresField();                        // once per field: code and data patches (each checked against the original)
// A linear DMA read `words` words from system memory `src` into the MPE's local `intaddr`: fix the local copy (game
// data with 360x240 values; display list entries by type).
void HiresLinearRead(MPE& mpe, uint32 src, uint32 words, uint32 intaddr);
// A linear DMA wrote `words` words from the MPE's local `intaddr` to system memory `dst` (display list write-back).
void HiresLinearWrite(MPE& mpe, uint32 dst, uint32 words, uint32 intaddr);

#endif
