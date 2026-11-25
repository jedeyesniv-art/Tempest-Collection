#pragma once
#include "TempestCommands.h"
namespace tempest_native {
// Native request preparation at 80010426, ending at entry to 800228E2.
// These preserve the six original scalar arguments; the audio allocator and
// playback effects of the callee remain dependencies, not silent no-ops.
struct SoundRequest {uint32_t resource,argument1,resourceWord,argument3,argument4,argument5;};
inline bool prepare_sound_request(CommandMemory& memory,uint32_t cue,SoundRequest& out){
    uint32_t address=0x80115B10+(cue&255)*8,a,b;
    if(!memory.read32(address,a)||!memory.read32(address+4,b))return false;
    out={a,0x1000+(((cue>>16)&255)<<6),b,((cue>>8)&255)<<24,((cue>>24)&255)<<10,0};
    return true;
}
}
