#pragma once
#include "TempestCommands.h"
#include <array>
namespace tempest_native {
struct GraphicsInitializationServices {
    virtual bool allocate_heap(uint32_t bytes,uint32_t& address)=0;
    virtual bool local_scratch(uint32_t& address,uint32_t& bytes)=0;
    virtual ~GraphicsInitializationServices()=default;
};
// SDK environment initializer 80024AAE..80024B5A. Preserve its allocation and
// default font-name copy even though this function does not publish that name
// pointer in the environment. Scratch is shared through the original globals.
inline bool initialize_graphics_environment(CommandMemory& m,GraphicsInitializationServices& services,uint32_t environment){
    uint32_t length=0;uint8_t byte;
    do{if(length==4096||!m.read8(0x806A2890+length,byte))return false;if(byte)++length;}while(byte);
    uint32_t name;if(!services.allocate_heap(length+1,name))return false;
    uint32_t scratch,bytes;if(!m.read32(0x804FB37C,scratch))return false;
    if(!scratch&&(!services.local_scratch(scratch,bytes)||!m.write32(0x806B9998,bytes)||!m.write32(0x804FB37C,scratch)))return false;
    if(!name)return false;
    for(uint32_t i=0;i<=length;++i){uint32_t word,address=(name+i)&~3U;unsigned shift=24-8*((name+i)&3);
        if(!m.read8(0x806A2890+i,byte)||!m.read32(address,word)||!m.write32(address,(word&~(255U<<shift))|(uint32_t(byte)<<shift)))return false;
    }
    return m.read32(0x806B9998,bytes)&&m.read32(0x804FB37C,scratch)&&
           m.write32(environment,1)&&m.write32(environment+0x1C,1)&&
           m.write32(environment+0x28,bytes)&&m.write32(environment+0x2C,scratch)&&m.write32(environment+0x18,0);
}
// SDK context initializer 80022E64..8002309E. This is graphics-context state,
// not a framebuffer allocation or replacement rasterizer. Unwritten fields
// retain their original values; no blanket structure clear is substituted.
inline bool initialize_graphics_context(CommandMemory& m,uint32_t context,uint32_t environment){
    if(!context)return false; // Original invokes its assertion handler.
    if(!m.write32(context,environment))return false;
    constexpr std::array<std::array<uint32_t,2>,39> defaults{{
        {0x04,0},{0x08,0xFFFFFFFF},{0x0C,0x10808000},{0x10,0xEB808000},
        {0x48,1},{0x4C,2},{0x50,16},{0x54,2},{0x58,1},{0x60,0},{0x5C,0},
        {0xC4,0},{0xC8,0},{0x68,0},{0x14,0},{0x64,1},{0x78,3},{0x6C,0x10808000},
        {0x7C,0x10808000},{0x80,63},{0x84,0},{0x74,0},{0x70,10},{0x88,0x2574BEA7},
        {0x8C,0xA3000000},{0x90,0x83659328},{0x94,0xA3000000},{0x98,0xE000},
        {0x9C,0x10808000},{0xA4,0x100},{0xA8,0x100},{0xAC,0},{0xB0,1},{0xB4,0},
        {0xBC,16},{0xB8,3},{0xC0,0},{0xD0,0x10000},{0xD4,0}
    }};
    for(auto field:defaults)if(!m.write32(context+field[0],field[1]))return false;
    return m.write32(context+0xCC,0);
}
}
