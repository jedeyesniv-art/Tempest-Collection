#pragma once
#include "TempestCommands.h"
#include <array>
namespace tempest_native {
struct GraphicsScratchLayout {uint32_t otherCommand=0,mainCommand=0,parameters=0,tile=0,end=0,readParameters=0;};
inline GraphicsScratchLayout graphics_scratch_layout(uint32_t descriptor,uint32_t words,uint32_t present=31){
    GraphicsScratchLayout layout;layout.tile=0x20100000U|((descriptor&511)<<4);
    uint32_t cursor=layout.end=layout.tile+((((descriptor>>9)&31)+1)<<7);
    if(present&1){cursor-=16;layout.otherCommand=cursor;}
    if(present&2){cursor-=32;layout.mainCommand=cursor;}
    cursor-=words*4;if(present&4)layout.parameters=cursor;
    layout.readParameters=(descriptor>>14)&1;return layout;
}
// 80025060..800250B0: the FFFF operation entry is the original fallback entry,
// not an implicit successful implementation of an unknown operation.
inline bool find_graphics_operation(CommandMemory& m,uint32_t table,uint32_t operation,uint32_t& result,unsigned budget=4096){
    while(budget--){uint32_t id;if(!m.read32(table+4,id))return false;
        if(id==operation||id==0xFFFF)return m.read32(table,result);table+=8;
    }return false;
}
// 800250B2..800250E2: retain the original arithmetic shift and unmasked fields.
inline uint32_t encode_graphics_scratch(uint32_t address,uint32_t bytes,uint32_t mode){
    return ((address-0x20100000U)>>4)|((uint32_t(int32_t(bytes)>>7)-1)<<9)|(mode?0x4000U:0U);
}
// 800242EE..8002434C. Write size before reading the pointer, including when the
// output aliases an environment field. Mode zero/unknown returns zero unchanged.
inline bool select_graphics_scratch(CommandMemory& m,uint32_t environment,uint32_t sizeOutput,uint32_t& result){
    uint32_t mode,bytes;if(!m.read32(environment+0x1C,mode))return false;
    if(mode!=1&&mode!=2){result=0;return true;}
    uint32_t field=mode==1?0x28:0x20;
    return m.read32(environment+field,bytes)&&m.write32(sizeOutput,bytes)&&m.read32(environment+field+4,result);
}
// 80025920..80025986. Optional outputs consume their scratch reservations only
// when present; preserving this order also preserves overlapping output slots.
inline bool decode_graphics_scratch(CommandMemory& m,uint32_t descriptor,uint32_t words,
                                    const std::array<uint32_t,5>& outputs,uint32_t& result){
    uint32_t present=(outputs[0]?1U:0U)|(outputs[1]?2U:0U)|(outputs[2]?4U:0U);
    auto layout=graphics_scratch_layout(descriptor,words,present);
    if(outputs[3]&&!m.write32(outputs[3],layout.tile))return false;
    if(outputs[4]&&!m.write32(outputs[4],layout.end))return false;
    if(outputs[0]&&!m.write32(outputs[0],layout.otherCommand))return false;
    if(outputs[1]&&!m.write32(outputs[1],layout.mainCommand))return false;
    if(outputs[2]&&!m.write32(outputs[2],layout.parameters))return false;
    result=layout.readParameters;return true;
}
}
