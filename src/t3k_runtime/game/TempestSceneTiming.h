#pragma once
#include "TempestCommands.h"
namespace tempest_native {
enum class SceneTimingKind:unsigned {none,opening,transition,animatedMenu,particleCrawl,levelSelect,levelTravel,levelClear,bonus,death,entry,asset,gameOver,nameEntry,tool,pause,warpGame,settle};
struct SceneCadence {
    SceneTimingKind kind=SceneTimingKind::none;unsigned referenceHz=0,divisions=1,numerator=1;
    unsigned sample_hz()const{return numerator?referenceHz*divisions/numerator:0;}
};
struct SceneTimingServices {
    virtual unsigned begin_scene_timing(SceneTimingKind){return 1;}
    virtual void end_scene_timing(){}
    virtual SimulationStep scene_timing_step()const{return {};}
    virtual ~SceneTimingServices()=default;
};
struct SceneTimingScope {
    SceneTimingServices& services;unsigned divisions;
    int64_t remainders[32]{},eulerRemainders[8]{};
    SimulationStep step()const{return services.scene_timing_step();}
    int32_t delta(int32_t value,unsigned slot=0){
        auto s=step();if(!s.valid()||slot>=32)return 0;
        int64_t total=int64_t(value)*s.numerator+remainders[slot];
        int32_t result=int32_t(total/s.divisions);remainders[slot]=total%s.divisions;return result;
    }
    int32_t euler_correction(int32_t acceleration,unsigned slot=0){
        auto s=step();if(!s.valid()||slot>=8)return 0;int64_t denominator=2LL*s.divisions*s.divisions;
        int64_t total=int64_t(acceleration)*(s.divisions-s.numerator)*s.numerator+eulerRemainders[slot];
        int32_t result=int32_t(total/denominator);eulerRemainders[slot]=total%denominator;return result;
    }
    bool change(SceneTimingKind kind){
        if(divisions)services.end_scene_timing();divisions=services.begin_scene_timing(kind);
        for(auto& v:remainders)v=0;for(auto& v:eulerRemainders)v=0;return divisions!=0;
    }
    uint64_t budget(uint64_t original)const{return original*divisions;}

    SceneTimingScope(SceneTimingServices& s,SceneTimingKind kind):services(s),divisions(s.begin_scene_timing(kind)){}
    SceneTimingScope(const SceneTimingScope&)=delete;
    SceneTimingScope& operator=(const SceneTimingScope&)=delete;
    ~SceneTimingScope(){if(divisions)services.end_scene_timing();}
};
}
