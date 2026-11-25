#pragma once
#include "TempestOpeningAnimation.h"
#include "TempestDriver.h"
namespace tempest_native {
inline bool next_driver_random(CommandMemory& m,uint32_t& value){
    uint32_t state;if(!m.read32(0x806B9918,state))return false;value=(state>>1)^((state&1)?0xA3000000:0);return m.write32(0x806B9918,value);
}
// Original palette-cycle helper 80015A72..80015B12, nonnegative cycle domain.
inline bool advance_menu_palette(CommandMemory& m){
    uint32_t cycle;if(!m.read32(0x801133AC,cycle)||int32_t(cycle)<0)return false;
    for(unsigned i=0;i<5;++i){uint32_t color;if(!m.read32(0x806A250C+((cycle%8)*5+i)*4,color)||!m.write32(0x800B4834+i*4,color))return false;}
    return m.write32(0x801133AC,cycle+1);
}
struct SelectionMenuInput {uint32_t content,choices,label;OpeningAnimationInput caller;};
struct SelectionMenuServices:OpeningAnimationServices {
    virtual bool select_music(uint32_t)=0;
    virtual bool transition(const DriverTransitionInput&,const std::array<uint32_t,20>&)=0;
    virtual bool vertical_axis(uint32_t latch,uint32_t releaseMask,uint32_t& result)=0;
};
// Complete original 8001817C..8001867C, with explicit scene/frame dependencies.
// Positive menu sizes up to 32 and nonnegative palette cycles are supported.
inline bool run_selection_menu(CommandMemory& m,const SelectionMenuInput& input,SelectionMenuServices& services,uint32_t& output,unsigned budget=4096){
    if(!input.choices||input.choices>32)return false;uint32_t stack=input.caller.incoming[19];if((stack&15)||stack<96)return false;
    auto context=input.caller.incoming;context[0]=input.content;context[2]=input.choices;context[16]=input.label;
    context[7]=1;context[17]=input.caller.returnAddress;context[18]=stack-16;context[19]=stack-96;
    auto frame=[&](){return budget--&&services.menu_frame(context);};
    auto write=[&](uint32_t a,uint32_t v){return m.write32(a,v);};
    uint32_t flags,buttons,random,selection=0;bool fresh=true,edge=false;
    if(!services.select_music(0)||!write(0x800B6BC4,input.label)||!write(0x800DC274,0)||!advance_menu_palette(m))return false;
    context[3]=0;context[17]=0x800181C8;if(!services.transition({0x3400,0x10808000,0x10600,0,1},context))return false;
    context[17]=0x800181E8;context[16]=0x80015652;if(!services.initialize_scene({14,context})||!services.play_sound(31,0x800)||!services.play_sound(31,0x810))return false;
    const std::array<std::array<uint32_t,2>,18> setup{{
        {0x800DC240,3000},{0x800B9520,0x60000},{0x800B9524,0xB0000},{0x800B9528,0},{0x800B952C,0x00180020},
        {0x800B84B4,0x10808000},{0x800B84BC,0},{0x800B84B0,0x3C00},{0x800B84B8,0x11200},{0x800B84C8,0x11200},
        {0x800B77CC,0x1F0},{0x800B77D0,0},{0x800B77D4,0x00B40040},{0x800B4BD0,0x60000},{0x800B4BD8,0xFFFF},
        {0x800B4BDC,0x00B40044},{0x800B6AA0,input.content},{0x800B6A90,0xFFFFFFFF}}};
    for(auto pair:setup)if(!write(pair[0],pair[1]))return false;
    if(!next_driver_random(m,random)||!write(0x800B6A98,random)||!next_driver_random(m,random)||!m.read32(0x800B6A9C,flags)||!write(0x800B6A9C,flags^(random&0x20)))return false;
    context[0]=0x800B9778;context[1]=0x800B6A94;context[4]=0xFF3F;context[5]=0x806B992C;context[17]=0x80018312;
    uint32_t fade=0xF800;
    do {if(!m.read32(0x806B992C,buttons))return false;if(buttons&0xFF3F)fade=0;
        uint32_t angle;if(!write(0x800B6A94,fade)||!m.read32(0x800B9778,angle)||!write(0x800B9778,angle+0x8000))return false;
        fade-=0x400;context[16]=fade;if(!frame())return false;context[17]=0x80018372;
    }while(int32_t(fade)>0);
    if(!m.read32(0x806B992C,buttons))return false;
    if(buttons&0xFF3F){context[0]=0x806B992C;context[16]=0xFF3F;do{if(!frame()||!m.read32(0x806B992C,buttons))return false;context[17]=0x800183AA;}while(buttons&0xFF3F);}
    uint32_t latch=stack-0x3C;
    if(!write(0x801133B8,0x40)||!write(0x800DC274,0)||!write(0x800B6A94,0)||!write(latch,0)||!services.play_sound(43,0x1000))return false;
    context[1]=0;context[6]=0xA00;context[16]=0x800DC274;context[17]=0x80018400;
    if(!m.read32(0x806B992C,buttons)||!m.read32(0x800DC274,flags))return false;
    while(!(buttons&0x4008)&&!(flags&1)){
        uint32_t idle,angle;if(!m.read32(0x800DC270,idle))return false;
        if(int32_t(idle)>600){if(!write(0x800DC274,1))return false;selection=0;}
        if(!m.read32(0x800B9778,angle)||!write(0x800B9778,angle+0x8000)||!write(0x800B6A90,selection)||!m.read32(0x800DC270,idle)||!write(0x800DC270,idle+1))return false;
        for(unsigned i=0;i<input.choices;++i){uint32_t delta;
            if(!services.vertical_axis(latch,0xA00,delta))return false;
            context[17]=0x8001849A;
            int32_t next=int32_t(selection+delta)%int32_t(input.choices);selection=next<0?input.choices-1:uint32_t(next);
        }
        uint32_t held;if(!m.read32(latch,held))return false;if(held&&!write(0x800DC270,0))return false;
        context[0]=0x800DC270;context[1]=selection;context[4]=0x800B9778;context[5]=0x800DC274;context[16]=input.choices;
        if(!frame()||!m.read32(0x806B992C,buttons)||!m.read32(0x800DC274,flags))return false;context[17]=0x800184EA;
    }
    uint32_t cancel=buttons&8;
    if(!next_driver_random(m,random)||!write(0x800B6A98,random)||!next_driver_random(m,random)||!m.read32(0x800B6A9C,flags)||!write(0x800B6A9C,flags^(random&0x20)))return false;
    context[0]=0x800DC274;context[1]=selection;context[2]=cancel;context[4]=0xFF3F;context[5]=0x806B992C;context[17]=0x80018540;
    fade=0;
    do {
        if(!m.read32(0x806B992C,buttons))return false;
        if(buttons&0xFF3F){
            if(!m.read32(0x800DC274,flags)||!write(0x800DC274,flags|2))return false;
            if(fresh){uint32_t cue=8;if(cancel){if(!next_driver_random(m,random)||!m.read32(0x80113410+(random&3)*4,cue))return false;}
                fresh=false;edge=true;if(!services.play_sound(cue,0x1000))return false;context[17]=0x800185B6;
            }else if(!edge){fade=0x10000;edge=true;}
        }else edge=false;
        if(!m.read32(0x800DC274,flags))return false;if((flags&3)==3)break;
        uint32_t angle;if(!write(0x800B6A94,fade)||!m.read32(0x800B9778,angle)||!write(0x800B9778,angle+0x8000))return false;
        fade+=0x400;context[3]=edge?1:0;context[7]=fresh?1:0;context[16]=fade;if(!frame())return false;context[17]=0x8001861A;
    }while(fade<=0xFFFF);
    if(!write(0x800B4BDC,0x00B40078)||!write(0x800B4BD0,0x40000)||!m.read32(0x800DC274,flags))return false;
    output=cancel||flags==1?0xFFFFFFFF:selection;return true;
}
}
