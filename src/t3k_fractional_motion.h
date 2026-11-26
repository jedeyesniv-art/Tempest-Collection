#pragma once
#include "t3k_runtime/game/TempestMovement.h"
#include "t3k_runtime/game/TempestObjects.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace tempest_native::timing_research {
// Stateful bounded integration. Public velocities remain in original-tick
// units; only displacement is subdivided. External writes to position,
// velocity, scale or the boundary policy start a new integration segment, so
// a new input/force takes effect on the next substep.
class FractionalMovement {
    MovementInput origin_{};
    MovementOutput previous_{};
    unsigned phase_=0,divisions_=0,cutoffPhase_=0;
    int64_t progress_=0;
    int32_t finalVelocity_=0;
    double logarithm_=0;
    bool valid_=false;
    uint32_t contact_=0;
    static uint32_t magnitude(uint32_t value){return int32_t(value)<0?0U-value:value;}
public:
    void reset(){valid_=false;cutoffPhase_=0;contact_=0;}
    bool advance(const MovementInput& input,unsigned divisions,MovementOutput& out){
        if(!divisions||divisions>65536||((input.flags>>8)&15)>5)return false;
        if(divisions==1){out=movement_step(input);reset();return true;}
        if(input.paused&1){
            // Original pause still executes its final grid-snap check.
            out=movement_step(input);if(out.position!=input.position)reset();return true;
        }
        if(int32_t(input.scale)<0||int32_t(input.velocity)==std::numeric_limits<int32_t>::min())return false;
        if(divisions_!=divisions)cutoffPhase_=0;
        if(contact_&&input.position!=previous_.position)contact_=0;
        bool changed=!valid_||divisions_!=divisions||input.position!=previous_.position||
            input.velocity!=previous_.velocity||input.scale!=origin_.scale||
            (input.flags&~0x30000U)!=(origin_.flags&~0x30000U)||
            input.lower!=origin_.lower||input.upper!=origin_.upper||
            input.topologyMask!=origin_.topologyMask||input.topologyPhase!=origin_.topologyPhase;
        if(changed||phase_==divisions_){
            int64_t product=(int64_t(int32_t(input.scale))*int32_t(input.velocity))>>16;
            if(product<std::numeric_limits<int32_t>::min()||product>std::numeric_limits<int32_t>::max())return false;
            int32_t final=int32_t(product);
            origin_=input;phase_=0;progress_=0;divisions_=divisions;finalVelocity_=final;
            logarithm_=input.velocity&&final?std::log(double(final)/int32_t(input.velocity)):0;
            valid_=true;
        }
        ++phase_;double time=double(phase_)/divisions_;
        double fraction=logarithm_?std::expm1(logarithm_*time)/std::expm1(logarithm_):time;
        int64_t progress=phase_==divisions_?finalVelocity_:std::llround(double(finalVelocity_)*fraction);
        int64_t displacement=progress-progress_;progress_=progress;
        int64_t velocity=phase_==divisions_?finalVelocity_:logarithm_?
            std::llround(double(int32_t(origin_.velocity))*std::exp(logarithm_*time)):finalVelocity_;
        // Cutoff is a discrete operation at the legacy-time boundary. A fine
        // force can contribute less than 255 per sample while accumulating a
        // valid original-unit velocity; cutting it off after each rebase
        // prevents that force from ever producing movement.
        if(++cutoffPhase_==divisions_){
            cutoffPhase_=0;
            if(int32_t(magnitude(uint32_t(velocity)))<255){velocity=0;displacement=0;valid_=false;}
        }
        out={input.position+uint32_t(displacement),uint32_t(velocity),input.scale,input.flags&~0x30000U,input.objectStatus};
        bool negative=int32_t(out.velocity)<0;
        if(negative?int32_t(out.position)<=int32_t(input.lower):int32_t(out.position)>=int32_t(input.upper)){
            uint32_t event=negative?0x10000U:0x20000U;
            bool notify=contact_!=event||cutoffPhase_==0;
            if(notify){out.flags|=event;if(((input.flags>>24)&3U)&(event>>16))out.objectStatus|=0x200000U;}
            unsigned mode=(input.flags>>8)&15;
            if(mode==4||mode==5){bool wrap=(input.topologyMask&((input.topologyPhase>>1)^1U)&1U)!=0;mode=wrap?1:mode==4?3:2;}
            if(mode==1)out.position=negative?out.position+input.upper:out.position-input.upper+input.lower;
            if(mode==2){out.velocity=0U-out.velocity;out.position-=uint32_t(displacement);}
            if(mode==3){out.position=negative?input.lower:input.upper;if(negative)out.velocity=0;if(notify&&(input.flags&(1U<<27)))out.objectStatus|=negative?0x1000000U:0x2000000U;}
            contact_=(mode==1||mode==2)?0:event;
            valid_=false;
        }else contact_=0;
        if(input.flags&(1U<<26)){
            // Compare the actual fine-step displacement, not original-unit
            // stored velocity; otherwise snapping happens five times early.
            if(std::abs(displacement)>int64_t(magnitude(out.position)&65535U)){
                bool negativePosition=int32_t(out.position)<0;out.position&=0xffff0000U;
                out.objectStatus|=0x800000U;if(negativePosition)out.position+=0x10000U;valid_=false;
            }
        }
        previous_=out;return true;
    }
};
// Handler 1 accepts a fresh target every substep. The remainder belongs to the
// attribute, not the target: a direction reversal discards the old direction's
// fraction, and reaching a target cannot bank motion for a future target.
class FractionalApproach {
    uint64_t remainder_=0;
    int direction_=0;
    unsigned divisions_=0;
public:
    bool advance(ObjectValue& value,uint32_t target,bool paused,unsigned divisions) {
        if(!divisions || divisions>65536)return false;
        if(divisions==1){value=approach_parameter(value,target,paused);remainder_=0;direction_=0;return true;}
        if(int32_t(value.velocity)<0)return false;
        int64_t difference=int64_t(int32_t(target))-int32_t(value.position);
        if(std::abs(difference)>std::numeric_limits<int32_t>::max())return false;
        if(paused)return true;
        // A cadence change starts a fresh fractional accumulator; a residue
        // measured in fifths must never be reinterpreted as sevenths.
        if(divisions_!=divisions){remainder_=0;direction_=0;divisions_=divisions;}
        int direction=(difference>0)-(difference<0);
        if(!direction){remainder_=0;direction_=0;return true;}
        if(direction!=direction_)remainder_=0;
        direction_=direction;
        uint64_t numerator=uint64_t(value.velocity)+remainder_;
        uint64_t step=numerator/divisions;
        remainder_=numerator%divisions;
        if(uint64_t(std::abs(difference))<=step){value.position=target;remainder_=0;direction_=0;}
        else value.position=uint32_t(int64_t(int32_t(value.position))+direction*int64_t(step));
        return true;
    }
};
// Experimental free-flight kernel, deliberately not connected to gameplay.
// A segment owns one original tick's fixed-point rounding and cutoff. Samples
// evaluate motion within it; they do not interpolate rendered pictures.
// Boundaries, grid snapping and mid-segment forces require explicit policies.
class FreeFlightSegment {
    MovementInput input_{};
    MovementOutput endpoint_{};
    unsigned divisions_=0, phase_=0;
    double ratio_=1;
public:
    bool begin(const MovementInput& input,unsigned divisions) {
        divisions_=0;phase_=0;
        if(!divisions || divisions>65536 || (input.paused&1) ||
           (input.flags&(1U<<26)) || ((input.flags>>8)&15)>5 ||
           int32_t(input.scale)<0 || int32_t(input.velocity)==std::numeric_limits<int32_t>::min())return false;
        auto endpoint=movement_step(input);
        int64_t start=int32_t(input.position),velocity=int32_t(endpoint.velocity);
        int64_t end=start+velocity;
        // No signed wrap or boundary event may hide inside this segment.
        if(start<=int32_t(input.lower) || start>=int32_t(input.upper) ||
           end<=int32_t(input.lower) || end>=int32_t(input.upper) ||
           end<std::numeric_limits<int32_t>::min() || end>std::numeric_limits<int32_t>::max() ||
           (endpoint.flags&0x30000U) || endpoint.position!=uint32_t(end))return false;
        int64_t initial=int32_t(input.velocity);
        if(initial && velocity && ((initial<0)!=(velocity<0)))return false;
        // The cutoff is a discrete legacy operation. Its zero endpoint stays
        // stationary for this segment, rather than testing velocity/divisions.
        ratio_=initial && velocity ? double(velocity)/double(initial):1;
        input_=input;endpoint_=endpoint;divisions_=divisions;return true;
    }
    bool advance(MovementOutput& output) {
        if(!divisions_ || phase_==divisions_)return false;
        ++phase_;
        if(phase_==divisions_){output=endpoint_;return true;}
        double time=double(phase_)/divisions_;
        int32_t initial=int32_t(input_.velocity),final=int32_t(endpoint_.velocity);
        double fraction=time,velocity=final;
        if(initial && final && ratio_!=1){
            double logarithm=std::log(ratio_);
            fraction=std::expm1(logarithm*time)/std::expm1(logarithm);
            velocity=double(initial)*std::exp(logarithm*time);
        }
        // Round absolute progress from the segment origin. Rounding each
        // displacement separately loses sub-word motion over repeated steps.
        int64_t displacement=std::llround(double(final)*fraction);
        output={uint32_t(int64_t(int32_t(input_.position))+displacement),
                uint32_t(int64_t(std::llround(velocity))),input_.scale,
                input_.flags&~0x30000U,input_.objectStatus};
        return true;
    }
};
// One instance belongs to one object incarnation and ordered value list. The
// caller must reset it on pool reuse, and snapshot it with frame transactions.
// Both clocks share the original traversal/alias implementation above.
class FractionalObjectValues {
    struct State{FractionalMovement movement;FractionalApproach approach;ObjectValue last{};bool seen=false;};
    std::vector<State> states_;
public:
    void reset(){states_.clear();}
    bool update(const ObjectUpdateInput& input,unsigned divisions,ObjectUpdateOutput& output){
        if(!divisions||divisions>65536)return false;
        if(divisions==1){bool ok=update_object_values(input,output);if(ok)reset();return ok;}
        auto before=states_;states_.resize(input.values.size());
        for(size_t i=0;i<states_.size();++i){
            auto& state=states_[i];auto& value=input.values[i];
            if(state.seen&&(state.last.flags!=value.flags||
               (((value.flags>>12)&15)==1&&(state.last.position!=value.position||state.last.velocity!=value.velocity))))state=State{};
        }
        bool ok=update_object_values_integrated(input,output,
            [&](size_t index,const MovementInput& value,MovementOutput& result){return states_[index].movement.advance(value,divisions,result);},
            [&](size_t index,ObjectValue& value,uint32_t target,bool paused){return states_[index].approach.advance(value,target,paused,divisions);});
        if(!ok){states_=std::move(before);return false;}
        for(size_t i=0;i<states_.size();++i){states_[i].last=output.values[i];states_[i].seen=true;}
        return true;
    }
};
}
