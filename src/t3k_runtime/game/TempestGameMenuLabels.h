#pragma once
#include "TempestCommands.h"
#include <charconv>
namespace tempest_native {
// Original 8001A5F4..8001A7A2. Private decimal-format buffer is native storage;
// public menu bytes retain the original padding and password termination.
inline bool prepare_game_menu_labels(CommandMemory& m,uint32_t maximumLevel){
    if(maximumLevel>127)return false;
    const uint8_t format[]{'%', 'u', 0};for(unsigned i=0;i<3;++i){uint8_t byte;if(!m.read8(0x806A25B4+i,byte)||byte!=format[i])return false;}
    auto store=[&](uint32_t address,uint8_t byte){uint32_t word;unsigned shift=24-8*(address&3);return m.read32(address,word)&&m.write32(address,(word&~(255U<<shift))|(uint32_t(byte)<<shift));};
    char number[11];auto result=std::to_chars(number,number+sizeof(number),maximumLevel+1);if(result.ec!=std::errc())return false;
    // The original formatter exposes its reversed digit scratch in shared RAM.
    // Preserve those bytes too, rather than treating it as a private C local.
    for(unsigned i=0;i<unsigned(result.ptr-number);++i)if(!store(0x8003C788+i,uint8_t(result.ptr[-1-int(i)])))return false;
    for(unsigned i=0;i<3;++i)if(!store(0x8010BDB3+i,' '))return false;
    for(unsigned i=0;i<unsigned(result.ptr-number);++i)if(!store(0x8010BDB3+i,uint8_t(number[i])))return false;
    uint32_t password=0x8010BDE5;
    if(maximumLevel>=16&&!m.read32(0x801136EC+((maximumLevel-16)>>1)*4,password))return false;
    for(unsigned i=0;i<128;++i){uint8_t byte;if(!m.read8(password+i,byte))return false;
        if(!store(0x8010BDD7+i,byte))return false;if(!byte)return true;}
    return false;
}
}
