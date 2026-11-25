#pragma once
#include "native_menu_flow.h"
#include "game/TempestBootDriver.h"
#include "game/TempestMainDispatch.h"
#include "game/TempestGameSetupEntry.h"
#include "game/TempestSessionEntry.h"
#include "game/TempestPassword.h"
#include "game/TempestMainContinuation.h"
#include "game/TempestOptions.h"
#include "game/TempestGameSession.h"
// Serializable state belongs to the host driver. Original game functions still
// own every semantic operation; no native C++ stack is written into a save.
namespace tempest_native {
enum class CollectionPhase:uint32_t {opening,selection,setup,password,sessionEntry,sessionBegin,sessionStep,continuation,options,transition,finished,invalid};
struct CollectionDriverState {
    CollectionPhase phase=CollectionPhase::opening;
    std::array<uint32_t,20> c{};
    uint32_t mainState=0,sessionResult=0;
    bool firstOpening=true;
    SessionStartRequest request{};
    GameSessionState session{};
};
inline CollectionDriverState initial_collection_driver(uint32_t stack){
    CollectionDriverState s;auto& c=s.c;
    c[1]=0x80000388;c[2]=2;c[3]=14;c[4]=0xFFFFFFFF;c[7]=0x800177C6;
    c[16]=0x800B1700;c[18]=stack-16;c[19]=stack-0x1C0;return s;
}
inline void collection_route_state(CollectionDriverState& s){
    s.phase=s.mainState==0?CollectionPhase::selection:s.mainState==1?CollectionPhase::setup:
        s.mainState==2?CollectionPhase::options:int32_t(s.mainState)<0?CollectionPhase::transition:CollectionPhase::invalid;
}
template<class Calls> bool step_collection_driver(NativeFrameMemory& m,NativeMenuFlow& flow,Calls& calls,CollectionDriverState& s){
    auto& c=s.c;
    auto route=[&](const MainDispatchResult& r){
        c=r.handoff;s.mainState=r.state;
        if(r.destination==MainDispatchDestination::firmwareExit)s.phase=CollectionPhase::finished;
        else if(r.destination==MainDispatchDestination::openingLoop)s.phase=CollectionPhase::opening;
        else collection_route_state(s);
    };
    switch(s.phase){
    case CollectionPhase::opening:{
        BootMenuEntryResult entry;calls.last=c;
        if(!enter_boot_menu(m,c,calls,entry,s.firstOpening))return false;
        s.firstOpening=false;c=entry.handoff;MainDispatchResult r;
        if(!route_primary_menu_result(m,c,calls,r))return false;route(r);return true;
    }
    case CollectionPhase::selection:{
        uint32_t choice;c[16]=0;c[7]=0x8001817C;
        if(!m.write32(0x801133CC,0)||!calls.selection({0x8010C8C0,2,0x8010C170,{c,0x80020AE6}},choice))return false;
        c[0]=choice;c[17]=0x80020AE6;MainDispatchResult r;
        if(!route_primary_menu_result(m,c,calls,r))return false;route(r);return true;
    }
    case CollectionPhase::setup:{
        c[17]=0x801133F4;GameSetupEntryResult setup;
        if(!enter_game_setup(m,c,calls,setup))return false;c=setup.handoff;
        if(int32_t(setup.selection)<0){s.mainState=0;c[17]=0x801133F4;c[7]=0;s.phase=CollectionPhase::selection;}
        else s.phase=setup.selection==2?CollectionPhase::password:CollectionPhase::sessionEntry;
        return true;
    }
    case CollectionPhase::password:{
        uint32_t password;c[7]=0x8001B0DC;
        if(!m.write32(0x800DC270,c[16])||!run_level_password(m,{c,0x80020B92},flow,password))return false;
        c[17]=0x801133F4;c[16]=password;
        if(int32_t(password)>int32_t(c[3])){c[3]=password;s.mainState=1;if(!m.write32(0x801133F4,1))return false;}
        else s.mainState=0;
        c[0]=0xFFFFFFFF;collection_route_state(s);return true;
    }
    case CollectionPhase::sessionEntry:
        if(!prepare_session_entry(m,c,calls,s.request))return false;s.phase=CollectionPhase::sessionBegin;return true;
    case CollectionPhase::sessionBegin:
        calls.prepare_session_input();s.session={};
        if(!begin_game_session(m,{s.request.arguments,{s.request.incoming,s.request.incoming[17]==0x80020BC8?0x80020BEEU:0x80020B34U}},flow,s.session))return false;
        s.phase=CollectionPhase::sessionStep;return true;
    case CollectionPhase::sessionStep:
        if(!step_game_session(m,flow,s.session))return false;
        if(s.session.phase==GameSessionPhase::finished){s.sessionResult=s.session.context[9];s.phase=CollectionPhase::continuation;}
        return true;
    case CollectionPhase::continuation:
        if(!continue_after_session(m,s.request.incoming,s.sessionResult,calls,c)||!m.read32(0x801133F4,s.mainState))return false;
        collection_route_state(s);return true;
    case CollectionPhase::options:
        if(!run_original_options(m,c,calls,c)||!m.read32(0x801133F4,s.mainState))return false;
        collection_route_state(s);return true;
    case CollectionPhase::transition:
        c[7]=0x80016BE6;if(!flow.transition({0x3E00,0x10808000,0x10200,0,60},c))return false;
        c[17]=0x80020E70;s.phase=CollectionPhase::opening;return true;
    case CollectionPhase::finished:return true;
    case CollectionPhase::invalid:return false;
    }
    return false;
}
}
