#pragma once
#include "TempestMenuInput.h"
namespace tempest_native {
inline bool driver_held_axis(CommandMemory& m,uint32_t negative,uint32_t positive,uint32_t& result){uint32_t buttons;if(!m.read32(0x806B992C,buttons))return false;result=(buttons&negative)?0xFFFFFFFF:(buttons&positive)?1:0;return true;}
inline bool driver_button_edge(CommandMemory& m,uint32_t latch,uint32_t pressMask,uint32_t releaseMask,uint32_t& result){
    uint32_t held,buttons;if(!m.read32(latch,held)||!m.read32(0x806B992C,buttons))return false;
    if(held){result=0;return m.write32(latch,buttons&releaseMask);}result=(buttons&pressMask)?1:0;return !result||m.write32(latch,releaseMask);
}
// 80017DC6..80017EBA: unlike poll_driver_menu_axis, there is no UI sound and
// no analog conversion. Audio-test callers perform their own analog OR first.
inline bool driver_raw_horizontal(CommandMemory& m,uint32_t latch,uint32_t releaseMask,uint32_t& result){
    uint32_t port,table,config,phase,buttons,held;
    if(!m.read32(0x801133E8,port)||!m.read32(0x806B9910,table)||!m.read32(table+port*16,config))return false;
    if(config&0x1000){if(!m.read32(0x801133B8,phase))return false;if(phase&64){result=0;return m.write32(latch,0);}
        if(!m.read32(0x806B992C,buttons))return false;
        if(buttons&0x1000){if(!m.read32(latch,held))return false;result=0;if(held)return true;uint32_t last;if(!m.read32(0x801133B4,last)||!m.write32(latch,1))return false;result=int32_t(last)<0?0xFFFFFFFF:1;return true;}}
    if(!m.read32(latch,held)||!m.read32(0x806B992C,buttons))return false;
    if(held){result=0;return m.write32(latch,buttons&releaseMask);}result=(buttons&0x400)?0xFFFFFFFF:(buttons&0x100)?1:0;
    return !result||m.write32(latch,1);
}
}
