#pragma once
#include "TempestTemplates.h"
#include "TempestQueueDecode.h"
#include "TempestLists.h"
namespace tempest_native {
struct QueueConsumption {uint32_t cursor,records;};
// Native composition of the complete template operation, including its
// post-initializer reset. Unsupported initializer requests fail explicitly.
// On failure writes can be partial: callers must use a transaction/overlay.
inline bool instantiate_object(CommandMemory& memory,uint32_t index,uint32_t object,InitializerState& state){
    InitializerRequest request;
    if(!prepare_object_template(memory,{index,object},request))return false;
    if(request.target!=0x80011DF6&&!initialize_with_state(memory,request.target,object,state))return false;
    return memory.write32(0x80112C90,0);
}
inline bool activate_queued_object(CommandMemory& memory,uint32_t selector,uint32_t after,InitializerState& state){
    uint32_t node,header;
    if(!memory.read32(0x800BA470+selector*4,node))return false;
    if(!node)return true;
    if(!memory.read32(0x80112C94,header))return false;
    ListMoveOutput moved;
    if(!move_object_list(memory,{node,after,0x800BA470+(header&15)*4,0x800BA470},moved)||!memory.read32(0x80112C94,header))return false;
    uint32_t index=(header>>8)&255;
    return !index||instantiate_object(memory,index,moved.node,state);
}
// Complete semantic consumer loop at 800105BC. Uses the original dispatch
// pointers, not assumed opcode meanings. Unknown handlers/initializers and
// exhausted work budgets fail; callers must discard partial writes on failure.
inline bool consume_object_queue(CommandMemory& memory,uint32_t configuration,QueueConsumption& out,unsigned budget=4096){
    if(!memory.write32(0x80112C8C,configuration))return false;
    InitializerState state;
    uint32_t* words[4]{&state.polynomial,&state.first,&state.second,&state.third};
    for(unsigned k=0;k<4;++k)if(!memory.read32(0x80112C50+k*4,*words[k]))return false;
    if(!memory.write32(0x80112C90,0))return false;
    uint32_t cursor=0x800DBCD0,records=0;
    while(records<=budget){
        QueueRecord record;if(!decode_queue_record(memory,cursor,record))return false;
        cursor=record.cursor;
        if(record.terminal){
            for(unsigned k=0;k<4;++k)if(!memory.write32(0x80112C50+k*4,*words[k]))return false;
            out={cursor,records};return true;
        }
        if(records++==budget)return false;
        uint32_t header;ListMoveOutput moved;
        switch(record.handler){
        case 0x800106A4:if(!transfer_queued_object(memory,record.selector,record.argument,moved))return false;break;
        case 0x80010642:if(!activate_queued_object(memory,record.selector,record.argument,state))return false;break;
        case 0x80010780:
            if(!memory.read32(0x80112C94,header)||!instantiate_object(memory,(header>>8)&255,record.argument,state))return false;break;
        case 0x8001073E:if(!consume_payload_packet(memory,cursor,cursor))return false;break;
        default:return false;
        }
    }
    return false;
}
}
