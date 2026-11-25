#pragma once
#include "TempestNiseStream.h"
#include "TempestNiseVoice.h"
namespace tempest_native {
struct NiseStreamProgress {
    // Deliver real pending transport/audio events. These must not manufacture
    // a successful read; the caller verifies the resulting shared state.
    virtual bool await_read_ready(CommandMemory&)=0;
    virtual bool await_stream_state(CommandMemory&)=0;
    virtual ~NiseStreamProgress()=default;
};
// Complete SDK 80022346..800223DC. The second argument is the last sector,
// not a byte length. Enqueue acceptance remains distinct from completion.
inline bool prepare_nise_music_stream(CommandMemory& m,NiseStreamServices& reads,NiseStreamProgress& progress,uint32_t first,uint32_t last,uint32_t& result){
    uint32_t handle,state,ready,blocks,destination;
    if(!m.read32(0x8003C7BC,handle))return false;if(!handle){result=0;return true;}
    if(!m.read32(0x804FB370,state))return false;
    if(int32_t(state)>0){if(!m.read32(0x806B9990,ready))return false;if(!ready&&(!progress.await_read_ready(m)||!m.read32(0x806B9990,ready)||!ready))return false;}
    if(!m.write32(0x806B9968,first)||!m.write32(0x806B9964,first)||!m.write32(0x806B996C,last)||!m.write32(0x806B9970,1)||!m.write32(0x804FB370,0)||!m.read32(0x806B9960,blocks)||!m.read32(0x806B995C,destination)||!request_nise_stream_read(m,first,blocks,destination,reads))return false;
    result=1;return true;
}
// Complete SDK activation 80022450..800224E2, with the recovered acknowledged
// pause/resume and coherent single-word DMA semantics used by voice allocation.
inline bool activate_nise_music_stream(CommandMemory& m,NiseVoiceDevice& device,NiseStreamProgress& progress,uint32_t& result,unsigned budget=4096){
    uint32_t state;if(!m.read32(0x804FB370,state))return false;
    while(state!=1&&state!=2&&int32_t(state)>=0){if(!budget--||!progress.await_stream_state(m)||!m.read32(0x804FB370,state))return false;}
    if(state==2){result=1;return true;}if(int32_t(state)<0){result=0;return true;}
    uint32_t locked,engine,pool,stage,flags;
    if(!m.read32(0x804FB36C,locked))return false;
    if(!locked){if(!m.read32(0x804FB378,engine)||engine>3||!m.write32(0x8003C7B8,0)||!device.pause(engine)||!m.write32(0x8003C7B8,1)||!m.write32(0x804FB36C,1))return false;}
    if(!m.read32(0x8003C7B0,pool)||!m.read32(0x8003C7A8,stage)||!m.read32(pool+4,flags)||!m.write32(stage,flags)||!m.write32(stage,flags|0x80)||!m.write32(pool+4,flags|0x80))return false;
    if(!m.read32(0x804FB36C,locked))return false;
    if(locked&&(!m.read32(0x804FB378,engine)||engine>3||!device.resume(engine)||!m.write32(0x804FB36C,0)))return false;
    if(!m.write32(0x804FB370,2))return false;result=1;return true;
}
// Complete SDK reset 800224E4..80022572: disable streamed mixing and restore
// its cursor to the ring base. Status 1 means ready; activation sets status 2.
inline bool reset_nise_music_stream(CommandMemory& m,NiseVoiceDevice& device){
    uint32_t locked,engine,pool,stage,flags,base;
    if(!m.read32(0x804FB36C,locked))return false;
    if(!locked){if(!m.read32(0x804FB378,engine)||engine>3||!m.write32(0x8003C7B8,0)||!device.pause(engine)||!m.write32(0x8003C7B8,1)||!m.write32(0x804FB36C,1))return false;}
    if(!m.read32(0x8003C7B0,pool)||!m.read32(0x8003C7A8,stage)||!m.read32(pool+4,flags)||!m.write32(stage,flags)||!m.read32(0x806B995C,base)||!m.write32(stage,base)||!m.write32(pool+20,base)||!m.write32(stage,flags&~0x80U)||!m.write32(pool+4,flags&~0x80U)||!m.read32(0x804FB36C,locked))return false;
    if(locked&&(!m.read32(0x804FB378,engine)||engine>3||!device.resume(engine)||!m.write32(0x804FB36C,0)))return false;
    return m.write32(0x804FB370,1);
}
// Complete SDK 80022184..800221FC. Exact Q30 unity is reduced by one before
// conversion to the two Q14 gain halfwords. Preserve signed addition rather
// than replacing it with a masked OR for out-of-range original arguments.
inline bool set_nise_mix_volumes(CommandMemory& m,NiseVoiceDevice& device,uint32_t stream,uint32_t voices){
    uint32_t locked,engine,pool,stage;
    if(!m.read32(0x804FB36C,locked))return false;
    if(!locked){if(!m.read32(0x804FB378,engine)||engine>3||!m.write32(0x8003C7B8,0)||!device.pause(engine)||!m.write32(0x8003C7B8,1)||!m.write32(0x804FB36C,1))return false;}
    if(stream==0x40000000)--stream;if(voices==0x40000000)--voices;
    uint32_t value=(uint32_t(int32_t(stream)>>14)<<16)+uint32_t(int32_t(voices)>>14);
    if(!m.read32(0x8003C7B0,pool)||!m.read32(0x8003C7A8,stage)||!m.write32(stage,value))return false;
    // The original single-scalar DMA destination is pool+0x12, aligned down
    // by scalar DMA to the packed master gain word at pool+0x10.
    if(!m.read32(0x8003C7A8,stage)||!m.read32(stage,value)||!m.write32((pool+0x12)&~3U,value)||!m.read32(0x804FB36C,locked))return false;
    if(locked&&(!m.read32(0x804FB378,engine)||engine>3||!device.resume(engine)||!m.write32(0x804FB36C,0)))return false;
    return true;
}
}
