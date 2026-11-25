#pragma once
#include "native_frame_memory.h"
#include "game/TempestBootPlatform.h"
#include "game/TempestGraphicsInitialization.h"
namespace tempest_native {
// Replace the recognized MPE0 coordinator with the existing native GPU pipeline.
// Code bytes identify the recovered contract; they are never uploaded/executed.
// The caller owns memory rollback and must call abort() on a failed startup.
class NativeBootRenderer final:public BootWorkerServices {
public:
    NativeBootRenderer(NativeFrameMemory& memory,GraphicsInitializationServices& scratch,NativeFrameRenderer& pipeline,SDL_Renderer* renderer):memory_(memory),scratch_(scratch),pipeline_(pipeline),renderer_(renderer){}
    bool local_scratch(uint32_t output,uint32_t& address)override{uint32_t bytes;return scratch_.local_scratch(address,bytes)&&memory_.write32(output,bytes);}
    bool load_worker(uint32_t engine,uint32_t destination,uint32_t source,uint32_t bytes)override{
        if(ready_||engine!=0)return false;
        if(destination==0x20100000&&source==0x8003D650&&bytes==4&&!data_){uint32_t job;
            if(!memory_.read32(source,job)||!memory_.renderer_word(0x100000,job))return false;data_=true;return true;}
        if(destination!=0x20300000||source!=0x8003D658||bytes!=0x3EC||!data_||code_)return false;
        uint64_t hash=0xCBF29CE484222325ULL;for(unsigned i=0;i<bytes;++i){uint8_t byte;if(!memory_.read8(source+i,byte))return false;hash=(hash^byte)*0x100000001B3ULL;}
        if(hash!=0x7932AE2324D9F259ULL)return false;code_=true;return true;
    }
    bool start_worker(uint32_t engine,uint32_t entry)override{
        if(ready_||engine!=0||entry!=0x20300000||!data_||!code_)return false;
        ready_=pipeline_.initialize(renderer_,failure_);return ready_;
    }
    bool ready()const{return ready_;}
    const std::string& failure()const{return failure_;}
    void abort(){ready_=data_=code_=false;failure_.clear();}
private:
    NativeFrameMemory& memory_;GraphicsInitializationServices& scratch_;NativeFrameRenderer& pipeline_;SDL_Renderer* renderer_;
    bool data_=false,code_=false,ready_=false;std::string failure_;
};
}
