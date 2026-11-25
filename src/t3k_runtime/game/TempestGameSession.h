#pragma once
#include "TempestSessionDeath.h"
#include "TempestSessionLevelClear.h"
namespace tempest_native {
// Original 8001EE04..8001EF06: score handoff, surviving second-player routing,
// and complete normal session return. It does not manufacture a replay/menu.
inline bool finish_game_session(CommandMemory& m,GameSessionServices& services,GameSessionState& state){
    if(state.phase!=GameSessionPhase::sessionEnding)return false;auto c=state.context;uint32_t base=state.frameBase,player,value,flags;
    if(!c[4]){state.phase=GameSessionPhase::nextLevel;return true;}
    if(!m.read32(base-0x390,player)||player>1||!m.read32(0x800DC240,value)||!m.write32(base-0x34C+player*4,value)||!m.read32(0x800DC274,flags))return false;
    if(!(flags&1)){c[15]=base-0x378;if(!services.high_score_entry(0x8010BF80,18,base-0x34C,player,c))return false;c[17]=0x8001EE80;}
    if(!m.read32(base-0x378,c[15]))return false;
    if(c[15]){if(!m.read32(base-0x344+(player^1)*4,value))return false;if(int32_t(value)>0){state.context=c;state.phase=GameSessionPhase::retry;return true;}}
    if(!m.write32(0x800B850C,0)||!m.write32(0x801133F0,0))return false;state.context=c;state.phase=GameSessionPhase::finished;return true;
}
// One original semantic phase, shared by the continuous runner and the
// collection's serializable outer driver. This does not subdivide a logic tick.
inline bool step_game_session(CommandMemory& m,GameSessionServices& services,GameSessionState& state){
    switch(state.phase){
        case GameSessionPhase::levelPrepared:if(!enter_session_play(m,services,state))return false;break;
        case GameSessionPhase::playing:case GameSessionPhase::active:if(!step_session_gameplay(m,services,state))return false;break;
        case GameSessionPhase::lifeEnded:if(!settle_session_life(m,services,state))return false;break;
        case GameSessionPhase::lifeResolved:{uint32_t reason;if(!m.read32(0x800BA2F8,reason))return false;
            if(!reason){if(!resolve_session_level_clear(m,services,state))return false;}
            else if(reason==1||reason==2){if(!resolve_session_death(m,services,state))return false;}
            else{if(!m.write32(0x80113448,0xFFFFFFFF))return false;state.phase=state.context[4]?GameSessionPhase::sessionEnding:GameSessionPhase::nextLevel;}
            break;}
        case GameSessionPhase::retry:case GameSessionPhase::nextLevel:if(!prepare_session_player(m,services,state))return false;break;
        case GameSessionPhase::sessionEnding:if(!finish_game_session(m,services,state))return false;break;
        default:return false;
        }
    return true;
}
// Full original 8001D0A2..8001EF06, decomposed at validated semantic phases.
// A caller can give a bounded budget for diagnostics; the runtime can retain
// the original continuous session while host cancellation remains explicit.
inline bool run_game_session(CommandMemory& m,const GameSessionInput& input,GameSessionServices& services,uint32_t& result,unsigned budget=0xFFFFFFFF){
    GameSessionState state;if(!begin_game_session(m,input,services,state))return false;
    while(state.phase!=GameSessionPhase::finished){if(!budget--)return false;
        if(!step_game_session(m,services,state))return false;
    }
    result=state.context[9];return true;
}
}
