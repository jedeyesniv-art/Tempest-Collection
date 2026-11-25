#pragma once
#include <cstdint>
// Semantic recovery of MPE3's bounded-value handler, not a packet translation.
// All words retain the game's wrapping fixed-point representation.
namespace tempest_native {
struct MovementInput {
    uint32_t position, velocity, scale, flags;
    uint32_t lower, upper, paused, topologyMask, topologyPhase, objectStatus;
};
struct MovementOutput { uint32_t position, velocity, scale, flags, objectStatus; };
inline MovementOutput movement_step(const MovementInput& in) {
    MovementOutput out{in.position,in.velocity,in.scale,in.flags,in.objectStatus};
    if(!(in.paused&1)) {
        out.flags &= ~0x30000U;
        out.velocity=uint32_t((int64_t(int32_t(in.scale))*int64_t(int32_t(in.velocity)))>>16);
        uint32_t magnitude=int32_t(out.velocity)<0 ? 0U-out.velocity : out.velocity;
        if(int32_t(magnitude)<255)out.velocity=0;
        const bool negative=int32_t(out.velocity)<0;
        out.position+=out.velocity;
        if(negative ? int32_t(out.position)<=int32_t(in.lower) : int32_t(out.position)>=int32_t(in.upper)) {
            const uint32_t event=negative?0x10000U:0x20000U;
            out.flags|=event;
            if(((in.flags>>24)&3U)&(event>>16))out.objectStatus|=0x200000U;
            unsigned mode=(in.flags>>8)&15;
            if(mode==4 || mode==5) {
                bool wrap=(in.topologyMask&((in.topologyPhase>>1)^1U)&1U)!=0;
                mode=wrap?1:mode==4?3:2;
            }
            if(mode==1) out.position=negative?out.position+in.upper:out.position-in.upper+in.lower;
            if(mode==2) {out.velocity=0U-out.velocity;out.position+=out.velocity;}
            if(mode==3) {
                out.position=negative?in.lower:in.upper;
                if(negative)out.velocity=0;
                if(in.flags&(1U<<27))out.objectStatus|=negative?0x1000000U:0x2000000U;
            }
        }
    }
    if(in.flags&(1U<<26)) {
        uint32_t v=int32_t(out.velocity)<0?0U-out.velocity:out.velocity;
        uint32_t p=int32_t(out.position)<0?0U-out.position:out.position;
        if(int32_t(v)>int32_t(p&65535U)) {
            const bool negativePosition=int32_t(out.position)<0;
            out.position&=0xffff0000U;
            out.objectStatus|=0x800000U;
            if(negativePosition)out.position+=0x10000U;
        }
    }
    return out;
}
}
