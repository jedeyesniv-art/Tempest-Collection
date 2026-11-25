#pragma once
#include "TempestCommands.h"
namespace tempest_native {
struct QueueRecord{uint32_t cursor,header,selector,handler,argument,terminal;};
inline bool read_queue_word(CommandMemory& memory,uint32_t& cursor,uint32_t& word){
    if(!memory.read32(cursor,word))return false;cursor+=4;return true;
}
// Dispatch-prefix contract of 800105E2..8001062C. Handler effects are NOT
// executed here; the output explicitly names the original selected handler.
inline bool decode_queue_record(CommandMemory& memory,uint32_t cursor,QueueRecord& out){
    QueueRecord record{};record.cursor=cursor;
    if(!read_queue_word(memory,record.cursor,record.header))return false;
    if(record.header==0xffffffffU){record.terminal=1;out=record;return true;}
    if(!memory.write32(0x80112C94,record.header))return false;
    record.selector=record.header&15;
    if(!memory.read32(0x80112D20+(record.header>>16)*4,record.handler)||!read_queue_word(memory,record.cursor,record.argument))return false;
    out=record;return true;
}
// Queue family 3, 8001073E..8001077E. Input cursor is already past the first
// prefetched argument. Original code replaces the caller's saved queue cursor;
// native code returns that semantic output directly. Count zero still reads one.
inline bool consume_payload_packet(CommandMemory& memory,uint32_t cursor,uint32_t& next){
    uint32_t header;if(!memory.read32(0x80112C94,header))return false;
    cursor-=4;
    if(!memory.write32(0x80112C90,(header>>8)&255))return false;
    unsigned count=header&255;if(!count)count=1;
    for(unsigned i=0;i<count;++i){uint32_t word;if(!read_queue_word(memory,cursor,word)||!memory.write32(0x80112CA0+i*4,word))return false;}
    next=cursor;return true;
}
}
