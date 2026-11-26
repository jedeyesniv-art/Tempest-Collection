#pragma once
#include "TempestSession.h"
namespace tempest_native {
struct AssetPresentationInput {uint32_t source,scene;OpeningAnimationInput caller;};
// Complete original 8001C7E6..8001CA18. This presents original scene/text data;
// there is no captured bitmap or replacement asset underneath the transition.
inline bool run_session_asset(CommandMemory& m,const AssetPresentationInput& input,GameSessionServices& services,unsigned budget=1024){
    SceneTimingScope cadence(services,SceneTimingKind::asset);if(!cadence.divisions)return false;uint64_t remainingBudget=cadence.budget(budget);
    uint32_t stack=input.caller.incoming[19];if((stack&15)||stack<48)return false;
    auto c=input.caller.incoming;c[18]=stack-16;c[19]=stack-48;c[17]=input.caller.returnAddress;
    c[2]=input.source;c[3]=input.scene;c[4]=0;c[16]=0x800B6A90;c[1]=0x800B6A94;c[0]=0x80015500;
    uint32_t random,value;if(!m.write32(0x800B6A90,0xFFFFFFFF)||!m.write32(0x800B6A94,65535)||!next_driver_random(m,random)||!m.write32(0x800B6A98,random)||!next_driver_random(m,random)||!m.read32(0x800B6A9C,value)||!m.write32(0x800B6A9C,value^(random&32))||!m.write32(0x800B6AA0,input.source))return false;
    c[16]=0x800B6AA0;c[17]=0x8001C82C;
    if(!services.transition({0x3E00,0x10808000,0x10400,0,20},c))return false;c[17]=0x8001C864;
    if(!m.write32(0x800B850C,1)||!services.initialize_scene({input.scene,c})||!m.read32(0x800B6A94,c[1]))return false;c[17]=0x8001C87A;c[16]=0x800B6A94;c[0]=0x800B92D4;
    auto frame=[&](uint32_t link){if(!remainingBudget--||!services.menu_frame(c))return false;c[17]=link;return true;};
    if(int32_t(c[1])>0){do{if(!m.read32(0x800B6A94,value)||!m.write32(0x800B6A94,int32_t(value)>0?value-uint32_t(cadence.delta(0x400,0)):0)||!frame(0x8001C8B6)||!m.read32(0x800B92D4,value)||!m.write32(0x800B92D4,value+uint32_t(cadence.delta(0x2000,2)))||!m.read32(0x800B6A94,value))return false;}while(int32_t(value)>0);}
    c[16]=0;c[1]=0x800B92D4;c[3]=0x806B992C;c[2]=0xFF3F;c[0]=0x800DC274;
    do{uint32_t buttons,flags;if(!frame(0x8001C8FC)||!m.read32(0x800B92D4,value)||!m.write32(0x800B92D4,value+uint32_t(cadence.delta(0x2000,2)))||!m.read32(0x806B992C,buttons))return false;c[16]+=unsigned(cadence.step().legacy_boundary());
        if(buttons&0xFF3F){c[16]=600;if(!m.read32(0x800DC274,flags)||!m.write32(0x800DC274,flags|2))return false;c[16]+=unsigned(cadence.step().legacy_boundary());}
    }while(c[16]<=599);
    if(!services.play_sound(46,0x1000)||!m.read32(0x800B6A94,value))return false;c[17]=0x8001C946;
    if(int32_t(value)<=65535){c[17]=65535;c[16]=0x800B6A94;c[0]=0x800B92D4;
        do{if(!m.read32(0x800B6A94,value)||!m.write32(0x800B6A94,value+uint32_t(cadence.delta(0x400,1)))||!frame(0x8001C9A2)||!m.read32(0x800B92D4,value)||!m.write32(0x800B92D4,value-uint32_t(cadence.delta(0x8000,3)))||!m.read32(0x800B6A94,value))return false;}while(int32_t(value)<=65535);
    }
    return m.write32(0x800B850C,0)&&services.transition({0x3F00,0x10808000,0x10100,0,40},c);
}
}
