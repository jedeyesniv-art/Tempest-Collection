#pragma once
#include "../t3k_fractional_motion.h"
#include <map>
namespace tempest_native {
// Frame-owned fractional state, keyed by object incarnation and value storage.
// This plugs into NativeFrameIntegration; it does not select a scheduler rate.
class NativeFractionalValues final:public ObjectValueServices {
    struct Object{uint32_t base=0;timing_research::FractionalObjectValues values;};
    std::map<uint32_t,Object> objects_,before_;
    unsigned divisions_=5,numerator_=1;bool active_=false;
public:
    bool set_divisions(unsigned count,unsigned numerator=1){if(active_||!count||count>65536||!numerator||numerator>count)return false;divisions_=count;numerator_=numerator;objects_.clear();return true;}
    bool clear(){if(active_)return false;objects_.clear();return true;}
    bool begin_values()override{if(active_)return false;before_=objects_;active_=true;return true;}
    void commit_values()override{before_.clear();active_=false;}
    void abort_values()override{if(active_)objects_=std::move(before_);before_.clear();active_=false;}
    void object_instantiated(uint32_t object)override{objects_.erase(object);}
    bool object_values(uint32_t object,uint32_t base,const ObjectUpdateInput& input,ObjectUpdateOutput& output)override{
        if(!active_)return false;
        auto& state=objects_[object];
        if(state.base!=base){state.values.reset();state.base=base;}
        if(numerator_==1)return state.values.update(input,divisions_,output);
        ObjectUpdateInput current=input;
        for(unsigned unit=0;unit<numerator_;++unit){
            if(!state.values.update(current,divisions_,output))return false;
            if(unit+1<numerator_){current.values=output.values;current.objectStatus=output.objectStatus;}
        }
        return true;
    }
};
}
