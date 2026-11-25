#pragma once
#include "TempestSessionUtilities.h"
#include "TempestSelectionMenu.h"
namespace tempest_native {
// 8001A24E..8001A2C2. The level is saved before the RNG call (delay slot).
inline bool choose_session_spawn(CommandMemory& m,uint32_t level,uint32_t& kind){
    uint32_t random;if(!next_driver_random(m,random))return false;uint32_t sample=(random>>12)&255,table=0x80114F4C+(level&127)*16;
    for(unsigned i=0;i<8;++i){uint8_t threshold;if(!m.read8(table+i,threshold))return false;if(sample<=threshold){uint8_t value;if(!m.read8(table+8+i,value))return false;kind=value;return true;}}
    kind=17;return true;
}
// 8001A2C4..8001A398. Schedule the next original object command; this does not
// create replacement enemies or bypass their native command/callback lifecycle.
template<class Services> inline bool prepare_session_spawn(CommandMemory& m,uint32_t threshold,uint32_t level,Services& services){
    uint32_t pending,total,remaining;if(!m.read32(0x800BA308,pending))return false;if(pending)return true;
    if(!m.read32(0x800BA300,total)||!m.read32(0x800BA304,remaining))return false;if(int32_t(total-remaining)>int32_t(threshold))return true;
    uint32_t kind,current;if(!choose_session_spawn(m,level,kind)||!m.read32(0x801095A0,current))return false;
    if(int32_t(current)>255&&kind==17)kind=37;
    if(kind==37&&!services.play_sound(42,0x1000))return false;
    unsigned index=0;for(;index<171;++index){uint32_t value;if(!m.read32(0x8011352C+index*4,value))return false;if(value==kind)break;}
    uint32_t command;if(index==171||!m.write32(0x800BA308,kind)||!m.read32(0x8010DB7C+index*4,command)||!m.write32(0x800DC2C8,command))return false;
    return true;
}
}
