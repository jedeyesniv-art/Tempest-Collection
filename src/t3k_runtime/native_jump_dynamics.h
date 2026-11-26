#pragma once
#include <cstdint>
#include <cmath>
namespace tempest_native {
struct NativeJumpState {uint32_t position=0,velocity=0,fuel=0;};
// The session driver still owns button choice, flags, sounds and resource
// display. This component integrates its actual jet motion in legacy units.
class NativeJumpDynamics {
    int64_t accelerationRemainder_=0,positionRemainder_=0,fuelRemainder_=0;
    unsigned divisions_=0;
    static uint32_t magnitude(uint32_t v){return int32_t(v)<0?0U-v:v;}
public:
    void reset(){accelerationRemainder_=positionRemainder_=fuelRemainder_=0;divisions_=0;}
    bool advance(NativeJumpState& state,bool held,uint32_t capacity,unsigned divisions,bool& thrust){
        if(!divisions||divisions>65536)return false;
        if(divisions_!=divisions){reset();divisions_=divisions;}
        thrust=held&&state.fuel&&!(int32_t(state.fuel)<=4&&!state.position);
        // Preserve the authored one-tick arithmetic, including multiply wrap.
        if(divisions==1){
            if(thrust){uint32_t before=magnitude(0x1000000-state.position);
                state.velocity+=(int32_t(state.position)<=0xFFFFFF?1U:0U)<<17;
                --state.fuel;state.position+=state.velocity;
                if(int32_t(magnitude(0x1000000-state.position))>int32_t(before))state.velocity=uint32_t(int32_t(state.velocity*240)>>8);
            }else if(state.position){state.velocity-=0x40000;state.position+=state.velocity;if(int32_t(state.position)<=0x7FFFF)state.position=state.velocity=0;}
            else if(int32_t(state.fuel)<int32_t(capacity))++state.fuel;
            return true;
        }
        if(!thrust&&!state.position){
            if(int32_t(state.fuel)<int32_t(capacity)||fuelRemainder_<0){
                ++fuelRemainder_;state.fuel+=uint32_t(fuelRemainder_/int64_t(divisions));fuelRemainder_%=int64_t(divisions);
            }
            accelerationRemainder_=positionRemainder_=0;return true;
        }
        uint32_t before=magnitude(0x1000000-state.position);
        int64_t acceleration=thrust?(int32_t(state.position)<=0xFFFFFF?0x20000:0):-0x40000;
        int64_t numerator=acceleration+accelerationRemainder_;
        state.velocity+=uint32_t(numerator/int64_t(divisions));accelerationRemainder_=numerator%int64_t(divisions);
        // Fractional generator of the original semi-implicit Euler map. The
        // half-acceleration correction retains its coarse ballistic trajectory
        // instead of silently changing jump height as subdivision increases.
        int64_t denominator=2LL*divisions*divisions;
        numerator=int64_t(int32_t(state.velocity))*2*divisions+acceleration*(divisions-1)+positionRemainder_;
        state.position+=uint32_t(numerator/denominator);positionRemainder_=numerator%denominator;
        if(thrust){
            --fuelRemainder_;state.fuel+=uint32_t(fuelRemainder_/int64_t(divisions));fuelRemainder_%=int64_t(divisions);
            if(int32_t(magnitude(0x1000000-state.position))>int32_t(before))state.velocity=uint32_t(int64_t(std::llround(int32_t(state.velocity)*std::pow(240.0/256,1.0/divisions))));
        }else if(int32_t(state.position)<=0x7FFFF){state.position=state.velocity=0;accelerationRemainder_=positionRemainder_=0;}
        return true;
    }
};
}
