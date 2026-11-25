#pragma once
#include "../t3k_native.h"
#include <span>
#include <string>
#include <cmath>
namespace tempest_native {
// Owned native RAM views; no Nuance linkage, scheduling or instruction execution.
struct RendererMemory {
    std::span<const uint8_t> system,main,local;
};
bool build_renderer_picture(const RendererMemory&,uint32_t picture,t3knative::Picture&,std::string& error);
// Owns feedback history. One submitted game picture advances it once; repeated
// display refreshes only redraw. No motion interpolation or presentation clock
// is allowed to advance gameplay/feedback implicitly.
class NativeFrameRenderer {
public:
    bool initialize(SDL_Renderer*,std::string& error);
    bool seed(SDL_Renderer*,SDL_Texture* previous,const SDL_FRect&,std::string& error);
    bool submit(SDL_Renderer*,const RendererMemory&,uint64_t serial,const SDL_FRect&,std::string& error);
    bool submit_picture(SDL_Renderer*,t3knative::Picture,uint64_t serial,const SDL_FRect&,std::string& error);
    struct PresentationState{t3knative::Picture previous,current;t3knative::FeedbackState feedback;uint64_t serial=0;bool valid=false,interpolate=false,pending=false;float alpha=1;};
    bool capture_presentation(SDL_Renderer* renderer,PresentationState& out,std::string& error){out={previous_,picture_,{},serial_,valid_,interpolate_,pendingBlend_,alpha_};return capture_feedback(renderer,out.feedback,error);}
    bool restore_presentation(SDL_Renderer* renderer,const PresentationState& in,std::string& error){if(in.alpha<0||in.alpha>1||!std::isfinite(in.alpha)){error="Invalid presentation phase";return false;}if(!restore_feedback(renderer,in.feedback,error))return false;previous_=in.previous;picture_=in.current;serial_=in.serial;valid_=in.valid;interpolate_=in.interpolate;pendingBlend_=in.pending;alpha_=in.alpha;return true;}
    void set_interpolation(bool value){interpolate_=value;}
    bool advance_presentation(SDL_Renderer*,const SDL_FRect&,float alpha);
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
    t3knative::Picture picture_,previous_;
    bool interpolate_=false,pendingBlend_=false;float alpha_=1;
    uint64_t serial_=0;bool valid_=false,initialized_=false;
};
}
