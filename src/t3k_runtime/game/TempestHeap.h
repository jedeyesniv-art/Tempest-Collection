#pragma once
#include "TempestCommands.h"
namespace tempest_native {
// The game's C heap uses boundary tags and a circular free list. These routines
// preserve the original data layout and allocation order, including allocation
// from the high end of a split block. Host malloc is not a substitute.
inline bool set_heap_limit(CommandMemory& m,uint32_t limit,uint32_t& previous){return m.read32(0x804FBA48,previous)&&m.write32(0x804FBA48,limit);}
inline bool grow_heap(CommandMemory& m,uint32_t bytes,uint32_t stack,uint32_t& result){
    uint32_t current,limit;if(!m.read32(0x804FBA44,current))return false;if(!current)current=0x806B9540;
    uint32_t next=current+bytes;if(!m.read32(0x804FBA48,limit))return false;
    if(next>limit||((stack-current)^(stack-next))&0x80000000U){result=0;return true;}
    result=current;return m.write32(0x804FBA44,next);
}
inline bool add_heap_region(CommandMemory& m,uint32_t address,uint32_t bytes){
    constexpr uint32_t head=0x804FB6FC;uint32_t previous;
    if(!m.write32(address,0)||!m.write32(address+8,head)||!m.read32(head+12,previous)||
       !m.write32(address+12,previous)||!m.write32(head+12,address))return false;
    bytes=(bytes&~15U)-8;
    return m.write32(previous+8,address)&&m.write32(address+4,bytes)&&m.write32(address+bytes,bytes)&&m.write32(address+bytes+4,0x80000000);
}
inline bool allocate_heap_block(CommandMemory& m,uint32_t bytes,uint32_t& result,unsigned budget=4096){
    constexpr uint32_t head=0x804FB6FC;uint32_t block;
    if(bytes<16)bytes=16;if(!m.read32(head+8,block))return false;bytes=(bytes+23)&~15U;
    while(block!=head){
        if(!budget--)return false;uint32_t size;if(!m.read32(block+4,size))return false;
        if(int32_t(size)>=int32_t(bytes)){
            uint32_t remaining=size-bytes;
            if(remaining>24){uint32_t allocated=block+remaining;
                if(!m.write32(block+4,remaining)||!m.write32(allocated,remaining)||!m.write32(allocated+4,0U-bytes)||!m.write32(allocated+bytes,0))return false;
                result=allocated+8;return true;
            }
            uint32_t previous,next;if(!m.read32(block+12,previous)||!m.read32(block+8,next)||!m.write32(previous+8,next)||
               !m.read32(block+8,next)||!m.read32(block+12,previous)||!m.write32(next+12,previous)||
               !m.read32(block+4,size)||!m.write32(block+4,0U-size)||!m.write32(block+size,0))return false;
            result=block+8;return true;
        }
        if(!m.read32(block+8,block))return false;
    }
    result=0;return true;
}
inline bool release_heap_block(CommandMemory& m,uint32_t pointer){
    constexpr uint32_t head=0x804FB6FC;uint32_t block=pointer-8,previousBytes,allocatedSize,sizeAddress=pointer-4,size;
    if(!m.read32(block,previousBytes)||!m.read32(sizeAddress,allocatedSize))return false;
    if(previousBytes){block-=previousBytes;sizeAddress=block+4;
        if(!m.read32(sizeAddress,size)||!m.write32(sizeAddress,size-allocatedSize))return false;
    }else{
        uint32_t previous;if(!m.write32(pointer,head)||!m.read32(head+12,previous)||!m.write32(pointer+4,previous)||
           !m.write32(head+12,block)||!m.read32(pointer+4,previous)||!m.write32(previous+8,block)||
           !m.read32(pointer-4,allocatedSize)||!m.write32(pointer-4,0U-allocatedSize))return false;
    }
    if(!m.read32(sizeAddress,size))return false;uint32_t next=block+size,nextSize;
    if(!m.read32(next+4,nextSize))return false;
    if(int32_t(nextSize)>0){uint32_t previous,following;
        if(!m.read32(next+12,previous)||!m.read32(next+8,following)||!m.write32(previous+8,following)||
           !m.read32(next+8,following)||!m.read32(next+12,previous)||!m.write32(following+12,previous)||
           !m.read32(next+4,nextSize)||!m.read32(sizeAddress,size))return false;
        size+=nextSize;if(!m.write32(sizeAddress,size))return false;next=block+size;
    }
    return m.write32(next,size);
}
// malloc wrapper 80033E24..80033EB2. stack is the original caller's C stack
// DATA address, used only for the original heap/stack collision condition.
inline bool allocate_c_heap(CommandMemory& m,uint32_t bytes,uint32_t stack,uint32_t& result){
    uint32_t initialized;if(!m.read32(0x804FB710,initialized))return false;
    if(!initialized){uint32_t limit,ignored,address;
        if(!set_heap_limit(m,0,limit)||!set_heap_limit(m,limit,ignored)||!grow_heap(m,limit-0x806B9540,stack-32,address))return false;
        if(!address){result=0;return true;}
        if(!add_heap_region(m,address,limit-0x806B9540)||!m.write32(0x804FB710,1))return false;
    }
    return allocate_heap_block(m,bytes,result);
}
}
