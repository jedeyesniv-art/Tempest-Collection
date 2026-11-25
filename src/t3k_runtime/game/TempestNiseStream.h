#pragma once
#include "TempestCommands.h"
namespace tempest_native {
struct NiseStreamRead{uint32_t handle,mode,sector,blocks,destination,callback;};
struct NiseStreamServices{
    unsigned immediateDepth=0;
    // Accept an actual asynchronous read request. Acceptance is not completion;
    // the driver must later deliver its bytes and call complete_nise_stream_read.
    virtual bool enqueue(const NiseStreamRead&)=0;
    virtual bool custom(uint32_t,uint32_t,uint32_t){return false;}
    virtual ~NiseStreamServices()=default;
};
inline bool complete_nise_stream_read(CommandMemory&,uint32_t,uint32_t,NiseStreamServices&);
// 80021976, preparing the original MediaRead request at 800002E8.
inline bool request_nise_stream_read(CommandMemory& m,uint32_t sector,uint32_t blocks,uint32_t destination,NiseStreamServices& services){
    uint32_t handle;if(!m.read32(0x8003C7BC,handle)||!m.write32(0x806B9990,0)||!m.write32(0x806B9978,sector)||!m.write32(0x806B9980,blocks)||!m.write32(0x806B9988,destination))return false;
    if(!blocks){if(services.immediateDepth>=8)return false;++services.immediateDepth;bool ok=complete_nise_stream_read(m,0x40000000,0xffffffff,services);--services.immediateDepth;return ok;}
    return services.enqueue({handle,0x40000000,sector,blocks,destination,0x800219DA});
}
// 800219DA: retry partial failures, signal readiness and submit a queued wrap.
inline bool complete_nise_stream_read(CommandMemory& m,uint32_t status,uint32_t block,NiseStreamServices& services){
    uint32_t retries;if(!m.read32(0x806B9994,retries))return false;
    if(int32_t(status)<0){
        ++block;if(!block){++retries;if(!m.write32(0x806B9994,retries))return false;}
        if(int32_t(retries)<=3){uint32_t start,count,destination;if(!m.read32(0x806B9978,start)||!m.read32(0x806B9980,count)||!m.read32(0x806B9988,destination))return false;return request_nise_stream_read(m,start+block,count-block,destination+(block<<11),services);}
    }
    uint32_t state;if(!m.write32(0x806B9990,1)||!m.read32(0x804FB370,state)||!m.write32(0x806B9994,0))return false;
    if(!state){uint32_t cursor,blocks;if(!m.read32(0x806B9964,cursor)||!m.read32(0x806B9960,blocks))return false;return m.write32(0x804FB370,1)&&m.write32(0x806B9964,cursor+blocks);}
    uint32_t queued;if(!m.read32(0x806B997C,queued))return false;
    if(int32_t(queued)>=0){uint32_t count,destination;if(!m.read32(0x806B9984,count)||!m.read32(0x806B998C,destination)||!request_nise_stream_read(m,queued,count,destination,services))return false;return m.write32(0x806B997C,0xffffffff);}
    return true;
}
// 80021AD2, messages A0 (consumed stream half) and AF (pause acknowledgment).
inline bool handle_nise_stream_message(CommandMemory& m,uint32_t type,uint32_t half,NiseStreamServices& services){
    if(type==0xAF)return m.write32(0x8003C7B8,1);if(type!=0xA0)return true;
    uint32_t ready;if(!m.read32(0x806B9990,ready))return false;if(!ready)return m.write32(0x804FB370,0xfffffffe);
    uint32_t base,blocks,hook;if(!m.read32(0x806B995C,base)||!m.read32(0x806B9960,blocks)||!m.read32(0x806B9974,hook))return false;
    uint32_t count=uint32_t(int32_t(blocks)/2),destination=base+(half?(count<<11):0);
    if(hook)return services.custom(hook,destination,count<<11);
    uint32_t end,cursor;if(!m.read32(0x806B996C,end)||!m.read32(0x806B9964,cursor))return false;uint32_t available=end-cursor+1;
    if(int32_t(count)<int32_t(available)){
        if(!request_nise_stream_read(m,cursor,count,destination,services)||!m.write32(0x806B997C,0xffffffff))return false;return m.write32(0x806B9964,cursor+count);
    }
    bool firstQueued=available!=0;
    if(firstQueued&&(!request_nise_stream_read(m,cursor,available,destination,services)||!m.write32(0x806B997C,0xffffffff)))return false;
    destination+=available<<11;count-=available;uint32_t loop;if(!m.read32(0x806B9970,loop))return false;
    if(loop){
        uint32_t restart;if(!m.read32(0x806B9968,restart)||!m.write32(0x806B9964,restart))return false;
        if(firstQueued){if(!m.write32(0x806B997C,restart)||!m.write32(0x806B998C,destination)||!m.write32(0x806B9984,count))return false;}
        else if(!request_nise_stream_read(m,restart,count,destination,services)||!m.write32(0x806B997C,0xffffffff))return false;
        return m.write32(0x806B9964,restart+count);
    }
    if(!available&&!m.write32(0x804FB370,3))return false;
    // The original follows memset with DCacheSync. Host-owned coherent memory
    // needs no NUON cache operation; these bytes still participate in rollback.
    uint64_t bytes=uint64_t(count)<<11;if(bytes>0x800000)return false;
    for(uint32_t i=0;i<bytes;i+=4)if(!m.write32(destination+i,0))return false;
    return m.write32(0x806B9964,end+1);
}
}
