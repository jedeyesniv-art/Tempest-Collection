#pragma once
#include "game/TempestGraphicsDispatch.h"
#include "game/TempestBitmapCopy32.h"
#include <string>
namespace tempest_native {
// CPU SDK drawing is separate from gameplay display-list rendering. Supported
// branches use native memory algorithms, never an original instruction runner.
class NativeSdkDrawing final:public GraphicsDispatchServices {
public:
    explicit NativeSdkDrawing(CommandMemory& memory):memory_(memory){}
    bool invoke_cpu(uint32_t function,uint32_t scratch,uint32_t parameters,uint32_t,uint32_t)override{
        if(function!=0x806B3960){failure_="unsupported SDK drawing function "+std::to_string(function);return false;}
        if(scratch&0x4000){failure_="SDK parameter-copy scratch mode is not implemented";return false;}
        uint32_t status;
        if(!copy_bitmap32_unscaled(memory_,parameters,status)){failure_="unsupported or invalid 32-bit bitmap request";return false;}
        failure_.clear();return status==0;
    }
    bool append_sequence(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t&)override{failure_="SDK drawing sequences are not implemented";return false;}
    const std::string& failure()const{return failure_;}
private:
    CommandMemory& memory_;std::string failure_;
};
}
