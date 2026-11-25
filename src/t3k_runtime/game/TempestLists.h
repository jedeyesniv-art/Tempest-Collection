#pragma once
#include "TempestCommands.h"
namespace tempest_native {
struct ListMoveInput{uint32_t node,after,sourceHeadSlot,destinationHeadSlot;};
struct ListMoveOutput{uint32_t traversal,node;};
// Complete semantic operation at 800106C0..8001073C. Reads intentionally remain
// ordered: removal can change fields subsequently read during insertion.
inline bool move_object_list(CommandMemory& memory,const ListMoveInput& in,ListMoveOutput& out){
    if(!in.node){out={0,in.after};return true;}
    uint32_t previous,next,traversal=0;
    if(!memory.read32(in.node,previous)||!memory.read32(in.node+4,next))return false;
    if(previous){if(!memory.write32(previous+4,next))return false;}
    else if(!memory.write32(in.sourceHeadSlot,next))return false;
    if(!memory.read32(in.node+4,next))return false;
    traversal=next;
    if(next){if(!memory.read32(in.node,previous)||!memory.write32(next,previous))return false;}
    if(!in.after){
        next=0;
        if(!memory.write32(in.node,0)||!memory.write32(in.node+4,0)||!memory.write32(in.destinationHeadSlot,in.node))return false;
    }else{
        if(!memory.read32(in.after+4,next)||!memory.write32(in.after+4,in.node)||!memory.write32(in.node,in.after)||!memory.write32(in.node+4,next))return false;
    }
    if(next&&!memory.write32(next,in.node))return false;
    out={traversal,in.node};return true;
}
// Queue family 0 wrapper at 800106A4. The selected list-table slot is also
// published for subsequent engine operations; it is part of the side effects.
inline bool transfer_queued_object(CommandMemory& memory,uint32_t selector,uint32_t node,ListMoveOutput& out){
    uint32_t slot=0x800BA470U+selector*4,after;
    if(!memory.read32(slot,after)||!memory.write32(0x80112C98,slot))return false;
    return move_object_list(memory,{node,after,0x800BA470,slot},out);
}
}
