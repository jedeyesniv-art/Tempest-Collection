#pragma once
#include "native_renderer_bridge.h"
#include "native_frame_memory.h"
#include "game/TempestFrameOrchestrator.h"
#include <optional>
namespace tempest_native {
struct FrameComputeBackend{
    virtual bool initialize(const std::array<uint32_t,48>&)=0;
    virtual bool submit(const FrameWorkerRequest&)=0;
    // Fence the one preparation job. A synchronous submit publishes completed
    // outputs before returning. A background implementation snapshots inputs
    // and commits through NativeFrameMemory on the caller's complete() call;
    // background workers must not write its transaction map directly.
    // Implementations own their persistent worker-local state.
    virtual bool complete()=0;
    virtual void commit_frame()=0;
    virtual void abort_frame()=0;
    virtual ~FrameComputeBackend()=default;
};
struct FrameAudioBackend:CallbackServices{
    // Reserve cue/handle outcomes during a step; make playback visible only
    // after its data and picture are accepted. Abort releases reservations.
    virtual bool begin_frame()=0;
    virtual void commit_frame()=0;
    virtual void abort_frame()=0;
};
struct PublishedGameFrame{uint64_t serial;t3knative::Picture picture;GameFrameResult result;};
struct NativeGameFrameExecutor {
    virtual bool game_frame(const GameFrameInput&)=0;
    virtual ~NativeGameFrameExecutor()=default;
};
struct NativeFrameOperation {
    virtual bool run(NativeGameFrameExecutor&,std::string& error)=0;
    virtual ~NativeFrameOperation()=default;
};
struct FramePlatformBackend{
    virtual bool settle(NativeFrameMemory&,std::string& error)=0;
    virtual void commit_frame()=0;
    virtual void abort_frame()=0;
    virtual ~FramePlatformBackend()=default;
};
struct NativeFrameCheckpoint{
    virtual bool prepared(const NativeFrameMemory&,const GameFrameResult&,std::string& error)=0;
    virtual ~NativeFrameCheckpoint()=default;
};
// Orchestration -> preparation fence -> immutable CPU picture -> GPU handoff.
// The compute and sound backends are required; this library provides no silent
// no-op worker backend and never calls an emulator to execute missing work.
class NativeFrameIntegration:private FrameHostServices,private NativeGameFrameExecutor{
public:
    NativeFrameIntegration(FrameComputeBackend& compute,FrameAudioBackend& sound,NativeFrameCheckpoint* checkpoint=nullptr,FramePlatformBackend* platform=nullptr):compute_(compute),sound_(sound),checkpoint_(checkpoint),platform_(platform){}
    bool step(NativeFrameMemory&,const GameFrameInput&,uint64_t serial,std::string& error);
    // One original outer frame, including its caller's input/presentation/music
    // effects, participates in the same rollback/publication transaction.
    bool step_operation(NativeFrameMemory&,NativeFrameOperation&,uint64_t serial,std::string& error);
    std::optional<PublishedGameFrame> take();
    // Explicit picture-domain input, independent of gameplay counters. The
    // baseline uses the original job clock until a caller supplies an override.
    void set_picture_clocks(const t3knative::PictureBuildClocks& clocks){pictureClocks_=clocks;}
    bool set_value_services(ObjectValueServices* services){if(memory_||published_||pending_)return false;values_=services;return true;}
    bool set_callback_timing(CallbackTimingServices* services){if(memory_||published_||pending_)return false;timing_=services;return true;}
    bool restore_sequence(uint64_t serial){
        if(memory_||published_||pending_)return false;
        lastSerial_=serial;haveSerial_=serial!=0;computePending_=false;return true;
    }
private:
    bool game_frame(const GameFrameInput&)override;
    bool initialize_workers(const std::array<uint32_t,48>&)override;
    bool prepare_worker_slot()override;
    bool submit_worker(const FrameWorkerRequest&)override;
    bool wait_compute()override;
    bool submit_render(const FrameRenderRequest&)override;
    bool wait_render()override;
    bool play_sound(uint32_t,uint32_t&)override;
    bool frame_failure(const char*,uint32_t,uint32_t)override;
    SimulationStep simulation_step()const override{return timing_?timing_->simulation_step():SimulationStep{};}
    bool fire_opportunity()const override{return !timing_||timing_->fire_opportunity();}
    bool surface_response(uint32_t object,uint32_t lane)override{return !timing_||timing_->surface_response(object,lane);}
    bool callback_opportunity(uint32_t entry,bool event)const override{return !timing_||timing_->callback_opportunity(entry,event);}
    bool object_values(uint32_t object,uint32_t base,const ObjectUpdateInput& input,ObjectUpdateOutput& output)override{
        profiling::Scope profile(profiling::values);
        return values_?values_->object_values(object,base,input,output):update_object_values(input,output);
    }
    void object_instantiated(uint32_t object)override{if(values_)values_->object_instantiated(object);}
    FrameComputeBackend& compute_;FrameAudioBackend& sound_;
    NativeFrameCheckpoint* checkpoint_;
    FramePlatformBackend* platform_;
    ObjectValueServices* values_=nullptr;
    CallbackTimingServices* timing_=nullptr;
    NativeFrameMemory* memory_=nullptr;std::optional<t3knative::Picture> pending_;
    std::optional<PublishedGameFrame> published_;uint64_t lastSerial_=0;bool haveSerial_=false;
    bool computeComplete_=false,computePending_=false,renderComplete_=false;std::string failure_;
    t3knative::PictureBuildClocks pictureClocks_{};
    GameFrameResult result_{};bool executed_=false,operationFailed_=false;
};
}
