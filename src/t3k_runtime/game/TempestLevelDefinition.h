#pragma once
#include "TempestCommands.h"
namespace tempest_native {
// The NUON halfword stores read and replace an aligned word. Do not substitute
// host-endian structs: the source and destination are permitted to overlap.
inline bool read_level_halfword(CommandMemory& m,uint32_t address,uint32_t& value){
    uint32_t word;if(!m.read32(address,word))return false;value=(word>>((address&2)?0:16))&65535;return true;
}
inline bool write_level_halfword(CommandMemory& m,uint32_t address,uint32_t value){
    uint32_t word;if(!m.read32(address,word))return false;unsigned shift=(address&2)?0:16;
    return m.write32(address,(word&~(65535U<<shift))|((value&65535)<<shift));
}
struct LevelDefinitionServices {
    virtual bool generate_level(uint32_t destination)=0;
    virtual ~LevelDefinitionServices()=default;
};
// Complete 8001924A..800194A0 including its required generated-level dependency.
// The generated packed definition is never replaced by a stock/captured level.
inline bool load_level_definition(CommandMemory& m,uint32_t destination,uint32_t level,LevelDefinitionServices& services){
    if(destination&1)return false;uint32_t maximum,flags;
    if(!m.read32(0x801133B0,maximum))return false;
    if(int32_t(level)>int32_t(maximum)){
        if(!m.read32(0x801133A8,flags))return false;
        if(flags&4){if(!services.generate_level(0x800B11F8))return false;level=128;}
    }
    uint32_t offset=level*13,word;
    auto source=[&](uint32_t index){return m.read32(0x800AF7F8+(offset+index)*4,word);};
    for(unsigned i=0;i<2;++i)if(!source(i)||!write_level_halfword(m,destination+i*4,word>>16)||!write_level_halfword(m,destination+i*4+2,word))return false;
    for(unsigned i=0;i<2;++i){if(!source(i+2))return false;uint32_t packed=word;
        for(int n=7;n>=0;--n){if(!write_level_halfword(m,destination+8+(i*8+n)*2,packed<<12))return false;packed=uint32_t(int32_t(packed)>>4);}}
    if(!source(4)||!write_level_halfword(m,destination+40,word>>16)||!write_level_halfword(m,destination+42,word)||!m.write32(0x800BA2D4,((((word>>8)&255)-1)<<16)|0xEFFF))return false;
    for(unsigned i=0;i<8;++i)if(!source(i+5)||!write_level_halfword(m,destination+44+i*8,0)||!write_level_halfword(m,destination+46+i*8,word>>16)||!write_level_halfword(m,destination+48+i*8,0)||!write_level_halfword(m,destination+50+i*8,word))return false;
    return true;
}
}
