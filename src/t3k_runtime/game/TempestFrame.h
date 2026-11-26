#pragma once
#include "TempestCommands.h"
#include "TempestObjectLayout.h"
#include "TempestObjects.h"
#include <cmath>
namespace tempest_native {
inline uint32_t frame_abs(uint32_t v){return int32_t(v)<0?0U-v:v;}
inline bool frame_begin(CommandMemory& m){
    uint32_t marker,tracking,cap,control,flags,nearest,player,word;
    if(!m.read32(0x800DC27C,marker)||!m.read32(0x800DC2D8,tracking)||
       !m.write32(0x800DC27C,(marker<<16)&~1U)||!m.write32(0x800DC2D8,tracking<<16))return false;
    for(unsigned i=0;i<3;++i)if(!m.write32(0x80112D30+i*4,0))return false;
    if(!m.read32(0x801095A0,cap))return false;
    uint32_t offset=0;if(int32_t(cap)>127){offset=((cap&255)<<1)+((cap&15)<<5);if(offset&2)offset=0U-offset;}
    if(!m.write32(0x800DC258,offset))return false;
    // The original decrement/BGE loop writes SEVENTEEN words, not sixteen.
    for(unsigned i=0;i<17;++i)if(!m.write32(0x800DC2F0+i*4,0x08000000))return false;
    if(!m.read32(0x80110AAC,control)||!m.write32(0x80113158,control<<16)||!m.write32(0x8011315C,control<<24)||
       !m.read32(0x800DC274,flags)||!m.read32(0x800DC268,nearest))return false;
    if((flags&1)&&!(flags&4)){
        uint32_t height;if(!m.read32(0x800DC248,player)||!m.read32(0x800DC260,height))return false;
        if(int32_t(uint32_t(int32_t(frame_abs(player-nearest))>>16))<=12&&int32_t(height)<=0x01A00000){
            if(!m.read32(0x800DC250,word)||!m.write32(0x800DC250,(word&0xFFFFFF00)|0x40))return false;
        }
        if(!m.read32(0x800DC268,nearest)||!m.read32(0x800DC248,player))return false;
        nearest&=0xFFFFF;player&=0xFFFFF;uint32_t difference=frame_abs(nearest-player);
        if(int32_t(difference)>0xC000)difference=int64_t(int32_t(nearest))-int32_t(player)>0?0xB0000:0U-0xB0000;
        if(!m.write32(0x80113158,difference<<11))return false;
    }
    uint32_t list;
    return m.write32(0x80113160,control)&&m.write32(0x800DC260,0x0C000000)&&m.write32(0x800AF7F4,0)&&
           m.read32(0x800BA470,list)&&m.write32(0x80113154,0x8004E040)&&m.write32(0x80113150,list);
}
inline bool frame_finish(CommandMemory& m,SimulationStep step={}){
    if(!step.valid())return false;
    for(unsigned i=0;i<17;++i){uint32_t word;if(!m.read32(0x800DC2F0+i*4,word)||!m.write32(0x800DC280+i*4,word))return false;}
    uint32_t velocity,packed,target;
    if(!m.read32(0x800DC25C,velocity)||!m.read32(0x800DC254,packed)||!m.read32(0x800DC258,target))return false;
    if(!target){
        uint32_t signedHalf=uint32_t(int32_t(packed<<16)>>17),magnitude=frame_abs(signedHalf);
        if(int32_t(magnitude)>0x200)magnitude=0x200;
        target=int32_t(signedHalf)>0?0U-magnitude:magnitude;
    }
    uint32_t distance=frame_abs(target-velocity);
    uint32_t increment=uint32_t(step.delta(32));
    if(int32_t(distance)<=int32_t(increment))velocity=target;
    else velocity+=int32_t(target)>int32_t(velocity)?increment:0U-increment;
    if(!m.write32(0x800DC25C,velocity)||!m.write32(0x800DC254,(packed&0xFFFF0000)|((packed+uint32_t(step.delta(int32_t(velocity))))&65535)))return false;
    uint32_t mode;if(!m.read32(0x8010F170,mode)||!m.write32(0x8010F170,mode&~3U))return false;
    bool active=(mode&1)!=0;
    if(!active){
        unsigned live=16;
        for(unsigned i=0;i<16;++i){
            uint32_t pair;if(!m.read32(0x8010F174+i*4,pair))return false;
            uint32_t speed=uint32_t(int32_t(pair)>>16),position=uint32_t(int32_t(pair<<16)>>16);
            if(int32_t(frame_abs(position))<128){--live;if(!m.write32(0x8010F174+i*4,0))return false;continue;}
            speed+=uint32_t(step.delta(int32_t(position)<0?128:-128));
            if(int32_t(frame_abs(speed))>0x400)speed=int32_t(speed)>0?0x400:0U-0x400;
            position+=uint32_t(step.delta(int32_t(speed)));
            if((int32_t(speed)<0)==(int32_t(position)<0)){
                uint32_t drag=step.divisions==1?0xF000:uint32_t(std::llround(std::pow(15.0/16,step.fraction())*65536));
                position=command_mul(drag,position,16);speed=command_mul(drag,speed,16);
            }
            if(!m.write32(0x8010F174+i*4,(speed<<16)|(position&65535)))return false;
        }
        active=live!=0;
    }
    if(active){if(!m.read32(0x8010F170,mode)||!m.write32(0x8010F170,mode|2))return false;}
    return true;
}
struct FrameObjectLayout{uint32_t values,parameters,script,storageEnd;};
// Complete object preparation 80011B60/6C, including the ordered attribute pass.
inline bool prepare_frame_object(CommandMemory& m,uint32_t object,bool skip,FrameObjectLayout& out,ObjectValueServices* integration=nullptr){
    uint32_t picture,descriptor,vp,pp,sp,gp,globals,flags;
    if(!m.write32(0x80113168,skip?1:0)||!m.read32(0x80110AA0,picture)||!m.write32(0x801131B8,picture)||
       !m.read32(object+8,descriptor)||!m.read32(object+12,vp)||!m.read32(object+16,pp)||
       !m.read32(object+20,sp)||!m.read32(object+28,gp)||!m.read32(0x80113164,globals)||!m.read32(0x800DC274,flags))return false;
    auto layout=resolve_object_layout({object,descriptor,vp,pp,sp,gp,globals,skip?1U:0U,flags});
    if((descriptor>>24)&63)if(!m.write32(0x80113164,layout.globals))return false;
    if(layout.updateValues){
        ObjectUpdateInput input{};uint32_t topology,phase;
        if(!m.read32(0x80110AC4,input.paused)||!m.read32(0x800AF7B0,topology)||!m.read32(0x8010F170,phase)||!m.read32(object+0x38,input.objectStatus))return false;
        input.picture=picture;input.topologyMask=topology;input.topologyPhase=phase;
        input.values.resize(layout.valueCount);
        uint32_t valueBase=layout.values&~15U;
        for(unsigned i=0;i<layout.valueCount;++i){std::array<uint32_t,4> words;for(unsigned k=0;k<4;++k)if(!m.read32(valueBase+i*16+k*4,words[k]))return false;input.values[i]={words[0],words[1],words[2],words[3]};}
        uint32_t parametersNeeded=0,globalsNeeded=0;
        for(const auto& value:input.values)if(value.flags&0x80000000){
            unsigned handler=(value.flags>>12)&15;if(handler>2)return false;
            uint32_t needed=(1U<<(value.flags&15));if(handler!=1)needed|=1U<<((value.flags>>4)&15);
            (handler==2?globalsNeeded:parametersNeeded)|=needed;
        }
        auto view=[&](uint32_t base,uint32_t needed,auto& values,auto& aliases){
            for(unsigned i=0;i<16;++i){if(!(needed&(1U<<i)))continue;uint32_t address=(base+i*4)&~3U;if(!m.read32(address,values[i]))return false;
                if(address==((object+0x38)&~3U))aliases[i]=-2;
                else if(address>=valueBase&&uint64_t(address)<uint64_t(valueBase)+layout.valueCount*16)aliases[i]=int32_t((address-valueBase)/4);
            }return true;
        };
        // Inactive/stamped records never dereference either parameter view.
        // An inherited null global view is legal when no active handler uses it.
        if(!view(layout.parameters,parametersNeeded,input.parameters,input.parameterAliases)||!view(layout.globals,globalsNeeded,input.globalParameters,input.globalAliases))return false;
        ObjectUpdateOutput result;if(!(integration?integration->object_values(object,valueBase,input,result):update_object_values(input,result)))return false;
        // The actual list stores every record, including unchanged inactive ones.
        for(unsigned i=0;i<layout.valueCount;++i){const auto& value=result.values[i];std::array<uint32_t,4> words{value.position,value.velocity,value.scale,value.flags};for(unsigned k=0;k<4;++k)if(!m.write32(valueBase+i*16+k*4,words[k]))return false;}
        if(!m.write32(object+0x38,result.objectStatus))return false;
    }
    out={layout.values,layout.parameters,layout.script,layout.storageEnd};return true;
}
}
