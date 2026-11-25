#pragma once
#include "native_audio_backend.h"
#include "game/TempestNiseInitialization.h"
namespace tempest_native {
// The native SDK initializer owns shared data construction. This bridge handles
// the two processor uploads and A0 message using the recovered native mixer.
// Allocators and physical audio-device configuration remain explicit platform
// services. The supplied platform's upload/start methods are never invoked.
class NativeNiseInitialization final:public NiseInitializationServices {
public:
    NativeNiseInitialization(CommandMemory& memory,NativeAudioBackend& audio,NiseInitializationServices& platform):memory_(memory),audio_(audio),platform_(platform){}
    bool initialize_media_processor(uint32_t& engine)override{return platform_.initialize_media_processor(engine);}
    bool local_scratch(uint32_t& address,uint32_t& bytes)override{return platform_.local_scratch(address,bytes);}
    bool allocate_heap(uint32_t bytes,uint32_t& address)override{return platform_.allocate_heap(bytes,address);}
    bool allocate_dma(uint32_t bytes,uint32_t alignment,uint32_t flags,uint32_t& address)override{return platform_.allocate_dma(bytes,alignment,flags,address);}
    bool processor_properties(uint32_t engine,uint32_t& properties)override{return platform_.processor_properties(engine,properties);}
    bool configure_audio(uint32_t op,uint32_t argument)override{return platform_.configure_audio(op,argument);}
    bool install_interrupt(uint32_t interrupt,uint32_t handler)override{return platform_.install_interrupt(interrupt,handler);}
    bool upload(uint32_t engine,uint32_t destination,uint32_t source,uint32_t bytes)override{
        if(started_||engine>3)return false;
        if(destination==0x20301000&&source==0x806B14A0&&bytes==0xA68&&!code_){
            uint64_t hash=0xCBF29CE484222325ULL;
            for(uint32_t i=0;i<bytes;++i){uint8_t value;if(!memory_.read8(source+i,value))return false;hash=(hash^value)*0x100000001B3ULL;}
            // Reject an unported processor binary, rather than treating its
            // instructions as a different native mixer with the same ABI.
            if(hash!=0x2DB96E20EB3B3AEDULL)return false;engine_=engine;code_=true;return true;
        }
        if(destination!=0x20101000||source!=0x806B1F10||bytes!=0x7AC||!code_||dataLoaded_||engine!=engine_)return false;
        data_.resize(bytes);for(uint32_t i=0;i<bytes;++i)if(!memory_.read8(source+i,data_[i]))return false;dataLoaded_=true;return true;
    }
    bool start(uint32_t engine,const std::array<uint32_t,4>& message)override{
        if(!code_||!dataLoaded_||started_||engine!=engine_)return false;message_=message;started_=true;return true;
    }
    // Call after successful SDK initialization, before committing its memory
    // transaction. Failure leaves the previous private processor state intact.
    bool commit_processor(){return started_&&!committed_&&(committed_=audio_.initialize_processor(data_,engine_,message_));}
    bool has_start_request()const{return started_;}
private:
    CommandMemory& memory_;NativeAudioBackend& audio_;NiseInitializationServices& platform_;
    std::vector<uint8_t> data_;std::array<uint32_t,4> message_{};uint32_t engine_=0;
    bool code_=false,dataLoaded_=false,started_=false,committed_=false;
};
}
