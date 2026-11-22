#ifndef CAPTURE_H
#define CAPTURE_H

// Picture capture for a native renderer (llamasoft-nuon): see capture.cpp.

#include "basetypes.h"

class MPE;

void CaptureLinearRead(const MPE& mpe, uint32 flags, uint32 base, uint32 intaddr);   // after each linear DMA
void CaptureFlip();                                                                 // VidConfig / VidChangeBase
void CaptureDrawGate(const MPE& mpe, uint32 flags, uint32 base, uint32 intaddr);     // after each linear DMA read, before the MPE uses it
void CaptureBiLinear(const MPE& mpe, uint32 flags, uint32 base);                   // pixel DMA (frame buffers)

#endif
