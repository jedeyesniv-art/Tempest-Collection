#pragma once
#include "TempestMainDispatch.h"
namespace tempest_native {
struct MainContinuationServices {
    virtual bool select_music(uint32_t)=0;
    virtual bool show_asset(uint32_t,uint32_t,const std::array<uint32_t,20>&)=0;
    virtual ~MainContinuationServices()=default;
};
// Original 80020BEE..80020C4E and shared 80020E32..80020E46.
inline bool continue_after_session(CommandMemory& m,const std::array<uint32_t,20>& incoming,uint32_t result,MainContinuationServices& services,std::array<uint32_t,20>& output){
    auto c=incoming;c[7]=0x80015734;c[16]=result;
    if(!services.select_music(0))return false;c[17]=0x80020BFC;c[16]=(result-1)&~1U;
    c[5]=0;if(int32_t(c[16])>int32_t(c[3]))c[3]=c[16];
    if(int32_t(c[16])>0)c[1]=c[16];
    uint32_t skip;if(!m.read32(0x801133C8,skip))return false;
    if(!skip){c[7]=0x8001C7E6;if(!services.show_asset(0x8010C3A0,16,c))return false;c[17]=0x80020C44;}
    if(!m.write32(0x801133C8,0))return false;c[17]=0x801133F4;if(!m.read32(0x801133F4,c[7]))return false;output=c;return true;
}
}
