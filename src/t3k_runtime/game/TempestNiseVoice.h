#pragma once
#include "TempestSound.h"
#include <array>
namespace tempest_native {
// Data preparation from 80022744..80022882. Allocation, pause/resume handshakes
// and publication remain the caller's responsibility; this function changes no
// device state and never substitutes a reference output.
struct NiseVoiceDescriptor {std::array<uint32_t,11> sample{};std::array<uint32_t,4> spatial{};uint32_t volume=0,auxiliary=0;};
inline uint32_t nise_half_toward_zero(uint32_t value){return uint32_t((int64_t(int32_t(value)))/2);}
inline std::array<uint32_t,24> prepare_nise_voice(const NiseVoiceDescriptor& input){
    const auto& d=input.sample;const auto& p=input.spatial;std::array<uint32_t,24> v{};
    uint32_t format=(d[5]>>1)&7,bytes=format?1:2;
    v[0]=v[1]=d[0];v[2]=d[0]+bytes*d[1];v[3]=(d[5]&1)?d[0]+bytes*d[2]:0;
    v[5]=((d[4]*3U)>>1)<<16;v[6]=format?0x80:0x81;
    for(unsigned i=0;i<4;++i)v[11+i]=d[7+i];
    v[20]=0x20000000+nise_half_toward_zero(0-p[0]);
    v[21]=0x20000000+nise_half_toward_zero(p[1]);v[22]=input.auxiliary;v[23]=input.volume;return v;
}
inline NiseVoiceDescriptor nise_voice_descriptor(const SoundRequest& request){
    NiseVoiceDescriptor d;d.sample[0]=request.resource;d.sample[1]=request.resourceWord;d.sample[4]=request.argument1;
    d.spatial[0]=request.argument4;d.spatial[1]=request.argument5;d.volume=request.argument3;return d;
}
struct NiseVoiceDevice {
    virtual bool pause(uint32_t engine)=0;
    virtual bool resume(uint32_t engine)=0;
    virtual ~NiseVoiceDevice()=default;
};
// 800228E2 -> 800226DE, including selection at 80021928 and stop at
// 80022996. Transport is explicit: a native audio device must acknowledge
// pause before shared voice state is changed. A surrounding memory transaction
// owns rollback when any device or memory operation fails.
inline bool start_nise_sound(CommandMemory& m,NiseVoiceDevice& device,const SoundRequest& request,uint32_t& handle){
    uint32_t stage,pool,mask,engine;
    if(!m.read32(0x804FB378,engine)||engine>3||!m.read32(0x8003C7A8,stage)||!m.read32(0x8003C7B0,pool)||!m.read32(pool,mask)||!m.write32(stage,mask))return false;
    unsigned voice=0;while(voice<31&&(mask&(1U<<voice)))++voice;if(voice==31)voice=0;
    auto pause=[&](){uint32_t locked;if(!m.read32(0x804FB36C,locked))return false;if(locked)return true;
        return m.write32(0x8003C7B8,0)&&device.pause(engine)&&m.write32(0x8003C7B8,1)&&m.write32(0x804FB36C,1);};
    auto resume=[&](){uint32_t locked;if(!m.read32(0x804FB36C,locked))return false;return !locked||(device.resume(engine)&&m.write32(0x804FB36C,0));};
    uint32_t current;
    if(!pause()||!m.read32(pool,current)||!m.write32(stage,current)||!m.write32(stage,current&~(1U<<voice))||!m.write32(pool,current&~(1U<<voice))||!resume())return false;
    auto data=prepare_nise_voice(nise_voice_descriptor(request));for(unsigned i=0;i<24;++i)if(!m.write32(stage+i*4,data[i]))return false;
    if(!pause())return false;for(unsigned i=0;i<24;++i){uint32_t value;if(!m.read32(stage+i*4,value)||!m.write32(pool+48+voice*96+i*4,value))return false;}
    uint32_t active=mask|(1U<<voice);if(!m.write32(stage,active)||!m.write32(pool,active)||!resume())return false;handle=voice;return true;
}
}
