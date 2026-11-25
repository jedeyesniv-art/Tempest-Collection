#pragma once
#include "TempestCommands.h"
namespace tempest_native {
struct CollisionInput{uint32_t object,drawRecord,record,packedPosition,status,otherStatus;};
struct CollisionResult{uint32_t status,otherStatus,hit;};
// Five original collision predicates, including the predicate-1 marker effect.
// Their signed, wrapping fixed-point arithmetic is retained at exact boundaries.
inline bool collision_predicate(CommandMemory& m,uint32_t entry,const CollisionInput& in,bool& hit){
    uint32_t x=in.packedPosition&0xFFFF0000,y=uint32_t(int32_t(in.packedPosition<<16)>>16),radius;
    bool truncate=false;
    switch(entry){
    case 0x80014C72:radius=(in.otherStatus&0x100)?0x2000:0x8000;break;
    case 0x80014A50:{uint32_t marker;if(!m.read32(0x800DC27C,marker)||!m.write32(0x800DC27C,marker|1))return false;}
        [[fallthrough]];
    case 0x80014A46:x|=0x8000;radius=(in.otherStatus&0x100)?0x8000:0x4000;break;
    case 0x80014A2E:x|=0x8000;radius=0xF000;break;
    case 0x80014C48:radius=0x4000;truncate=true;break;
    default:return false;
    }
    uint32_t otherX,otherY;
    if(!m.read32(in.drawRecord+0x20,otherX)||!m.read32(in.drawRecord+0x24,otherY))return false;
    if(truncate)otherX&=0xFFFF0000;
    uint32_t dx=x-otherX;if(int32_t(dx)<0)dx=0U-dx;
    uint32_t dy=y-uint32_t(int32_t(otherY)>>16);if(int32_t(dy)<0)dy=0U-dy;
    hit=!(int32_t(radius)<int32_t(dx)||int32_t(dy)>60);
    return !hit||m.write32(0x8011316C,in.record);
}
// Complete collision dispatch/status function at 80011CE6..80011D44.
inline bool collide_objects(CommandMemory& m,const CollisionInput& in,CollisionResult& out){
    uint32_t descriptor,offset;
    if(!m.read32(in.object+0x34,descriptor)||!m.read32(0x801131D0+((descriptor>>8)&255)*4,offset))return false;
    bool hit;if(!collision_predicate(m,0x800113B4+offset,in,hit))return false;
    out={in.status,in.otherStatus,hit?1U:0U};
    if(hit){uint32_t types=(((in.status>>8)|(in.otherStatus>>8))&255)<<8;out.status|=0x100000|types;out.otherStatus|=0x100000|types;}
    return true;
}
}
