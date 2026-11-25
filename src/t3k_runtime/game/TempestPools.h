#pragma once
#include "TempestTemplates.h"
namespace tempest_native {
struct PoolBatchInput{uint32_t source,count,destination,previous;};
struct PoolBatchResult{uint32_t next,last;};
struct PoolSceneInput{uint32_t index,storage,descriptor;};
struct PoolSceneResult{uint32_t storage,nextHeadSlot;InitializerState random;};
inline bool initialize_pool_entry(CommandMemory& m,uint32_t descriptor,uint32_t source,uint32_t remaining,InitializerState& state){
    uint32_t offset;if(!m.read32(0x80112C60+(descriptor&255)*4,offset))return false;
    uint32_t entry=0x800105BC+offset;
    if(entry==0x80011DF6||entry==0x80010800||entry==0x80010844)return true;
    if(entry==0x80011DE4){uint32_t frame;return m.read32(0x80110AA4,frame)&&m.write32(remaining+0x24,frame);}
    // These constructors receive the original template in r8, not the freshly
    // copied destination. Preserve that binding and its ordered alias effects.
    return (entry==0x80010802||entry==0x800107A4)&&initialize_with_state(m,entry,source,state);
}
inline bool pool_copy_vectors(CommandMemory& m,uint32_t source,uint32_t destination,uint32_t size,unsigned budget){
    unsigned vectors=size>>4;if(!vectors)vectors=1;if(vectors>budget)return false;
    for(unsigned i=0;i<vectors;++i){uint32_t words[4],from=(source+i*16)&~15U,to=(destination+i*16)&~15U;
        for(unsigned k=0;k<4;++k)if(!m.read32(from+k*4,words[k]))return false;
        for(unsigned k=0;k<4;++k)if(!m.write32(to+k*4,words[k]))return false;
    }
    return true;
}
// Complete batched pool creation at 80010A74, including its six-entry creation
// dispatch. Count zero still creates one node. Copy ordering is vector-granular.
inline bool create_object_pool(CommandMemory& m,const PoolBatchInput& in,InitializerState& state,PoolBatchResult& out,unsigned budget=65536){
    uint32_t previous=in.previous,current=in.destination,size,remaining=in.count,descriptor;
    if(previous&&!m.write32(previous+4,current))return false;
    if(!m.read32(in.source+0x20,size)||!pool_copy_vectors(m,in.source,current,size,budget))return false;
    for(unsigned made=0;made<budget;++made){
        bool more=int64_t(int32_t(remaining))-1>0;--remaining;uint32_t next=more?current+size:0;
        if(!m.write32(current,previous)||!m.write32(current+4,next)||!m.read32(current+0x24,descriptor)||!initialize_pool_entry(m,descriptor,in.source,remaining,state))return false;
        if(int32_t(remaining)<=0){out={current+size,current};return true;}
        previous=current;current=next;
        if(!pool_copy_vectors(m,previous,current,size,budget))return false;
    }
    return false;
}
// Complete scene pool setup at 800109CE. Negative command -2 opens the next
// list; any other negative command ends setup. Empty-list head writes, seeded
// state and repeated template-table reads intentionally match the original.
inline bool initialize_scene_pools(CommandMemory& m,const PoolSceneInput& in,PoolSceneResult& out,unsigned budget=65536){
    for(unsigned k=0;k<8;++k)if(!m.write32(0x80110A60+k*4,0))return false;
    InitializerState state{0xA3000000,0x37264865,0x76737362,0x10958767};
    uint32_t storage=in.storage,head=storage,slot=0x800BA470,previous=0,cursor=in.descriptor+8;
    for(unsigned commands=0;commands<budget;++commands){
        uint32_t command;if(!m.read32(cursor,command))return false;cursor+=4;
        if(command&0x80000000){
            if(!m.write32(slot,head))return false;slot+=4;
            if(command==0xFFFFFFFE){head=storage;previous=0;continue;}
            out={storage,slot,state};return true;
        }
        uint32_t table,source;
        if(!m.read32(in.descriptor,table)||!m.read32(table+(command>>16)*4,source))return false;
        PoolBatchResult batch;
        if(!create_object_pool(m,{source,command&65535,storage,previous},state,batch,budget))return false;
        storage=batch.next;previous=batch.last;
    }
    return false;
}
}
