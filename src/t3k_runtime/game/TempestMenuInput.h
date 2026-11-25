#pragma once
#include "TempestDriver.h"
namespace tempest_native {
// 80015602 / 80015652: UI sample lookup, fixed volume, optional explicit pitch.
inline bool play_driver_ui_sound(CommandMemory& m,DriverSound& sound,uint32_t index,uint32_t& handle,uint32_t pitch=0x1000){
    uint32_t resource,word;if(!m.read32(0x80115B10+index*8,resource)||!m.read32(0x80115B14+index*8,word))return false;
    return sound.play_request({resource,pitch,word,0x3F000000,0,0},handle);
}
enum class MenuAxis{vertical,horizontal};
// Complete 80017A04 and 80017C28. Preserve the rotary modifier distinction,
// phase-bit latch reset, signed analog thresholds, release mask and UI sounds.
inline bool poll_driver_menu_axis(CommandMemory& m,DriverSound& sound,uint32_t latch,uint32_t releaseMask,MenuAxis axis,uint32_t& result){
    uint32_t first,base,config,buttons;
    if(!m.read32(0x801133E8,first)||!m.read32(0x806B9910,base)||!m.read32(base+first*16,config)||!m.read32(0x806B992C,buttons))return false;
    bool rotary=(config&0x1000)&&((axis==MenuAxis::horizontal)==bool(buttons&0x1000));
    auto action=[&](uint32_t value,uint32_t cue){uint32_t handle;if(!m.write32(latch,1)||!play_driver_ui_sound(m,sound,cue,handle))return false;result=value;return true;};
    if(config&0x1000){
        // Horizontal checks phase before its modifier, unlike vertical.
        if(axis==MenuAxis::horizontal||rotary){uint32_t phase;if(!m.read32(0x801133B8,phase))return false;if(phase&64){result=0;return m.write32(latch,0);}}
    }
    if(rotary){uint32_t held,last;if(!m.read32(latch,held))return false;if(held){result=0;return true;}if(!m.read32(0x801133B4,last))return false;return action(int32_t(last)<0?0xFFFFFFFF:1,23);}
    uint32_t active,port;if(!m.read32(0x801133F0,active)||!m.read32(0x801133E8+active*4,port)||!m.read32(0x806B9910,base)||!m.read32(base+port*16,config))return false;
    uint32_t negative=axis==MenuAxis::vertical?0x200:0x400,positive=axis==MenuAxis::vertical?0x800:0x100;
    if(config&16){uint8_t raw;if(!m.read8(base+port*16+(axis==MenuAxis::vertical?7:6),raw))return false;int32_t position=int8_t(raw);
        if(axis==MenuAxis::vertical){if(position>40)buttons|=negative;if(position<-40)buttons|=positive;}
        else{if(position<-40)buttons|=negative;if(position>40)buttons|=positive;}
    }
    uint32_t held;if(!m.read32(latch,held))return false;
    if(held){result=0;return m.write32(latch,buttons&releaseMask);}
    if(buttons&negative)return action(0xFFFFFFFF,9);
    if(buttons&positive)return action(1,9);
    result=0;return true;
}
// Complete 80017B9E..80017C26: Start/NUON edge selection with the same latch.
inline bool poll_driver_menu_action(CommandMemory& m,DriverSound& sound,uint32_t latch,uint32_t releaseMask,uint32_t& result){
    uint32_t held,buttons;if(!m.read32(latch,held)||!m.read32(0x806B992C,buttons))return false;
    if(held){result=0;return m.write32(latch,buttons&releaseMask);}
    uint32_t handle;if(buttons&0x2000){if(!m.write32(latch,1)||!play_driver_ui_sound(m,sound,9,handle))return false;result=0xFFFFFFFF;return true;}
    if(buttons&0x1000){if(!m.write32(latch,1)||!play_driver_ui_sound(m,sound,9,handle))return false;result=1;return true;}
    result=0;return true;
}
}
