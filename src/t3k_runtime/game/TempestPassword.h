#pragma once
#include "TempestHighScores.h"
namespace tempest_native {
// Original 8001B0DC..8001B4D4. Twelve-character password editor and the original
// 58-entry lookup, including the final entry's special option toggle.
inline bool run_level_password(CommandMemory& m,const OpeningAnimationInput& input,GameSessionServices& services,uint32_t& output,unsigned budget=0xFFFFFFFF){
    SceneTimingScope cadence(services,SceneTimingKind::tool);if(!cadence.divisions)return false;uint64_t remainingBudget=cadence.budget(budget);
    auto c=input.incoming;uint32_t stack=c[19],base=stack-16;if((stack&15)||stack<112)return false;c[18]=base;c[19]=stack-112;c[17]=input.returnAddress;
    for(unsigned i=0;i<29;++i){uint8_t b;if(!m.read8(0x806A25B8+i,b)||!write_session_byte(m,base-32+i,b))return false;}
    c[2]=0;c[3]=1;c[7]=16;c[5]=3;c[4]=0;c[16]=0;if(!m.write32(base-36,0))return false;
    for(unsigned i=0;i<12;++i)if(!write_session_byte(m,0x8010C1D3+i*3,'<'))return false;
    c[17]=0x8010C1F4;
    if(!m.write32(0x800B6B04,0x8010C1D0)||!m.write32(0x800B6A90,0xFFFFFFFF)||!m.write32(0x800B6A94,65535))return false;
    c[16]=0x800B6A90;c[1]=0x800B6A94;c[0]=0x80015500;uint32_t value,random;
    if(!next_driver_random(m,random)||!m.write32(0x800B6A98,random)||!next_driver_random(m,random)||!m.read32(0x800B6A9C,value)||!m.write32(0x800B6A9C,value^(random&32))||!m.write32(0x800B6AA0,0x8010C020))return false;
    c[16]=0x800B6AA0;c[17]=0x8001B1AA;
    if(!services.transition({0x3E00,0x10808000,0x10400,0,20},c))return false;c[17]=0x8001B1EA;
    if(!m.write32(0x800B850C,1)||!services.initialize_scene({18,c}))return false;c[17]=0x8001B204;
    if(!m.read32(0x800B6A94,c[1]))return false;c[16]=0x800B6A94;c[0]=0x800B92D4;
    auto frame=[&](uint32_t link){if(!remainingBudget--||!services.menu_frame(c))return false;c[17]=link;return true;};
    auto rotate=[&](){uint32_t angle;return m.read32(0x800B92D4,angle)&&m.write32(0x800B92D4,angle+uint32_t(cadence.delta(0x2000,1)));};
    if(int32_t(c[1])>0){do{if(!m.read32(0x800B6A94,value)||!m.write32(0x800B6A94,int32_t(value)>0?value-uint32_t(cadence.delta(0x400,2)):0)||!frame(0x8001B23C)||!rotate()||!m.read32(0x800B6A94,value))return false;}while(int32_t(value)>0);}
    if(!m.write32(0x801133B8,64))return false;
    c[8]=base-36;c[10]=0x806B992C;c[9]=0x4000;
    while(c[3]){
        uint32_t delta,held;if(!services.vertical_axis(base-36,0xA00,delta))return false;c[17]=0x8001B292;c[16]=delta;
        if(!m.read32(base-36,held))return false;
        if(!delta&&!held)c[7]=16;else if(held){c[7]-=unsigned(cadence.step().legacy_boundary());if(int32_t(c[7])<=0){if(!m.write32(base-36,0))return false;c[7]=4;}}
        c[2]-=delta;if(int32_t(c[2])<0)c[2]=27;if(int32_t(c[2])>27)c[2]=0;
        uint32_t characterAddress=base-32+c[2];uint8_t ch;if(!m.read8(characterAddress,ch))return false;
        c[16]=0x8010C1D0+c[5];c[0]=c[16]<<3;c[1]=0U-c[0];c[6]=0xFFFFFFE8;c[17]=characterAddress;
        if(!write_session_byte(m,c[16],ch)||!m.read32(0x806B992C,value))return false;
        if(value&0x4000){
            if(ch=='='){if(!services.play_sound(8,0x1000))return false;c[17]=0x8001B350;
                if(int32_t(c[4])>0){if(!write_session_byte(m,c[16],'<'))return false;--c[4];c[5]-=3;}}
            else if(ch=='>'){c[3]=0;c[2]=0;}
            else{++c[4];c[5]+=3;if(c[4]==12)c[3]=0;if(!services.play_sound(49,0x1000))return false;c[17]=0x8001B39A;}
            c[2]=0;
            do{if(!m.read32(0x806B992C,value))return false;if(!(value&0x4000))break;c[16]=0x4000;c[0]=0x806B992C;if(!frame(0x8001B3BE))return false;}while(true);
        }
        if(!frame(0x8001B3DA)||!rotate())return false;
    }
    // Compare the entered bytes at stride three, preserving prefix termination.
    uint32_t index=0;bool searching=true;
    while(searching&&index<=57){uint32_t name;if(!m.read32(0x801136EC+index*4,name))return false;
        for(unsigned i=0;i<=12;++i){uint8_t expected,actual;if(!m.read8(name+i,expected))return false;
            if(!expected){searching=false;break;}if(!m.read8(0x8010C1D3+i*3,actual))return false;if(expected!=actual)break;}
        if(searching)++index;
    }
    uint32_t doubled=index<=57?index*2:0xFFFFFFFE;output=doubled+16;
    if(index==57){uint32_t flags;if(!m.read32(0x801133A8,flags)||!m.write32(0x801133A8,flags^32)||!services.play_sound(23,0x1000))return false;output=0xFFFFFFFF;}
    return true;
}
}
