#include "native_frame_integration.h"
#include <cstdio>
namespace tempest_native {
bool NativeFrameIntegration::frame_failure(const char* operation,uint32_t object,uint32_t target){
    char detail[192];std::snprintf(detail,sizeof(detail),"native %s failed: object=%08X target=%08X",operation,object,target);failure_=detail;return false;
}
bool NativeFrameIntegration::initialize_workers(const std::array<uint32_t,48>& environment){computePending_=false;return compute_.initialize(environment);}
bool NativeFrameIntegration::prepare_worker_slot(){
    if(computeComplete_)return false;
    if(computePending_&&!compute_.complete())return false;
    computePending_=false;return true;
}
bool NativeFrameIntegration::submit_worker(const FrameWorkerRequest& request){
    if(computeComplete_||computePending_||request.type>18)return false;
    if(!compute_.submit(request))return false;computePending_=true;return true;
}
bool NativeFrameIntegration::wait_compute(){computeComplete_=compute_.complete();if(computeComplete_)computePending_=false;return computeComplete_;}
bool NativeFrameIntegration::play_sound(uint32_t cue,uint32_t& handle){return sound_.play_sound(cue,handle);}
bool NativeFrameIntegration::submit_render(const FrameRenderRequest& request){profiling::Scope profile(profiling::decode);
    if(!computeComplete_||pending_||!memory_||request.list!=0x8004E040){failure_="invalid native renderer handoff";return false;}
    t3knative::Picture picture;
    // The original drawing kernel copies the picture clock to MPE0's job word;
    // the web texture scroll consumes it. Keep renderer-local and gameplay-local
    // state separate even though both used the same local addresses on hardware.
    if(!memory_->renderer_word(0x100000,request.picture)||!build_renderer_picture(memory_->renderer_view(),request.picture,picture,failure_,pictureClocks_))return false;
    pending_=std::move(picture);return true;
}
bool NativeFrameIntegration::wait_render(){renderComplete_=pending_.has_value();return renderComplete_;}
bool NativeFrameIntegration::step(NativeFrameMemory& memory,const GameFrameInput& input,uint64_t serial,std::string& error){
    struct Operation:NativeFrameOperation{const GameFrameInput& input;Operation(const GameFrameInput& i):input(i){}bool run(NativeGameFrameExecutor& executor,std::string&)override{return executor.game_frame(input);}} operation(input);
    return step_operation(memory,operation,serial,error);
}
bool NativeFrameIntegration::game_frame(const GameFrameInput& input){
    if(!memory_||executed_){operationFailed_=true;failure_="outer frame must execute exactly one game frame";return false;}executed_=true;
    bool ok=prepare_game_frame(*memory_,input,result_,*this);
    if(ok&&platform_){profiling::Scope profile(profiling::platform);ok=platform_->settle(*memory_,failure_);}
    if(ok&&checkpoint_)ok=checkpoint_->prepared(*memory_,result_,failure_);
    if(ok)ok=complete_game_frame(*memory_,result_,*this);if(!ok)operationFailed_=true;return ok;
}
bool NativeFrameIntegration::step_operation(NativeFrameMemory& memory,NativeFrameOperation& operation,uint64_t serial,std::string& error){
    profiling::Scope profile(profiling::core);
    if(memory_){error="nested native frame step";return false;}
    if(published_){error="consume the published picture before advancing the next frame";return false;}
    if(haveSerial_&&serial<=lastSerial_){error="native frames must advance in serial order";return false;}
    if(!memory.begin()){error="native frame memory is already in a transaction";return false;}
    memory.begin_render_precision_frame();
    if(!sound_.begin_frame()){memory.rollback();error="native sound frame could not begin";return false;}
    if(values_&&!values_->begin_values()){sound_.abort_frame();memory.rollback();error="native value integration could not begin";return false;}
    memory_=&memory;pending_.reset();computeComplete_=renderComplete_=executed_=operationFailed_=false;failure_.clear();result_={};
    bool ok=operation.run(*this,failure_);memory_=nullptr;
    if(!ok||operationFailed_||!executed_||!renderComplete_||!pending_){if(values_)values_->abort_values();compute_.abort_frame();sound_.abort_frame();if(platform_)platform_->abort_frame();memory.rollback();pending_.reset();error=failure_.empty()?"native frame dependency failed at stage "+std::to_string(uint32_t(result_.stage)):failure_;return false;}
    {profiling::Scope profile(profiling::transaction);memory.commit();compute_.commit_frame();sound_.commit_frame();if(platform_)platform_->commit_frame();}
    if(values_)values_->commit_values();
    published_=PublishedGameFrame{serial,std::move(*pending_),result_};pending_.reset();lastSerial_=serial;haveSerial_=true;error.clear();return true;
}
std::optional<PublishedGameFrame> NativeFrameIntegration::take(){auto result=std::move(published_);published_.reset();return result;}
}
