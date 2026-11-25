#pragma once
#include "TempestCommands.h"
#include <initializer_list>
namespace tempest_native {
// Complete 8001F70A..8001FBE0. Ordered scene-object updates from the expanded
// preset. All arithmetic wraps at 32 bits, including products before shifts.
inline bool apply_menu_parameters(CommandMemory& m,uint32_t source,uint32_t mask){
    auto asr=[](uint32_t v,unsigned n){return uint32_t(int32_t(v)>>n);};
    uint32_t v,w;
    auto load=[&](uint32_t offset){return m.read32(source+offset,v);};
    auto store=[&](uint32_t address,uint32_t value){return m.write32(address,value);};
    if(mask&2){
        if(!load(0x40)||!store(0x800B84BC,v)||!load(0x48)||!store(0x800B84B0,v)||
           !load(0x60)||!store(0x800B84B8,asr(v*0xF80,8)+0x8000)||
           !load(0x64)||!store(0x800B84C8,asr(v*0xF80,8)+0x8000))return false;
    }
    if(mask&0x100){
        for(unsigned i=0;i<6;++i)if(!load(i*4)||!store(0x800B993C+i*16,v<<14))return false;
        if(!load(0x28))return false;uint32_t width=(v*360)>>16;if(!width)width=1;
        if(!load(0x2C))return false;uint32_t height=(v*240)>>16;
        if(!load(0x20))return false;uint32_t x=asr(360-width,1)+asr(v*360,16)-180;
        if(!load(0x24))return false;uint32_t y=asr(240-height,1)+asr(v*240,16)-120;
        if(!store(0x800B7B50,(width<<16)|height)||!store(0x800B7B4C,(x<<16)|y)||!load(0x4C)||!store(0x800B7B58,v<<16))return false;
    }
    if(mask&0x200){
        constexpr uint32_t b=0x800B9760;
        if(!load(0x84)||!m.read32(b+0x18,w)||!store(b+0x18,w+((v-0x7FFF)<<2))||
           !load(0x88)||!store(b+0x20,v<<1)||!load(0x90)||!store(b+0x1C,(v<<8)+0x1000)||
           !load(0x94)||!store(b+0x24,((v>>8)&255)+1))return false;
        for(uint32_t o:{0x14U,0x30U})if(!m.read32(b+o,w)||!load(o==0x14?0x98:0x9C)||!store(b+o,(w&65535)|(v<<16)))return false;
        if(!load(0xA0)||!store(b+0x28,(v-0x7FFF)<<12)||!load(0xA4)||!store(b+0x2C,asr(v,8))||
           !load(0xCC)||!store(b+0x38,v)||!load(0xAC)||!store(b+0x34,v<<1)||
           !load(0xC4)||!m.read32(b+0x3C,w)||!store(b+0x3C,w+asr((v&65535)-0x7FFF,4)))return false;
    }
    if(mask&0x400){
        constexpr uint32_t b=0x800B92D0;
        if(!load(0x84)||!m.read32(b+4,w)||!store(b+4,w+((v-0x7FFF)<<2))||
           !load(0x90)||!store(b+0x1C,(v<<8)+0x1000)||!load(0x94)||!store(b+0xC,((v>>8)&255)+1))return false;
        for(uint32_t o:{0x14U,0x30U})if(!m.read32(b+o,w)||!load(o==0x14?0x98:0x9C)||!store(b+o,(w&65535)|(v<<16)))return false;
        if(!load(0xA4)||!store(b+0x10,asr(v,8)+1)||!load(0xCC)||!store(b+0x20,v)||
           !load(0x88)||!store(b+0x24,asr(v,4))||!load(0xC4)||!m.read32(b+0x28,w)||!store(b+0x28,w+asr((v&65535)-0x7FFF,2)))return false;
    }
    if(mask&0x800){
        if(!load(0x18))return false;w=(asr(v,7)-0x100)<<16;
        if(!load(0x1C)||!store(0x800B6990,w|((asr(v,8)-0x80)&65535))||!load(0x30)||!store(0x800B6A94,v&65535))return false;
    }
    if(mask&0x1000){
        if(!load(0x84)||!m.read32(0x800B4830,w)||!store(0x800B4830,w-((v-0x7FFF)<<6))||
           !load(0xC4)||!m.read32(0x800B4848,w)||!store(0x800B4848,w+asr((v&65535)-0x7FFF,2)))return false;
    }
    return true;
}
}
