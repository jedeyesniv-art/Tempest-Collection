#pragma once
#include "TempestSessionGameplay.h"
namespace tempest_native {
// Complete 8001A502..8001A5A6. The game-over presentation remains a required
// dependency, with the original helper's inherited scene/frame data.
inline bool notify_session_life_lost(CommandMemory& m,GameSessionServices& services,uint32_t lives,uint32_t player,const std::array<uint32_t,20>& incoming){
    uint32_t flags;if(!m.read32(0x800DC244,flags)||!m.write32(0x800DC244,flags|0x4000000))return false;
    if(lives){uint32_t random,cue;if(!next_driver_random(m,random)||!m.read32(0x80113828+(random&3)*4,cue))return false;return services.play_sound(cue,0x1000);}
    if(!services.play_sound(44,0x1000)||!services.play_sound(44,0xF80))return false;
    uint32_t score;if(!m.read32(0x800DC240,score))return false;auto c=incoming;c[0]=player;c[1]=0x800DC240;c[16]=0x80015652;c[17]=0x800175B4;c[18]=incoming[19]-16;c[19]=incoming[19]-32;
    return services.game_over_banner(player,score,c);
}
// Original outcome dispatch at 8001E480 and both death branches through
// 8001EE04. Clear-level/warp handling is a distinct original branch.
inline bool resolve_session_death(CommandMemory& m,GameSessionServices& services,GameSessionState& state,unsigned budget=4096){
    if(state.phase!=GameSessionPhase::lifeResolved)return false;auto c=state.context;uint32_t base=state.frameBase,reason,value,flags,buttons;
    if(!m.read32(0x800BA2F8,reason)||(reason!=1&&reason!=2))return false;
    auto write=[&](uint32_t address,uint32_t v){return m.write32(address,v);};
    auto finish=[&](){if(!write(0x800B850C,0)||!write(0x801133F0,0))return false;state.context=c;state.phase=GameSessionPhase::finished;return true;};
    auto cancelled=[&](bool& result){if(!m.read32(0x806B992C,buttons)||!m.read32(0x800DC274,flags))return false;
        if(buttons&0xFF3F){c[15]=flags;flags|=2;if(!write(0x800DC274,flags))return false;}
        if((flags&3)==3&&!write(0x801133C8,1))return false;
        if(!m.read32(0x801133C8,value))return false;result=bool(value);return true;};
    auto frame=[&](uint32_t link){if(!budget--||!services.gameplay_frame(c))return false;c[17]=link;return true;};
    if(!write(0x80113448,0xFFFFFFFF))return false;
    if(reason==1){c[15]=0x80015602;if(!services.play_sound(41,0x1000))return false;c[17]=0x8001EB90;
        if(!m.read32(0x800DC274,flags)||!write(0x800DC274,flags|8)||!m.read32(0x800BA2E4,c[2])||!m.read32(0x800BA2EC,c[4]))return false;
        if(int32_t(c[4])<=0x4FFFFFF){c[5]=0x806B992C;c[3]=0xFF3F;c[16]=0x800DC274;c[1]=0x801133C8;c[0]=0x800B9778;
            do{bool quit;if(!cancelled(quit))return false;if(quit)return finish();uint32_t increment;
                if(!write(0x800BA2EC,c[4])||!m.read32(0x800B9778,value)||!m.read32(base-0x380,increment)||!write(0x800B9778,value-increment)||!write(c[2]+0x50,c[4]))return false;c[15]=increment;
                if(!services.consume_sounds())return false;c[17]=0x8001EC56;c[4]+=0x100000;
                if(!frame(0x8001EC68))return false;
            }while(int32_t(c[4])<=0x4FFFFFF);
        }
        c[15]=0;if(!write(0x800B850C,0))return false;--c[11];uint32_t player;if(!m.read32(base-0x390,player)||!notify_session_life_lost(m,services,c[11],player,c))return false;c[17]=0x8001EC9A;
        if(int32_t(c[11])<=0){state.context=c;state.phase=GameSessionPhase::sessionEnding;return true;}c[15]=0x800DC274;
    }else{
        c[16]=0;if(!m.read32(0x800DC274,flags)||!write(0x800DC274,flags|8)||!write(0x800B850C,0))return false;
        c[4]=0;c[3]=0x806B992C;c[2]=0xFF3F;c[0]=0x800DC274;c[1]=0x801133C8;
        do{bool quit;if(!cancelled(quit))return false;if(quit)return finish();uint32_t increment;
            if(!write(0x800DC0E8,c[16])||!m.read32(0x800B9778,value)||!m.read32(base-0x380,increment)||!write(0x800B9778,value-increment)||!m.read32(0x800BA2EC,value)||!write(0x800BA2EC,value-0x40000)||!m.read32(0x800B907C,c[15])||!write(0x800B907C,c[15]+0xC0000))return false;
            if(c[4]==40&&!write(0x800B84CC,0x8010F1F4))return false;
            if(int32_t(c[4])>40)c[16]+=0x800;
            if(!services.consume_sounds())return false;c[17]=0x8001EDB6;++c[4];if(!frame(0x8001EDC0))return false;
        }while(int32_t(c[4])<=69);
        --c[11];uint32_t player;if(!m.read32(base-0x390,player))return false;c[15]=player;
        if(!notify_session_life_lost(m,services,c[11],player,c))return false;c[17]=0x8001EDE8;
        if(int32_t(c[11])<=0){state.context=c;state.phase=GameSessionPhase::sessionEnding;return true;}
    }
    if(!m.read32(0x800DC274,flags))return false;state.context=c;state.phase=(flags&1)?GameSessionPhase::sessionEnding:GameSessionPhase::retry;return true;
}
}
