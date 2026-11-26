#pragma once
#include "TempestSceneInitialization.h"
#include "TempestSceneTiming.h"
namespace tempest_native {
struct OpeningAnimationInput {
    std::array<uint32_t,20> incoming;
    uint32_t returnAddress;
};
struct OpeningAnimationServices:SceneTimingServices {
    virtual bool initialize_scene(const SceneInitializationInput&)=0;
    virtual bool play_sound(uint32_t index,uint32_t pitch)=0;
    virtual bool menu_frame(const std::array<uint32_t,20>&)=0;
    virtual ~OpeningAnimationServices()=default;
};
struct OpeningAnimationResult {uint32_t movementFrames=0,fadeFrames=0;};
// Complete original 800177C6..80017A02. Incoming saved-engine data is explicit;
// only this caller's established fields are replaced. No zero launch seeds or
// host pointers substitute for the inherited data consumed by scene/frame code.
inline bool run_opening_animation(CommandMemory& m,const OpeningAnimationInput& input,OpeningAnimationServices& services,OpeningAnimationResult& result,unsigned budget=126){
    SceneTimingScope cadence(services,SceneTimingKind::opening);if(!cadence.divisions||cadence.divisions>120)return false;unsigned divisions=cadence.divisions,numerator=cadence.step().numerator;
    if(!numerator||numerator>divisions)return false;uint32_t carry=0;uint64_t remainingBudget=uint64_t(budget)*divisions;
    result={};uint32_t stack=input.incoming[19];if((stack&15)||stack<48)return false;
    auto context=input.incoming;context[0]=0;context[17]=input.returnAddress;context[18]=stack-16;context[19]=stack-48;
    if(!m.write32(0x800B77D4,0x00B40078)||!services.initialize_scene({0,context})||
       !services.play_sound(31,0x800)||!services.play_sound(31,0x810))return false;
    const std::array<std::array<uint32_t,2>,7> setup{{{0x800DC240,3000},{0x800B9520,0x60000},{0x800B9524,0xB0000},
        {0x800B9528,0},{0x800B952C,0x00180020},{0x800B84B4,0x10808000},{0x800B84BC,0}}};
    for(auto w:setup)if(!m.write32(w[0],w[1]))return false;
    uint32_t flags;if(!m.read32(0x800DC274,flags))return false;
    if(!(flags&2)){
        context[1]=0x800B84B0;context[2]=0x800B77D0;context[3]=0x800B77B0;context[4]=0x800B84C8;
        context[5]=0xFF3F;context[6]=0x806B992C;context[16]=0x800DC274;context[17]=0x800B84B4;
        for(uint32_t sample=0;sample<62*divisions;sample+=numerator){uint32_t step=sample*8/divisions,buttons;
            if(!m.read32(0x806B992C,buttons))return false;
            if(buttons&0xFF3F){if(!m.read32(0x800DC274,flags)||!m.write32(0x800DC274,flags|2))return false;}
            uint32_t delta=496*divisions-sample*8,scale=0x10200+(delta<<5)/divisions;
            if(!m.write32(0x800B84B0,0x3A00+delta*2/divisions)||!m.write32(0x800B84B8,scale)||!m.write32(0x800B84C8,scale)||
               !m.write32(0x800B77CC,step)||!m.write32(0x800B77D0,(sample*8<<8)/divisions))return false;
            context[0]=(sample+numerator)*8/divisions; // Original increment is a call delay-slot effect.
            if(!remainingBudget--||!services.menu_frame(context))return false;++result.movementFrames;context[17]=0x8001792E;
            if(sample+numerator>=62*divisions)carry=sample+numerator-62*divisions;
            if(!m.read32(0x800DC274,flags))return false;if(flags&2)break;
        }
    }
    if(!services.play_sound(46,0x1000)||!m.read32(0x800DC274,flags))return false;context[17]=0x80017954;
    if(!(flags&2)){
        context[1]=0xFF3F;context[2]=0x806B992C;
        for(uint32_t sample=carry;sample<64*divisions;sample+=numerator){uint32_t value=sample*0x400/divisions,buttons;
            if(!m.read32(0x806B992C,buttons))return false;
            if(buttons&0xFF3F){if(!m.read32(0x800DC274,flags)||!m.write32(0x800DC274,flags|2))return false;}
            if(!m.write32(0x800B9528,value)||!m.write32(0x800B84B0,0x3A00+(value>>6)))return false;
            context[0]=(sample+numerator)*0x400/divisions;
            if(!remainingBudget--||!services.menu_frame(context))return false;++result.fadeFrames;context[17]=0x800179D2;
            if(!m.read32(0x800DC274,flags))return false;if(flags&2)break;
        }
    }
    return true;
}
}
