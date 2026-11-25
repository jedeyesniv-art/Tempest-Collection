#pragma once
#include "TempestLevelSelection.h"
namespace tempest_native {
struct SessionStartRequest {std::array<uint32_t,4> arguments;std::array<uint32_t,20> incoming;};
struct SessionEntryServices {
    virtual bool level_selection(const LevelSelectionInput&,uint32_t& result)=0;
    virtual ~SessionEntryServices()=default;
};
// Supported C-main branch 80020B7C..80020BE6: setup choices 0/1. Password and
// cancellation routes are separate caller branches and are explicitly rejected.
inline bool prepare_session_entry(CommandMemory& m,const std::array<uint32_t,20>& incoming,SessionEntryServices& services,SessionStartRequest& output){
    if(incoming[0]>1)return false;auto c=incoming;
    if(!m.write32(0x800DC270,c[16]))return false;c[17]=0x801133F4;c[7]=0x8001CA1A;uint32_t level;
    if(!services.level_selection({c[1],c[6],c[3],{c,0x80020BC8}},level)||!m.write32(0x80113448,1))return false;
    c[2]=level;c[17]=0x80020BC8;c[7]=0x8001D0A2;output={{level,level*0x494C,c[6],c[0]},c};return true;
}
}
