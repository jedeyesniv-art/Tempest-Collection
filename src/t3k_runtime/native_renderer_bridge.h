#pragma once
#include "../t3k_native.h"
#include "native_web_precision.h"
#include "native_draw_sources.h"
#include <span>
#include <string>
#include <cmath>
namespace tempest_native {
// Owned native RAM views; no Nuance linkage, scheduling or instruction execution.
struct RendererMemory {
    std::span<const uint8_t> system,main,local;
    const WebRenderPrecision* webPrecision=nullptr;
    const std::vector<NativeDrawSource>* drawSources=nullptr;
};
bool build_renderer_picture(const RendererMemory&,uint32_t picture,t3knative::Picture&,std::string& error,const t3knative::PictureBuildClocks& clocks = {});
// Owns feedback history. One submitted game picture advances it once; repeated
// display refreshes only redraw. No motion interpolation or presentation clock
// is allowed to advance gameplay/feedback implicitly.
class NativeFrameRenderer {
public:
    void end_simulation_interval(unsigned phase=0){renderer_.EndSimulationInterval(phase);}
    bool initialize(SDL_Renderer*,std::string& error);
    bool seed(SDL_Renderer*,SDL_Texture* previous,const SDL_FRect&,std::string& error);
    bool submit(SDL_Renderer*,const RendererMemory&,uint64_t serial,const SDL_FRect&,std::string& error);
    bool submit_picture(SDL_Renderer*,t3knative::Picture,uint64_t serial,const SDL_FRect&,std::string& error);
    bool submit_simulation_picture(SDL_Renderer*,t3knative::Picture,uint64_t serial,const SDL_FRect&,unsigned substep,unsigned divisions,std::string& error,unsigned numerator=1,t3knative::FeedbackSampling feedback=t3knative::FeedbackSampling::pinned);
    struct PresentationState{t3knative::Picture current;t3knative::FeedbackState feedback;uint64_t serial=0;bool valid=false;};
    bool capture_presentation(SDL_Renderer* renderer,PresentationState& out,std::string& error){out={picture_,{},serial_,valid_};return capture_feedback(renderer,out.feedback,error);}
    bool restore_presentation(SDL_Renderer* renderer,const PresentationState& in,std::string& error){if(!restore_feedback(renderer,in.feedback,error))return false;picture_=in.current;serial_=in.serial;valid_=in.valid;return true;}
    bool capture_feedback(SDL_Renderer* renderer,t3knative::FeedbackState& out,std::string& error){return renderer_.CaptureFeedback(renderer,out,error);}
    bool restore_feedback(SDL_Renderer* renderer,const t3knative::FeedbackState& in,std::string& error){return renderer_.RestoreFeedback(renderer,in,error);}
    bool accept_without_draw(t3knative::Picture next,uint64_t serial,std::string& error){if(!initialized_||!next.complete){error="Invalid replay picture";return false;}picture_=std::move(next);serial_=serial;valid_=true;return true;}
    void restore_sequence(uint64_t serial){serial_=serial;valid_=serial!=0;}
    void set_view(const SDL_FRect* view){wide_=view!=nullptr;if(view)view_=*view;}
    bool redraw(SDL_Renderer*,const SDL_FRect&);
    const t3knative::Picture& picture()const{return picture_;}
private:
    t3knative::Renderer renderer_;
    SDL_FRect view_{};bool wide_=false;
    t3knative::Picture picture_;
    uint64_t serial_=0;bool valid_=false,initialized_=false;
};
}
