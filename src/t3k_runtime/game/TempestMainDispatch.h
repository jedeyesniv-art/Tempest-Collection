#pragma once
#include "TempestBootMenuEntry.h"
namespace tempest_native {
enum class MainDispatchDestination:uint32_t {stateLoop,openingLoop,firmwareExit};
struct MainDispatchResult {MainDispatchDestination destination;uint32_t state;std::array<uint32_t,20> handoff;};
struct MainDispatchServices {
    virtual bool exit_process(uint32_t status,bool& returned)=0;
    virtual bool game_session(const std::array<uint32_t,4>& arguments,const std::array<uint32_t,20>& incoming,uint32_t& result)=0;
    virtual bool transition(const DriverTransitionInput&,const std::array<uint32_t,20>&)=0;
    virtual ~MainDispatchServices()=default;
};
// C-main continuation after 80020AE6's result assignment: 80020AE8..80020B40
// plus shared tail 80020E28..80020E7E. Stop at the next state/opening iteration
// or a non-returning firmware exit. Missing session/transition calls must fail.
inline bool route_primary_menu_result(CommandMemory& m,const std::array<uint32_t,20>& incoming,MainDispatchServices& services,MainDispatchResult& output){
    auto context=incoming;uint32_t selected=context[0],state;
    if(selected==2){bool returned;context[7]=0x800000F0;if(!services.exit_process(context[16],returned))return false;
        if(!returned){output={MainDispatchDestination::firmwareExit,0,context};return true;}}
    if(int32_t(selected)>-1){
        if(!m.write32(0x801133F4,selected+1)||!m.write32(0x800DC270,0))return false;
    }else{
        if(!m.write32(0x801133F4,0xFFFFFFFF)||!m.write32(0x800DC274,1))return false;
        uint32_t result;context[7]=0x8001D0A2;if(!services.game_session({0,0,0,0},context,result))return false;context[17]=0x80020B34;
    }
    context[17]=0x801133F4;if(!m.read32(0x801133F4,state))return false;context[7]=state;
    if(int32_t(state)>-1){output={MainDispatchDestination::stateLoop,state,context};return true;}
    context[7]=0x80016BE6;if(!services.transition({0x3E00,0x10808000,0x10200,0,60},context))return false;
    context[17]=0x80020E70;output={MainDispatchDestination::openingLoop,state,context};return true;
}
}
