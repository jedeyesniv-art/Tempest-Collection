#pragma once
#include "TempestSession.h"
#include "TempestToolInput.h"
namespace tempest_native {
struct InspectorInputServices {
    virtual bool horizontal_axis(uint32_t,uint32_t,uint32_t&)=0;
    virtual bool menu_action(uint32_t,uint32_t,uint32_t&)=0;
    virtual ~InspectorInputServices()=default;
};
// Original 80019FC2..8001A24C, including the rotary A modifier and pad shoulder
// controls for vector rotation, scale and shade. This is the original viewer.
inline bool run_vector_inspector(CommandMemory& m,const OpeningAnimationInput& input,GameSessionServices& services,InspectorInputServices& axes,unsigned budget=0xFFFFFFFF){
    SceneTimingScope cadence(services,SceneTimingKind::tool);if(!cadence.divisions)return false;uint64_t remainingBudget=cadence.budget(budget);
    auto c=input.incoming;uint32_t stack=c[19],base=stack-16;c[18]=base;c[19]=stack-64;c[17]=0x80010200;
    c[16]=0;if(!services.initialize_scene({5,c}))return false;c[17]=0x80019FE8;c[0]=0;
    if(!m.write32(base-4,0)||!m.write32(0x800B7614,0x100)||!m.write32(0x800B84B0,0)||!m.write32(0x801133B8,0x140))return false;
    uint32_t inspectorAngle=0;
    uint32_t buttons;if(!m.read32(0x806B992C,buttons))return false;
    c[5]=0x801133F0;c[4]=0x806B9910;c[3]=0x1000;c[2]=0x806B992C;c[6]=0x800B761C;c[1]=0x800B7614;
    while(!(buttons&8)){
        if(!remainingBudget--)return false;uint32_t active,port,table,config,value,delta;
        if(!m.read32(0x801133F0,active)||!m.read32(0x801133E8+active*4,port)||!m.read32(0x806B9910,table)||!m.read32(table+port*16,config))return false;
        bool rotary=config&0x1000;
        if(rotary){if(!axes.menu_action(base-4,0x3000,delta))return false;}
        else if(!axes.horizontal_axis(base-4,0x500,delta))return false;
        c[17]=0x8001A09E;c[16]+=delta;if(int32_t(c[16])<0)c[16]=0;
        if(!m.read32(0x800B1750+c[16]*4,value))return false;if(!value){c[16]=0;if(!m.read32(0x800B1750,value))return false;}
        if(!m.write32(0x800B7610,value)||!m.read32(0x806B992C,buttons))return false;
        if(rotary){if(!(buttons&0x4000)){if(!m.read32(0x801133B8,value)||!m.write32(0x800B761C,0U-(value<<6)))return false;}}
        else{if(!driver_held_axis(m,32,16,delta))return false;c[17]=0x8001A138;c[0]+=uint32_t(cadence.delta(int32_t(delta),2));inspectorAngle+=uint32_t(cadence.delta(int32_t(delta<<10),3));if(!m.write32(0x800B761C,inspectorAngle))return false;}
        if(rotary){if(buttons&0x4000){if(!m.read32(0x801133B8,value))return false;value&=1023;if(!m.write32(0x800B7614,value|(value<<16)))return false;}}
        else{if(!driver_held_axis(m,0x800,0x200,delta)||!m.read32(0x800B7614,value))return false;c[17]=0x8001A1AA;
            uint32_t half=uint32_t(cadence.delta(int32_t(delta<<4),4))&65535;if(!m.write32(0x800B7614,(value+(half|(half<<16)))&1023))return false;}
        if(!driver_held_axis(m,4,1,delta)||!m.read32(0x800B7620,value)||!m.write32(0x800B7620,(value+uint32_t(cadence.delta(int32_t(delta<<8),5)))&65535)||!m.read32(0x800B9778,value)||!m.write32(0x800B9778,value+uint32_t(cadence.delta(0x10000,1))))return false;c[17]=0x8001A1DA;
        c[17]=0x80015C74;if(!services.menu_frame(c)||!m.read32(0x806B992C,buttons))return false;c[17]=0x8001A222;
    }
    return true;
}
}
