#pragma once
#include "TempestEvents.h"
#include "TempestCommands.h"
namespace tempest_native {
struct CallbackServices{virtual bool play_sound(uint32_t cue,uint32_t& handle)=0;virtual ~CallbackServices()=default;};
struct CallbackSoundCall{uint32_t cue,handle;};
struct CallbackInput{uint32_t entry,object,draw,values,parameters;std::array<uint32_t,8> arguments{};};
// A few legacy branches unwind a saved caller vector. Preserve that explicit
// continuation effect rather than treating every callback as a normal return.
struct CallbackResult{uint32_t unwindWords,trap=0,trapValue=0;};
inline bool callback_sound(CallbackServices* services,uint32_t cue,uint32_t* result=nullptr){
    uint32_t handle;if(!services||!services->play_sound(cue,handle))return false;if(result)*result=handle;return true;
}
// Runtime generator 80014F54 differs from the initializer generator: feedback
// XOR occurs on ODD input here. Do not merge these two contracts.
inline bool callback_random(CommandMemory& m,uint32_t index,uint32_t& result){
    uint32_t seed,polynomial;uint32_t address=0x80112C50+index*4;
    if(!m.read32(address,seed)||!m.read32(0x80112C50,polynomial))return false;
    result=(seed>>1)^((seed&1)?polynomial:0);return m.write32(address,result);
}
inline bool callback_vector(CommandMemory& m,uint32_t address,std::array<uint32_t,4>& words){
    address&=~15U;for(unsigned k=0;k<4;++k)if(!m.read32(address+k*4,words[k]))return false;return true;
}
inline bool callback_score(CommandMemory& m,uint32_t object,uint32_t amount,CallbackServices* services){
    uint32_t flags,word,level,score,remaining;
    if(!m.read32(0x801131B4,flags))return false;
    if(flags&0x100){if(!m.read32(0x800DC274,word)||!m.write32(0x800DC274,word|4))return false;}
    if(!m.read32(0x800DC2E0,level)||!m.read32(0x800DC240,score)||!m.read32(0x800DC278,remaining))return false;
    uint32_t delta=(level+1)*amount,next=remaining-delta;score+=delta;
    bool positive=int64_t(int32_t(remaining))-int32_t(delta)>0;
    if(!m.write32(0x800DC240,score)||!m.write32(0x800DC278,next))return false;
    if(positive||int32_t(remaining)<0)return true;
    return append_event_packet(m,0x30004,{0x8010CF50,score,next,remaining})&&append_object_event(m,0x12D05,object)&&callback_sound(services,0x2F18);
}
inline bool callback_hit_effect(CommandMemory& m,const CallbackInput& in,CallbackServices* services){
    uint32_t word,level;
    if(!m.read32(0x800DC244,word))return false;
    uint32_t high=word&0xFFFF0000;
    uint32_t countdown=(word&65535)-1;
    if(!m.write32(0x800DC244,high|countdown))return false;
    std::array<uint32_t,4> payload;
    if(int32_t(countdown)<=0){
      if(!m.read32(0x800DC2E0,level))return false;
      if(int32_t(level)<4){
        if(!m.write32(0x800DC244,high|16)||!callback_sound(services,0x80002F12)||!m.write32(0x800DC2DC,0x8010F28C)||!callback_vector(m,in.draw+0x20,payload)||!append_event_packet(m,0x30A04,payload)||!append_object_event(m,0x12904,in.object))return false;
      }
    }
    uint32_t pointer,descriptor,data,size;
    if(!m.read32(0x800DC244,word)||!callback_vector(m,in.draw+0x20,payload)||!m.read32(0x8011316C,pointer)||!m.read32(pointer+0x10,descriptor)||!m.read32(pointer+0x14,data))return false;
    if(!descriptor)return append_object_event(m,1,in.object);
    if(!m.write32(0x80113170,data)||!m.read32(data,size))return false;
    payload[2]=((size-0x2000)<<16)|0xF00;payload[3]=descriptor;
    return append_event_packet(m,0x30204,payload)&&append_object_event(m,0x21C00,in.object);
}
inline bool callback_emit_12602(CommandMemory& m,const CallbackInput& in,CallbackServices* services){
    std::array<uint32_t,4> payload;
    if(!callback_sound(services,0x2F0C)||!callback_vector(m,in.draw+0x20,payload))return false;
    payload[2]=0xABCDEF00;
    return append_event_packet(m,0x30004,payload)&&append_object_event(m,0x12602,in.object);
}
inline bool callback_optional_event(CommandMemory& m,const CallbackInput& in,CallbackServices* services){
    uint32_t paused,frame,y,value;
    if(!m.read32(0x80110AC4,paused))return false;if(paused&1)return true;
    if(!m.read32(0x80110AA0,frame))return false;if((frame&7)!=7)return true;
    if(!m.read32(in.draw+0x24,y))return false;if((y>>16)<=0x300)return true;
    if(!callback_random(m,1,value))return false;if(((value>>13)&255)>32)return true;
    return m.write32(0x80110AA0,frame+1)&&callback_emit_12602(m,in,services);
}
inline bool callback_periodic_event(CommandMemory& m,const CallbackInput& in,uint32_t limit,CallbackServices* services){
    uint32_t cap,frame;if(!m.read32(0x801095A0,cap))return false;if(int32_t(limit)>int32_t(cap))return true;
    if(!m.read32(0x80110AA0,frame))return false;
    return (frame&15)||callback_emit_12602(m,in,services);
}
inline bool callback_track_appearance(CommandMemory& m,const CallbackInput& in,uint32_t appearance,CallbackServices* services){
    uint32_t x,y,slot,minimum,oldPosition,flags,word;
    if(!m.read32(in.draw+0x20,x)||!m.read32(in.draw+0x24,y))return false;
    slot=0x800DC2F0+((x>>16)&15)*4;
    if(!m.read32(slot,word))return false;
    if(int32_t(word)>int32_t(y)&&!m.write32(slot,y))return false;
    if(!m.read32(0x800DC260,minimum))return false;
    if(int32_t(y)<int32_t(minimum)){
        if(!m.write32(0x800DC264,minimum)||!m.write32(0x800DC260,y)||!m.read32(0x800DC268,oldPosition)||!m.write32(0x800DC268,x)||!m.write32(0x800DC26C,oldPosition))return false;
    }
    if(!m.read32(0x800DC244,flags))return false;bool near=false;
    if(flags&0x40000){std::array<uint32_t,4> reference;
        if(!callback_vector(m,0x800DC2D0,reference)||!m.read32(in.draw+0x20,x)||!m.read32(in.draw+0x24,y))return false;
        uint32_t dx=((x>>16)&15)-((reference[0]>>16)&15);if(int32_t(dx)<0)dx=0U-dx;
        uint32_t dy=y-reference[1];if(int32_t(dy)<0)dy=0U-dy;
        near=int32_t(dx)<=2&&int32_t(dy)<=0x600000;
        if(near&&!callback_sound(services,0x80001028))return false;
    }
    if(!near){
        if(!m.read32(0x800DC250,word))return false;if(!(word&0x10000))return true;
        uint32_t random;if(!callback_random(m,1,random)||!m.write32(0x800DC250,((word|(((random&1)+1)<<8))&~0x10000U))||!m.write32(0x80112D34,in.draw))return false;
    }
    if(!m.read32(in.object+0x38,word)||!m.write32(in.object+0x38,word|0x100000)||!m.write32(0x8011316C,0x8011316C)||!callback_sound(services,0x1814))return false;
    std::array<uint32_t,4> payload;
    if(!callback_vector(m,in.draw+0x20,payload)||!m.read32(in.draw+0x18,word))return false;
    payload[2]=((word-0x2000)<<16)|0xF00;payload[3]=appearance;
    return append_event_packet(m,0x30204,payload)&&append_object_event(m,0x11C01,in.object)&&m.write32(0x800DC2DC,0x8010F1F4);
}
inline bool callback_track_then_event(CommandMemory& m,const CallbackInput& in,CallbackServices* services){
    uint32_t x;
    return callback_track_appearance(m,in,0x8010A27C,services)&&m.read32(in.draw+0x20,x)&&((x&65535)||callback_optional_event(m,in,services));
}
inline bool callback_mark_collision(CommandMemory& m,const CallbackInput& in,uint32_t appearance){
    uint32_t pointer;return m.read32(0x8011316C,pointer)&&m.write32(pointer+0x10,appearance)&&m.write32(pointer+0x14,in.draw+0x18);
}
inline bool callback_emit_21B00(CommandMemory& m,const CallbackInput& in){
    std::array<uint32_t,4> payload;if(!callback_vector(m,in.draw+0x20,payload))return false;
    payload[2]=0;payload[3]=0;return append_event_packet(m,0x30104,payload)&&append_object_event(m,0x21B00,in.object);
}
inline bool callback_enemy_contact(CommandMemory& m,const CallbackInput& in,uint32_t appearance,CallbackServices* services){
    uint32_t flags,value;if(!m.read32(0x801131B4,flags))return false;
    if(!(flags&0x100000))return append_object_event(m,2,in.object);
    if(flags&0x100){if(!m.read32(0x800DC274,value)||!m.write32(0x800DC274,value|0x20))return false;}
    else if(!callback_mark_collision(m,in,appearance)||!callback_sound(services,0x80002F0D))return false;
    return callback_emit_21B00(m,in);
}
inline bool callback_disable_common(CommandMemory& m,const CallbackInput& in){
    uint32_t count,flags;if(!m.read32(in.parameters+0x30,count)||!m.write32(in.parameters+0x30,count-1)||!m.write32(in.values+0x3C,0)||!m.write32(in.values+4,0)||!m.write32(in.values+0x14,0)||!m.write32(in.object+0x30,0)||!m.write32(in.object+0x38,0)||!m.read32(0x801131B4,flags))return false;
    if(flags&0x100)return m.write32(in.object+0x30,0)&&m.write32(in.parameters+0x14,in.object);
    uint32_t vector=(in.values+0x20)&~15U,words[4]{0,0x1000,0x10000,0x82000304};
    for(unsigned k=0;k<4;++k)if(!m.write32(vector+k*4,words[k]))return false;
    return m.write32(in.object+0x30,0x200000)&&m.write32(in.object+0x24,0x900);
}
inline bool callback_disable_counted(CommandMemory& m,const CallbackInput& in){
    uint32_t mode,count;if(!m.read32(in.object+0x30,mode)||!m.read32(in.parameters+0x2C,count))return false;
    if(!(mode&0x1000000)&&!m.write32(in.parameters+0x2C,count-1))return false;
    return callback_disable_common(m,in);
}
inline bool callback_disable_hit(CommandMemory& m,const CallbackInput& in,CallbackServices* services){return callback_sound(services,0x80003F35)&&callback_disable_counted(m,in);}
inline bool callback_counter_positive(uint32_t previous){return int64_t(int32_t(previous))-1>0;}
inline bool callback_turn_velocity(CommandMemory& m,const CallbackInput& in){
    uint32_t parameters,velocity,x,configuration,mode,first,second;
    if(!m.read32(0x80113164,parameters)||!m.read32(parameters+0x10,velocity)||!m.read32(in.draw+0x20,x))return false;
    uint32_t index=(x>>16)&15;
    if(!m.read32(0x800DACC0+index*4,first)||!m.read32(0x800AF7B0,configuration)||!m.read32(0x8010F170,mode))return false;
    uint32_t count=(configuration>>8)&31;bool wrap=(configuration&((mode>>1)^1)&1)!=0,negative;
    unsigned budget=64;
    for(;;){if(!budget--)return false;negative=(velocity&0x80000000)!=0;++index;--count;
        if(negative){index-=2;if(int32_t(index)<0){if(wrap)index=count;else{velocity=0U-velocity;if(!m.write32(parameters+0x10,velocity))return false;continue;}}}
        else if(int32_t(index)>int32_t(count)){if(wrap)index=0;else{velocity=0U-velocity;if(!m.write32(parameters+0x10,velocity))return false;continue;}}
        break;
    }
    if(!m.read32(0x800DACC0+index*4,second))return false;
    if(negative){uint32_t old=first;first=second;second=old;}
    first=uint32_t(int32_t(first<<16)>>16);second=uint32_t(int32_t(second<<16)>>16);
    uint32_t difference=uint32_t(int32_t(((second-first)-0x8000)<<16)>>16);
    if(int32_t(difference)>=0)difference=0U-(0xFFFF-difference);
    if(int32_t(difference)<0)difference=0U-difference;
    uint32_t factor;if(!m.read32(0x80113338+((difference>>12)&15)*4,factor)||!m.read32(parameters+0x10,velocity))return false;
    return m.write32(in.values+4,command_mul(factor,velocity,16))&&m.write32(in.values+0x3C,0);
}
struct CallbackMotion{uint32_t position,velocity,flags;};
inline CallbackMotion callback_motion(uint32_t position,uint32_t velocity,uint32_t limit,uint32_t acceleration,uint32_t drag,uint32_t target){
    bool negative=int32_t(velocity)<0;uint32_t flag=0;
    if(int32_t(target)<=int32_t(position)){
        velocity-=acceleration;
        if(!negative){velocity=command_mul(drag,velocity,16);flag=4;}
        else{uint32_t magnitude=int32_t(velocity)<0?0U-velocity:velocity;if(int32_t(magnitude)>int32_t(limit))velocity=limit;}
    }else{
        velocity+=acceleration;
        if(negative){velocity=command_mul(drag,velocity,16);flag=4;}
        else if(int32_t(velocity)>int32_t(limit))velocity=limit;
    }
    return {position+velocity,velocity,flag};
}
inline bool callback_record_lane(CommandMemory& m,uint32_t x,uint32_t& y){
    uint32_t address=0x800AF7B4+((x>>16)&15)*4,old;
    if(!m.read32(address,old))return false;
    if(old&&int32_t(old<<16)<int32_t(y))return true;
    y>>=16;return m.write32(address,y);
}
inline bool callback_track_lanes(CommandMemory& m,const CallbackInput& in,bool all,CallbackServices* services){
    uint32_t velocity,y,x,parameters,upper,count;
    if(!callback_track_appearance(m,in,0x8010A488,services)||!callback_optional_event(m,in,services)||!m.read32(in.values+0x14,velocity)||!m.read32(in.draw+0x24,y))return false;
    if(int32_t(velocity)>=0)return true;
    if(all){for(int lane=15;lane>=0;--lane){uint32_t savedY=y;if(!callback_record_lane(m,uint32_t(lane)<<16,savedY))return false;}}
    else if(!m.read32(in.draw+0x20,x)||!callback_record_lane(m,x,y))return false;
    if(!m.read32(0x80113164,parameters)||!m.read32(parameters+0x1C,upper))return false;
    bool flip=int32_t(upper)>=int32_t(y);
    if(!flip){if(!m.read32(parameters+0x10,count)||!m.write32(parameters+0x10,count-1))return false;flip=int64_t(int32_t(count))-1<0;}
    return !flip||m.write32(in.values+0x14,0U-velocity);
}
inline uint32_t callback_shifted_one(uint32_t shift){
    shift&=63;if(shift&32){unsigned amount=64-shift;return amount<32?1U<<amount:0;}return 1U>>shift;
}
inline bool callback_global_emit(CommandMemory& m,const CallbackInput& in,CallbackServices* services,uint32_t& volatile3){
    uint32_t flags,phase;if(!m.read32(0x800DC244,flags)||!m.read32(0x800DC2D8,phase))return false;
    if(!(flags&0x40000))return true;
    std::array<uint32_t,4> payload;
    if(phase&0x80000000){
        if(!callback_vector(m,0x800DC2D0,payload))return false;
        payload[2]=(((payload[0]>>2)-0x2000)<<16)|0xF00;payload[3]=0x8010A46C;volatile3=payload[3];
        return append_event_packet(m,0x30204,payload)&&append_object_event(m,0x11C01,in.object);
    }
    if(((phase>>16)&255)>=5)return true;
    if(!callback_sound(services,0x80001F04)||!callback_vector(m,0x800DC2D0,payload))return false;
    volatile3=payload[3];return append_event_packet(m,0x31204,payload)&&append_object_event(m,0x11301,in.object);
}
inline bool callback_player_terminate(CommandMemory& m,const CallbackInput& in,uint32_t arg1,uint32_t arg2){
    uint32_t flags,parameters;
    if(!m.write32(0x80113158,0)||!m.write32(0x8011315C,0)||!append_event_packet(m,0x30004,{0x8010CE80,arg1,arg2,0x8011315C})||!append_object_event(m,0x12D05,in.object)||!m.read32(0x800DC274,flags)||!m.write32(0x800DC274,(flags|4)&~0x20U)||!m.write32(in.object+8,0x05020016)||!m.read32(0x80113164,parameters))return false;
    return m.write32(parameters,0)&&m.write32(parameters+4,0)&&m.write32(in.parameters+0x28,2);
}
inline bool callback_player_actions(CommandMemory& m,const CallbackInput& in,CallbackServices* services){
    uint32_t volatile3=in.arguments[3],flags,a,b,word;
    if(!callback_global_emit(m,in,services,volatile3)||!m.read32(0x800DC274,flags))return false;
    if(flags&0x20)return callback_player_terminate(m,in,0x800DC274,in.arguments[2]);
    if(flags&0x40){
        if(!m.write32(0x800DC274,flags&~0x40U)||!append_event_packet(m,0x30004,{0x8010CF70,0x800DC274,in.arguments[2],volatile3})||!append_object_event(m,0x12D05,in.object))return false;
    }
    if(!m.read32(0x800DC27C,a)||!m.read32(0x800DC2C4,b))return false;if(int32_t(a>>24)>=int32_t(b))return true;
    if(!m.read32(0x800DC274,flags))return false;if(flags&4)return true;
    bool bypass=(flags&1)!=0;
    if(!bypass){if(!m.read32(0x800DC244,word))return false;bypass=(word&0x10000)!=0;}
    if(!bypass){if(!m.read32(0x80113160,word))return false;if(word&0x1000000)return true;}
    if(!m.read32(0x80110AC4,word))return false;if(word&1)return true;
    if(!m.read32(0x800DC244,flags))return false;
    if(flags&0x20000){if(!m.read32(0x80113160,word))return false;if(!(word&0x1000000))return callback_sound(services,0x80001F1D)&&append_object_event(m,0x13701,in.object);}
    return callback_sound(services,0x80001F04)&&append_object_event(m,0x11301,in.object);
}
inline bool callback_split_event(CommandMemory& m,const CallbackInput& in,uint32_t extra,uint32_t appearance,uint32_t step,uint32_t last,bool alternate){
    std::array<uint32_t,4> payload;if(!callback_vector(m,in.draw+0x20,payload))return false;
    uint32_t shifted=step<<16,position=payload[0]+shifted;
    bool below=int64_t(int32_t(payload[0]))+int32_t(shifted)<0;
    if(!below&&int32_t(last)>int32_t(position>>16))payload[0]=position;
    payload[2]=extra;payload[3]=appearance;
    uint32_t count;
    return append_event_packet(m,alternate?0x30D04:0x30E04,payload)&&append_object_event(m,alternate?0x11102:0x12402,in.object)&&m.read32(in.parameters+0x30,count)&&m.write32(in.parameters+0x30,count+1);
}
inline bool callback_split_contact(CommandMemory& m,const CallbackInput& in,bool alternate,CallbackServices* services){
    uint32_t flags,configuration;
    if(!m.read32(0x801131B4,flags))return false;
    if(flags&0x100000){if(!callback_score(m,in.object,alternate?500:1000,services)||!callback_mark_collision(m,in,alternate?0x8010A060:0x8010A02C))return false;}
    if(!callback_sound(services,0x80001F16)||!m.read32(0x800AF7B0,configuration))return false;
    uint32_t last=((configuration>>8)&31)-1,extra=alternate?0xE00:0x600;
    return callback_split_event(m,in,extra,0xFFFFFFFF,0xFFFFFFFF,last,alternate)&&callback_split_event(m,in,extra,1,1,last,alternate)&&callback_disable_common(m,in);
}
inline unsigned callback_bit_length(uint32_t value){unsigned n=0;for(;value;value>>=1)++n;return n;}
inline bool callback_count_prepared_object(CommandMemory& m){
    uint32_t pointer,kind,value;
    if(!m.read32(0x80113164,pointer)||!m.read32(pointer,kind))return false;
    if(kind==0x8010A2F4)return m.read32(0x800DC2D8,value)&&m.write32(0x800DC2D8,(value&0xFFFF0000)|((value&65535)+1));
    return m.read32(0x800DC27C,value)&&m.write32(0x800DC27C,(value&0xFFFF00FF)|((((value>>8)&255)+1)<<8));
}
inline bool callback_select_lane(CommandMemory& m,const CallbackInput& in){
    uint32_t paused,configuration,mode,position;
    if(!m.read32(0x80110AC4,paused))return false;if(paused&1)return true;
    if(!m.read32(0x800AF7B0,configuration)||!m.read32(0x8010F170,mode)||!m.read32(in.values,position))return false;
    uint32_t lanes=(configuration>>8)&31;bool wrap=(configuration&((mode>>1)^1)&1)!=0;
    uint32_t current=(position>>16)&15,best=current,nearest=0x08000000;
    int32_t cursor=int32_t(current)-6;
    for(unsigned k=0;k<14;++k,++cursor){
        uint32_t index=uint32_t(cursor);
        if(cursor<0)index=wrap?index+lanes:0;
        else if(int32_t(lanes)<=cursor)index=wrap?index-lanes:lanes-1;
        uint32_t sample,y;if(!m.read32(0x800DC280+index*4,sample))return false;
        if(sample==0x08000000)continue;
        if(!m.read32(in.draw+0x24,y))return false;
        uint32_t delta=sample-y;
        if(int64_t(int32_t(sample))-int32_t(y)<=0||int32_t(nearest)<=int32_t(delta))continue;
        nearest=delta;best=index;
    }
    uint32_t acceleration=current==best?0:int32_t(current)<int32_t(best)?0x1C00:0xFFFFE400;
    uint32_t marker,velocity;
    if(!m.read32(0x800DC27C,marker)||!m.read32(in.values+4,velocity))return false;
    velocity+=acceleration;if(!m.write32(in.values+4,velocity))return false;
    uint32_t x=(position&0xFFFFF)+velocity,maximum=((lanes-1)<<16)|65535;
    if(int32_t(velocity)>0){if(int32_t(x)>int32_t(maximum))x=wrap?x-maximum:maximum;}
    else if(int32_t(x)<0)x=wrap?x+maximum:0;
    if(!m.write32(in.values,x))return false;
    uint32_t drawX,drawY,parameters,packed;
    if(!m.read32(0x80113164,parameters)||!m.read32(in.draw+0x20,drawX)||!m.read32(in.draw+0x24,drawY))return false;
    packed=(((drawX>>8)&65535)<<16)|(drawY>>16);
    for(unsigned k=0;k<3;++k){uint32_t destination=parameters+0x18-k*4,old;if(!m.read32(destination-4,old)||!m.write32(destination,old?old:packed))return false;}
    if(!m.read32(in.draw+0x20,drawX)||!m.read32(in.draw+0x24,drawY)||!m.write32(parameters+0xC,(((drawX>>8)&65535)<<16)|(drawY>>16))||!m.read32(parameters+0x18,packed)||!m.write32(in.draw+4,packed))return false;
    // Original entry falls through into the object-category counter at 1346E.
    return callback_count_prepared_object(m);
}
inline uint32_t callback_magnitude(uint32_t value){return int32_t(value)<0?0U-value:value;}
inline bool callback_evade_player(CommandMemory& m,const CallbackInput& in,CallbackServices* services){
    uint32_t parameters,target,y,velocity;
    if(!m.read32(0x80113164,parameters)||!m.read32(parameters+0x20,target)||!m.read32(in.values+0x10,y)||!m.read32(in.values+0x14,velocity))return false;
    velocity+=int32_t(target)>int32_t(y)?0x8000:0xFFFF8000;
    if(int32_t(callback_magnitude(velocity))<0xC0000&&!m.write32(in.values+0x14,velocity))return false;
    uint32_t player,configuration,x,mode,lanes;
    if(!m.read32(0x800DC248,player)||!m.read32(0x800AF7B0,configuration)||!m.read32(in.values,x)||!m.read32(in.values+4,velocity)||!m.read32(parameters+0x24,mode))return false;
    player=(player>>16)&15;uint32_t own=(x>>16)&15;lanes=(configuration>>8)&31;
    uint32_t chosen=player;
    if(!(mode&8)){
        uint32_t distance=callback_magnitude(player-own);
        if(int32_t(distance)>5){mode&=~1U;velocity=command_mul(0xF000,velocity,16);if(!m.write32(in.values+4,velocity))return false;goto after_acceleration;}
        uint32_t topology;if(!m.read32(0x8010F170,topology))return false;
        bool wrap=(configuration&((topology>>1)^1)&1)!=0,endpoint=false;
        chosen=lanes;
        if(!wrap){
            if(mode&1)endpoint=true;
            else if(int32_t(player)>int32_t(own)){
                --own;if(int32_t(own)<0){chosen=own;mode|=1;chosen=lanes;endpoint=true;}else chosen=own;
            }else{
                ++own;chosen=own;if(int32_t(lanes)<=int32_t(own)){mode|=1;chosen=lanes;endpoint=true;}
            }
        }else{
            uint32_t half=lanes>>1;if(int32_t(player)<=int32_t(own))half=0U-half;
            chosen=own+half;
            if(int32_t(chosen)<0)chosen+=lanes;
            else if(int32_t(lanes)>=int32_t(chosen))chosen-=lanes;
            if(int32_t(distance)<=0){mode|=1;endpoint=true;}
        }
        if(endpoint){lanes>>=1;if(int32_t(lanes)<=int32_t(player))chosen=0;}
    }
    if(!m.read32(in.values,x))return false;
    velocity+=int32_t(chosen<<16)>int32_t(x)?0x800:0xFFFFF800;
    if(int32_t(callback_magnitude(velocity))<0x2000&&!m.write32(in.values+4,velocity))return false;
    after_acceleration:
    if(!m.write32(parameters+0x24,mode))return false;
    if(mode&1){uint32_t blocked,paused;
        if(!m.read32(0x801131B8,blocked))return false;
        if(!(blocked&3)){
            if(!m.read32(0x80110AC4,paused))return false;
            if(!(paused&1)){std::array<uint32_t,4> payload;
                if(!callback_sound(services,0x2F2D)||!callback_vector(m,in.draw+0x20,payload))return false;
                payload[2]=0xABCDEF00;
                if(!append_event_packet(m,0x30004,payload)||!append_object_event(m,0x11E02,in.object))return false;
            }
        }
    }
    if(mode&6){uint32_t timer;if(!m.read32(parameters+0x28,timer))return false;uint32_t high=timer>>16,remaining=(timer&65535)-1;
        if(int32_t(remaining)<=0){remaining=high;if(!m.write32(parameters+0x20,0x01000000)||!m.write32(parameters+0x24,mode|8))return false;}
        if(!m.write32(parameters+0x28,remaining|(high<<16)))return false;
    }
    return callback_track_appearance(m,in,0x8010A27C,services);
}
inline bool callback_random_drop(CommandMemory& m,const CallbackInput& in,CallbackServices* services,uint32_t previousHandle){
    uint32_t random;if(!callback_random(m,1,random))return false;
    uint32_t byte=random&255,asset=byte<=0x60?0x800FC4B0:byte<=0xC0?0x800FDCC0:0x800FF4D0;
    uint32_t cue=byte<=0x60?0x80004F0E:byte<=0xC0?0x80004F0F:0x80004F10;
    if(!callback_sound(services,cue)||!append_event_packet(m,0x30004,{asset,random,0x80112C54,previousHandle})||!m.write32(0x20100F78,0x12802))return false;
    return append_object_event(m,0x12802,in.object);
}
inline bool callback_power_hit(CommandMemory& m,const CallbackInput& in,uint32_t flags,CallbackResult& out,CallbackServices* services){
    uint32_t handle,count,parameters;
    if(flags&0x100){if(!m.write32(0x80112D38,in.draw)||!m.write32(in.object+0x24,0x042F0000)||!m.write32(in.object+0x30,0)||!m.write32(in.object+0x38,0)||!m.write32(0x800DC2DC,0x8010F20C))return false;}
    if(!callback_score(m,in.object,500,services)||!callback_mark_collision(m,in,0x80109CA8)||!callback_sound(services,0x80001F00,&handle)||!m.read32(0x801131B4,flags))return false;
    if(!(flags&0x800)&&!callback_random_drop(m,in,services,handle))return false;
    if(!m.read32(0x80113164,parameters)||!m.read32(parameters+0x34,count))return false;
    if(count){if(!m.read32(in.parameters+0x2C,count)||!m.write32(in.parameters+0x2C,count-1))return false;}
    out.unwindWords=flags&0x800?0:4;
    return callback_disable_common(m,in);
}
inline bool execute_callback(CommandMemory& m,const CallbackInput& in,CallbackResult& out,CallbackServices* services=nullptr){
    out={0};uint32_t a,b,c,d,command=0;bool prepare=false;
    switch(in.entry){
    case 0x80013998:return callback_track_appearance(m,in,0x8010A2A0,services);
    case 0x800139A4:
        if(!m.read32(in.draw+0x20,a)||!m.read32(0x800DC248,b)||!m.read32(0x800DC24C,c))return false;
        if(((a>>16)&15)==((b>>16)&15)){
            if(!m.read32(0x800DC274,a)||!m.write32(0x800DC274,a|0x24)||!m.read32(0x80113164,b)||!m.read32(b+8,c)||!m.write32(b+8,0x200))return false;
            if(int32_t(c)<=100&&!m.write32(0x800DC2DC,0x8010F20C))return false;
        }
        if(!m.read32(0x80113164,b)||!m.read32(b+8,c)||!m.write32(b+8,c-1))return false;
        return callback_counter_positive(c)||append_object_event(m,2,in.object);
    case 0x800138AE:
        if(!m.read32(0x801131B4,a))return false;
        if(a&0x100000){
            if(!callback_score(m,in.object,106,services)||!m.read32(0x8011316C,b)||!m.write32(b+0x10,0x80109A24)||!callback_sound(services,0x80001F05))return false;
            out.unwindWords=4;return callback_disable_common(m,in);
        }
        if(!(a&0x1000000))return true;
        if(!m.read32(0x80113164,b)||!m.read32(b+0x24,c)||!callback_random(m,2,d)||!m.write32(b+0x24,c&~8U)||!m.write32(b+0x20,0x02000000+((d&15)<<20)))return false;
        if(!(c&4))return true;
        {std::array<uint32_t,4> payload;if(!callback_sound(services,0x2F2B)||!callback_vector(m,in.draw+0x20,payload))return false;payload[2]=d;return append_event_packet(m,0x30004,payload)&&append_object_event(m,0x10A02,in.object);}
    case 0x80013A2A:
        if(!m.read32(0x801131B4,a))return false;
        if(!(a&0x100000)){CallbackInput shared=in;shared.entry=0x80013BB4;return execute_callback(m,shared,out,services);}
        if(!m.read32(0x80113164,b)||!m.read32(b+0x24,c)||!m.write32(b+0x24,c-1))return false;
        if(!callback_counter_positive(c))return callback_mark_collision(m,in,0x8010A27C)&&callback_disable_counted(m,in);
        {std::array<uint32_t,4> payload;if(!callback_vector(m,in.draw+0x20,payload))return false;payload[2]=0xABCDEF00;
        return append_event_packet(m,0x30004,payload)&&append_object_event(m,0x16002,in.object)&&callback_sound(services,0x80001F32)&&callback_score(m,in.object,50,services)&&callback_mark_collision(m,in,0x8010A2A0);}
    case 0x80013AD0:
        if(!m.read32(0x801131B4,a))return false;
        if(!(a&0x100000)){CallbackInput shared=in;shared.entry=0x80013BB4;return execute_callback(m,shared,out,services);}
        if(!callback_sound(services,0x80001F2E)||!callback_score(m,in.object,50,services))return false;
        if(a&0x100)return callback_mark_collision(m,in,0x8010A27C)&&callback_disable_counted(m,in);
        if(!m.read32(0x80113164,b)||!m.read32(b+0x30,c))return false;
        if(int64_t(int32_t(c))-1<0)return callback_mark_collision(m,in,0x8010A27C)&&callback_disable_counted(m,in);
        if(!m.write32(b+0x30,c-1))return false;
        {std::array<uint32_t,4> payload;if(!callback_vector(m,in.draw+0x20,payload))return false;payload[2]=0xABCDEF00;
        if(!append_event_packet(m,0x30004,payload)||!m.read32(b+0x30,c)||!append_object_event(m,c?0x14702:0x14802,in.object))return false;
        return callback_mark_collision(m,in,0x8010A27C);}
    case 0x800144F2:
        if(!m.read32(0x801131B4,a))return false;if(a&0x1000000)return append_object_event(m,2,in.object);
        return m.read32(0x800DC244,b)&&m.write32(0x800DC244,b|7)&&callback_sound(services,0x80001F34)&&m.write32(0x8011316C,0x8011316C);
    case 0x80014536:
        if(!m.read32(0x801131B4,a))return false;
        if(a&0x100000){
            if(!m.read32(0x800DC244,b)||!m.write32(0x800DC244,b|7)||!callback_sound(services,0x80001F33)||!m.read32(in.object+8,b))return false;
            if(((b>>16)&255)!=5){std::array<uint32_t,4> payload;if(!callback_vector(m,in.draw+0x20,payload))return false;payload[2]=0xABCDEF00;if(!append_event_packet(m,0x30004,payload)||!append_object_event(m,0x16302,in.object))return false;}
            if(!m.read32(0x80113164,b)||!m.read32(in.object+0x54,c)||!m.read32(b+0x20,d)||!m.write32(b+0x20,d-1)||!m.write32(in.object+0x54,c<<1))return false;
            if(callback_counter_positive(d))return true;
            if(!callback_sound(services,0x80001F33,&c)||!callback_random_drop(m,in,services,c)||!callback_score(m,in.object,500,services))return false;
        }
        out.unwindWords=4;return callback_mark_collision(m,in,0x80109CA8)&&callback_disable_common(m,in);
    case 0x80014376:
        if(!m.read32(0x801131B4,a)||!m.read32(0x80113164,b))return false;
        if(a&0x100000)return callback_power_hit(m,in,a,out,services);
        if(a&0x1000000){
            if(!m.read32(b+0x18,c)||!m.write32(in.values+4,c)||!m.write32(b+0x34,1)||!m.read32(in.values+0x14,c)||!m.write32(b+0x1C,c)||!m.write32(in.values+0x14,0)||!m.read32(in.parameters+0x2C,c)||!m.write32(in.parameters+0x2C,c+1)||!m.read32(in.object+0x30,d))return false;
            if(!(d&0x1000000)){out.trap=1;out.trapValue=d;return true;}
            return m.write32(in.object+0x30,d&~0x01A00000U);
        }
        if(a&0x200000){
            if(!m.read32(b+0x18,c)||!m.write32(in.values+4,c)||!m.read32(in.values+0x14,c)||!m.write32(b+0x1C,c)||!m.write32(in.values+0x14,0)||!m.read32(in.object+0x30,d))return false;
            return m.write32(in.object+0x30,(d&~0x200000U)|0x800000);
        }
        if(!m.read32(in.values+4,c)||!m.write32(in.values+4,0)||!m.write32(b+0x18,c)||!m.read32(b+0x1C,c)||!m.write32(in.values+0x14,c)||!callback_random(m,2,d))return false;
        d=(d&0xFFFFFF)|0x1000000;
        if(int32_t(c)>=0){if(!m.write32(b+0x14,0x05000000-d))return false;}
        else{
            uint32_t unused;
            if(!m.read32(in.parameters+0x30,unused)||!m.read32(in.parameters+0x2C,unused)||!m.read32(b+0x20,c)||!m.write32(b+0x20,c-1))return false;
            if(!callback_counter_positive(c)){
                if(!m.write32(b+0x10,0x01000000)||!m.write32(in.values+0x1C,0x88002345)||!m.read32(in.object+0x30,c))return false;
                return m.write32(in.object+0x30,(c&~0x800000U)|0x1000000);
            }
            if(!m.write32(b+0x10,d+0x01000000))return false;
        }
        return m.read32(in.object+0x30,c)&&m.write32(in.object+0x30,(c|0x200000)&~0x800000U);
    case 0x80013668:return callback_evade_player(m,in,services);
    case 0x80013612:
        if(!m.read32(0x801131B4,a))return false;
        if(!(a&0x100000)){CallbackInput shared=in;shared.entry=0x80013BB4;return execute_callback(m,shared,out,services);}
        return callback_score(m,in.object,150,services)&&m.write32(0x800DC2DC,0x8010F274)&&callback_mark_collision(m,in,0x801095C0)&&callback_disable_hit(m,in,services);
    case 0x800132C0:return callback_select_lane(m,in);
    case 0x800131DA:
        if(!m.read32(0x801131B4,a))return false;
        if((a&0x100000)||!(a&0x2000000)){out.unwindWords=4;return callback_mark_collision(m,in,0x8010A488)&&callback_sound(services,0x80003F1F)&&callback_disable_common(m,in);}
        if(!callback_random(m,1,a)||!m.read32(0x800AF7B0,b))return false;
        a=(a>>16)&15;b=(b>>8)&31;if(int32_t(b)<=int32_t(a))a=b-1;a=(a<<16)|0x8000;
        if(!m.read32(0x80113164,b)||!m.read32(b+0x18,c)||!m.write32(b+0x18,c-1))return false;
        if(!callback_counter_positive(c)){
            if(!m.read32(in.draw+0x24,d))return false;
            return append_event_packet(m,0x30004,{a,d,0x80112C54,in.arguments[3]})&&append_object_event(m,0x23300,in.object);
        }
        return m.write32(in.values,a)&&m.read32(b+0x14,c)&&m.write32(b+0x10,c)&&m.write32(in.values+0x14,0xFFEC0000);
    case 0x800147DA:out.unwindWords=4;return callback_split_contact(m,in,false,services);
    case 0x800148C6:out.unwindWords=4;return callback_split_contact(m,in,true,services);
    case 0x80014870:
        if(!m.read32(0x801131B4,a))return false;if(!(a&0x100000))return true;
        out.unwindWords=4;return callback_score(m,in.object,150,services)&&callback_mark_collision(m,in,0x8010AF74)&&callback_sound(services,0x80001F1F)&&callback_disable_common(m,in);
    case 0x80014A86:
        if(!m.read32(0x800DC248,a)||!m.read32(0x800DC24C,b)||!m.read32(in.draw,c)||!m.read32(in.draw+0x10,d))return false;
        if((a>>16)!=(c>>16))return true;
        if(!m.read32(0x800B18C8,a))return false;
        if(a&1){if(!m.write32(0x800B18C8,a|2))return false;}
        else{int32_t difference=(int32_t(b)>>16)-(int32_t(d)>>16);if(difference<=0&&(difference<0?-difference:difference)>0x200)return true;}
        if(!m.read32(0x800DC2E0,a))return false;++a;if(int32_t(a)<=4&&!m.write32(0x800DC2E0,a))return false;
        if(!m.read32(0x800DC244,a))return false;
        {uint32_t byte=(a>>16)&255,bits=callback_bit_length(byte),cue=0x80003F17,asset=0x8010CEF0,argument2=bits;
        if(bits==0){cue=0x80001F11;asset=0x8010CE90;}
        else if(bits==1){cue=0x80001F24;asset=0x8010CEB0;}
        else if(bits==2){cue=0x80001F21;asset=0x8010CED0;}
        else{
            if(bits==3){if(!m.read32(0x800DC244,a)||!m.write32(0x800DC244,a&~0x08000000U))return false;}
            asset=0x8010CF20;if(!m.read32(0x800DC2E4,argument2))return false;
            if(int32_t(argument2)<=2){asset=0x8010CF10;++argument2;if(!m.write32(0x800DC2E4,argument2))return false;}
        }
        if(bits<=2&&!append_object_event(m,0x14402,in.object))return false;
        if(!append_event_packet(m,0x30004,{asset,byte,argument2,asset})||!append_object_event(m,0x12D05,in.object)||!callback_sound(services,cue)||!m.read32(0x800DC2C4,a)||!m.write32(0x800DC2C4,a+2)||!m.write32(0x800DC2DC,0x8010F2A4)||!m.write32(in.values+0x20,0)||!m.write32(in.values+0x24,0x100)||!m.read32(0x800DC244,a))return false;
        byte=(a>>16)&255;bits=callback_bit_length(byte);byte|=1U<<bits;
        return m.write32(0x800DC244,a|(byte<<16))&&append_object_event(m,2,in.object);}
    case 0x800128FE:return callback_player_actions(m,in,services);
    case 0x80014CD6:
        if(!m.read32(0x800DC274,a))return false;
        if(a&0x20)return callback_player_terminate(m,in,in.arguments[1],in.arguments[2]);
        if(!m.read32(0x801131B4,b))return false;
        if(b&0x200){
            if(!append_event_packet(m,0x30004,{0x8010CE60,b,in.arguments[2],in.arguments[3]})||!append_object_event(m,0x12D05,in.object)||!m.read32(0x800DC274,a)||!m.write32(0x800DC274,a|4))return false;
            return m.write32(in.parameters+0x28,1);
        }
        return m.write32(0x800DC2DC,0x8010F254)&&m.write32(0x800DC274,a|4)&&callback_player_terminate(m,in,0x8010F254,0x800DC274);
    case 0x80014DCA:
        if(!m.write32(0x80112C40,in.draw-0x40)||!m.read32(0x800DC2E4,a)||!m.write32(0x80112C44,(a<<8)|3)||!m.read32(in.draw-0x20,b))return false;
        return m.write32(0x80112C48,b)&&m.write32(0x80112C4C,0x8010D840);
    case 0x800140DE:
        if(!callback_track_appearance(m,in,0x801095C0,services)||!m.read32(0x801095A0,a))return false;
        if(int32_t(a)>=0x28&&!callback_optional_event(m,in,services))return false;
        if(!m.read32(in.draw+0x20,a)||!m.read32(in.draw+0x10,b)||!m.read32(0x800AF7F4,c)||!m.read32(0x80113164,d))return false;
        {uint32_t lane=(a+0x8000)>>16,mask=c|callback_shifted_one(0U-lane),special,flags,playerY;
        if(!m.read32(in.object+0xB0,special))return false;
        if(special&1){mask=0xFFFFFFFF;if(!m.write32(d,0x80109680))return false;}
        else{
            if((b>>16)<=0x34){return m.write32(d,0x80109640)&&m.read32(0x800DC274,flags)&&m.write32(0x800AF7F4,mask)&&m.write32(0x800DC274,flags&~0x10U);}
            mask|=callback_shifted_one(0U-lane-16);
            if(!m.write32(d,0x80109680)||!m.read32(0x800DC248,a)||!m.read32(0x800DC274,flags))return false;
            if(((a>>16)&15)!=lane)goto finish_contact;
        }
        if(!m.read32(0x800DC24C,playerY)||!m.read32(0x800DC274,flags))return false;
        if(int32_t(playerY)>=0x01000000){
            if(!m.write32(0x80112D38,in.draw)||!m.write32(in.object+0x24,0x042F0000)||!m.write32(in.object+0x30,0)||!m.write32(in.object+0x38,0)||!m.write32(0x800DC2DC,0x8010F20C))return false;
            flags|=0x24;
        }
        finish_contact:
        if(!m.write32(0x800DC274,flags))return false;
        if(!(flags&0x10)){if(!m.write32(0x800DC274,flags|0x10)||!callback_sound(services,0x80003F1A))return false;}
        return m.write32(0x800AF7F4,mask);}
    case 0x80013148:
        if(!m.read32(in.draw+0x20,a)||!m.read32(0x800DC254,b)||!m.write32(0x800DC254,b|(1U<<(((a>>16)&15)+16))))return false;
        return callback_track_lanes(m,in,false,services);
    case 0x80013168:return callback_track_lanes(m,in,false,services);
    case 0x80013100:return callback_track_lanes(m,in,true,services);
    case 0x80012A20:
        if(!m.read32(0x80113164,a)||!m.read32(a,b))return false;if(int32_t(b)>40)return true;
        ++b;if(!m.write32(a,b))return false;if(int32_t(b)>40)return true;
        if(!m.read32(0x80110AA0,c))return false;if(!(c&1))return true;
        if(!callback_random(m,1,c)||!callback_random(m,2,c))return false;
        if((c&255)>=0xA0){
            if(!callback_sound(services,0x80002F04,&d)||!m.write32(a+4,d)||!callback_random(m,3,c))return false;
            uint32_t x,y;if(!m.read32(in.draw+0x20,x)||!m.read32(in.draw+0x24,y))return false;
            return append_event_packet(m,0x30104,{x,y,uint32_t(int32_t(c<<16)>>10),uint32_t(int32_t(c<<24)>>10)})&&append_object_event(m,0x11B01,in.object);
        }
        return callback_sound(services,0x80002F06,&d)&&m.write32(a+4,d)&&append_object_event(m,0x11C01,in.object);
    case 0x80012F08:
        if(!m.read32(0x800DC244,a))return false;if(!(a&0x40000))return true;
        if(!m.read32(0x80113164,a)||!m.write32(a,0x208)||!m.read32(0x800DC2D8,b)||!m.write32(0x800DC2D8,b|0x8000))return false;
        {std::array<uint32_t,4> payload;if(!callback_vector(m,in.draw+0x20,payload)||!m.write32(0x800DC2D0,payload[0])||!m.write32(0x800DC2D4,payload[1])||!m.read32(a+4,b)||!m.read32(a+8,c))return false;
        bool positive=callback_counter_positive(b);--b;if(!positive){b=0x50;c^=1;if(!callback_sound(services,0x80001F28))return false;}
        if(!m.write32(a+4,b)||!m.write32(a+8,c))return false;
        uint32_t nearest=0x08000000,lane=0;
        for(unsigned k=0;k<16;++k){if(!m.read32(0x800DC280+k*4,b))return false;if(int32_t(b)<=int32_t(nearest)){nearest=b;lane=k;}}
        if(!m.read32(a+8,c))return false;
        if(c&&nearest==0x08000000)return m.write32(a+8,0)&&m.write32(a+4,0x50);
        if(!callback_vector(m,in.values,payload))return false;
        CallbackMotion movement=callback_motion(payload[0],payload[1],0x18000,0x1000,0xE800,(lane<<16)|0x8000);
        if(!m.write32(in.values+4,movement.velocity)||!callback_vector(m,in.values+0x10,payload))return false;
        movement=callback_motion(payload[0],payload[1],0x800000,c?0x80000:0x40000,c?0xE800:0xE000,c?nearest:0x800000);
        if(!m.write32(in.values+0x14,movement.velocity))return false;
        if(c)return true;
        if(!m.read32(in.values+0x10,b))return false;if((b>>16)>0x140)return true;
        return m.read32(0x800DC2D8,b)&&m.write32(0x800DC2D8,b&~0x8000U);}
    case 0x80013FEE:
        if(!m.read32(0x80113164,a)||!m.read32(a+0xC,b)||!m.read32(0x80110AC4,c))return false;
        if(!(c&1)){
            if(int32_t(b)>0x05000000){b-=0x004C0000;if(!m.write32(a+0xC,b))return false;}
            else if(int32_t(b)==0x05000000){b-=0x004C0000;if(!m.write32(a+0xC,b))return false;}
            else{
                if(!m.read32(a+0x14,c)||!m.write32(in.draw+0x20,c)||!m.read32(a+0x10,d))return false;
                if(d){std::array<uint32_t,4> payload;
                    if(!callback_vector(m,in.draw+0x20,payload)||!m.read32(a+0xC,payload[1])||!m.read32(a+0x14,payload[0])||!append_event_packet(m,0x30004,payload))return false;
                    return append_object_event(m,0x20000|(d<<8),in.object);
                }
            }
        }
        return m.read32(a+0x14,c)&&m.write32(in.draw+0x20,c)&&m.write32(in.draw+0x30,0x7000);
    case 0x8001409E:
        if(!callback_track_appearance(m,in,0x8010AF74,services)||!callback_periodic_event(m,in,0x80,services)||!m.read32(0x80113164,a)||!m.read32(0x800DC258,b)||!m.read32(a+0x10,c))return false;
        return m.write32(0x800DC258,b+uint32_t(int32_t(c)>>5));
    case 0x800135C0:return callback_enemy_contact(m,in,0x8010A178,services);
    case 0x80013574:return callback_enemy_contact(m,in,0x8010B928,services);
    case 0x8001281E:
        if(!callback_random(m,1,a)||!m.read32(0x8010B8E8+((a>>5)&15)*4,b))return false;
        c=a&0x01FF00FF;d=((a&0xFFF)|0x800)|(((c>>2)-0x2000)<<16);
        return append_event_packet(m,0x30204,{c,0x03000000,d,b})&&append_object_event(m,0x15C01,in.object);
    case 0x8001346E:return callback_count_prepared_object(m);
    case 0x80013F56:
        if(!m.read32(in.parameters+0x34,a))return false;if(!a)return true;
        if(!m.read32(in.parameters+0x3C,b))return false;
        c=(b&65535)-1;d=(b&0xFFFF0000)|c;
        if(!m.write32(in.parameters+0x3C,d))return false;if(c)return true;
        if(!m.write32(in.parameters+0x3C,d|(b>>16))||!m.read32(in.parameters+0x38,c))return false;if(!c)return true;
        if(!m.read32(0x80113164,d)||!m.write32(d+0x10,c)||!m.write32(in.parameters+0x38,0)||!m.write32(in.parameters+0x34,a-1))return false;
        {std::array<uint32_t,4> payload;
        if(!m.read32(in.draw+0x38,payload[0])||!m.read32(in.draw+0x24,payload[1])||!m.read32(d+0x10,payload[2])||!m.read32(0x800DC2C8,payload[3]))return false;
        return append_event_packet(m,0x30004,payload)&&append_object_event(m,0x12302,in.object);}
    case 0x80013CA2:
        if(!m.read32(0x801131B4,a))return false;if(!(a&0x1000000))return true;
        if(!m.write32(in.object+0x30,0x01300000)||!m.write32(in.object+0x38,0x220)||!m.write32(in.object+0x24,0x03130E04)||!m.read32(0x80113164,b)||!m.read32(b+0x20,c)||!m.write32(in.values+0x14,c))return false;
        for(unsigned k=0;k<4;++k)if(!m.write32(((in.values+0x20)&~15U)+k*4,0))return false;
        return m.read32(b+0x24,c)&&m.write32(in.values+0x34,c);
    case 0x80013D14:
        if(!m.read32(0x801131B4,a))return false;if(!(a&0x1000000))return true;
        if(!m.read32(0x80113164,b)||!m.read32(b+0x30,c)||!m.write32(in.object+0x30,c)||!m.write32(in.object+0x38,0x20)||!m.read32(b+0x2C,c)||!m.write32(in.object+0x24,c))return false;
        for(unsigned k=0;k<4;++k)if(!m.write32(((in.values+0x20)&~15U)+k*4,0))return false;return true;
    case 0x80013BB4:
        if(!m.read32(0x801131B4,a))return false;
        if(a&0x100000)return callback_score(m,in.object,50,services)&&callback_mark_collision(m,in,0x8010A27C)&&callback_disable_hit(m,in,services);
        if(a&0x1000000){
            if(!m.read32(in.parameters+0x2C,b)||!m.write32(in.parameters+0x2C,b+1)||!m.read32(0x80113164,b)||!m.read32(b+0x1C,c))return false;
            if(c==0x80109640&&!m.write32(b+0x20,1))return false;
            if(!m.read32(0x80113164,b)||!m.read32(b+0x18,c)||!m.write32(b+0x14,c)||!m.read32(in.object+0x30,d)||!m.write32(in.object+0x30,(d&~0x1000000U)|0x800000))return false;
            return callback_turn_velocity(m,in);
        }
        if(a&0x800000){
            if(!m.read32(0x80113164,b)||!m.write32(in.values+4,0)||!m.write32(in.values+0x30,0)||!m.read32(b+0x14,c)||!m.write32(in.values+0x34,c)||!m.write32(in.values+0x3C,0x82000304)||!m.read32(in.object+0x30,d))return false;
            return m.write32(in.object+0x30,(d&~0x800000U)|0x200000);
        }
        return m.read32(in.object+0x30,d)&&m.write32(in.object+0x30,(d&~0x200000U)|0x800000)&&callback_turn_velocity(m,in);
    case 0x800124C0:return callback_track_appearance(m,in,0x8010A178,services);
    case 0x800126D4:return callback_track_then_event(m,in,services);
    case 0x80012ED0:
        if(!callback_track_appearance(m,in,0x8010A27C,services)||!m.read32(0x801095A0,a))return false;
        return int32_t(a)<0x4C||callback_optional_event(m,in,services);
    case 0x80012664:
        if(!m.read32(0x80113164,d)||!m.read32(in.draw+0x20,a)||!m.read32(in.draw+0x24,b)||!m.read32(0x800DC248,c))return false;
        if((b>>16)<=0x100){uint32_t velocity,configuration,mode;
            if(!m.read32(d+0x10,velocity))return false;
            int32_t difference=int32_t((c>>16)&15)-int32_t((a>>16)&15);
            if(int32_t(velocity)<0)velocity=0U-velocity;if(difference<0)velocity=0U-velocity;
            if(!m.read32(0x800AF7B0,configuration)||!m.read32(0x8010F170,mode))return false;
            if(((configuration|((mode>>1)^1))&1)&&int32_t((configuration>>9)&15)<=(difference<0?-difference:difference))velocity=0U-velocity;
            if(!m.write32(d+0x10,velocity))return false;
        }
        return callback_track_then_event(m,in,services);
    case 0x8001242E:
        if(!callback_periodic_event(m,in,0x40,services)||!m.read32(in.values+4,a)||!m.read32(0x80113164,b))return false;
        if(!a){if(!m.read32(b+0x38,c)||!m.write32(b+0x38,c+0x300))return false;}
        else if(!m.write32(b+0x38,(a&0x80000000)?0xFFFFC000:0x4000))return false;
        if(!m.read32(in.draw+0x20,a)||!m.read32(0x8010F170,b)||!m.write32(0x8010F170,b|1))return false;
        c=0x8010F174+((a>>16)&15)*4;
        return m.read32(c,b)&&m.write32(c,(b&0xFFFF0000)|((b+0x400)&65535))&&callback_track_appearance(m,in,0x8010A178,services);
    case 0x80012B12:return callback_hit_effect(m,in,services);
    case 0x80012CE6:
        if(!m.read32(0x801131B4,a))return false;
        if(a&0x400000){std::array<uint32_t,4> payload;
            if(!callback_vector(m,in.draw+0x20,payload)||!m.read32(0x8011316C,b)||!m.read32(b+0x10,c))return false;
            payload[2]=0x1F00;payload[3]=c==0x8010A178?0x8010A264:c;
            return append_event_packet(m,0x30204,payload)&&append_object_event(m,0x21C00,in.object);
        }
        return a&0x100000?callback_hit_effect(m,in,services):append_object_event(m,1,in.object);
    case 0x80011DF6:return true;
    case 0x80011DE4:return m.read32(0x80110AA4,a)&&m.write32(in.draw+0x24,a);
    case 0x80012DDE:return append_object_event(m,1,in.object);
    case 0x80012DD2:case 0x80012DD8:return append_object_event(m,2,in.object);
    case 0x800134E6:command=1;prepare=true;break;
    case 0x80013500:command=4;prepare=true;break;
    case 0x80013532:command=5;prepare=true;break;
    case 0x80012E7A:command=6;prepare=true;break;
    case 0x800134F2:command=7;prepare=true;break;
    case 0x8001350C:command=8;prepare=true;break;
    case 0x80013518:command=9;prepare=true;break;
    case 0x80012E28:command=12;prepare=true;break;
    case 0x80012E96:command=13;prepare=true;break;
    case 0x80012E88:command=14;prepare=true;break;
    case 0x80012EEC:command=15;prepare=true;break;
    case 0x80013524:command=16;prepare=true;break;
    case 0x80012EC0:command=18;prepare=true;break;
    case 0x80012EFA:command=3;prepare=true;break;
    case 0x80012EA4:
        if(!m.read32(0x800AF7B0,a)||!m.write32(0x80112C44,2)||!m.write32(0x80112C48,a))return false;
        return m.write32(0x80112C40,in.draw-0x40);
    case 0x800134CE:
        if(!m.read32(in.draw+4,a)||!m.write32(0x80112C48,a))return false;
        command=10;prepare=true;break;
    case 0x80012E36:
        if(!m.read32(0x800DC0FC,a)||!m.write32(0x80112C48,a))return false;
        command=11;prepare=true;break;
    case 0x80012E56:
        if(!m.read32(in.draw-0x10,a))return false;
        command=(a&0xFFFF0000)|17;prepare=true;break;
    case 0x8001353E:
        if(!m.write32(0x80112D30,in.draw-0x40)||!m.read32(0x800DC0FC,a)||!m.write32(0x80112C48,a))return false;
        command=0;prepare=true;break;
    case 0x8001420E:return m.write32(0x80112D38,in.draw);
    case 0x80014E0C:return m.write32(0x80112C40,in.draw-0x40)&&m.write32(0x80112C44,2);
    case 0x80014E22:
        if(!m.write32(0x80112C40,in.draw-0x40)||!m.read32(in.draw-0x38,a)||!m.read32(in.draw-0x20,b)||!m.read32(in.draw-0x1C,c))return false;
        return m.write32(0x80112C48,b)&&m.write32(0x80112C4C,c)&&m.write32(0x80112C44,a);
    case 0x80014E50:case 0x80014E4A:
        if(!m.write32(0x80112C40,in.draw-0x40)||!m.read32(0x8010F890,a)||!m.read32(0x8010F898+a*4,b)||!m.read32(0x8010F894+a*4,c))return false;
        d=(((b-c)>>2)+1)<<16;d|=in.entry==0x80014E50?1:2;
        return m.write32(0x80112C44,d)&&m.write32(0x80112C48,0x800B4D60)&&m.write32(0x80112C4C,c);
    case 0x80014E96:
        if(!m.write32(0x80112C40,in.draw-0x40)||!m.write32(0x80112C44,1)||!m.read32(0x80112D38,a)||!m.read32(0x80112D30,b))return false;
        if(a)return m.write32(0x80112C48,a)&&m.write32(0x80112C4C,b);
        return m.write32(0x80112C48,b)&&m.read32(0x80112D34,c)&&m.write32(0x80112C4C,c);
    case 0x80014ED4:
        if(!m.read32(0x80112D30,a)||!m.write32(in.draw,a)||!m.read32(0x800DC250,b))return false;
        return !(b&0x80000)||(m.write32(0x800DC250,b&~0x80000U)&&m.write32(in.values+0x14,0x400));
    case 0x80014236:
        if(!m.read32(0x801131B4,a))return false;
        if(a&0x200000)return m.write32(in.values+4,0x800)&&m.write32(in.object+0x30,0x02000000);
        if(a&0x1000000)return m.write32(in.values+4,0)&&m.write32(in.values+0x14,0xFFFF0000)&&m.write32(in.object+0x30,0x200000);
        return !(a&0x2000000)||append_object_event(m,5,in.object);
    case 0x800142A0:
        if(!m.read32(0x801131B4,a))return false;
        if(a&0x200000){
            if(!m.read32(0x80113164,b)||!m.write32(in.object+0x24,0x001B1A00)||!m.write32(b+0x2C,0))return false;
            return m.write32(in.values+4,0)&&m.write32(in.values+0xC,0x80000000)&&m.write32(in.values+0x14,0xFFE00000);
        }
        if(a&0x2000000){out.unwindWords=4;return append_object_event(m,4,in.object);}
        if(!m.read32(0x800DC244,b)||!m.write32(0x800DC244,b|0x1F000000)||!m.write32(in.object+0x24,0x1A00)||!m.write32(in.values+0x14,0)||!m.write32(in.values+0x1C,0x80000000))return false;
        return m.write32(in.values,0x21000)&&m.write32(in.values+4,0x4000)&&m.write32(in.values+0xC,0x88000314)&&m.write32(in.values+0x34,0xFFFFFC00);
    default:return false;
    }
    return prepare&&m.write32(0x80112C44,command)&&m.write32(0x80112C40,in.draw-0x40);
}
}
