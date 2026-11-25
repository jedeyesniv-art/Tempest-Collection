#pragma once
#include "native_frame_memory.h"
#include "game/TempestBootEntry.h"
namespace tempest_native {
// A port is the original 16-byte table record, not a fabricated CPU register
// frame. The host supplies actual device properties/buttons/axes. Slot zero
// holds remote input; slots one through eight hold controller input.
struct NativeControllerPort {std::array<uint32_t,4> words{};};
class NativeControllers final:public BootControllerServices {
public:
    static constexpr uint32_t table=0x807FFF70;
    NativeControllers(CommandMemory& memory,const std::array<NativeControllerPort,9>& initial):memory_(memory),initial_(initial){}
    bool initialize_controllers(uint32_t& address)override{
        for(unsigned port=0;port<9;++port)if(!publish(port,initial_[port]))return false;
        address=table;return true;
    }
    bool publish(unsigned port,const NativeControllerPort& state){
        if(port>=9)return false;
        for(unsigned i=0;i<4;++i)if(!memory_.write32(table+port*16+i*4,state.words[i]))return false;
        return true;
    }
private:
    CommandMemory& memory_;std::array<NativeControllerPort,9> initial_;
};
}
