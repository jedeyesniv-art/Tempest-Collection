#pragma once
#include "TempestNiseStreamControl.h"
namespace tempest_native {
// Complete SDK 8002224E..80022324: attach an opened medium and initialize the
// stereo stream ring. An odd sector count is retained in SDK bookkeeping while
// the actual mixer ring uses the original even-sector byte length.
inline bool initialize_nise_stream(CommandMemory& m,NiseVoiceDevice& device,
                                   uint32_t handle,uint32_t ring,uint32_t blocks){
    uint32_t stage,pool,engine,locked;
    if(!m.read32(0x8003C7A8,stage)||!m.write32(0x806B9990,1)||
       !m.write32(0x8003C7BC,handle)||!m.write32(0x806B9994,0)||
       !m.write32(0x804FB370,0)||!m.read32(0x804FB36C,locked))return false;
    if(!locked&&(!m.read32(0x804FB378,engine)||engine>3||!m.write32(0x8003C7B8,0)||
       !device.pause(engine)||!m.write32(0x8003C7B8,1)||!m.write32(0x804FB36C,1)))return false;
    if(!m.read32(0x8003C7B0,pool))return false;
    for(unsigned i=0;i<12;++i){uint32_t value;if(!m.read32(pool+i*4,value)||!m.write32(stage+i*4,value))return false;}
    uint32_t bytes=(blocks&~1U)<<11;
    if(!m.write32(0x806B995C,ring)||!m.write32(0x806B9960,blocks)||!m.write32(0x806B9974,0)||
       !m.write32(stage+8,ring)||!m.write32(stage+12,bytes)||!m.write32(stage+20,ring))return false;
    for(unsigned i=0;i<12;++i){uint32_t value;if(!m.read32(stage+i*4,value)||!m.write32(pool+i*4,value))return false;}
    if(!m.read32(0x804FB36C,locked))return false;
    if(locked&&(!m.read32(0x804FB378,engine)||engine>3||!device.resume(engine)||!m.write32(0x804FB36C,0)))return false;
    if(!m.read32(stage+12,bytes)||bytes>0x800000)return false;
    for(uint32_t i=0;i<bytes;i+=4)if(!m.write32(ring+i,0))return false;
    return true;
}
}
