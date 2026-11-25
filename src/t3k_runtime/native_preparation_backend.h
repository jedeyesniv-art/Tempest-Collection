#pragma once
#include "native_frame_integration.h"
#include "game/TempestPreparation.h"
namespace tempest_native {
// Synchronous native preparation, with the same submit/fence ownership contract
// as a future thread-pool backend. Worker 1 owns a separate persistent data
// window; it never aliases the gameplay processor's local memory.
class NativePreparationBackend final:public FrameComputeBackend{
public:
    explicit NativePreparationBackend(NativeFrameMemory& memory):memory_(memory){}
    bool restore_worker_data(std::span<const uint8_t> data){if(active_||data.size()!=local_.size())return false;std::copy(data.begin(),data.end(),local_.begin());return true;}
    bool initialize(const std::array<uint32_t,48>& environment)override{
        if(active_)return false;before_=local_;active_=true;
        for(unsigned i=0;i<48;++i)put(local_,i*4,environment[i]);return true;
    }
    bool submit(const FrameWorkerRequest& request)override{
        if(!active_||pending_||(request.type!=2&&request.type!=3&&request.type!=6&&request.type!=7&&request.type!=10&&request.type!=12&&request.type!=17))return false;
        staged_=local_;writes_.clear();View view(*this);
        if(!load_preparation_data(view,request.type)||!execute_preparation(view,{request.type,1,request.context})){writes_.clear();return false;}
        // This backend has finished the function on the caller's thread.
        // Publish its completed writes now; holding them until the next submit
        // invents a one-job latency and can overwrite later CPU record edits.
        // complete() remains an idempotent fence for the shared host contract.
        pending_=true;
        for(auto [address,value]:writes_)if(!memory_.write32(address,value))return false;
        local_=staged_;writes_.clear();return true;
    }
    bool complete()override{
        if(!active_)return false;if(!pending_)return true;
        pending_=false;return true;
    }
    void commit_frame()override{if(pending_)return;active_=false;}
    void abort_frame()override{if(active_)local_=before_;writes_.clear();pending_=active_=false;}
    std::span<const uint8_t> local_data()const{return local_;}
private:
    static constexpr uint32_t localBase=0x20100000,localSize=0x2000;
    NativeFrameMemory& memory_;std::array<uint8_t,localSize> local_{},before_{},staged_{};
    std::map<uint32_t,uint32_t> writes_;bool active_=false,pending_=false;
    static bool external(uint32_t a){return (a>=0x40000000&&a<0x40800000)||(a>=0x80000000&&a<0x80800000);}
    static void put(std::array<uint8_t,localSize>& data,uint32_t offset,uint32_t value){for(unsigned k=0;k<4;++k)data[offset+k]=uint8_t(value>>(24-k*8));}
    struct View:CommandMemory{
        NativePreparationBackend& owner;explicit View(NativePreparationBackend& value):owner(value){}
        bool read8(uint32_t address,uint8_t& value)override{
            if(address>=localBase&&address<localBase+localSize){value=owner.staged_[address-localBase];return true;}
            auto it=owner.writes_.find(address&~3U);if(it!=owner.writes_.end()){value=uint8_t(it->second>>(24-(address&3)*8));return true;}
            return external(address)&&owner.memory_.read8(address,value);
        }
        bool read32(uint32_t address,uint32_t& value)override{address&=~3U;value=0;for(unsigned k=0;k<4;++k){uint8_t b;if(!read8(address+k,b))return false;value=(value<<8)|b;}return true;}
        bool write32(uint32_t address,uint32_t value)override{
            address&=~3U;if(address>=localBase&&address<localBase+localSize){put(owner.staged_,address-localBase,value);return true;}
            if(!external(address))return false;owner.writes_[address]=value;return true;
        }
    };
};
}
