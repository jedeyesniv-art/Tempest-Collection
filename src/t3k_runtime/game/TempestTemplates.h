#pragma once
#include "TempestCommands.h"
namespace tempest_native {
struct TemplateInput{uint32_t index,destination;};
struct InitializerRequest{uint32_t source,destination,parameters,index,target;};
// Preparation portion of 8001086C..80010902. Return an explicit initializer
// dependency; the native runtime must execute it and the original continuation.
// The first two link words remain untouched, and scalar-copy ordering matters.
inline bool prepare_object_template(CommandMemory& memory,const TemplateInput& in,InitializerRequest& out,unsigned budget=4096){
    uint32_t source,size;
    if(!memory.read32(0x800B30E0+in.index*4,source)||!memory.read32(source+0x20,size))return false;
    unsigned words=size>>2;words=words>2?words-2:1;if(words>budget)return false;
    for(unsigned i=0;i<words;++i){uint32_t word;if(!memory.read32(source+8+i*4,word)||!memory.write32(in.destination+8+i*4,word))return false;}
    uint32_t descriptor,overrideMode,index,offset;
    if(!memory.read32(in.destination+8,descriptor))return false;
    uint32_t parameters=in.destination+0x40+((descriptor>>16)&255)*16;
    if(!memory.write32(0x80112C9C,parameters)||!memory.read32(0x80112C90,overrideMode)||!memory.write32(0x80112C90,0))return false;
    if(overrideMode)index=overrideMode;
    else{uint32_t word;if(!memory.read32(in.destination+0x24,word))return false;index=word>>24;}
    if(!memory.read32(0x80112CC0+index*4,offset))return false;
    out={source,in.destination,parameters,index,0x800105BCU+offset};return true;
}
// Complete initializer 4, 80010802..80010842, including return-delay stores.
// Shared inputs are read before writes, preserving destination aliases.
inline bool initialize_shared_position(CommandMemory& memory,uint32_t object){
    uint32_t position,secondary,mode;
    if(!memory.read32(0x800DC100,position)||!memory.read32(0x800DC104,secondary)||!memory.read32(0x800DC0FC,mode))return false;
    if(!(mode&1))position=(position&0xFFFF0000U)|0x8000;
    return memory.write32(object+0x40,position)&&memory.write32(object+0x60,secondary)&&memory.write32(object+0x50,0);
}
// Complete initializer 2, 80010916..8001094C. The payload vector is fetched
// before any destination store; the final vector destination aligns to 16 bytes.
inline bool initialize_payload_object(CommandMemory& memory,uint32_t object){
    uint32_t parameters,payload[4];
    if(!memory.read32(0x80112C9C,parameters))return false;
    for(unsigned i=0;i<4;++i)if(!memory.read32(0x80112CA0+i*4,payload[i]))return false;
    if(!memory.write32(parameters+0x10,payload[3])||!memory.write32(object+0x44,payload[2]&0xFFFF)||!memory.write32(object+0xF8,payload[2]>>16))return false;
    const uint32_t values[4]{payload[0],payload[1],0,2};uint32_t destination=(object+0xE0)&~15U;
    for(unsigned i=0;i<4;++i)if(!memory.write32(destination+i*4,values[i]))return false;
    return true;
}
// Persistent random state is held in r16..r19 by the original consumer.
struct InitializerState {uint32_t polynomial,first,second,third;};
// 80010B42: the conditional return executes the logical shift in its delay
// slot. Only the even-input path also executes the feedback XOR.
inline uint32_t advance_object_random(uint32_t value,uint32_t polynomial){
    return (value>>1)^((value&1)?0:polynomial);
}
inline bool initialize_known_object(CommandMemory& memory,uint32_t entry,uint32_t object){
    if(entry==0x80010802)return initialize_shared_position(memory,object);
    if(entry==0x80010916)return initialize_payload_object(memory,object);
    return false;
}
// 800109A4: feedback step, signed range/offset and shared fixed-point scale.
inline bool draw_object_random(CommandMemory& memory,uint32_t& seed,uint32_t polynomial,uint32_t range,uint32_t offset,uint32_t& result){
    seed=advance_object_random(seed,polynomial);
    uint32_t value=command_mul(range,seed,31),scale;
    value=int32_t(value)<0?value-offset:value+offset;
    if(!memory.read32(0x80112C88,scale))return false;
    result=command_mul(scale,value,16);return true;
}
inline uint32_t object_abs(uint32_t value){return int32_t(value)<0?0U-value:value;}
inline uint32_t capped_object_abs(uint32_t value){value=object_abs(value);return int32_t(value)>0x10000?0x10000:value;}
// Complete shared body for initializer 3 and alternate-entry initializer 13.
inline bool initialize_randomized_object(CommandMemory& m,uint32_t object,InitializerState& state,bool alternate,bool simple=false){
    uint32_t position,secondary,parameters,value,speed,difficulty;
    if(!m.read32(0x80112CA0,position)||!m.read32(0x80112CA4,secondary))return false;
    position&=0xFFFF0000;
    if(!draw_object_random(m,state.first,state.polynomial,simple?0x400:0xA00,simple?0x400:0xF00,value)||!m.read32(0x80112C9C,parameters)||!m.write32(parameters+0x10,value))return false;
    if(!draw_object_random(m,state.third,state.polynomial,0x3000,0x3000,value)||!m.write32(parameters+0x14,capped_object_abs(value)))return false;
    if(!draw_object_random(m,state.second,state.polynomial,0x3000,0x6000,value)||!m.write32(parameters+0x18,capped_object_abs(value)))return false;
    if(!draw_object_random(m,state.first,state.polynomial,0x300,0x800,value)||!m.write32(object+0x74,capped_object_abs(value)))return false;
    if(!draw_object_random(m,state.second,state.polynomial,simple?0x20000:0x38000,simple?0x10000:0x50000,value)||!m.read32(0x80110ABC,difficulty))return false;
    value=0U-object_abs(value);difficulty=(difficulty>>16)&255;
    uint32_t bits=0;for(uint32_t v=difficulty;v;v>>=1)++bits;
    speed=value+command_mul(bits+1,value,3);
    if(!m.write32(object+0x54,speed)||!m.write32(object+0x50,secondary)||!m.write32(object+0x40,position))return false;
    if(simple)return true;
    if(!alternate){
        uint32_t vector=(object+0x60)&~15U;
        for(unsigned k=0;k<4;++k)if(!m.write32(vector+k*4,0))return false;
        return m.write32(object+0x24,0x03130E00)&&m.write32(object+0x30,0x01300000)&&m.write32(object+0x38,0x220);
    }
    uint32_t parameterValue,payload,discard;
    if(!m.read32(0x80112C9C,parameters)||!m.read32(parameters+0x10,parameterValue)||!m.read32(0x80112CAC,payload)||!m.write32(parameters+0x10,command_mul(object_abs(parameterValue),payload,0)))return false;
    if(!m.read32(object+0x54,speed)||!m.read32(object+0x74,discard)||!m.write32(object+0x74,0)||!m.write32(object+0x54,0)||!m.write32(parameters+0x20,speed))return false;
    if(!m.read32(0x80112CA8,payload)||!m.write32(parameters+0x24,payload))return false;
    uint32_t values[4]{0xFF00,0U-payload,0x10000,0x88000304};uint32_t vector=(object+0x60)&~15U;
    for(unsigned k=0;k<4;++k)if(!m.write32(vector+k*4,values[k]))return false;
    if(!m.read32(object+0x24,value)||!m.write32(parameters+0x28,value)||!m.write32(object+0x24,0x2200))return false;
    return m.read32(object+0x30,value)&&m.write32(parameters+0x2C,value)&&m.write32(object+0x30,0x01000000)&&m.write32(object+0x38,0);
}
inline bool initialize_type5(CommandMemory& m,uint32_t object,InitializerState& state){
    uint32_t position,secondary,parameters,value,scale;
    if(!m.read32(0x80112CA0,position)||!m.read32(0x80112CA4,secondary))return false;
    position&=0xFFFF0000;
    if(!draw_object_random(m,state.first,state.polynomial,0x800,0xC00,value)||!m.read32(0x80112C9C,parameters)||!m.write32(parameters+0x10,value))return false;
    if(!draw_object_random(m,state.third,state.polynomial,0x300,0x100,value)||!m.write32(parameters+0x14,capped_object_abs(value)))return false;
    if(!draw_object_random(m,state.second,state.polynomial,0x3000,0x6000,value)||!m.write32(parameters+0x18,capped_object_abs(value)))return false;
    if(!draw_object_random(m,state.first,state.polynomial,0x100,0x100,value)||!m.write32(object+0x74,capped_object_abs(value)))return false;
    if(!draw_object_random(m,state.second,state.polynomial,0x30000,0x20000,value)||!m.read32(0x80112C88,scale)||!m.write32(object+0x54,0U-object_abs(value))||!m.write32(object+0x88,command_mul(scale,0x20000,16)))return false;
    return m.write32(object+0x50,secondary)&&m.write32(object+0x40,position);
}
inline bool initialize_type6(CommandMemory& m,uint32_t object,InitializerState& state,bool alternate){
    uint32_t position,secondary,parameters,value;
    if(!m.read32(0x80112CA0,position)||!m.read32(0x80112CA4,secondary))return false;
    position=((position>>16)&15)<<16;
    if(!draw_object_random(m,state.third,state.polynomial,0x400,0x400,value))return false;
    // Branch condition is the signed ADD flags, including overflow.
    bool positive=int64_t(int32_t(position))+int32_t(value)>0;
    uint32_t moved=position+value;if(!positive){moved-=value;value=0U-value;}
    if(!m.read32(0x80112C9C,parameters)||!m.write32(parameters+0x18,value)||!m.write32(parameters+0x10,0x01800000+(state.third&0xFFFFFF))||!m.write32(object+0x50,secondary)||!m.write32(object+0x40,moved))return false;
    if(!alternate)return true;
    uint32_t parameterValue,payload3,payload2;
    if(!m.read32(parameters+0x18,parameterValue)||!m.read32(0x80112CAC,payload3)||!m.read32(0x80112CA8,payload2)||!m.write32(parameters+0x18,command_mul(object_abs(parameterValue),payload3,0)))return false;
    uint32_t vector=(object+0x60)&~15U,words[4]{0xFF00,0U-payload2,0x10000,0x88000304};
    for(unsigned k=0;k<4;++k)if(!m.write32(vector+k*4,words[k]))return false;
    if(!m.read32(object+0x24,value)||!m.write32(parameters+0x2C,value)||!m.write32(object+0x24,0x2300))return false;
    return m.read32(object+0x30,value)&&m.write32(parameters+0x30,value)&&m.write32(object+0x30,0x01000000)&&m.write32(object+0x38,0);
}
inline bool initialize_with_state(CommandMemory& memory,uint32_t entry,uint32_t object,InitializerState& state){
    if(entry==0x80010802||entry==0x80010916)return initialize_known_object(memory,entry,object);
    if(entry==0x80010C64||entry==0x80010C5C)return initialize_randomized_object(memory,object,state,entry==0x80010C5C);
    if(entry==0x80010B4C)return initialize_randomized_object(memory,object,state,false,true);
    if(entry==0x80010EB2)return initialize_type5(memory,object,state);
    if(entry==0x80011052||entry==0x8001104C)return initialize_type6(memory,object,state,entry==0x8001104C);
    uint32_t parameters,a,b,c;
    switch(entry){
    case 0x80011DF6:return true; // original initializer 0 is RTS,NOP
    case 0x80010FBA: // 12, identical stores to initializer 7
        return memory.read32(0x80112CA0,a)&&memory.read32(0x80112CA4,b)&&memory.write32(object+0x40,(a&0xFFFF0000)|0x8000)&&memory.write32(object+0x50,b);
    case 0x800111B2: // 23
        return memory.read32(0x80112CA0,a)&&memory.read32(0x80112CA4,b)&&memory.read32(0x80112C9C,parameters)&&memory.write32(parameters,(a&0xFFFF0000)|0x8000)&&memory.write32(parameters+4,b);
    case 0x80011130: // 18
        return memory.read32(0x80112CA0,a)&&memory.read32(0x80112CA4,b)&&memory.read32(0x80112C9C,parameters)&&memory.write32(parameters+0x40,0x8010A2F4)&&memory.write32(object+0x40,a)&&memory.write32(object+0x60,b);
    case 0x8001118E: // 20: the branch's delay store replaces payload0 with its address
        {uint32_t payload[4];for(unsigned k=0;k<4;++k)if(!memory.read32(0x80112CA0+k*4,payload[k]))return false;
        if(!memory.write32(object+0x44,(payload[2]&1)?0xFFFFC000:0x4000))return false;
        return memory.write32(object+0x40,((object+0x44)&0xFFFF0000)|0x8000)&&memory.write32(object+0x50,payload[1]);}
    case 0x8001131C: // 8
        state.first=advance_object_random(state.first,state.polynomial);c=state.first&65535;
        return memory.read32(0x800DC100,a)&&memory.read32(0x800DC104,b)&&memory.read32(0x80112C9C,parameters)&&memory.write32(parameters+0x40,a)&&memory.write32(parameters+0x44,b)&&memory.write32(parameters+0x50,0x2000)&&memory.write32(parameters+0x54,4)&&memory.write32(parameters+0x58,c);
    case 0x80010E44: // 19
        if(!memory.read32(0x80112CA0,a)||!memory.read32(0x80112CA4,b)||!memory.write32(object+0x50,b)||!memory.write32(object+0x40,a&0xFFFF0000))return false;
        state.first=advance_object_random(state.first,state.polynomial);
        if(!memory.read32(0x80112C9C,parameters)||!memory.write32(parameters+0x20,0x02000000+((state.first&15)<<20)))return false;
        state.second=advance_object_random(state.second,state.polynomial);a=(state.second&255)+16;
        return memory.write32(parameters+0x28,(a<<16)|a);
    case 0x80010FE0: // 17
        if(!memory.read32(0x80112CA0,a)||!memory.read32(0x80112CA4,b)||!draw_object_random(memory,state.third,state.polynomial,0x20,0x40,c)||!memory.read32(0x80112C9C,parameters))return false;
        c=object_abs(c);if(!memory.write32(parameters+0x10,c)||!memory.write32(parameters+0x14,c)||!draw_object_random(memory,state.second,state.polynomial,4,1,c)||!memory.write32(parameters+0x18,object_abs(c)))return false;
        return memory.write32(object+0x40,(a&0xFFFF0000)|0x8000)&&memory.write32(object+0x50,b);
    case 0x800112B4: // 9
        if(!memory.read32(0x80112C9C,parameters)||!memory.read32(0x80112CA0,a)||!memory.write32(parameters+0x54,a))return false;
        state.third=advance_object_random(state.third,state.polynomial);a=uint32_t(int32_t(state.third)>>20);
        if(!memory.write32(object+0x74,a)||!memory.write32(parameters+0x6C,uint32_t(int32_t(a)>>2)))return false;
        state.second=advance_object_random(state.second,state.polynomial);
        return memory.write32(object+0x54,uint32_t(int32_t(state.second<<16)>>28))&&memory.write32(object+0x64,uint32_t(int32_t(state.second)>>28));
    case 0x80011200: // 15
        if(!memory.read32(0x80112CA0,a)||!memory.read32(0x80112CA4,b)||!draw_object_random(memory,state.third,state.polynomial,0x8000,0xFFFFE000,c)||!memory.read32(0x80112C9C,parameters))return false;
        c<<=6;if(!memory.write32(object+0x78,c)||!memory.write32(parameters+0x10,uint32_t(int32_t(c)>>6))||!draw_object_random(memory,state.second,state.polynomial,0x6000,0xFFFFD000,c)||!memory.write32(object+0x44,c)||!memory.read32(0x80112C88,c)||!memory.write32(object+0x54,0U-(c<<2)))return false;
        return memory.write32(object+0x40,(a&0xFFFF0000)|0x8000)&&memory.write32(object+0x50,b);
    case 0x800107A4: // initializer 16
        state.first=advance_object_random(state.first,state.polynomial);
        if(!memory.write32(object+0x48,((state.first>>14)&0x3FFFF)+0x10000)||
           !memory.write32(object+0x54,((state.first&0x3FF)+0x800)<<16))return false;
        if(!memory.read32(0x80112CA0,a)||!memory.read32(0x80112C9C,parameters)||
           !memory.read32(0x80112CA8,b)||!memory.read32(0x80112CAC,c))return false;
        return memory.write32(parameters+0x14,a)&&memory.write32(parameters+0x10,b)&&memory.write32(parameters+0x18,c);
    case 0x80010950: // initializer 1
        state.third=advance_object_random(state.third,state.polynomial);
        a=(state.third&255)|0x80;
        if(!memory.write32(object+0xC0,(a<<16)|a))return false;
        {uint32_t payload[4];for(unsigned k=0;k<4;++k)if(!memory.read32(0x80112CA0+k*4,payload[k]))return false;
        return memory.write32(object+0xC4,(payload[0]&0xFFFF0000)|0x8000)&&memory.write32(object+0xCC,payload[1])&&memory.write32(object+0xD0,payload[2])&&memory.write32(object+0xD4,payload[3]);}
    case 0x80011168: // initializer 21
        state.second=advance_object_random(state.second,state.polynomial);
        return memory.read32(0x80112C9C,parameters)&&memory.write32(parameters+0x10,(state.second>>12)&3);
    case 0x800111DA: // initializer 7
        return memory.read32(0x80112CA0,a)&&memory.read32(0x80112CA4,b)&&
            memory.write32(object+0x40,(a&0xFFFF0000)|0x8000)&&memory.write32(object+0x50,b);
    case 0x8001128C: // initializer 10
        return memory.read32(0x80112C9C,parameters)&&memory.read32(0x80112CA0,a)&&
            memory.write32(parameters+0x40,a)&&memory.write32(object+0x50,0x05000000);
    case 0x8001136C: // initializer 11
        return memory.read32(0x80112C9C,parameters)&&memory.read32(0x80112CA0,a)&&memory.write32(parameters+0x14,a);
    default:return false;
    }
}
}
