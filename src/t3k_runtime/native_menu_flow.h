#pragma once
#include <memory>
#include <vector>
#include "native_preparation_backend.h"
#include "native_driver_audio.h"
#include "native_jump_dynamics.h"
#include "native_simulation_timing.h"
#include "game/TempestOpeningAnimation.h"
#include "game/TempestMenuInput.h"
#include "game/TempestOuterFrames.h"
#include "game/TempestSelectionMenu.h"
#include "game/TempestMenuTransition.h"
#include "game/TempestAnimatedMenu.h"
#include "game/TempestLevelSelection.h"
#include "game/TempestSession.h"
#include "game/TempestSessionWarpBonus.h"
#include "game/TempestSessionGameOver.h"
#include "game/TempestGeneratedLevel.h"
#include "game/TempestSessionAsset.h"
#include "game/TempestSessionWarpSkip.h"
#include "game/TempestHighScores.h"
namespace tempest_native {
// Device input and clocks are explicit host services. A deterministic validation
// host is not a calibrated video/audio clock or a live controller mapping.
struct NativeMenuHost {
    virtual bool begin_frame()=0;
    virtual void commit_frame()=0;
    // Commit controller/media services before a possibly multi-frame pause,
    // without advancing the picture clock or claiming a published frame.
    virtual void commit_services()=0;
    virtual void abort_frame()=0;
    virtual bool poll_inputs(NativeFrameMemory&)=0;
    virtual bool video_sync(int32_t,uint32_t&)=0;
    virtual bool audio_target(uint64_t current,uint64_t& target)=0;
    virtual bool detect(CommandMemory&,uint32_t port)=0;
    virtual void simulation_step(SimulationStep){}
    virtual bool precise_web_geometry()const{return false;}
    virtual bool gpu_effects()const{return false;}
    virtual SceneCadence scene_policy(SceneTimingKind)const{return {};}
    virtual void scene_clock(SceneCadence){}
    virtual void name_entry(bool){}
    virtual bool submit_picture(NativeFrameRenderer& pipeline,SDL_Renderer* renderer,t3knative::Picture picture,uint64_t serial,const SDL_FRect& dst,std::string& error){return pipeline.submit_picture(renderer,std::move(picture),serial,dst,error);}
    virtual void published_picture(const t3knative::Picture&,NativeFrameMemory&,bool gameplay){}
    virtual ~NativeMenuHost()=default;
};
class NativeMenuFlow final:public AnimatedMenuServices,public LevelSelectionServices,public GameSessionServices,private SceneInitializationServices,private DriverGameFrameServices,private NativeFrameOperation {
public:
    NativeMenuFlow(NativeFrameMemory& memory,NativePreparationBackend& compute,NativeAudioBackend& audio,NativeDriverAudio& driver,
                   NativeMenuHost& host,NativeFrameRenderer& pipeline,SDL_Renderer* renderer,SDL_FRect destination):
        memory_(memory),compute_(compute),audio_(audio),driver_(driver),host_(host),pipeline_(pipeline),renderer_(renderer),destination_(destination),integration_(compute,audio){}
    bool configure_timing(NativeSimulationTiming* timing){
        if(executor_||prepared_)return false;
        if(timing_!=timing)jumpDynamics_.reset();timing_=timing;return true;
    }
    unsigned begin_scene_timing(SceneTimingKind kind)override{
        auto cadence=host_.scene_policy(kind);if(!cadence.referenceHz)return 1;
        // Menu-owned fades share their parent's pace, including custom menu
        // settings. Gameplay transitions retain their separate scene profile.
        if(kind==SceneTimingKind::transition&&!scenes_.empty()){
            auto parent=scenes_.back().cadence;
            if(parent.kind==SceneTimingKind::particleCrawl||parent.kind==SceneTimingKind::animatedMenu||parent.kind==SceneTimingKind::levelSelect){cadence=parent;cadence.kind=kind;}
        }
        if(executor_||!cadence.divisions||cadence.divisions>120)return 0;
        auto timing=std::make_unique<NativeSimulationTiming>(memory_);if(!timing->reset(cadence.divisions,cadence.numerator))return 0;
        pipeline_.end_simulation_interval();scenes_.push_back({cadence,std::move(timing)});host_.scene_clock(cadence);return cadence.divisions;
    }
    void end_scene_timing()override{
        if(scenes_.empty())return;pipeline_.end_simulation_interval();integration_.set_value_services(nullptr);integration_.set_callback_timing(nullptr);scenes_.pop_back();frameTiming_=nullptr;
        if(scenes_.empty())host_.scene_clock({});else host_.scene_clock(scenes_.back().cadence);
        auto* resumed=!scenes_.empty()?scenes_.back().timing.get():timing_?timing_:backgroundTiming_.get();
        pipeline_.end_simulation_interval(resumed?resumed->simulation_step().index:0);
    }
    SimulationStep scene_timing_step()const override{return scenes_.empty()?SimulationStep{}:scenes_.back().timing->simulation_step();}
    SimulationStep simulation_step()const override{
        auto* active=executor_?frameTiming_:!scenes_.empty()?scenes_.back().timing.get():timing_?timing_:backgroundTiming_.get();
        return active?active->simulation_step():SimulationStep{};
    }
    uint64_t simulation_frames()const{return simulationFrames_;}
    bool initialize_scene(const SceneInitializationInput& input)override{
        if(executor_||!compute_.set_simulation_step({})||!memory_.begin())return fail("scene initialization cannot nest a frame");PoolSceneResult result;
        bool ok=initialize_driver_scene(memory_,input,*this,result)&&compute_.complete();
        if(ok){memory_.commit();compute_.commit_frame();if(profiling::enabled())profiling::scene=input.index;return true;}compute_.abort_frame();memory_.rollback();return fail("native menu scene initialization failed");
    }
    bool play_sound(uint32_t index,uint32_t pitch)override{
        if(executor_||!memory_.begin())return fail("opening sound cannot nest a frame");
        if(!audio_.begin_frame()){memory_.rollback();return fail("opening sound transaction failed");}
        uint32_t handle;bool ok=play_driver_ui_sound(memory_,driver_,index,handle,pitch);
        if(ok){memory_.commit();audio_.commit_frame();return true;}audio_.abort_frame();memory_.rollback();return fail("opening sound failed: "+audio_.failure());
    }
    bool menu_frame(const std::array<uint32_t,20>& context)override{
        bool ok=execute_frame(context,false);if(ok){auto* t=!scenes_.empty()?scenes_.back().timing.get():backgroundTiming_.get();if(t)ok=t->advance();}return ok;
    }
    bool gameplay_frame(const std::array<uint32_t,20>& context)override{
        if(executor_)return fail("gameplay caller cannot nest a native frame");
        if(scenes_.empty()&&!timing_)background_timing();
        if(scenes_.empty())host_.scene_clock(timing_?SceneCadence{}:host_.scene_policy(SceneTimingKind::none));
        host_.simulation_step(simulation_step());
        if(prepared_){if(context!=preparedContext_)return fail("gameplay retry changed the prepared caller");}
        else{
            if(!host_.begin_frame())return fail("gameplay services cannot begin");
            if(!memory_.begin()){host_.abort_frame();return false;}
            if(!audio_.begin_frame()){memory_.rollback();host_.abort_frame();return false;}
            DriverGameFramePreparation preparation;bool ok=host_.poll_inputs(memory_)&&prepare_driver_game_frame(memory_,context,*this,preparation,simulation_step());
            if(!ok){audio_.abort_frame();memory_.rollback();host_.abort_frame();return fail("gameplay control preparation failed");}
            memory_.commit();audio_.commit_frame();host_.commit_services();
            if(preparation.pause&&!pause_menu(preparation.context))return false;
            preparedContext_=context;prepared_=true;
        }
        if(!execute_frame(context,true))return false;prepared_=false;
        auto* t=!scenes_.empty()?scenes_.back().timing.get():!timing_?backgroundTiming_.get():nullptr;return !t||t->advance();
    }
    bool fractional_jump(uint32_t& position,uint32_t& velocity,uint32_t& fuel,bool held,uint32_t capacity,unsigned divisions)override{
        NativeJumpState state{position,velocity,fuel};bool thrust=false;
        auto step=simulation_step();
        for(unsigned unit=0;unit<step.numerator;++unit)
            if(!jumpDynamics_.advance(state,held,capacity,divisions,thrust))return false;
        position=state.position;velocity=state.velocity;fuel=state.fuel;return true;
    }
    bool execute_frame(const std::array<uint32_t,20>& context,bool gameplay){
        if(profiling::enabled())profiling::caller=context[17];
        auto* timing=!scenes_.empty()?scenes_.back().timing.get():gameplay&&timing_?timing_:background_timing();frameTiming_=timing;
        if(scenes_.empty())host_.scene_clock(timing==timing_?SceneCadence{}:host_.scene_policy(SceneTimingKind::none));
        SimulationStep step=timing?timing->simulation_step():SimulationStep{};host_.simulation_step(step);
        bool precise=host_.precise_web_geometry();
        t3knative::PictureBuildClocks clocks{};clocks.gpuRings=host_.gpu_effects()&&std::getenv("T3K_CPU_EFFECTS")==nullptr;clocks.gpuNumbers=clocks.gpuRings;clocks.gpuWeb=host_.gpu_effects()&&std::getenv("T3K_CPU_WEB")==nullptr;clocks.preciseWeb=precise;integration_.set_picture_clocks(clocks);
        if(!integration_.set_value_services(timing)||!integration_.set_callback_timing(timing)||!compute_.set_simulation_step(step,precise))return fail("simulation timing cannot change during a frame");
        if(executor_||!host_.begin_frame())return fail("menu host cannot begin frame");incoming_=context;gameplay_=gameplay;presented_=false;failure_.clear();
        if(!integration_.step_operation(memory_,*this,serial_+1,failure_)){host_.abort_frame();return false;}
        auto picture=integration_.take();if(!picture){host_.abort_frame();return fail("native menu frame was not published");}
        if(!host_.submit_picture(pipeline_,renderer_,std::move(picture->picture),picture->serial,destination_,failure_)){host_.abort_frame();return false;}
        host_.published_picture(pipeline_.picture(),memory_,gameplay_);
        jobs_+=picture->result.pass.workers;callbacks_+=picture->result.pass.callbacks;++serial_;if(gameplay&&timing&&timing==timing_)++simulationFrames_;host_.commit_frame();frameTiming_=nullptr;return true;
    }
    bool select_music(uint32_t track)override{
        if(executor_||!host_.begin_frame())return fail("music selection cannot nest a frame");
        if(!memory_.begin()){host_.abort_frame();return false;}
        if(!audio_.begin_frame()){memory_.rollback();host_.abort_frame();return false;}
        if(host_.poll_inputs(memory_)&&select_driver_music_track(memory_,driver_,track)){memory_.commit();audio_.commit_frame();host_.commit_services();return true;}
        audio_.abort_frame();memory_.rollback();host_.abort_frame();return fail("menu music selection failed: "+audio_.failure());
    }
    bool transition(const DriverTransitionInput& input,const std::array<uint32_t,20>& context)override{return run_menu_transition(memory_,input,context,static_cast<AnimatedMenuServices&>(*this));}
    bool vertical_axis(uint32_t latch,uint32_t mask,uint32_t& result)override{
        return axis(latch,mask,false,result);
    }
    bool menu_action(uint32_t latch,uint32_t mask,uint32_t& result){
        if(executor_||!memory_.begin())return false;if(!audio_.begin_frame()){memory_.rollback();return false;}
        if(poll_driver_menu_action(memory_,driver_,latch,mask,result)){memory_.commit();audio_.commit_frame();return true;}
        audio_.abort_frame();memory_.rollback();return fail("menu action dependency failed");
    }
    bool generate_level(uint32_t destination)override{return generate_session_level(memory_,destination);}
    bool axis(uint32_t latch,uint32_t mask,bool horizontal,uint32_t& result)override{
        if(executor_||!memory_.begin())return false;if(!audio_.begin_frame()){memory_.rollback();return false;}
        if(poll_driver_menu_axis(memory_,driver_,latch,mask,horizontal?MenuAxis::horizontal:MenuAxis::vertical,result)){memory_.commit();audio_.commit_frame();return true;}
        audio_.abort_frame();memory_.rollback();return fail("menu axis dependency failed");
    }
    bool play_request(const SoundRequest& request,uint32_t& handle)override{
        if(executor_)return audio_.play_request(request,handle);
        if(!memory_.begin())return false;if(!audio_.begin_frame()){memory_.rollback();return false;}
        if(audio_.play_request(request,handle)){memory_.commit();audio_.commit_frame();return true;}
        audio_.abort_frame();memory_.rollback();return fail("session sound request failed");
    }
    bool consume_sounds()override{
        if(executor_)return consume_driver_sounds(memory_,driver_);
        if(!memory_.begin())return false;if(!audio_.begin_frame()){memory_.rollback();return false;}
        if(consume_driver_sounds(memory_,driver_)){memory_.commit();audio_.commit_frame();return true;}
        audio_.abort_frame();memory_.rollback();return fail("session queued sound failed");
    }
    bool set_volume_levels(uint32_t stream,uint32_t voices){
        if(executor_)return audio_.set_volumes(stream,voices);
        if(!memory_.begin())return false;if(!audio_.begin_frame()){memory_.rollback();return false;}
        if(audio_.set_volumes(stream,voices)){memory_.commit();audio_.commit_frame();return true;}
        audio_.abort_frame();memory_.rollback();return fail("pause volume update failed");
    }
    bool start_descriptor(uint32_t requested,uint32_t descriptor,uint32_t spatial,uint32_t volume,uint32_t auxiliary,uint32_t& handle)override{
        if(executor_)return audio_.play_descriptor(requested,descriptor,spatial,volume,auxiliary,handle);
        if(!memory_.begin())return false;if(!audio_.begin_frame()){memory_.rollback();return false;}
        if(audio_.play_descriptor(requested,descriptor,spatial,volume,auxiliary,handle)){memory_.commit();audio_.commit_frame();return true;}
        audio_.abort_frame();memory_.rollback();return fail("session descriptor voice failed");
    }
    bool stop_voice(uint32_t handle)override{
        if(executor_)return audio_.stop_voice(handle);
        if(!memory_.begin())return false;if(!audio_.begin_frame()){memory_.rollback();return false;}
        if(audio_.stop_voice(handle)){memory_.commit();audio_.commit_frame();return true;}
        audio_.abort_frame();memory_.rollback();return fail("session voice stop failed");
    }
    bool show_asset(uint32_t source,uint32_t scene,const std::array<uint32_t,20>& incoming)override{return run_session_asset(memory_,{source,scene,{incoming,0}},*this);}
    bool game_over_banner(uint32_t player,uint32_t score,const std::array<uint32_t,20>& incoming)override{return run_session_game_over(memory_,{player,score,{incoming,0x8001A568}},*this);}
    bool warp_bonus(const std::array<uint32_t,20>& incoming,uint32_t& result)override{return run_session_warp_bonus(memory_,incoming,*this,result);}
    bool warp_skip(const std::array<uint32_t,20>& incoming,uint32_t& result)override{return run_session_warp_skip(memory_,{incoming,0x8001EAAC},*this,result);}
    bool high_score_entry(uint32_t content,uint32_t scene,uint32_t scores,uint32_t player,const std::array<uint32_t,20>& incoming)override{uint32_t result;host_.name_entry(true);bool ok=run_high_score_entry(memory_,{content,scene,scores,player,{incoming,0x8001EE80}},*this,result,0xFFFFFFFF);host_.name_entry(false);return ok;}
    struct Checkpoint{
        uint64_t serial=0,jobs=0,callbacks=0;
        std::array<uint32_t,20> incoming{},preparedContext{};
        bool prepared=false,gameplay=false;
    };
    bool checkpoint(Checkpoint& out)const{
        if(executor_||memory_.in_transaction())return false;
        out={serial_,jobs_,callbacks_,incoming_,preparedContext_,prepared_,gameplay_};return true;
    }
    bool restore(const Checkpoint& in){
        if(executor_||memory_.in_transaction()||!integration_.restore_sequence(in.serial))return false;
        serial_=in.serial;jobs_=in.jobs;callbacks_=in.callbacks;incoming_=in.incoming;preparedContext_=in.preparedContext;
        prepared_=in.prepared;gameplay_=in.gameplay;presented_=false;failure_.clear();return true;
    }
    uint64_t frames()const{return serial_;}
    uint64_t preparation_jobs()const{return jobs_;}
    uint64_t callbacks()const{return callbacks_;}
    const std::string& failure()const{return failure_;}
private:
    NativeSimulationTiming* background_timing(){
        auto c=host_.scene_policy(SceneTimingKind::none);if(!c.referenceHz)return nullptr;
        if(!backgroundTiming_){backgroundTiming_=std::make_unique<NativeSimulationTiming>(memory_);if(!backgroundTiming_->reset(c.divisions,c.numerator))return nullptr;}
        return backgroundTiming_.get();
    }
    bool initialize_workers(const std::array<uint32_t,48>& environment)override{return compute_.initialize(environment);}
    bool run(NativeGameFrameExecutor& executor,std::string& error)override{
        executor_=&executor;bool ok=host_.poll_inputs(memory_)&&(gameplay_?finish_driver_game_frame(memory_,incoming_,*this):run_driver_menu_frame(memory_,incoming_,*this));uint64_t target;
        if(ok)ok=host_.audio_target(audio_.completed_blocks(),target)&&audio_.advance_to(target);executor_=nullptr;
        if(!ok){if(failure_.empty())failure_=audio_.failure();if(!failure_.empty())error=failure_;return false;}
        if(!presented_){error="menu frame omitted presentation";return false;}return true;
    }
    bool video_sync(int32_t request,uint32_t& result)override{return host_.video_sync(request,result);}
    bool game_frame(const GameFrameInput& input)override{return executor_&&executor_->game_frame(input);}
    bool present(const DriverPresentation& request)override{
        uint32_t current;if(presented_||!memory_.read32(0x806B9934,current)||request.buffer!=current||request.flags!=0x002DC840||request.width!=360||request.height!=240||request.extra)return false;
        presented_=true;return true;
    }
    bool maintain_music()override{return driver_.maintain_music();}
    bool detect(uint32_t port)override{return host_.detect(memory_,port);}
    bool menu_frame()override{return fail("interactive discovery frame context is not reconstructed");}
    bool pause_menu(const std::array<uint32_t,20>& context)override{
        if(executor_)return fail("pause must precede the prepared gameplay frame");
        SceneTimingScope cadence(static_cast<AnimatedMenuServices&>(*this),SceneTimingKind::pause);if(!cadence.divisions)return false;
        struct Services:DriverSound,DriverVolumeServices{
            NativeMenuFlow& owner;Services(NativeMenuFlow& f):owner(f){}
            bool play_request(const SoundRequest& r,uint32_t& h)override{return owner.play_request(r,h);}
            bool set_volumes(uint32_t stream,uint32_t voices)override{return owner.set_volume_levels(stream,voices);}
        } services(*this);
        return run_driver_pause_menu_frames(memory_,context,services,[&](const auto& data){return menu_frame(data);},0xFFFFFFFF);
    }
    bool fail(std::string message){failure_=std::move(message);return false;}
    NativeFrameMemory& memory_;NativePreparationBackend& compute_;NativeAudioBackend& audio_;NativeDriverAudio& driver_;
    NativeMenuHost& host_;NativeFrameRenderer& pipeline_;SDL_Renderer* renderer_;SDL_FRect destination_;
    NativeJumpDynamics jumpDynamics_;
    struct SceneContext {SceneCadence cadence;std::unique_ptr<NativeSimulationTiming> timing;};
    std::vector<SceneContext> scenes_;
    std::unique_ptr<NativeSimulationTiming> backgroundTiming_;
    NativeSimulationTiming* frameTiming_=nullptr;
    NativeSimulationTiming* timing_=nullptr;uint64_t simulationFrames_=0;
    NativeFrameIntegration integration_;NativeGameFrameExecutor* executor_=nullptr;std::array<uint32_t,20> incoming_{};
    std::array<uint32_t,20> preparedContext_{};bool prepared_=false;
    uint64_t serial_=0,jobs_=0,callbacks_=0;bool presented_=false,gameplay_=false;std::string failure_;
};
}
