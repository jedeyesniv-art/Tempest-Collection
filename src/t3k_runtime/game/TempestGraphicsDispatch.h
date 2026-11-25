#pragma once
#include "TempestGraphicsUtilities.h"
#include "TempestHeap.h"
namespace tempest_native {
struct GraphicsDispatchServices {
    // These identifiers must resolve to implemented native algorithms. Neither
    // method permits executing the original instructions or treating a missing
    // drawing kernel as a successful no-op.
    virtual bool invoke_cpu(uint32_t function,uint32_t scratch,uint32_t parameters,uint32_t a,uint32_t b)=0;
    virtual bool append_sequence(uint32_t sequence,uint32_t operation,uint32_t parameters,uint32_t a,uint32_t b,uint32_t& status)=0;
    virtual ~GraphicsDispatchServices()=default;
};
// 800250E4..800251F0. CPU operations 80..90 retain their parameter allocation;
// other CPU operations free it after the call. Operation E always uses CPU.
// Coherent host memory needs no mutation for the original DCacheSync call.
inline bool dispatch_graphics_operation(CommandMemory& m,GraphicsDispatchServices& services,
                                        uint32_t context,uint32_t operation,uint32_t parameters,
                                        uint32_t /*parameterBytes*/,uint32_t a,uint32_t b,uint32_t& result){
    uint32_t sequence;if(!m.read32(context+0xCC,sequence))return false;
    if(sequence&&operation!=0xE){
        if(!services.append_sequence(sequence,operation,parameters,a,b,result))return false;
        return result==1; // Other status enters the original assertion path.
    }
    uint32_t function,scratch,bytes;
    if(!find_graphics_operation(m,0x804FB394,operation,function)||!m.read32(0x806B9998,bytes)||!m.read32(0x804FB37C,scratch))return false;
    scratch=encode_graphics_scratch(scratch,bytes,0);
    if(!services.invoke_cpu(function,scratch,parameters,a,b))return false;
    if(operation-0x80U>0x10U&&!release_heap_block(m,parameters))return false;
    result=1;return true;
}
}
