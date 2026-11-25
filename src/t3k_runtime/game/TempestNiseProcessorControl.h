#pragma once
#include "TempestCommands.h"
#include <array>
namespace tempest_native {
struct NiseProcessorControlServices {
    virtual bool enable_audio_interrupt()=0;
    virtual bool acknowledge_pause()=0;
    virtual ~NiseProcessorControlServices()=default;
};
// Original processor message handler 203015D4..2030161A. Communication delivery
// supplies four DATA words. This is native protocol handling, not instruction
// execution or a saved processor continuation.
inline bool handle_nise_processor_message(CommandMemory& local,NiseProcessorControlServices& services,
                                          uint32_t type,const std::array<uint32_t,4>& message){
    if(type==0xA0){
        for(unsigned i=0;i<4;++i)if(!local.write32(0x20101010+i*4,message[i]))return false;
        return services.enable_audio_interrupt()&&local.write32(0x20101034,0);
    }
    if(type!=0xAF)return true;
    if(message[0]&&!services.acknowledge_pause())return false;
    return local.write32(0x2010102C,message[0]?1:0);
}
}
