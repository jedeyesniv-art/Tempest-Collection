#pragma once
#include "TempestSelectionMenu.h"
#include "TempestMenuPresets.h"
#include "TempestMenuParameters.h"
namespace tempest_native {
struct AnimatedMenuServices:SelectionMenuServices,MenuPresetServices {
    virtual bool initialize_scene(const SceneInitializationInput&)override=0;
};
// Original 8001867E..80018F20. Caller-owned expanded presets and selector/latch
// words retain their original memory ownership; SDK register spills are private.
inline bool run_animated_menu(CommandMemory& m,const SelectionMenuInput& input,AnimatedMenuServices& services,uint32_t& output,unsigned budget=4096){
    SceneTimingScope cadence(services,SceneTimingKind::animatedMenu);if(!cadence.divisions)return false;uint64_t remainingBudget=uint64_t(budget)*cadence.divisions;
    uint32_t stack=input.caller.incoming[19];if(!input.choices||input.choices>32||(stack&15)||stack<0x390)return false;
    auto c=input.caller.incoming;c[1]=input.content;c[5]=input.choices;c[16]=input.label;c[11]=1;c[9]=0;c[8]=0;c[17]=input.caller.returnAddress;c[18]=stack-16;c[19]=stack-0x390;
    auto write=[&](uint32_t a,uint32_t v){return m.write32(a,v);};
    auto frame=[&](uint32_t link){if(!remainingBudget)return false;--remainingBudget;if(!services.menu_frame(c))return false;c[17]=link;return true;};
    uint32_t editing,flags,buttons,random;
    if(!m.read32(0x801133D0,editing))return false;
    if(int32_t(editing)<0){if(!advance_menu_palette(m))return false;c[17]=0x800186C4;}
    if(!write(0x800B6BC4,input.label)||!write(0x800DC274,0)||!m.read32(0x801133D0,editing))return false;
    if(int32_t(editing)<0){c[8]=0;if(!services.transition({0x3400,0x10808000,0x10600,0,1},c))return false;c[17]=0x80018708;}
    const uint32_t current=stack-0x238,target=stack-0x338,expanded=stack-0x138;
    const uint32_t selector=stack-0x33C,other=stack-0x340,maskAt=stack-0x344,oldMask=stack-0x348,remainingAt=stack-0x34C,latch=stack-0x350;
    if(!write(0x800B850C,1)||!write(remainingAt,0)||!write(0x800B6AA0,input.content)||!write(selector,20)||!write(other,21))return false;c[0]=0x800B6A90;
    auto animate=[&](uint32_t stepLink,uint32_t interpolationLink,uint32_t applyLink){
        MenuPresetStepInput request{{selector,other,maskAt,oldMask,remainingAt,0x800,current,target,expanded},c};
        if(services.scene_timing_step().interval_start()&&!step_menu_preset(m,services,request))return false;c[17]=stepLink;uint32_t remaining;
        if(!m.read32(remainingAt,remaining))return false;
        if(remaining){uint32_t next;if(!interpolate_menu_parameters(m,remaining,uint32_t(services.scene_timing_step().delta(0x200)),current,target,expanded,next)||!write(remainingAt,next))return false;c[17]=interpolationLink;}
        uint32_t mask;if(!m.read32(maskAt,mask)||!apply_menu_parameters(m,expanded,mask,services.scene_timing_step()))return false;c[17]=applyLink;return true;
    };
    auto randomize=[&](uint32_t firstLink,uint32_t secondLink){
        if(!next_driver_random(m,random)||!write(0x800B6A98,random))return false;c[17]=firstLink;
        if(!next_driver_random(m,random)||!m.read32(0x800B6A9C,flags)||!write(0x800B6A9C,flags^(random&0x20)))return false;c[17]=secondLink;return true;
    };
    if(!m.read32(0x801133D0,editing))return false;
    if(int32_t(editing)<0){uint32_t source,mask;c[16]=0x80015500;
        if(!m.read32(0x8010DCB0+80,source)||!unpack_menu_parameters(m,expanded,source,mask)||!write(maskAt,mask))return false;c[17]=0x80018792;
        if(!initialize_menu_layers(m,services,mask,c)||!write(0x800B6A90,0xFFFFFFFF))return false;
        if(!randomize(0x800187B2,0x800187BC))return false;
        c[16]=0xF800;c[6]=0x806B992C;c[4]=0xFF3F;c[3]=remainingAt;
        do{
            if(!write(0x800B6A94,c[16])||!m.read32(0x806B992C,buttons))return false;
            c[0]=current;c[1]=target;c[2]=expanded;if(buttons&0xFF3F)c[16]=0;
            if(!animate(0x8001885E,0x8001887E,0x800188A6))return false;c[16]-=uint32_t(services.scene_timing_step().delta(0x400));
            if(!frame(0x800188B0))return false;
        }while(int32_t(c[16])>0);
    }
    if(!m.read32(0x806B992C,buttons))return false;
    if(buttons&0xFF3F){c[0]=0x806B992C;c[16]=0xFF3F;do{if(!frame(0x800188F6)||!m.read32(0x806B992C,buttons))return false;}while(buttons&0xFF3F);}
    if(!write(0x801133B8,0x40)||!write(0x800DC274,0)||!write(0x800B6A94,0)||!m.read32(0x801133D0,editing))return false;
    c[3]=int32_t(editing)<0?0:editing;
    if(!write(0x801133D0,int32_t(editing)<0?0xFFFFFFFF:editing)||!write(latch,0))return false;c[12]=0xA00;
    if(!services.play_sound(1,0x1000))return false;c[17]=0x80018968;
    auto exit_mask=[&](){uint32_t editable;if(c[3]>31||!m.read32(0x801133CC,editable))return false;c[2]=(editable&(1U<<c[3]))?8:0x4008;return true;};
    auto analog=[&](bool& present){uint32_t port,device,table,capabilities;
        if(!m.read32(0x801133F0,port)||port>1||!m.read32(0x801133E8+port*4,device)||device>8||!m.read32(0x806B9910,table))return false;
        c[17]=table+device*16;if(!m.read32(c[17],capabilities))return false;present=bool(capabilities&16);
        if(present){uint8_t axis;if(!m.read8(c[17]+6,axis)||!m.read32(0x806B992C,buttons))return false;
            if(int8_t(axis)>40){buttons|=0x100;if(!write(0x806B992C,buttons))return false;}
            // The controller and axis are read again in the original routine.
            if(!m.read32(0x801133F0,port)||!m.read32(0x801133E8+port*4,device)||!m.read32(0x806B9910,table)||!m.read8(table+device*16+6,axis))return false;
            if(int8_t(axis)<-40){if(!m.read32(0x806B992C,buttons)||!write(0x806B992C,buttons|0x400))return false;}}
        return true;
    };
    if(!exit_mask()||!m.read32(0x806B992C,buttons)||!m.read32(0x800DC274,flags))return false;
    if(!(buttons&c[2])&&!(flags&1)){
        c[0]=0x80015500;c[4]=0x806B992C;c[7]=0x800DC270;c[10]=0x800DC274;c[6]=remainingAt;
        do{uint32_t idle;if(!m.read32(0x800DC270,idle))return false;
            if(services.scene_timing_step().legacy_boundary()&&int32_t(idle)>600){if(!write(0x800DC274,1))return false;c[3]=0;}
            if(!write(0x800B6A90,c[3])||!m.read32(0x800DC270,idle)||!write(0x800DC270,idle+unsigned(services.scene_timing_step().legacy_boundary())))return false;
            c[16]=current;c[0]=target;c[1]=expanded;
            if(!animate(0x80018A56,0x80018A78,0x80018AA0))return false;c[16]=0;c[0]=0x801133CC;
            for(unsigned i=0;i<input.choices;++i){uint32_t delta;if(!services.vertical_axis(latch,0xA00,delta))return false;
                c[17]=0x80018ACA;int32_t next=int32_t(c[3]+delta)%int32_t(input.choices);c[3]=next<0?input.choices-1:uint32_t(next);if(!exit_mask())return false;++c[16];}
            uint32_t editable;if(!m.read32(0x801133CC,editable))return false;
            if(editable&(1U<<c[3])){bool present;if(!analog(present))return false;c[16]=0x806B992C;
                if(!m.read32(0x806B992C,buttons))return false;
                if(buttons&0x500){if(!write(0x801133D0,c[3])||!services.play_sound(8,0x1000))return false;c[17]=0x80018C00;
                    if(!m.read32(0x806B992C,buttons))return false;
                    if(buttons&0xFF3F){c[0]=0x806B9910;c[1]=0x801133F0;
                        do{if(!frame(0x80018C2A))return false;uint32_t mask;if(!m.read32(maskAt,mask)||!apply_menu_parameters(m,expanded,mask,services.scene_timing_step()))return false;c[17]=0x80018C4A;
                            if(!analog(present)||!m.read32(0x806B992C,buttons))return false;
                        }while(buttons&0xFF3F);}
                    if(!m.read32(0x800DC274,flags))return false;output=c[9]||flags==1?0xFFFFFFFF:c[3];return true;
                }
            }
            uint32_t held;if(!m.read32(latch,held)||(held&&!write(0x800DC270,0))||!frame(0x80018D20)||!m.read32(0x806B992C,buttons)||!m.read32(0x800DC274,flags))return false;
            c[0]=0x80015500;
        }while(!(buttons&c[2])&&!(flags&1));
    }
    c[0]=0x80015500;c[16]=0x800B6A90;c[5]=0x800DC274;
    if(!randomize(0x80018D50,0x80018D5A)||!write(0x801133D0,0xFFFFFFFF))return false;c[16]=0;c[4]=remainingAt;
    do{
        if(!m.read32(0x806B992C,buttons))return false;
        if(buttons&0xFF3F){c[9]=buttons&8;if(!m.read32(0x800DC274,flags)||!write(0x800DC274,flags|2))return false;
            if(c[11]){uint32_t cue=8;if(c[9]){if(!next_driver_random(m,random)||!m.read32(0x80113410+(random&3)*4,cue))return false;}
                c[11]=0;c[8]=1;if(!services.play_sound(cue,0x1000))return false;c[17]=0x80018DE2;
            }else if(!c[8]){c[16]=0x10000;c[8]=1;}
        }else c[8]=0;
        if(!m.read32(0x800DC274,flags))return false;if((flags&3)==3)break;
        if(!write(0x800B6A94,c[16]))return false;c[0]=current;c[1]=target;c[2]=expanded;
        if(!animate(0x80018E6E,0x80018E8E,0x80018EB8))return false;c[16]+=uint32_t(services.scene_timing_step().delta(0x400));
        if(!frame(0x80018EC4))return false;
    }while(int32_t(c[16])<=0xFFFF);
    if(!m.read32(0x800DC274,flags))return false;output=c[9]||flags==1?0xFFFFFFFF:c[3];return true;
}
}
