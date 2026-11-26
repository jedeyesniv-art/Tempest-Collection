#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace tempest_native {
inline double input_gain(double value){return std::isfinite(value)?(std::clamp)(value,.25,3.0):1;}
// Curve 1 keeps native axis bytes exact. Higher exponents devote more stick travel to
// low/middle speeds; gain follows the curve, saturation precedes guest packing.
inline int16_t shape_stick_axis(int16_t value,double curve,double gain){
    curve=std::isfinite(curve)?(std::clamp)(curve,1.0,5.0):2.5;gain=input_gain(gain);
    if(curve==1&&gain==1)return value;
    // The native decoder ignores -19..19 and caps at +/-90. Shape the
    // useful travel between these knees, so the curve does not enlarge the
    // deadzone or require a harder push to reach native maximum speed.
    constexpr double deadzone=20*256,ceiling=90*256,span=ceiling-deadzone;
    double magnitude=std::abs(double(value));if(magnitude<=deadzone)return value;
    double active=(std::min)(1.0,(magnitude-deadzone)/span);
    double shaped=deadzone+std::pow(active,curve)*gain*span;
    double signedValue=std::copysign(shaped,double(value));
    return int16_t(std::lround((std::clamp)(signedValue,-32768.0,32767.0)));
}
}
