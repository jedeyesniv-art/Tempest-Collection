#pragma once
#include "TempestSession.h"
namespace tempest_native {
// Complete original 80016CC2..80016EBE, including both scene-11 phases and
// the original low-five-status-bit bonus count. The caller's r13 is data.
inline bool run_session_warp_bonus(CommandMemory& m,const std::array<uint32_t,20>& incoming,GameSessionServices& services,uint32_t& result,unsigned budget=256){
    uint32_t stack=incoming[19];if((stack&15)||stack<48)return false;auto c=incoming;c[18]=stack-16;c[19]=stack-48;
    auto frame=[&](){c[17]=0x80015C74;return budget--&&services.menu_frame(c);};
    c[17]=0x80016BE6;if(!services.transition({0x3C00,0x10808000,0x11600,0,10},c))return false;
    c[16]=0x800B5870;if(!services.play_sound(34,0x1000)||!m.write32(0x800BA538,0xFFFFFFFF)||!m.write32(0x800B7614,(c[1]<<16)|c[1]))return false;
    uint32_t random,word;if(!next_driver_random(m,random)||!m.read32(0x8010F2BC+(random&3)*4,word)||!m.write32(0x800B5890,word))return false;
    c[17]=0x80010200;if(!services.initialize_scene({11,c})||!m.write32(0x800B5894,0x04000400))return false;
    c[16]=0x10000;c[1]=0x800B84B8;c[0]=0x800B84C8;
    do{if(!m.write32(0x800B84B8,c[16])||!m.write32(0x800B84C8,c[16])||!frame())return false;c[16]+=0x800;}while(c[16]<=0x1FFFF);
    if(!m.write32(0x800BA538,0x00320001)||!m.read32(0x800DC244,word))return false;c[16]=uint32_t(int32_t(word)>>24);c[0]=0;
    for(c[1]=0;c[1]<5;++c[1])if(!(c[16]&(1U<<c[1])))++c[0];c[5]=c[0]?1U<<(c[0]-1):0;
    c[1]=0x100;c[16]=0x800B7618;
    if(!m.read32(0x800B1758+c[0]*4,word)||!m.write32(0x800B7610,word)||!m.write32(0x800B7618,15))return false;
    c[17]=0x80010200;if(!services.initialize_scene({11,c})||!m.read32(0x800B7618,c[0]))return false;
    c[16]=0x200;c[4]=0x800B7618;c[3]=0x800B7614;c[2]=0x800B5894;--c[0];
    do{if(int32_t(c[0])<=1)c[0]=2;c[1]-=10;if(int32_t(c[1])<=7)c[1]=8;
        if(!m.write32(0x800B7618,c[0])||!m.write32(0x800B7614,(c[1]<<16)|c[1])||!m.write32(0x800B5894,((c[16]+0x200)|(c[16]<<17)))||!frame())return false;
        c[16]+=0x80;--c[0];
    }while(c[16]<=0xFFF);++c[0];
    c[17]=0x80016BE6;if(!services.transition({0x3C00,0x10808000,0x11600,0,10},c))return false;result=c[5];return true;
}
}
