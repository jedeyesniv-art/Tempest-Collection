#pragma once
#include "TempestNiseVoice.h"
namespace tempest_native {
inline bool pause_nise_voice_transfer(CommandMemory& m,NiseVoiceDevice& device,uint32_t engine){uint32_t locked;if(!m.read32(0x804FB36C,locked))return false;if(locked)return true;return m.write32(0x8003C7B8,0)&&device.pause(engine)&&m.write32(0x8003C7B8,1)&&m.write32(0x804FB36C,1);}
inline bool resume_nise_voice_transfer(CommandMemory& m,NiseVoiceDevice& device,uint32_t engine){uint32_t locked;if(!m.read32(0x804FB36C,locked))return false;return !locked||(device.resume(engine)&&m.write32(0x804FB36C,0));}
// Complete SDK 80022996..80022A04, including visible transfer staging.
inline bool stop_nise_voice(CommandMemory& m,NiseVoiceDevice& device,uint32_t handle){
    if(handle>31)return false;uint32_t engine,pool,stage,mask;
    if(!m.read32(0x804FB378,engine)||engine>3||!m.read32(0x8003C7B0,pool)||!m.read32(0x8003C7A8,stage)||!pause_nise_voice_transfer(m,device,engine)||!m.read32(pool,mask)||!m.write32(stage,mask)||!m.write32(stage,mask&~(1U<<handle))||!m.write32(pool,mask&~(1U<<handle)))return false;
    return resume_nise_voice_transfer(m,device,engine);
}
// Complete SDK 800226DE..800228E0. Keep reads/stores ordered: descriptor and
// spatial data may alias the transfer buffer that the routine clears/fills.
inline bool start_nise_descriptor(CommandMemory& m,NiseVoiceDevice& device,uint32_t requested,uint32_t descriptor,uint32_t spatial,uint32_t volume,uint32_t auxiliary,uint32_t& handle){
    if(requested!=0xFFFFFFFF&&requested>31)return false;uint32_t engine,stage,pool,mask;
    if(!m.read32(0x804FB378,engine)||engine>3||!m.read32(0x8003C7A8,stage)||!m.read32(0x8003C7B0,pool)||!m.read32(pool,mask)||!m.write32(stage,mask))return false;
    handle=requested;if(handle==0xFFFFFFFF){handle=0;while(handle<31&&(mask&(1U<<handle)))++handle;if(handle==31)handle=0;}
    if(!stop_nise_voice(m,device,handle))return false;
    for(unsigned i=0;i<24;++i)if(!m.write32(stage+i*4,0))return false;
    uint32_t format,bytes,value,other;
    if(!m.read32(descriptor+20,format))return false;format=(format>>1)&7;bytes=format?1:2;
    if(!m.read32(descriptor,value)||!m.write32(stage,value)||!m.read32(descriptor,value)||!m.write32(stage+4,value)||!m.read32(descriptor+4,value)||!m.read32(descriptor,other)||!m.write32(stage+8,other+value*bytes)||!m.read32(descriptor+20,value))return false;
    if(value&1){if(!m.read32(descriptor,other)||!m.read32(descriptor+8,value)||!m.write32(stage+12,other+value*bytes))return false;}
    else if(!m.write32(stage+12,0))return false;
    if(!m.read32(descriptor+16,value)||!m.write32(stage+20,((value*3U)>>1)<<16))return false;
    for(unsigned i=0;i<4;++i)if(!m.read32(descriptor+28+i*4,value)||!m.write32(stage+44+i*4,value))return false;
    if(!m.read32(spatial,value)||!m.write32(stage+80,0x20000000+nise_half_toward_zero(0-value))||!m.read32(spatial+4,value)||!m.write32(stage+84,0x20000000+nise_half_toward_zero(value))||!m.write32(stage+88,auxiliary)||!m.write32(stage+92,volume)||!m.write32(stage+24,format?0x80:0x81)||!pause_nise_voice_transfer(m,device,engine))return false;
    for(unsigned i=0;i<24;++i)if(!m.read32(stage+i*4,value)||!m.write32(pool+48+handle*96+i*4,value))return false;
    if(!m.write32(stage,mask|(1U<<handle))||!m.write32(pool,mask|(1U<<handle)))return false;
    return resume_nise_voice_transfer(m,device,engine);
}
}
