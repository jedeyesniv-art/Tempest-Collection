#pragma once
#include "native_controller_input.h"
#include <cmath>
namespace tempest_native {
// These are different guest device identities. The host selects one per player;
// it must not OR pad analog and mouse spinner capabilities into one record.
enum class NativeInputMode {gamepad,mouseSpinner};
inline int32_t native_relative_direction(uint8_t previous,uint8_t current){return int8_t((current&0xFE)-(previous&0xFE));}
struct NativeMouseSample {bool connected=false,left=false,right=false,middle=false;};
class NativeMouseSpinner {
public:
    struct Checkpoint{uint8_t position=0;double pending=0,fraction=0;};
    Checkpoint checkpoint()const{return {position_,pending_,fraction_};}
    bool restore(const Checkpoint& in){if(!std::isfinite(in.pending)||!std::isfinite(in.fraction)||std::abs(in.pending)>1000000||std::abs(in.fraction)>=1)return false;position_=in.position;pending_=in.pending;fraction_=in.fraction;return true;}
    // Host relative X counts. Y and wheel scrolling do not turn the claw.
    // Accumulate event samples once; do not read/consume them again on a failed
    // frame retry. The host retains the published record until acceptance.
    bool add_motion(double x){
        if(!std::isfinite(x))return false;
        // Bound before addition so even hostile/overflowing input stays finite.
        pending_+=(std::clamp)(x,-1000000.0,1000000.0);pending_=(std::clamp)(pending_,-1000000.0,1000000.0);return true;
    }
    NativeControllerPort sample(const NativeMouseSample& input){
        if(!input.connected){pending_=0;fraction_=0;return {};}
        // The original device byte wraps. Unbounded mouse deltas could alias to
        // the wrong direction. Limit each published movement to the native
        // speed bound; discard excess rather than queueing delayed movement.
        double movement=(std::clamp)(pending_+fraction_,-80.0,80.0);pending_=0;
        int counts=int(movement);fraction_=movement-counts;
        position_=uint8_t(unsigned(position_)+unsigned(counts));
        uint16_t buttons=(input.left?0x4000:0)|(input.right?0x1000:0)|(input.middle?8:0);
        return native_spinner_record(true,buttons,position_);
    }
    // Reset on capture loss or explicit mode changes. A host changing device
    // identity must also reseed the game's previous-counter state before use.
    void clear_motion(){pending_=fraction_=0;}
private:
    uint8_t position_=0;double pending_=0,fraction_=0;
};
}
