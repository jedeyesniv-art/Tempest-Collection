#pragma once
#include "TempestSession.h"
namespace tempest_native {
// Complete original 80016EC0..800175B2: original scene-12 warp minigame,
// event scoring, steering/fire controls, success/failure presentation and result.
inline bool run_session_warp_skip(CommandMemory& m,const OpeningAnimationInput& input,GameSessionServices& services,uint32_t& result,unsigned budget=65536){
    SceneTimingScope cadence(services,SceneTimingKind::warpGame);if(!cadence.divisions)return false;uint64_t remainingBudget=cadence.budget(budget);
    uint32_t stack=input.incoming[19];if((stack&15)||stack<80)return false;auto c=input.incoming;c[18]=stack-16;c[19]=stack-80;c[17]=input.returnAddress;
    c[8]=c[11]=c[10]=0;c[6]=1;
    auto write=[&](uint32_t a,uint32_t v){return m.write32(a,v);};
    SimulationStep lastStep{};bool wasFire=false;auto frame=[&](bool gameplay,uint32_t link){lastStep=cadence.step();if(!remainingBudget--)return false;if(!(gameplay?services.gameplay_frame(c):services.menu_frame(c)))return false;c[17]=link;return true;};
    auto chord=[&](uint32_t cue){for(uint32_t pitch:{0x1000U,0x1010U,0x1020U,0x1030U})if(!services.play_sound(cue,pitch))return false;return true;};
    if(!services.select_music(18))return false;c[17]=0x80016EFA;c[2]=0;
    SoundRequest sound{};uint32_t handle;if(!m.read32(0x80115C88,sound.resource)||!m.read32(0x80115C8C,sound.resourceWord))return false;sound.argument1=0x1000;sound.argument3=0x5F000000;
    if(!services.play_request(sound,handle)||!write(0x801133C0,1)||!m.read32(0x8010F890,c[16]))return false;c[17]=0x80016F0A;
    uint32_t value,other,first,last,group=uint32_t(int32_t(c[16])>>4),index=c[16]&15;
    if(!m.read32(0x800B2AEC+(c[16]&7)*16,value)||!write(0x800BA674,value)||!m.read32(0x8010F890+(index+2)*4,last)||!m.read32(0x8010F890+(index+1)*4,first))return false;
    c[0]=uint32_t(int32_t(last-first)>>2);if(int32_t(c[0])>65536)return false;
    for(c[2]=0;int32_t(c[2])<int32_t(c[0]);++c[2])if(!m.read32(first+c[2]*4,value)||!write(0x8010F570+c[2]*4,value))return false;
    const std::array<std::array<uint32_t,2>,6> setup{{{0x800B4D60,0xFFFFFFFF},{0x800B4D74,0xFFFC0000},{0x800B4D6C,(group<<11)+0x1000},{0x800B4D68,0},{0x800B4D7C,0},{0x800B4C58,0}}};
    for(auto pair:setup)if(!write(pair[0],pair[1]))return false;for(unsigned i=8;i<16;++i)if(!write(0x800B4D60+i*4,0))return false;
    if(!m.read32(0x800DC240,c[15])||!write(0x800DC240,0))return false;c[0]=c[2]=c[1]=c[9]=c[4]=c[3]=0;c[5]=group<<6;c[16]=16;c[17]=0x800B4D60;
    if(!services.transition({0x3C00,0x10808000,0x11600,0,10},c))return false;c[17]=0x80017014;
    if(!services.initialize_scene({12,c}))return false;c[17]=0x80017022;
    if(int32_t(c[6])>0){c[13]=0x800B4D60;c[12]=0x800B4D74;c[14]=0x800B4D6C;c[7]=0x800B4D70;
        do{
            if(!m.read32(0x800B4830,value)||!write(0x800B4830,value-uint32_t(cadence.delta(0x60000,0)))||!m.read32(0x800B4D74,value)||!m.read32(0x800B4D6C,other)||!write(0x800B4D74,value+uint32_t(cadence.delta(int32_t(other),1))))return false;
            uint32_t target=c[9]-c[1];c[11]+=uint32_t(cadence.delta(int32_t(c[5]),2));if(int32_t(c[4])<int32_t(target)){c[4]+=uint32_t(cadence.delta(4,3));}if(int32_t(c[4])>int32_t(target))c[4]-=uint32_t(cadence.delta(4,4));
            if(!write(0x800B4D68,c[11]+(c[4]<<4))||!m.read32(0x800B4D70,value))return false;
            uint32_t event=value&255;
            if(event){c[9]=c[1];c[16]=event;c[3]+=0x3000;
                if(event==2){if(!m.read32(0x800B4D6C,value)||!write(0x800B4D6C,int32_t(value)>0xEFFF?0xF000:value+uint32_t(cadence.delta(0x200,9))))return false;}
                else if(event==3){if(!m.read32(0x800B4D6C,value)||!write(0x800B4D6C,int32_t(value)>0x1000?value-0x200:0x1000))return false;}
                else if(event==4)c[5]-=0x40;
                else if(event==5)c[5]+=0x40;
                else if(event==0xFE)c[6]=0xFFFFFFFF;
                else if(event==0xFF){c[6]=0;c[8]=2;}
                if(int32_t(c[6])>0){if(!m.read32(0x800B4D74,value))return false;
                    if(int32_t(value)>0x40000){if(!m.read32(0x800B4D6C,value)||!services.play_sound(8,0x1000+uint32_t(int32_t(value-0x1000)>>3)))return false;c[17]=0x8001718C;}}
                if(!m.read32(0x800B4D70,value))return false;uint32_t intensity=(value>>16)&255;if(intensity>127)intensity=127;c[17]=intensity;c[0]=uint32_t(int32_t((127-intensity)*(value>>24))>>5);
                if(!m.read32(0x800DC240,other)||!write(0x800DC240,other+c[0])||!write(0x800B4BD8,0x1FFFF)||!m.read32(0x800B4D70,value)||!write(0x800B4D70,value&0x8000))return false;
            }
            if(!write(0x800B84B0,0x3400+c[3]))return false;c[3]=int32_t(c[3])>0?c[3]-uint32_t(cadence.delta(0x800,5)):0;
            if(!m.read32(0x800B4BD8,value)||!write(0x800B4BD8,int32_t(value)>0x3FF?value-uint32_t(cadence.delta(0x2000,6)):0x3FF)||!m.read32(0x806B9930,value))return false;
            c[1]+=uint32_t(cadence.delta(int32_t(int8_t(value>>8)),7));if(int32_t(c[1])<-0xF00)c[1]=uint32_t(-0xF00);if(int32_t(c[1])>0xF00)c[1]=0xF00;c[2]-=uint32_t(cadence.delta(int32_t(c[1]),8)+cadence.euler_correction(int32_t(int8_t(value>>8))));
            if(int32_t(c[2])<-0x18000){c[2]=uint32_t(-0x18000);c[1]=0;}if(int32_t(c[2])>0x18000){c[2]=0x18000;c[1]=0;}
            if(!m.read32(0x800B4D70,value))return false;if(value&0x8000){if(!services.play_sound(54,0x1000))return false;c[17]=0x800172C0;}
            if(!m.read32(0x806B992C,value))return false;
            bool fire=bool(value&0x4000),pressed=fire&&!wasFire;wasFire=fire;
            if(fire&&((cadence.step().legacy_boundary()&&!(c[10]&3))||(cadence.divisions>1&&pressed))){c[1]=uint32_t(int32_t(c[1])>>1);c[16]=0;c[0]=((c[2]<<8)&0xFFFF0000)|1;
                do{uint32_t slot=0x8010F550+c[16]*4;if(!m.read32(slot,value))return false;++c[16];
                    if(!value){if(!write(slot,c[0]))return false;c[16]=8;if(!services.play_sound(53,0x1000))return false;c[17]=0x8001732A;++c[16];}
                }while(c[16]<=3);
            }
            if(!write(0x800B4D7C,uint32_t(int32_t(c[2])>>8)))return false;c[10]+=unsigned(cadence.step().legacy_boundary());if(!frame(true,0x80017348)||!write(0x800B4D60,1))return false;
        }while(int32_t(c[6])>0);
    }
    if(c[6]){c[3]=0;if(!chord(48))return false;c[17]=0x8001736A;
        if(!write(0x800B4D60,0xFFFFFFFF)||!write(0x800B84B8,0x10400)||!write(0x800B84C8,0x10400)||!m.read32(0x800B4C58,value))return false;c[17]=0x10400;
        if(int32_t(value)<=65535){c[2]=0x800B84B0;c[1]=0x3400+c[3];c[0]=0x800B4C58;c[16]=0x800B4BD8;
            do{if(!frame(false,0x800173D0)||!write(0x800B84B0,c[1])||!m.read32(0x800B4C58,value)||!write(0x800B4C58,value+uint32_t(cadence.delta(0x200,9)))||!m.read32(0x800B4BD8,other)||!write(0x800B4BD8,int32_t(other)>0xFFFE?0xFFFF:other+uint32_t(cadence.delta(0x800,10)))||!m.read32(0x800B4C58,value))return false;}while(int32_t(value)<=65535);
        }
    }else{
        if(!write(0x800B4D60,0xFFFFFFFF)||!write(0x800B84B8,0x10C00)||!write(0x800B84C8,0x10C00)||!write(0x800B4BD8,0x40FFF)||!chord(9))return false;c[17]=0x8001745A;c[0]=0;
        if(!services.initialize_scene({13,c}))return false;c[17]=0x80017468;c[6]=0x800B84B0;c[4]=0x800B4D74;c[5]=0x800B4D6C;c[1]=0x800DC240;c[2]=0x80015652;
        do{if(!frame(false,0x80017490)||!write(0x800B84B0,0x3F00+c[3]))return false;c[3]=int32_t(c[3])>0?c[3]-uint32_t(cadence.delta(0x800,5)):0;
            if(!m.read32(0x800B4D6C,value)||!m.read32(0x800B4D74,other)||!write(0x800B4D74,other+uint32_t(cadence.delta(int32_t(value<<3),11)))||!m.read32(0x800B4BD8,value))return false;
            if(int32_t(value)>0x7FFF){if(!write(0x800B4BD8,value-uint32_t(cadence.delta(0x2000,6))))return false;}
            else{if(!write(0x800B4BD8,0x40FFF)||!m.read32(0x800DC240,value)||!write(0x800DC240,value+value))return false;c[3]=0x8000;++c[0];c[16]=c[0]<<8;
                if(!services.play_sound(9,0x1000+c[16])||!services.play_sound(9,0x1010+c[16]))return false;c[17]=0x80017520;}
        }while(c[0]<=4);
    }
    if(!m.read32(0x800DC240,value)||!write(0x800DC240,value+c[15])||!services.transition({0x3C00,0x10808000,0x11600,0,10},c)||!m.read32(0x8010F890,value)||!write(0x8010F890,value+1)||!write(0x80113448,1)||!m.read32(0x801133C4,value)||!write(0x801133C0,value))return false;
    result=c[8];return true;
}
}
