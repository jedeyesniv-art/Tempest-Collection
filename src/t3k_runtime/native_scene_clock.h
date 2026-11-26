#pragma once
#include <array>
#include <cstdint>
namespace tempest_native {
// Exact default 24/30/60/120 Hz accounting. Per-rate remainders prevent
// cumulative drift; each distinct rate loses less than one third nanosecond.
class NativeSceneClock {
    static constexpr uint64_t units=3000000000ULL;
    uint64_t ticks_=0;std::array<unsigned,241> remainder_{};
    uint64_t next(unsigned rate)const{return ticks_+(units+remainder_[rate])/rate;}
    static uint64_t count(uint64_t ticks,unsigned hz){return (ticks/units)*hz+((ticks%units)*hz)/units;}
public:
    bool advance(unsigned rate){if(!rate||rate>240)return false;uint64_t sum=units+remainder_[rate];ticks_+=sum/rate;remainder_[rate]=unsigned(sum%rate);return true;}
    uint64_t events(unsigned hz)const{return count(ticks_,hz);}
    uint64_t next_events(unsigned rate,unsigned hz)const{return !rate||rate>240?events(hz):count(next(rate),hz);}
    uint64_t ticks()const{return ticks_;}
};
}
