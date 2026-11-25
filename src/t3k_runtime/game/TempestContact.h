#pragma once
#include "TempestCallbacks.h"
#include "TempestCollision.h"
namespace tempest_native {
struct CollisionRecordInput{uint32_t object,draw,status;};
struct CollisionRecord{uint32_t packedPosition,draw,status,object;};
inline bool prepare_collision_record(CommandMemory& m,const CollisionRecordInput& in,CollisionRecord& out){
    uint32_t x,y;if(!m.read32(in.draw+0x20,x)||!m.read32(in.draw+0x24,y))return false;
    out={(x&0xFFFF0000)|(y>>16),in.draw,in.status&~0x100000U,in.object};return true;
}
struct CollisionBatchInput{uint32_t object,draw,end,count,status,scratch21,scratch23;};
struct CollisionBatchResult{uint32_t status,processed;};
// Complete pair-scan phase 8001184E..800118A0. Reverse order determines which
// hit record remains selected for subsequent callbacks. Record vectors align
// to 16 bytes; the selected record pointer retains its original raw value.
inline bool collide_record_batch(CommandMemory& m,const CollisionBatchInput& in,CollisionBatchResult& out,unsigned budget=65536){
    out={in.status,0};if(!in.count)return true;
    uint32_t saved[4]{in.end,in.scratch21,in.count,in.scratch23};
    for(unsigned k=0;k<4;++k)if(!m.write32(0x80113180+k*4,saved[k]))return false;
    uint32_t header[4];for(unsigned k=0;k<4;++k)if(!m.read32(((in.object+0x30)&~15U)+k*4,header[k]))return false;
    uint32_t status=header[2],cursor=in.end,count=in.count;
    for(unsigned processed=0;processed<budget;++processed){
        cursor-=0x20;std::array<uint32_t,4> record;
        if(!callback_vector(m,cursor,record))return false;
        CollisionResult result;
        if(!collide_objects(m,{in.object,in.draw,cursor,record[0],status,record[2]},result))return false;
        status=result.status;record[2]=result.otherStatus;
        for(unsigned k=0;k<4;++k)if(!m.write32((cursor&~15U)+k*4,record[k]))return false;
        bool more=int64_t(int32_t(count))-1>0;--count;
        if(!more){
            // Preserve the original restore's data reads even if the save area
            // aliases a record; these are engine context, not a list rewrite.
            for(unsigned k=0;k<4;++k)if(!m.read32(0x80113180+k*4,saved[k]))return false;
            if(!m.write32(in.object+0x38,status))return false;
            out={status,processed+1};return true;
        }
    }
    return false;
}
struct LaneContactInput{uint32_t object,draw;};
struct LaneContactResult{uint32_t kind;}; // 0 none, 1 player notification, 2 surface response
// Complete lane/surface collision phase 8001198C..80011B0A. The enclosing
// function next dispatches events; that dispatch is intentionally not hidden.
inline bool resolve_lane_contact(CommandMemory& m,const LaneContactInput& in,LaneContactResult& out,CallbackServices* services){
    out={0};uint32_t interest,x,y,height,status;
    if(!m.read32(in.object+0x30,interest))return false;if(!(interest&0x400000))return true;
    if(!m.read32(in.draw+0x20,x)||!m.read32(in.draw+0x24,y))return false;if(y&0x80000000)return true;
    uint32_t address=0x800AF7B4+(x>>16)*4;
    if(!m.read32(address,height))return false;
    if(!height||int64_t(int32_t(y>>16))-int32_t(height)<0)return true;
    if(!m.read32(in.object+0x38,status))return false;
    if(status&0x100){
        uint32_t flags;if(!m.read32(0x800DC274,flags))return false;if(flags&4)return true;
        if(!m.read32(in.draw+0x20,x)||!append_event_packet(m,0x30C04,{x,height<<16,0x800AF7B4,height})||!append_object_event(m,0x13C04,in.object)||!m.read32(0x800DC274,flags)||!m.write32(0x800DC274,flags|0x24))return false;
        out.kind=1;return true;
    }
    uint32_t walls,unused,cap;
    if(!m.read32(in.draw+0x20,x)||!m.read32(0x800DC254,walls)||!m.read32(in.object+0x10,unused)||!m.read32(0x801095A0,cap))return false;
    bool marked=((walls>>16)&(1U<<((x>>16)&15)))!=0;
    uint32_t raised=height+(int32_t(cap)>127?(marked?2:8):(marked?4:16));
    uint32_t stored=int32_t(raised)>=0x500?0:raised;
    if(!m.write32(address,stored)||!callback_sound(services,0x80000F34|((raised>>5)<<16))||!m.read32(in.object+0x38,status)||!m.write32(in.object+0x38,status|0x400000))return false;
    out.kind=2;return true;
}
}
