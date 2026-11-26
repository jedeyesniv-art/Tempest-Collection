#pragma once
#include "native_fractional_values.h"
#include "game/TempestCallbacks.h"
#include <map>
namespace tempest_native {
// Explicit multi-rate policy; the outer session owns advancing its phase.
// Physics/contacts execute every substep. Held-repeat opportunities remain at
// original cadence, while a fresh fire press and new surface contact are prompt.
class NativeSimulationTiming final:public ObjectValueServices,public CallbackTimingServices {
    CommandMemory& memory_;NativeFractionalValues values_;
    SimulationStep step_{5,0};
    std::map<uint32_t,uint32_t> contacts_,current_,savedContacts_;
    bool previousFire_=false,heldFire_=false,pressedFire_=false,active_=false;
    mutable bool spentFire_=false;
public:
    explicit NativeSimulationTiming(CommandMemory& memory):memory_(memory){}
    bool reset(unsigned divisions=5,unsigned numerator=1){if(active_||!divisions||divisions>65536||!numerator||numerator>divisions||!values_.set_divisions(divisions,numerator))return false;step_={divisions,0,numerator};contacts_.clear();current_.clear();previousFire_=heldFire_=pressedFire_=false;return true;}
    SimulationStep simulation_step()const override{return step_;}
    bool advance(){if(active_)return false;step_.index=(step_.index+step_.numerator)%step_.divisions;return true;}
    bool begin_values()override{
        if(active_)return false;uint32_t packet;if(!memory_.read32(0x806B9930,packet)||!values_.begin_values())return false;
        heldFire_=!(packet&0x01000000);pressedFire_=heldFire_&&!previousFire_;spentFire_=false;
        savedContacts_=contacts_;current_.clear();active_=true;return true;
    }
    void commit_values()override{if(!active_)return;values_.commit_values();contacts_=std::move(current_);savedContacts_.clear();previousFire_=heldFire_;active_=false;}
    void abort_values()override{if(!active_)return;values_.abort_values();contacts_=std::move(savedContacts_);current_.clear();active_=false;}
    void object_instantiated(uint32_t object)override{values_.object_instantiated(object);contacts_.erase(object);current_.erase(object);}
    bool object_values(uint32_t object,uint32_t base,const ObjectUpdateInput& input,ObjectUpdateOutput& output)override{return values_.object_values(object,base,input,output);}
    bool fire_opportunity()const override{
        if(!active_||spentFire_||(!step_.legacy_boundary()&&!pressedFire_))return false;
        spentFire_=true;return true;
    }
    bool callback_opportunity(uint32_t entry,bool event)const override{
        if(event||step_.legacy_boundary())return true;
        switch(entry){
        // Fresh physical state, spatial census, continuous steering and input.
        // Their embedded elapsed/random/repeated effects are split in helpers.
        case 0x80011DF6:case 0x80011DE4:case 0x800128FE:case 0x8001346E:
        case 0x800132C0:case 0x80013668:case 0x80013998:case 0x800139A4:
        case 0x800140DE:case 0x80013148:case 0x80013168:case 0x80013100:
        case 0x800124C0:case 0x800126D4:case 0x80012ED0:case 0x80012664:
        case 0x8001242E:case 0x8001409E:case 0x80012F08:case 0x80013FEE:
        // Preparation/context bindings are recomputed from real current state.
        case 0x800134E6:case 0x80013500:case 0x80013532:case 0x80012E7A:
        case 0x800134F2:case 0x8001350C:case 0x80013518:case 0x80012E28:
        case 0x80012E96:case 0x80012E88:case 0x80012EEC:case 0x80013524:
        case 0x80012EC0:case 0x80012EFA:case 0x80012EA4:case 0x800134CE:
        case 0x80012E36:case 0x80012E56:case 0x8001353E:case 0x8001420E:
        case 0x80014E0C:case 0x80014E22:case 0x80014E50:case 0x80014E4A:
        case 0x80014E96:case 0x80014ED4:case 0x80014DCA:return true;
        // Other unconditional behavior is authored discrete AI/event/RNG work.
        // It still executes at legacy cadence; event dispatch remains immediate.
        default:return false;
        }
    }
    bool surface_response(uint32_t object,uint32_t lane)override{
        auto current=current_.find(object);if(current!=current_.end()&&current->second==lane)return false;
        current_[object]=lane;auto previous=contacts_.find(object);
        return step_.legacy_boundary()||previous==contacts_.end()||previous->second!=lane;
    }
};
}
