#pragma once
#include <cstdint>
#include <array>
#include "TempestCommands.h"
namespace tempest_native {
struct EventInput{uint32_t paused,status,interest,descriptor,forwarded;};
struct EventSelection{uint32_t route,index,status,forwarded;}; // 0 none, 1 callback, 2 worker
// Selection/status portion of 80011C60. Callback and worker effects are separate
// explicit dependencies; this does not pretend to execute an unknown callback.
inline EventSelection select_event(const EventInput& in){
    EventSelection out{0,0,in.status,in.forwarded};
    if(in.paused&1)return out;
    out.forwarded=0;
    if(!((in.status>>16)&(in.interest>>16)))return out;
    out.forwarded=in.status;out.status&=65535;
    unsigned opcode=(in.descriptor>>8)&255;
    out.route=opcode&128?2:1;out.index=opcode&127;return out;
}
// Recovered queue primitives. Opcodes remain raw original values until their
// consumer functions establish meaning; no guessed deletion/spawn behaviors.
inline bool append_object_event(CommandMemory& memory,uint32_t opcode,uint32_t object){
    uint32_t tail;if(!memory.read32(0x801131B0,tail))return false;
    return memory.write32(tail,opcode)&&memory.write32(tail+4,object)&&memory.write32(0x801131B0,tail+8);
}
inline bool append_event_packet(CommandMemory& memory,uint32_t opcode,const std::array<uint32_t,4>& payload){
    uint32_t tail;if(!memory.read32(0x801131B0,tail)||!memory.write32(tail,opcode))return false;
    for(unsigned i=0;i<4;++i)if(!memory.write32(tail+4+i*4,payload[i]))return false;
    return memory.write32(0x801131B0,tail+20);
}
inline bool append_selected_object_event(CommandMemory& memory,uint32_t object){
    uint32_t opcode;return memory.read32(0x80113190,opcode)&&append_object_event(memory,opcode,object);
}
}
