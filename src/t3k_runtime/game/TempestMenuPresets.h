#pragma once
#include "TempestSceneInitialization.h"
namespace tempest_native {
struct MenuPresetServices {
    virtual bool initialize_scene(const SceneInitializationInput&)=0;
    virtual ~MenuPresetServices()=default;
};
// 8001FC7E..8001FCCC. Preserve the two reads per packed source word, including
// aliases: the first destination store can change the second source read.
inline bool unpack_menu_parameters(CommandMemory& m,uint32_t destination,uint32_t source,uint32_t& mask){
    for(unsigned i=0;i<32;++i){uint32_t word;
        if(!m.read32(source+i*4,word)||!m.write32(destination+i*8,word>>16)||!m.read32(source+i*4,word)||!m.write32(destination+i*8+4,word&65535))return false;}
    return m.read32(source+128,mask);
}
// 8001FCCE..8001FD1E. This uses the original low-32-bit multiply followed by
// signed shift, not a widened floating-point interpolation or motion smoothing.
inline bool interpolate_menu_parameters(CommandMemory& m,uint32_t remaining,uint32_t step,uint32_t target,uint32_t start,uint32_t output,uint32_t& next){
    uint32_t candidate=remaining-step;next=int32_t(candidate)>0?candidate:0;
    for(unsigned i=0;i<64;++i){uint32_t a,b;if(!m.read32(target+i*4,a)||!m.read32(start+i*4,b))return false;
        uint32_t product=(a-b)*next,value=(b+uint32_t(int32_t(product)>>16))&65535;if(!m.write32(output+i*4,value))return false;}
    return true;
}
// 80017F84..8001800E. Build the original scene-3 layer descriptors in bit order,
// with at least five entries. More than five matches are retained, not truncated.
inline bool initialize_menu_layers(CommandMemory& m,MenuPresetServices& services,uint32_t mask,const std::array<uint32_t,20>& incoming){
    uint32_t count=0;for(unsigned bit=0;bit<32;++bit)if(mask&(1U<<bit)){uint32_t descriptor;
        if(!m.read32(0x800BA798+bit*4,descriptor)||!m.write32(0x800B2B68+count*4,descriptor))return false;++count;}
    while(count<5){if(!m.write32(0x800B2B68+count*4,0x00320001))return false;++count;}
    auto context=incoming;if((context[19]&15)||context[19]<16)return false;
    context[17]=count;context[18]=incoming[19]-16;context[19]=incoming[19]-16;
    return services.initialize_scene({3,context});
}
struct MenuPresetStepInput {std::array<uint32_t,9> arguments;std::array<uint32_t,20> incoming;};
// Complete 80018010..8001817A: swap preset selectors when idle, unpack new target,
// then either begin a same-layer transition or reinitialize changed scene layers.
inline bool step_menu_preset(CommandMemory& m,MenuPresetServices& services,const MenuPresetStepInput& input){
    const auto& a=input.arguments;uint32_t remaining;if(!m.read32(a[4],remaining))return false;if(remaining)return true;
    uint32_t first,second,previous,selector,source,mask;
    if(!m.read32(a[0],first)||!m.read32(a[1],second)||!m.write32(a[0],second)||!m.write32(a[1],first)||
       !m.read32(a[2],previous)||!m.write32(a[3],previous)||!m.read32(a[0],selector)||selector>255||
       !m.read32(0x8010DCB0+selector*4,source)||!unpack_menu_parameters(m,a[7],source,mask)||!m.write32(a[2],mask)||!m.read32(a[3],previous))return false;
    if(previous==mask){
        for(unsigned i=0;i<64;++i){uint32_t word;if(!m.read32(a[8]+i*4,word)||!m.write32(a[6]+i*4,word))return false;}
        if(!m.write32(a[4],0x10000))return false;
        for(uint32_t base:{a[6],a[7]})for(uint32_t offset:{0x40U,0x44U}){uint32_t word;if(!m.read32(base+offset,word)||!m.write32(base+offset,uint32_t(int32_t(int16_t(word)))))return false;}
        for(uint32_t base:{a[6],a[7]})for(uint32_t offset:{0xC0U,0xC4U}){uint32_t word;if(!m.read32(base+offset,word)||!m.write32(base+offset,uint32_t(int32_t(int16_t(word)))))return false;}
        return true;
    }
    for(unsigned i=0;i<64;++i){uint32_t word;if(!m.read32(a[7]+i*4,word)||!m.write32(a[8]+i*4,word))return false;}
    auto context=input.incoming;uint32_t stack=context[19];if((stack&15)||stack<48)return false;
    context[0]=a[2];context[1]=a[8];context[2]=a[6];context[3]=a[4];context[4]=previous;context[16]=a[7];context[17]=64;context[18]=stack-16;context[19]=stack-48;
    if(!initialize_menu_layers(m,services,mask,context)||!m.write32(a[4],0))return false;
    return true;
}
}
