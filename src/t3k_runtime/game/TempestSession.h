#pragma once
#include "TempestMenuParameters.h"
#include "TempestMenuPresets.h"
#include "TempestSessionEntry.h"
#include "TempestSessionUtilities.h"
#include "TempestSessionSpawning.h"
namespace tempest_native {
struct GameSessionInput {std::array<uint32_t,4> arguments;OpeningAnimationInput caller;};
struct GameSessionServices:OpeningAnimationServices,LevelDefinitionServices {
    virtual SimulationStep simulation_step()const{return {};}
    virtual unsigned simulation_divisions()const{return simulation_step().divisions;}
    virtual bool fractional_jump(uint32_t&,uint32_t&,uint32_t&,bool,uint32_t,unsigned){return false;}
    virtual bool select_music(uint32_t)=0;
    virtual bool show_asset(uint32_t source,uint32_t mode,const std::array<uint32_t,20>&)=0;
    virtual bool gameplay_frame(const std::array<uint32_t,20>&)=0;
    virtual bool play_request(const SoundRequest&,uint32_t& handle)=0;
    virtual bool start_descriptor(uint32_t handle,uint32_t descriptor,uint32_t spatial,uint32_t volume,uint32_t auxiliary,uint32_t& result)=0;
    virtual bool stop_voice(uint32_t handle)=0;
    virtual bool consume_sounds()=0;
    virtual bool game_over_banner(uint32_t player,uint32_t score,const std::array<uint32_t,20>& incoming)=0;
    virtual bool transition(const DriverTransitionInput&,const std::array<uint32_t,20>&)=0;
    virtual bool warp_bonus(const std::array<uint32_t,20>&,uint32_t& result)=0;
    virtual bool warp_skip(const std::array<uint32_t,20>&,uint32_t& result)=0;
    virtual bool vertical_axis(uint32_t latch,uint32_t mask,uint32_t& result)=0;
    virtual bool high_score_entry(uint32_t content,uint32_t scene,uint32_t scores,uint32_t player,const std::array<uint32_t,20>& incoming)=0;
};
enum class GameSessionPhase {uninitialized,playerPrepared,levelPrepared,playing,active,lifeEnded,lifeResolved,retry,nextLevel,sessionEnding,finished};
struct GameSessionState {std::array<uint32_t,20> context;uint32_t frameBase=0;GameSessionPhase phase=GameSessionPhase::uninitialized;};
inline bool prepare_session_player(CommandMemory&,GameSessionServices&,GameSessionState&);
// Original 8001D0A2..8001D8D2, ending immediately after scene-7 initialization.
// This is a checked phase of the original session, not a substitute play loop.
inline bool begin_game_session(CommandMemory& m,const GameSessionInput& input,GameSessionServices& services,GameSessionState& state){
    uint32_t stack=input.caller.incoming[19];if((stack&15)||stack<0x400||state.phase!=GameSessionPhase::uninitialized)return false;
    auto c=input.caller.incoming;uint32_t base=stack-16;c[18]=base;c[19]=stack-0x400;c[17]=input.caller.returnAddress;
    auto local=[&](uint32_t offset,uint32_t value){return m.write32(base-offset,value);};
    auto load=[&](uint32_t offset,uint32_t& value){return m.read32(base-offset,value);};
    uint32_t value,flags,random,player,definition;
    if(!local(0x370,input.arguments[0])||!local(0x374,input.arguments[2])||!local(0x378,input.arguments[3]))return false;
    const std::array<std::array<uint32_t,2>,24> initial{{{0x344,0},{0x340,0},{0x344,3},{0x340,5},{0x34C,0},{0x348,0},{0x354,0},{0x350,0},{0x35C,0},{0x358,0},
        {0x35C,0x18000000},{0x358,0x18000000},{0x364,input.arguments[0]},{0x360,input.arguments[0]},{0x36C,0},{0x368,0},{0x36C,64},{0x368,64},
        {0x390,input.arguments[3]},{0x33C,0x8044B590},{0x338,(0x80452C86-0x8044B590)/2},{0x334,0},{0x330,(0x80452C86-0x8044B590)/2},{0x32C,0x1000}}};
    for(auto pair:initial)if(!local(pair[0],pair[1]))return false;
    for(auto pair:{std::pair{0x328U,1U},std::pair{0x310U,0U},std::pair{0x30CU,0U},std::pair{0x308U,0U},std::pair{0x388U,0xFFFFFFFFU},std::pair{0x384U,input.arguments[1]}})if(!local(pair.first,pair.second))return false;
    c[1]=0;c[9]=input.arguments[0];c[16]=0x80112C88;if(!m.write32(c[16],0x10000)||!m.read32(0x800DC274,flags))return false;
    if(flags&1){if(!next_driver_random(m,random))return false;c[15]=0x80015500;c[17]=0x8001D2AC;c[9]=random&127;if(!m.read32(c[16],value)||!m.write32(c[16],value+(c[9]<<11)))return false;}
    c[11]=3;
    if(!m.write32(0x800DC240,0)||!m.write32(0x800DC2E0,0)||!m.write32(0x800DC244,0x18000000)||!m.write32(0x800DC2E4,0)||!m.write32(0x8010F890,0)||!local(0x38C,64)||!m.read32(0x800AF700,definition)||!local(0x39C,definition)||!load(0x378,value)||!load(0x390,player))return false;
    c[15]=player;
    state={c,base,GameSessionPhase::playerPrepared};return prepare_session_player(m,services,state);
}
inline bool prepare_session_player(CommandMemory& m,GameSessionServices& services,GameSessionState& state){
    if(state.phase!=GameSessionPhase::playerPrepared&&state.phase!=GameSessionPhase::retry&&state.phase!=GameSessionPhase::nextLevel)return false;bool nextLevel=state.phase==GameSessionPhase::nextLevel;
    auto c=state.context;uint32_t base=state.frameBase,value,flags,random,player,definition,startingLevel;
    auto local=[&](uint32_t offset,uint32_t v){return m.write32(base-offset,v);};
    auto load=[&](uint32_t offset,uint32_t& v){return m.read32(base-offset,v);};
    if(!load(0x378,value)||!load(0x390,player)||!load(0x39C,definition)||!load(0x370,startingLevel))return false;c[15]=player;
    if(!nextLevel&&value){
        uint32_t otherLives;if(!load(0x344-(player^1)*4,otherLives))return false;
        if(int32_t(otherLives)>0){
            if(!local(0x344-player*4,c[11]))return false;
            if(int32_t(c[11])<=0&&!local(0x378,0))return false;
            uint32_t score,extras,sessionFlags,position,range;
            if(!m.read32(0x800DC240,score)||!local(0x34C-player*4,score)||!local(0x364-player*4,c[9])||!m.read32(0x800DC2E4,extras)||!m.read32(0x800DC2E0,sessionFlags)||!m.read32(0x8010F890,position)||!local(0x354-player*4,(extras<<8)|sessionFlags|position)||!m.read32(0x800DC244,sessionFlags)||!local(0x35C-player*4,sessionFlags)||!load(0x38C,range)||!local(0x36C-player*4,range))return false;
            player^=1;if(!local(0x390,player)||!m.write32(0x801133F0,player)||!load(0x344-player*4,c[11])||!load(0x34C-player*4,score)||!m.write32(0x800DC240,score)||!load(0x354-player*4,extras)||!m.write32(0x800DC2E0,extras&7)||!load(0x35C,sessionFlags)||!m.write32(0x800DC244,sessionFlags)||!m.write32(0x800DC2E4,(extras>>8)&15)||!m.write32(0x8010F890,uint32_t(int32_t(extras)>>16))||!load(0x36C-player*4,position)||!local(0x38C,position)||!load(0x364-player*4,c[9]))return false;
            c[17]=0x8010C1A0+player*4;c[15]=position;
            uint32_t asset;if(!m.read32(c[17],asset)||!services.show_asset(asset,15,c))return false;c[17]=0x8001D530;
        }
    }
    if(!nextLevel){if(!m.write32(0x801095A0,c[9])||!m.write32(0x800B850C,0)||!prepare_session_lives(m,c[11]))return false;c[17]=0x8001D54E;}c[15]=0x8001A7A4;
    if(!prepare_session_level_label(m,c[9]))return false;c[17]=0x8001D55C;
    if(c[9]==128&&startingLevel!=128){if(!services.show_asset(0x8010C9C0,16,c))return false;c[17]=0x8001D598;}
    if(!load(0x390,player)||!m.write32(0x801133F0,player)||!m.write32(0x801095A0,c[9])||!local(0x394,c[9]&127)||!m.read32(0x800DC240,value))return false;c[15]=player;c[0]=0x800DC278;c[16]=20000-uint32_t(int32_t(value+20000)%20000);
    if(!m.write32(c[0],c[16])||!m.read32(0x800DC244,value)||!m.write32(0x800DC244,value&0x1F000000)||!m.read32(0x800B2AE0+(c[9]&7)*16,value))return false;
    c[0]=value>>16;c[1]=value;c[17]=0x800B2BD4;
    uint32_t paletteOffset=((c[9]>>4)&7)*16;
    if(!m.read32(0x800B2AE8+paletteOffset,value)||!m.write32(0x8010D04C,value)||!m.read32(0x800B2AEC+paletteOffset,value)||!m.write32(0x800B2BD4,value)||!m.read32(0x800DC274,flags))return false;
    c[1]&=255;if(!(flags&1)){uint32_t track;if(!m.read32(0x80113454+((c[9]>>3)&15)*4,track))return false;c[15]=0x80015734;if(!services.select_music(track))return false;c[17]=0x8001D68A;}
    uint32_t maximum,level,mode;if(!m.read32(0x801133B0,maximum)||!load(0x374,mode))return false;level=int32_t(c[9])>int32_t(maximum)?128:c[9]+mode;
    c[5]=0x10000;c[16]=0x8010DCB0;if(!load_level_definition(m,definition,level,services))return false;c[17]=0x8001D6E6;
    if(!m.read32(0x801133A8,flags)||!m.write32(0x801133A8,flags|4)||!read_level_halfword(m,definition+40,value)||!local(0x398,value))return false;
    uint32_t source,mask;if(!m.read32(0x8010DCB0+c[0]*4,source))return false;c[0]=0x8001FC7E;
    if(!unpack_menu_parameters(m,base-0x200,source,mask)||!m.read32(0x8010DCB0+c[1]*4,source)||!unpack_menu_parameters(m,base-0x300,source,mask))return false;c[17]=0x8001D758;c[13]=mask;
    uint32_t name;if(c[9]<=127){if(!m.read32(0x80114D4C+c[9]*4,name)||!local(0x3A0,name))return false;}
    if(!m.read32(0x801133B0,maximum)||!m.read32(0x801133A8,flags))return false;
    if(int32_t(c[9])>int32_t(maximum)||(flags&3)){
        c[15]=0x8001A884;if(!prepare_level_description(m,{int32_t(c[9])>int32_t(maximum)?128:c[9],c[3]}))return false;c[17]=int32_t(c[9])>int32_t(maximum)?0x8001D7BA:0x8001D7D6;
        if(!local(0x3A0,0x8010BF20))return false;
    }
    if(!load(0x3A0,name))return false;c[16]=0;c[1]=0x8010BF50;c[0]=0xFFFFFFE8;
    for(;c[16]<256;++c[16]){uint8_t byte;if(!m.read8(name+c[16],byte))return false;if(!byte)break;if(!write_session_byte(m,0x8010BF53+c[16],byte))return false;}
    if(c[16]==256||!write_session_byte(m,0x8010BF53+c[16],0))return false;c[15]=0;
    if(mask&0x1000){if(!advance_menu_palette(m))return false;c[17]=0x8001D8B0;}
    if(!prepare_session_layers(m,mask))return false;c[17]=0x8001D8C6;c[15]=0x80010200;c[3]=0x10000000;
    if(!services.initialize_scene({7,c}))return false;c[17]=0x8001D8D2;state={c,base,GameSessionPhase::levelPrepared};return true;
}
// Original 8001D8D2..8001DE44: first scene-7 transition, scene-1 player setup,
// both original preparation fades, and the live gameplay-loop boundary.
inline bool enter_session_play(CommandMemory& m,GameSessionServices& services,GameSessionState& state,unsigned budget=4096){
    SceneTimingScope cadence(services,SceneTimingKind::entry);if(!cadence.divisions)return false;uint64_t remainingBudget=cadence.budget(budget);
    if(state.phase!=GameSessionPhase::levelPrepared)return false;auto c=state.context;uint32_t base=state.frameBase,value,flags,buttons,random;
    auto write=[&](uint32_t address,uint32_t v){return m.write32(address,v);};
    auto local=[&](uint32_t offset,uint32_t v){return write(base-offset,v);};
    auto load=[&](uint32_t offset,uint32_t& v){return m.read32(base-offset,v);};
    auto finish=[&](){if(!write(0x800B850C,0)||!write(0x801133F0,0))return false;state.context=c;state.phase=GameSessionPhase::finished;return true;};
    auto frame=[&](uint32_t link){if(!remainingBudget--)return false;if(!services.gameplay_frame(c))return false;c[17]=link;return true;};
    auto blend=[&](uint32_t factor,uint32_t link){if(!blend_session_parameters(m,factor,base-0x200,base-0x300,base-0x100))return false;c[17]=link;return true;};
    if(!write(0x800B9520,0x20000)||!write(0x800B9524,0x20000)||!write(0x800B9528,0xCFFF)||!write(0x800B952C,0x00200020)||!m.read32(0x800DC100,value)||!local(0x37C,uint32_t(int32_t(value)>>16))||!write(0x800DC118,0)||!m.read32(0x800DC274,flags)||!write(0x800DC274,flags&3))return false;
    const std::array<std::array<uint32_t,2>,14> setup{{{0x800BA088,0x3F00},{0x800DC10C,0x800000},{0x800DC114,0},{0x800DC118,0},{0x800DC11C,0x20000},{0x800DC120,0x20000},{0x800DC260,0x0F000000},{0x800DC268,0},{0x800DC2C8,0},{0x800DC2D8,0},{0x800DC2E0,0},{0x800B4F64,0},{0x800B4F7C,0x400000},{0x800B18C8,0}}};
    if(!m.read32(0x800B18C8,value)||!write(0x800DC2C4,(value&2)*6+8))return false;
    for(auto pair:setup)if(!write(pair[0],pair[1]))return false;
    if(!m.read32(0x801133A8,flags)||!write(0x800BA358,(flags&32)?0:0x121)||!write(0x800B907C,c[3]))return false;
    c[1]=0x801133C8;c[4]=0x806B992C;c[2]=0xFF3F;c[0]=0x800DC274;c[16]=base-0x100;
    while(int32_t(c[5])>0x4000){
        if(!m.read32(0x801133C8,c[15]))return false;if(c[15])return finish();
        if(!m.read32(0x806B992C,buttons)||!m.read32(0x800DC274,flags))return false;
        if(buttons&0xFF3F){flags|=2;if(!write(0x800DC274,flags))return false;}
        if((flags&3)==3){if(!write(0x801133C8,1))return false;return finish();}
        if(!m.read32(0x800BA088,c[15])||!write(0x800BA088,int32_t(c[15])>0?c[15]-uint32_t(cadence.delta(0x80,0)):0))return false;c[5]-=uint32_t(cadence.delta(0x200,1));
        if(!blend(c[5],0x8001DAE2)||!apply_menu_parameters(m,base-0x100,c[13],cadence.step()))return false;c[17]=0x8001DAF0;c[15]=0x80016704;
        if(!frame(0x8001DAFA))return false;
    }
    c[4]=0;for(unsigned i=0;i<17;++i){if(!m.read32(0x80110AC8+i*4,value)||!write(0x80110B14+i*4,value))return false;++c[4];}
    SoundRequest sound{};if(!m.read32(0x80115BB0,sound.resource)||!m.read32(0x80115BB4,sound.resourceWord))return false;sound.argument1=0x1000;sound.argument3=0x7F000000;
    uint32_t handle;if(!services.play_request(sound,handle))return false;c[17]=0x8001DB62;
    if(!write(0x800DC254,0x1000)||!write(0x800DC25C,0)||!write(0x800BA2EC,0x1000000))return false;c[8]=0x4000;c[10]=((c[9]&15)<<3)+10;if(c[10]>56)c[10]=56;
    uint32_t level=c[9]>127?c[9]:(c[9]&63);c[17]=0x80112C88;
    if(!write(0x80112C88,((level&15)<<12)+(uint32_t(int32_t(level)>>4)<<14)+0x10000))return false;
    value=((16-(c[9]&15))>>1)+4;
    if(!write(0x800BA30C,(value<<16)|value)||!write(0x800BA308,0)||!write(0x800BA304,c[10])||!write(0x800BA300,c[10])||!write(0x800BA2FC,0)||!write(0x800BA2F8,0)||!next_driver_random(m,random))return false;
    c[16]=0x800B6108;c[17]=0x8001DC46;if(!write(c[16],(random&0x7FFFF)+0x90000)||!write(0x800DC0F0,0)||!write(0x800DC0E8,0)||!write(0x800DC0F4,0)||!write(0x8010B038,0)||!write_level_halfword(m,0x800B675C,40)||!write(0x800DC104,0x1000000))return false;
    c[1]=c[2]=c[7]=0;c[17]=(0x800B675CU>>1)<<4;c[15]=4;
    if(!services.initialize_scene({1,c}))return false;c[17]=0x8001DCD4;
    uint8_t count;if(!m.read8(0x80114F5B+c[9]*16,count)||!local(0x3A4,count)||!local(0x3A4,uint32_t(count)+1))return false;c[15]=uint32_t(count)+1;
    if(int32_t(c[5])>0){c[14]=0x806B992C;c[6]=0x800DC274;c[0]=base-0x100;c[12]=0x800B907C;
        if(!load(0x398,value))return false;c[16]=uint32_t(int32_t(int16_t(value)));
        do{
            if(!m.read32(0x806B992C,buttons)||!m.read32(0x800DC274,flags))return false;
            if(buttons&0xFF3F){c[15]=flags;flags|=2;if(!write(0x800DC274,flags))return false;}
            if(!m.read32(0x801133C8,value))return false;if(value)return finish();
            if((flags&3)==3){if(!write(0x800B850C,1))return false;return finish();}
            c[5]=int32_t(c[5])>0?c[5]-uint32_t(cadence.delta(0x100,2)):0;
            if(!blend(c[5],0x8001DDB4))return false;c[15]=0x8001F70A;
            if(!apply_menu_parameters(m,base-0x100,c[13],cadence.step())||!write(0x800B84B0,c[8]))return false;c[17]=0x8001DDC2;
            c[3]=int32_t(c[3])>0?c[3]-uint32_t(cadence.delta(0x400000,3)):0;c[8]=int32_t(c[8])>0?c[8]-uint32_t(cadence.delta(0x100,4)):0;
            if(!write(0x800B907C,(c[16]<<16)+c[3])||!frame(0x8001DDFC))return false;
        }while(int32_t(c[5])>0);
    }
    if(!load(0x398,c[15])||!write(0x800B907C,c[15]<<16)||!write(0x800DC250,0)||!write(0x800B850C,1))return false;
    state.context=c;state.phase=GameSessionPhase::playing;return true;
}
}
