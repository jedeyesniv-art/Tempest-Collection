#pragma once
#include "TempestSession.h"
namespace tempest_native {
inline bool prepare_rotary_direction_label(CommandMemory& m){uint32_t invert;if(!m.read32(0x801133C0,invert))return false;return write_session_byte(m,0x8010BD2E,invert?'N':'F')&&write_session_byte(m,0x8010BD2F,invert?' ':'F');}
inline bool prepare_rotary_sensitivity_label(CommandMemory& m){uint32_t sensitivity;if(!m.read32(0x801133BC,sensitivity)||sensitivity>15)return false;
    for(unsigned i=0;i<15;++i)if(!write_session_byte(m,0x8010BCD0+i*3,i<sensitivity?'+':' '))return false;return true;}
// Original 80019D30..80019FC0, including native sensitivity and inversion UI.
inline bool run_rotary_calibration(CommandMemory& m,uint32_t player,const OpeningAnimationInput& input,GameSessionServices& services,unsigned budget=0xFFFFFFFF){
    SceneTimingScope cadence(services,SceneTimingKind::tool);if(!cadence.divisions)return false;uint64_t remainingBudget=cadence.budget(budget);
    if(player>1)return false;auto c=input.incoming;uint32_t stack=c[19];c[18]=stack-16;c[19]=stack-48;c[17]=input.returnAddress;
    c[0]=c[1]=0;uint32_t previous,level,value,buttons;
    if(!m.read32(0x800AF700,c[16])||!m.read32(0x801133F0,c[5])||!m.write32(0x801133F0,player)||!m.write32(0x800BA2EC,0x1000000))return false;
    level=c[16];previous=c[5];for(unsigned i=0;i<9;++i)if(!m.write32(0x800DC100+i*4,0))return false;c[17]=0;c[2]=0;
    if(!prepare_rotary_sensitivity_label(m))return false;c[17]=0x80019D96;
    if(!load_level_definition(m,level,0,services))return false;c[17]=0x80019DA4;
    uint32_t height;if(!read_level_halfword(m,level+40,height)||!m.write32(0x800B907C,uint32_t(int32_t(int16_t(height)))<<16)||!m.write32(0x800B6A94,0)||!m.write32(0x800B6BC4,0x8010BCA0)||!m.write32(0x800B6BB4,0x0010001C))return false;
    c[17]=0x800B6BC4;if(!services.initialize_scene({19,c}))return false;c[17]=0x80019DF6;
    if(!m.write32(0x800B7614,0x100)||!m.write32(0x800B84B0,0)||!m.read32(0x800B1750,value)||!m.write32(0x800B7610,value)||!m.read32(0x806B992C,buttons))return false;c[17]=0x806B992C;
    if(!(buttons&8)){c[3]=0x801133C0;c[4]=0x800B761C;
        do{
            if(!remainingBudget--)return false;
            if(buttons&0x4000){if(!c[0]){c[16]=0x801133BC;if(!m.read32(0x801133BC,value)||!m.write32(0x801133BC,value+1)||!services.play_sound(10,0x1000))return false;c[17]=0x80019E8E;c[0]=1;
                    if(!m.read32(0x801133BC,value))return false;if(value>15&&!m.write32(0x801133BC,1))return false;if(!prepare_rotary_sensitivity_label(m))return false;c[17]=0x80019EAA;}}
            else c[0]=0;
            if(!m.read32(0x806B992C,buttons))return false;
            if(buttons&0x1000){if(!c[1]){if(!m.read32(0x801133C0,value)||!m.write32(0x801133C0,value^1)||!m.write32(0x801133C4,value^1)||!services.play_sound(10,0x1000)||!prepare_rotary_direction_label(m))return false;c[1]=1;c[17]=0x80019F0E;}}
            else c[1]=0;
            if(!m.read32(0x800DC100,value))return false;value=uint32_t(int32_t(value)>>16);
            if(value!=c[2]){c[2]=value;if(!services.play_sound(7,0x1000))return false;c[17]=0x80019F3A;}
            if(!m.read32(0x801133B8,value)||!m.write32(0x800B761C,0U-(value<<6))||!m.read32(0x800B9778,value)||!m.write32(0x800B9778,value+uint32_t(cadence.delta(0x10000,1)))||!services.gameplay_frame(c)||!m.read32(0x806B992C,buttons))return false;c[17]=0x806B992C;
        }while(!(buttons&8));
    }
    return m.write32(0x800B6BB4,0x0020001C)&&m.write32(0x801133F0,previous);
}
}
