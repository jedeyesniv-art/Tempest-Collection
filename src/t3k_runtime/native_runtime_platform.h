#pragma once
#include "native_nise_initialization.h"
#include "game/TempestHeap.h"
#include "game/TempestBootConstructors.h"
#include "game/TempestGraphicsInitialization.h"
namespace tempest_native {
// Native platform resources, separate from the isolated reference fixtures.
// MPE 0 denotes the recovered mixer; no guest program is launched here.
class NativeRuntimePlatform final:public NiseInitializationServices,public GraphicsInitializationServices,public DriverFirmwareServices,public DriverConstructorServices {
public:
    NativeRuntimePlatform(NativeFrameMemory& memory,uint32_t stack):memory_(memory),stack_(stack){}
    bool initialize_media_processor(uint32_t& engine)override{engine=0;return true;}
    bool local_scratch(uint32_t& address,uint32_t& bytes)override{address=0x20100C80;bytes=512;return true;}
    bool allocate_heap(uint32_t bytes,uint32_t& address)override{return allocate_c_heap(memory_,bytes,stack_,address);}
    bool allocate_dma(uint32_t bytes,uint32_t alignment,uint32_t flags,uint32_t& address)override{
        if(dmaOwned_||bytes!=0x2000||alignment!=0x2000||flags!=1)return false;
        // This reservation precedes the fixed framebuffers and the music ring.
        address=0x40004000;dmaOwned_=true;return true;
    }
    bool processor_properties(uint32_t engine,uint32_t& properties)override{properties=12;return engine==0;}
    bool upload(uint32_t,uint32_t,uint32_t,uint32_t)override{return false;}
    bool start(uint32_t,const std::array<uint32_t,4>&)override{return false;}
    bool configure_audio(uint32_t operation,uint32_t argument)override{
        constexpr std::array<std::array<uint32_t,2>,5> sequence{{{0x130,0},{0x128,0x100},{0x140,0x40004000},{0x138,0},{0x118,0x281}}};
        if(audioStage_>=sequence.size()||sequence[audioStage_]!=std::array<uint32_t,2>{operation,argument})return false;
        ++audioStage_;return true;
    }
    bool install_interrupt(uint32_t interrupt,uint32_t handler)override{return audioStage_==5&&interrupt==4&&handler==0x80022D60;}
    bool firmware_version(uint32_t& version,uint32_t& build)override{version=0x10002;build=0x3B0000;return true;}
    bool shutdown_media_processor()override{return true;}
    bool invoke_constructor(uint32_t address)override{return address==0x80010160&&initialize_driver_firmware_compatibility(*this);}
private:
    NativeFrameMemory& memory_;uint32_t stack_;unsigned audioStage_=0;bool dmaOwned_=false;
};
}
