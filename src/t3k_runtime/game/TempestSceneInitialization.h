#pragma once
#include "TempestPools.h"
#include <array>
#include <utility>
#include <initializer_list>
namespace tempest_native {
struct SceneInitializationServices {
    virtual bool initialize_workers(const std::array<uint32_t,48>&)=0;
    virtual ~SceneInitializationServices()=default;
};
struct SceneInitializationInput {uint32_t index;std::array<uint32_t,20> savedEngineData;};
// Complete 80010200..800102D8. The temporary saved engine-data ABI is the
// same explicit data contract used by the frame orchestrator, not an ISA VM.
inline bool initialize_driver_scene(CommandMemory& m,const SceneInitializationInput& in,SceneInitializationServices& services,PoolSceneResult& result){
    for(unsigned k=0;k<20;++k)if(!m.write32(0x800BAC50+k*4,in.savedEngineData[k]))return false;
    if(!m.write32(0x80110AC0,0))return false;
    for(auto range:{std::pair<uint32_t,unsigned>{0x800DC108,5},{0x800BA470,8},{0x8003DE40,8}})
        for(unsigned k=0;k<range.second;++k)if(!m.write32(range.first+k*4,0))return false;
    for(unsigned k=0;k<16;++k){uint32_t value;if(!m.read32(0x800AF7B4+k*4,value)||!m.write32(0x800DC360+k*4,value))return false;}
    std::array<uint32_t,48> environment;for(unsigned k=0;k<48;++k)if(!m.read32(0x800B1720+k*4,environment[k]))return false;
    if(!services.initialize_workers(environment))return false;
    uint32_t descriptor;if(!m.read32(0x800BA420+in.index*4,descriptor))return false;
    return initialize_scene_pools(m,{in.index,0x8003E040,descriptor},result);
}
}
