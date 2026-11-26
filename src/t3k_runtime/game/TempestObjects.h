#pragma once
#include "TempestMovement.h"
#include <array>
#include <vector>
#include <utility>
namespace tempest_native {
// Recovered 16-byte value record. These are attributes of an object, not a
// fabricated player/enemy class hierarchy. Names describe observed semantics.
struct ObjectValue { uint32_t position,velocity,scale,flags; };
inline constexpr std::array<int32_t,16> constant_parameters(){return {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};}
struct ObjectUpdateInput {
    uint32_t picture,paused,topologyMask,topologyPhase,objectStatus;
    std::array<uint32_t,16> parameters,globalParameters;
    std::vector<ObjectValue> values;
    // -1: independent input word; -2: current object status; otherwise a word
    // in the current value list. This preserves observed parameter/list aliasing.
    std::array<int32_t,16> parameterAliases=constant_parameters(),globalAliases=constant_parameters();
};
struct ObjectUpdateOutput { uint32_t objectStatus;std::vector<ObjectValue> values; };
// 80012244: approach a parameter using the signed step in the velocity field.
inline ObjectValue approach_parameter(ObjectValue value,uint32_t target,bool paused) {
    if(paused)return value;
    uint32_t difference=target-value.position;
    uint32_t magnitude=int32_t(difference)<0?0U-difference:difference;
    uint32_t step=int32_t(target)>int32_t(value.position)?value.velocity:0U-value.velocity;
    if(int32_t(magnitude)<=int32_t(value.velocity))value.position=target;
    else value.position+=step;
    return value;
}
// 800121DA..80012408, ending at its RTS: update the object's value-record list.
// Parameter views can refer to attributes modified earlier in this same call.
// Unsupported handler numbers are errors, never successful no-ops.
template<class Move,class Approach>
inline bool update_object_values_integrated(const ObjectUpdateInput& in,ObjectUpdateOutput& out,Move move,Approach approach) {
    if(in.values.size()>255)return false;
    ObjectUpdateOutput result{in.objectStatus,in.values};
    for(const auto& aliases:{in.parameterAliases,in.globalAliases})for(auto a:aliases)
        if(a< -2 || (a>=0 && size_t(a)>=in.values.size()*4))return false;
    auto parameter=[&](unsigned index,bool global){
        auto alias=(global?in.globalAliases:in.parameterAliases)[index];
        if(alias== -2)return result.objectStatus;
        if(alias<0)return (global?in.globalParameters:in.parameters)[index];
        const auto& v=result.values[size_t(alias)/4];
        switch(alias%4){case 0:return v.position;case 1:return v.velocity;case 2:return v.scale;default:return v.flags;}
    };
    for(auto& value:result.values) {
        if(!(value.flags&0x80000000U)) {
            if(value.flags&0x40000000U)value.position=in.picture;
            continue;
        }
        unsigned handler=(value.flags>>12)&15;
        if(handler>2)return false;
        if(handler==1) {
            if(!approach(size_t(&value-result.values.data()),value,parameter(value.flags&15,false),(in.paused&1)!=0))return false;
            continue;
        }
        if(((value.flags>>8)&15)>5)return false;
        MovementInput movement{value.position,value.velocity,value.scale,value.flags,
            parameter((value.flags>>4)&15,handler==2),parameter(value.flags&15,handler==2),in.paused,in.topologyMask,in.topologyPhase,result.objectStatus};
        MovementOutput next;
        if(!move(size_t(&value-result.values.data()),movement,next))return false;
        value={next.position,next.velocity,next.scale,next.flags};result.objectStatus=next.objectStatus;
    }
    out=std::move(result);return true;
}
inline bool update_object_values(const ObjectUpdateInput& in,ObjectUpdateOutput& out) {
    return update_object_values_integrated(in,out,
        [](size_t,const MovementInput& input,MovementOutput& result){result=movement_step(input);return true;},
        [](size_t,ObjectValue& value,uint32_t target,bool paused){value=approach_parameter(value,target,paused);return true;});
}
// Optional integration policy. The baseline owns no fractional state. A policy
// must participate in the frame transaction and forget reused pool objects.
struct ObjectValueServices {
    virtual bool begin_values(){return true;}
    virtual void commit_values(){}
    virtual void abort_values(){}
    virtual void object_instantiated(uint32_t){}
    virtual bool object_values(uint32_t,uint32_t,const ObjectUpdateInput& in,ObjectUpdateOutput& out){return update_object_values(in,out);}
    virtual ~ObjectValueServices()=default;
};
}
