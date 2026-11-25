#pragma once
#include "TempestSessionGameplay.h"
namespace tempest_native {
// Original clear-level outcome 8001E4C0..8001EB78, including interrupted warp,
// both transition animations, bonus/skip callers and initial-score accounting.
inline bool resolve_session_level_clear(CommandMemory& m,GameSessionServices& services,GameSessionState& state,unsigned budget=4096){
    if(state.phase!=GameSessionPhase::lifeResolved)return false;auto c=state.context;uint32_t base=state.frameBase,value,flags,buttons,random,reason;
    if(!m.read32(0x800BA2F8,reason)||reason)return false;
    auto write=[&](uint32_t a,uint32_t v){return m.write32(a,v);};
    auto load=[&](uint32_t offset,uint32_t& v){return m.read32(base-offset,v);};
    auto finish=[&](){if(!write(0x800B850C,0)||!write(0x801133F0,0))return false;state.context=c;state.phase=GameSessionPhase::finished;return true;};
    auto frame=[&](uint32_t link){if(!budget--||!services.gameplay_frame(c))return false;c[17]=link;return true;};
    auto blend=[&](uint32_t link){if(!blend_session_parameters(m,c[5],base-0x200,base-0x300,base-0x100))return false;c[17]=link;return true;};
    auto marked_input=[&](){if(!m.read32(0x806B992C,buttons)||!m.read32(0x800DC274,flags))return false;
        if(buttons&0xFF3F){flags|=2;if(!write(0x800DC274,flags))return false;}if((flags&3)==3&&!write(0x801133C8,1))return false;return true;};
    auto random_warp_sound=[&](unsigned modulus,uint32_t table,uint32_t link){
        if(!m.read32(0x800B18C8,value))return false;if(!(value&2))return true;
        c[0]=c[1]&(modulus-1);if(!c[0]){if(!next_driver_random(m,random)||!m.read32(0x80113410+(random&3)*4,c[16]))return false;
            SoundRequest sound{};uint32_t handle;if(!m.read32(table+(c[16]&63)*8,sound.resource)||!m.read32(table+(c[16]&63)*8+4,sound.resourceWord))return false;
            sound.argument1=0x1000+(c[1]<<3);sound.argument3=0x5F000000;if(!services.play_request(sound,handle))return false;c[17]=link;}
        ++c[1];return true;
    };
    if(!write(0x800B18C8,1)||!m.read32(0x800DC274,flags))return false;
    if(c[9]-4<=11){flags|=64;if(!write(0x800DC274,flags))return false;}
    if(!write(0x80113448,0)||!services.play_sound(1,0x1000))return false;c[17]=0x8001E500;c[16]=0x80015652;
    if(!services.play_sound(38,0x400)||!services.play_sound(38,0x410))return false;c[17]=0x8001E51E;++c[9];c[15]=c[4]=c[3]=c[2]=c[1]=0;
    if(!m.read32(0x800BA2F8,value))return false;
    if(!value){c[8]=0x800DC274;c[12]=0x801133C8;c[10]=0x80115B10;c[7]=base-0x100;
        if(!load(0x398,value))return false;c[6]=uint32_t(int32_t(int16_t(value)));c[14]=0x800DC104;
        do{
            if(!marked_input()||!random_warp_sound(8,0x80115B10,0x8001E62E))return false;
            c[2]+=0x10000;c[3]-=c[2];c[5]+=0x100;c[15]=0x8001A5A8;
            if(!blend(0x8001E660)||!apply_menu_parameters(m,base-0x100,c[13]))return false;c[17]=0x8001E66E;
            if(!write(0x800B907C,(c[6]<<16)+c[3]))return false;c[16]=0x800BA2D0;
            if(!write(0x800BA2EC,0x1000000-c[3])||!m.read32(0x800DC100,value)||!write(0x800DC248,value)||!m.read32(0x800DC104,value)||!write(0x800DC24C,value)||!m.read32(0x801133C8,c[15]))return false;
            if(c[15])return finish();if(!frame(0x8001E6D6)||!m.read32(0x800BA2F8,value))return false;
            if(int32_t(c[3])<=int32_t(0xFC000000))break;
        }while(!value);
    }
    if(!m.read32(0x800BA2F8,value))return false;
    if(value){--c[9];c[15]=0;if(!write(0x800B850C,0))return false;state.context=c;state.phase=GameSessionPhase::lifeResolved;return true;}
    c[15]=16;if(!write(0x800B4E80,0x00B40078)||!services.initialize_scene({7,c}))return false;c[17]=0x8001E740;c[15]=0;
    if(!write(0x800DC114,0)||!write(0x800DC118,0)||!write(0x800DC11C,0x2000)||!write(0x800DC120,0x2000))return false;
    if(int32_t(c[5])<=65535){c[3]=0x800DC274;c[7]=0x801133C8;c[2]=base-0x100;c[6]=0x80115B10;
        do{
            if(!marked_input()||!m.read32(0x800BA088,value))return false;c[15]=8;
            if(int32_t(value)<=0x3EFF)value+=0x100;if(!write(0x800BA088,value))return false;
            c[5]=int32_t(c[5])>65535?0x10000:c[5]+0x200;
            if(!blend(0x8001E820)||!apply_menu_parameters(m,base-0x100,c[13]))return false;c[17]=0x8001E82E;
            if(!random_warp_sound(16,0x80115B10,0x8001E8A2)||!m.read32(0x801133C8,value))return false;if(value)return finish();c[15]=0x80016704;
            if(!frame(0x8001E8C0))return false;
        }while(int32_t(c[5])<=65535);
    }
    if(!services.transition({0x3FF0,0x10808000,0x12600,0,40},c))return false;c[17]=0x8001E8FC;c[15]=0x80015500;
    if(!write(0x800B4E80,0x01380030)||!next_driver_random(m,random))return false;c[17]=0x8001E91A;c[16]=(random&0x7FF)|0x200;c[17]=0x800B9774;
    if(!write(0x800B9774,c[16]|0xFF000000)||!write(0x800B9790,(0x7FF-c[16])|0xFF000000)||!m.read32(0x800DC274,flags)||!m.read32(0x801133C8,value))return false;if((flags&1)||value)return finish();
    c[16]=0x80015652;if(!services.play_sound(31,0x800)||!services.play_sound(31,0x810))return false;c[17]=0x8001E9A6;
    if(!m.read32(0x800DC244,value))return false;
    if(value&0x2000000){if(!load(0x38C,c[15])||!write(base-0x38C,int32_t(c[15])>239?240:c[15]+16))return false;}
    if(int32_t(c[9])>-16&&((value>>24)&31)!=31){c[15]=0x80016CC2;c[1]=0;uint32_t bonus;
        if(!services.warp_bonus(c,bonus))return false;c[17]=0x8001EA24;c[16]=bonus;
        if(int32_t(c[16])>0){c[2]=0x800DC240;c[0]=0x800DC278;
            do{uint32_t score,threshold;if(!m.read32(0x800DC240,score)||!write(0x800DC240,score+1000)||!m.read32(0x800DC278,threshold)||!write(0x800DC278,threshold-1000))return false;
                if(int32_t(threshold-1000)<0){c[15]=0x80015602;if(!write(0x800DC278,threshold-1000+20000)||!services.play_sound(24,0x1000))return false;c[17]=0x8001EA76;++c[11];if(!prepare_session_lives(m,c[11]))return false;c[17]=0x8001EA84;}
                ++c[1];
            }while(int32_t(c[1])<int32_t(c[16]));
        }
    }
    c[16]=0x800DC2E4;if(!m.read32(c[16],c[15]))return false;
    if(int32_t(c[15])>2){uint32_t skipped;if(!services.warp_skip(c,skipped))return false;c[17]=0x8001EAAC;c[9]+=skipped;if(!write(0x800DC2E4,0))return false;}
    c[15]=0x800DC244;if(!write(0x800DC244,0x18000000)||!load(0x384,value))return false;
    if(value){uint32_t score,threshold;if(!m.read32(0x800DC240,c[15])||!write(0x800DC240,c[15]+value)||!m.read32(0x800DC278,threshold)||!write(0x800DC278,threshold-value))return false;
        if(int32_t(threshold-value)<0){c[17]=20000;do{if(!m.read32(0x800DC278,threshold)||!write(0x800DC278,threshold+20000))return false;}while(int32_t(threshold+20000)<0);}
        c[15]=0x8001A39A;++c[11];if(!prepare_session_lives(m,c[11]))return false;c[17]=0x8001EB44;
        if(!services.play_sound(24,0x1000)||!write(base-0x384,0))return false;c[17]=0x8001EB54;c[15]=0;
    }else{if(!services.play_sound(25,0x1000))return false;c[17]=0x8001EB74;}
    state.context=c;state.phase=GameSessionPhase::nextLevel;return true;
}
}
