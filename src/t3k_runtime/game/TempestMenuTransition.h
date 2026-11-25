#pragma once
#include "TempestOpeningAnimation.h"
#include "TempestDriver.h"
namespace tempest_native {
// The already-recovered transition algorithm with its original scene/frame
// saved-data handoff. This enriches the ABI contract, not a second transition.
inline bool run_menu_transition(CommandMemory& m,const DriverTransitionInput& input,const std::array<uint32_t,20>& incoming,OpeningAnimationServices& services,unsigned budget=4096){
    uint32_t stack=incoming[19],saved;if((stack&15)||stack<48||!m.read32(0x800B850C,saved))return false;
    struct Bridge:DriverSceneServices {
        OpeningAnimationServices& services;std::array<uint32_t,20> context;uint32_t frames=0;
        Bridge(OpeningAnimationServices& s,const std::array<uint32_t,20>& c):services(s),context(c){}
        bool initialize_scene(uint32_t index)override{return services.initialize_scene({index,context});}
        bool menu_frame()override{context[0]=0x800DC274;context[2]=0xFF3F;context[3]=0x806B992C;context[16]=++frames;context[17]=0x80015C74;return services.menu_frame(context);}
    } bridge(services,incoming);
    bridge.context[1]=input.frames;bridge.context[4]=saved;bridge.context[16]=0;bridge.context[17]=0x80010200;bridge.context[18]=stack-16;bridge.context[19]=stack-48;
    return run_driver_transition(m,bridge,input,budget);
}
}
