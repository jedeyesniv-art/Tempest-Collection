#pragma once
#include "TempestSession.h"
namespace tempest_native {
struct GameOverPresentationInput {uint32_t player,score;OpeningAnimationInput caller;};
// Complete original 800175B4..800177C4: per-player score text, scene 10,
// original button-skip/fade/pulse loop, and the final transition.
inline bool run_session_game_over(CommandMemory& m,const GameOverPresentationInput& input,GameSessionServices& services,unsigned budget=640){
    SceneTimingScope cadence(services,SceneTimingKind::gameOver);if(!cadence.divisions)return false;uint64_t remainingBudget=cadence.budget(budget);
    uint32_t stack=input.caller.incoming[19];if(input.player>1||(stack&15)||stack<80)return false;
    auto c=input.caller.incoming;c[16]=input.player;c[1]=input.score;c[0]=0x800B6BB0;c[17]=input.caller.returnAddress;c[18]=stack-16;c[19]=stack-80;
    if(!m.write32(0x800B6BB4,0x00200020)||!services.transition({0x3400,0x10808000,0x10400,0,1},c))return false;c[17]=0x80017600;c[16]=0x8010C0C0+input.player*4;
    if(!m.read32(c[16],c[0])||!m.write32(0x800B6BC4,c[0]))return false;
    const uint8_t format[]{'%', 'u',0};for(unsigned i=0;i<3;++i){uint8_t v;if(!m.read8(0x806A25B4+i,v)||v!=format[i])return false;}
    char number[11];auto converted=std::to_chars(number,number+11,input.score);if(converted.ec!=std::errc())return false;unsigned length=unsigned(converted.ptr-number);
    for(unsigned i=0;i<length;++i)if(!write_session_byte(m,0x8003C788+i,uint8_t(number[length-1-i])))return false;
    c[16]=29;for(unsigned i=0;i<length;++i){if(!write_session_byte(m,c[0]+c[16],uint8_t(number[i])))return false;c[16]+=unsigned(cadence.step().legacy_boundary());}
    c[17]=c[0]+c[16];if(!write_session_byte(m,c[17],0)||!services.initialize_scene({10,c}))return false;c[17]=0x800176FE;
    c[16]=0;c[3]=63;c[2]=0x800B84B0;c[1]=0x806B992C;c[0]=0xFF3F;
    do{uint32_t buttons,fade;
        if(!m.write32(0x800B84B0,0x2BE0+(((c[16]&63)<<5)+uint32_t(cadence.step().index*32/cadence.step().divisions)))||!m.read32(0x806B992C,buttons))return false;if(buttons&0xFF3F)c[16]=600;
        if(!m.read32(0x800B6A94,fade)||!m.write32(0x800B6A94,int32_t(fade)>0?fade-uint32_t(cadence.delta(0x800,0)):0))return false;c[16]+=unsigned(cadence.step().legacy_boundary());
        if(!remainingBudget--||!services.menu_frame(c))return false;c[17]=0x80017772;
    }while(c[16]<=599);
    if(!m.write32(0x800B6BB4,0x0020001C)||!services.transition({0x3400,0x10808000,0x10800,0,20},c))return false;
    return true;
}
}
