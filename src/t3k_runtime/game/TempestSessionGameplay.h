#pragma once
#include "TempestSession.h"
namespace tempest_native {
// One complete active-loop iteration from 8001DE44/8001DEA0 through 8001E390.
// Original object callbacks still own collision, damage and enemy lifetimes.
inline bool step_session_gameplay(CommandMemory& m,GameSessionServices& services,GameSessionState& state){
    if(state.phase!=GameSessionPhase::playing&&state.phase!=GameSessionPhase::active)return false;
    auto c=state.context;uint32_t base=state.frameBase,value,other,flags,buttons,handle,random;
    SimulationStep step=services.simulation_step();if(!step.valid())return false;
    auto write=[&](uint32_t address,uint32_t v){return m.write32(address,v);};
    auto local=[&](uint32_t offset,uint32_t v){return write(base-offset,v);};
    auto load=[&](uint32_t offset,uint32_t& v){return m.read32(base-offset,v);};
    auto finish=[&](){if(!write(0x800B850C,0)||!write(0x801133F0,0))return false;state.context=c;state.phase=GameSessionPhase::finished;return true;};
    auto still_active=[&](bool& result){uint32_t total,used,exit;if(!m.read32(0x800BA300,total)||!m.read32(0x800BA2FC,used)||!m.read32(0x800BA2F8,exit))return false;result=int32_t(total-used)>0&&!exit;return true;};
    if(state.phase==GameSessionPhase::playing){bool active;c[17]=0x800BA2D0;c[3]=2;if(!still_active(active))return false;
        if(!active){state.context=c;state.phase=GameSessionPhase::lifeEnded;return true;}
        c[8]=0x806B992C;c[6]=0x800DC244;c[14]=0x800DC2E0;c[12]=0x800DC100;
    }
    if(!m.read32(0x806B992C,buttons)||!m.read32(0x800DC274,flags))return false;
    if(buttons&0xFF3F){c[15]=flags;flags|=2;if(!write(0x800DC274,flags))return false;}
    if((flags&3)==3&&!write(0x801133C8,1))return false;
    if(!m.read32(0x801133C8,value))return false;if(value)return finish();
    uint32_t threshold,level;if(!load(0x3A4,threshold)||!load(0x394,level)||(step.legacy_boundary()&&!prepare_session_spawn(m,threshold,level,services)))return false;c[17]=0x8001DF28;c[15]=0x8001F70A;
    if(!apply_menu_parameters(m,base-0x100,c[13],step))return false;c[17]=0x8001DF40;
    if(!services.gameplay_frame(c))return false;c[17]=0x8001DF4A;
    if(!m.read32(0x800DC2E0,c[15]))return false;if(int32_t(c[15])>4&&!write(0x800DC2E0,4))return false;
    if(!m.read32(0x800DC2E0,value)||!m.read32(0x800B4F64,c[15]))return false;
    if(value!=c[15]&&(!write(0x800B4F64,value)||!write(0x800B4F7C,0x400000)))return false;
    if((step.legacy_boundary()&&!next_driver_random(m,random))||!m.read32(0x800DC244,value))return false;c[17]=0x8001DFA4;c[0]=(value&0xFF0000)>>13;if(c[0]>48)c[0]=48;
    uint32_t available=c[7];if(int32_t(c[7])<=4&&!c[1])available=0;
    uint32_t active,port,controllers,capabilities;if(!m.read32(0x801133F0,active)||!m.read32(0x801133E8+active*4,port)||!m.read32(0x806B9910,controllers)||!m.read32(controllers+port*16,capabilities)||!m.read32(0x806B992C,buttons))return false;
    c[17]=available;c[16]=(capabilities&0x1000)?0x1000:0x10;
    uint32_t twice;const uint32_t jumpCapacity=c[0];const unsigned divisions=services.simulation_divisions();
    if(!divisions||divisions>65536)return false;
    if((buttons&c[16])&&available){
        c[15]=0;if(!m.read32(0x800DC244,value)||!write(0x800DC244,value|0x1000000)||!write(0x800DC2E0,0))return false;
        c[0]=0x1000000-c[1];if(int32_t(c[0])<0)c[0]=0-c[0];c[16]=int32_t(c[1])<=0xFFFFFF?1:0;if(divisions==1)c[2]+=c[16]<<17;
        if(!m.read32(0x800DC0FC,value)||!write(0x800DC0FC,value|1)||!load(0x388,c[15]))return false;
        if(divisions==1)--c[7];if(c[15]==0xFFFFFFFF){if(!services.start_descriptor(c[15],base-0x33C,base-0x310,0x70000000,0x40000000,handle)||!local(0x388,handle))return false;c[17]=0x8001E0B6;c[15]=base-0x388;}
        if(divisions==1){c[1]+=c[2];uint32_t distance=0x1000000-c[1];if(int32_t(distance)<0)distance=0-distance;
            if(int32_t(distance)>int32_t(c[0]))c[2]=uint32_t(int32_t(c[2]*240)>>8);
        }else if(!services.fractional_jump(c[1],c[2],c[7],true,jumpCapacity,divisions))return false;
        twice=c[7]<<1;
    }else{
        if(!m.read32(0x800DC0FC,value)||!write(0x800DC0FC,value&~1U)||!load(0x388,handle))return false;
        if(handle!=0xFFFFFFFF){c[15]=0x80022996;if(!services.stop_voice(handle)||!local(0x388,0xFFFFFFFF))return false;c[15]=0xFFFFFFFF;c[17]=0x8001E130;}
        if(divisions==1){if(c[1]){c[2]-=0x40000;c[1]+=c[2];if(int32_t(c[1])<=0x7FFFF)c[1]=c[2]=0;}
            else if(int32_t(c[7])<int32_t(c[0]))++c[7];
        }else if(!services.fractional_jump(c[1],c[2],c[7],false,jumpCapacity,divisions))return false;
        twice=c[7]<<1;
    }
    if(!write(0x8010B038,(0-twice)&65535)||!write_level_halfword(m,0x800B675C,twice+40)||!write(0x800BA2EC,0x1000000-uint32_t(int32_t(c[1])>>1))||!load(0x398,c[15])||!write(0x800B907C,(c[15]<<16)+c[1])||!m.read32(0x800DC278,value))return false;
    if(int32_t(value)<0){++c[11];c[15]=0x8001A39A;if(!write(0x800DC278,value+20000)||!prepare_session_lives(m,c[11]))return false;c[17]=0x8001E222;}
    if(!m.read32(0x806B992C,buttons))return false;
    if(buttons&8){if(!m.read32(0x800DC250,value))return false;
        if(!(value&255)&&int32_t(c[3])>0){--c[3];if(!write(0x800DC2E0,0)||!load(0x38C,value)||!write(0x800DC250,value)||!local(0x38C,64)||!m.read32(0x800DC244,flags)||!write(0x800DC244,flags|0x2000000))return false;c[15]=64;
            SoundRequest sound{};uint32_t cue=c[3]==1?19:21;if(!m.read32(0x80115B10+cue*8,sound.resource)||!m.read32(0x80115B14+cue*8,sound.resourceWord))return false;sound.argument1=0x1000;sound.argument3=0x5F000000;
            if(!services.play_request(sound,handle))return false;c[17]=c[3]==1?0x8001E290:0x8001E2B0;
            if(c[3]==1)c[15]=0x800155AE;else {if(!m.read32(0x800DC250,c[15])||!write(0x800DC250,c[15]|0x20000))return false;}
        }
    }
    if(!write(0x800DC110,0x800000)||!m.read32(0x800DC100,value)||!write(0x800DC248,value)||!m.read32(0x800DC104,other)||!write(0x800DC24C,other)||!load(0x380,other)||!m.read32(0x800B9778,c[15])||!write(0x800B9778,c[15]+uint32_t(step.delta(int32_t(other)))))return false;
    uint32_t angle=uint32_t(int32_t(value)>>16),previous;if(!load(0x37C,previous))return false;
    if(angle!=previous){c[15]=angle;if(!local(0x37C,angle)||!services.play_sound(7,0x1000))return false;c[17]=0x8001E34E;}
    c[17]=0x800BA2D0;bool activeLoop;if(!still_active(activeLoop))return false;state.context=c;state.phase=activeLoop?GameSessionPhase::active:GameSessionPhase::lifeEnded;return true;
}
// Original 8001E390..8001E480: stop the jet voice, complete its physical return,
// then relinquish active scene ownership before the original outcome dispatch.
inline bool settle_session_life(CommandMemory& m,GameSessionServices& services,GameSessionState& state,unsigned budget=4096){
    SceneTimingScope cadence(services,SceneTimingKind::settle);if(!cadence.divisions)return false;uint64_t remainingBudget=cadence.budget(budget);
    if(state.phase!=GameSessionPhase::lifeEnded)return false;auto c=state.context;uint32_t value,handle;
    if(!m.read32(0x800DC0FC,value)||!m.write32(0x800DC0FC,value&~1U)||!m.read32(state.frameBase-0x388,handle))return false;
    if(handle!=0xFFFFFFFF){c[15]=0x80022996;if(!services.stop_voice(handle)||!m.write32(state.frameBase-0x388,0xFFFFFFFF))return false;c[17]=0x8001E3DA;c[15]=0xFFFFFFFF;}
    if(c[1]){
        c[3]=0x800BA2EC;c[0]=0x800B907C;if(!m.read32(state.frameBase-0x398,value))return false;c[16]=uint32_t(int32_t(int16_t(value)));c[2]-=uint32_t(cadence.delta(0x40000,0));
        do{
            c[1]+=uint32_t(cadence.delta(int32_t(c[2]),1)+cadence.euler_correction(-0x40000));if(int32_t(c[1])<0)c[1]=c[2]=0;
            if(!m.write32(0x800BA2EC,0x1000000-uint32_t(int32_t(c[1])>>1))||!m.write32(0x800B907C,(c[16]<<16)+c[1])||!m.read32(0x801133C8,value))return false;
            if(value){if(!m.write32(0x800B850C,0)||!m.write32(0x801133F0,0))return false;state.context=c;state.phase=GameSessionPhase::finished;return true;}
            if(!remainingBudget--||!services.gameplay_frame(c))return false;c[17]=0x8001E464;if(c[1])c[2]-=uint32_t(cadence.delta(0x40000,0));
        }while(c[1]);
    }
    c[15]=0;if(!m.write32(0x800B850C,0))return false;state.context=c;state.phase=GameSessionPhase::lifeResolved;return true;
}
}
