#include "native_renderer_bridge.h"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
namespace t3knative { extern bool g_ycc; }
namespace {
thread_local const tempest_native::RendererMemory* bound=nullptr;
}
// The sole host symbol consumed by John's renderer. Bind to native-owned data,
// not the original nuon_host.cpp or its emulator DLL.
namespace nuon {
uint8_t* Memory(int which,size_t* size){
    if(!bound){*size=0;return nullptr;}
    auto view=which==0?bound->system:which==1?bound->main:bound->local;
    *size=view.size();return const_cast<uint8_t*>(view.data());
}
}
namespace tempest_native {
bool build_renderer_picture(const RendererMemory& memory,uint32_t picture,t3knative::Picture& out,std::string& error){
    if(bound){error="nested renderer memory binding";return false;}
    if(!t3knative::g_ycc){error="initialize the native YCrCb pipeline before picture decoding";return false;}
    if(std::getenv("T3K_NATIVE_FORCE")||std::getenv("NUANCE_ONLYTYPES")||std::getenv("T3K_NATIVE_UNPORT")){error="renderer test overrides must be disabled for native frame integration";return false;}
    if(memory.system.size()!=0x800000||memory.main.size()!=0x800000||memory.local.size()!=0x800000){error="renderer requires complete 8 MiB memory banks";return false;}
    // Reject unterminated command lists before the inherited decoder's size cap.
    bool terminated=false;
    for(unsigned i=0;i<1024;++i){unsigned a=0x4E040+i*64+60;
        uint32_t type=uint32_t(memory.system[a])<<24|uint32_t(memory.system[a+1])<<16|uint32_t(memory.system[a+2])<<8|memory.system[a+3];
        if((type&255)==0xFF){terminated=true;break;}
    }
    if(!terminated){error="unterminated display list";return false;}
    struct Binding{Binding(const RendererMemory& m){bound=&m;}~Binding(){bound=nullptr;}} binding(memory);
    if(!t3knative::BuildPictureLive(0,picture,&out)){error="display-list memory is unavailable";return false;}
    if(!out.complete){char text[100];std::snprintf(text,sizeof(text),"unsupported drawing operation %08X",out.first_unknown);error=text;return false;}
    error.clear();return true;
}
bool NativeFrameRenderer::submit(SDL_Renderer* renderer,const RendererMemory& memory,uint64_t serial,const SDL_FRect& dst,std::string& error){
    if(!initialized_){error="native GPU pipeline is not initialized";return false;}
    if(valid_&&serial<=serial_){error="a game picture must be submitted exactly once and in order";return false;}
    t3knative::Picture next;if(!build_renderer_picture(memory,uint32_t(serial),next,error))return false;
    // The caller initializes the YCrCb pipeline once; select the inherited
    // original line style and always apply exactly one complete feedback step.
    return submit_picture(renderer,std::move(next),serial,dst,error);
}
bool NativeFrameRenderer::initialize(SDL_Renderer* renderer,std::string& error){
    if(initialized_)return true;
    if(!t3knative::InitPipeline(renderer)){error="native YCrCb GPU pipeline is unavailable";return false;}
    t3knative::SetLineStyle(false,1,0,100);initialized_=true;error.clear();return true;
}
bool NativeFrameRenderer::seed(SDL_Renderer* renderer,SDL_Texture* previous,const SDL_FRect& dst,std::string& error){
    if(!initialized_||valid_||!previous){error="feedback can only be seeded before the first native picture";return false;}
    renderer_.Seed(renderer,previous,dst);error.clear();return true;
}
bool NativeFrameRenderer::submit_picture(SDL_Renderer* renderer,t3knative::Picture next,uint64_t serial,const SDL_FRect& dst,std::string& error){
    if(!initialized_){error="native GPU pipeline is not initialized";return false;}
    if(valid_&&serial<=serial_){error="a game picture must be submitted exactly once and in order";return false;}
    if(!next.complete){error="cannot submit an incomplete game picture";return false;}
    if(pendingBlend_)advance_presentation(renderer,dst,1);
    if(interpolate_&&valid_){previous_=std::move(picture_);picture_=std::move(next);pendingBlend_=true;alpha_=0;renderer_.Redraw(renderer,dst,wide_?&view_:nullptr);}
    else{renderer_.Draw(renderer,next,next.segs,dst,1,wide_?&view_:nullptr,true);picture_=std::move(next);pendingBlend_=false;alpha_=1;}
    serial_=serial;valid_=true;error.clear();return true;
}
bool NativeFrameRenderer::advance_presentation(SDL_Renderer* renderer,const SDL_FRect& dst,float alpha){
    if(!valid_)return false;
    if(!pendingBlend_){renderer_.Redraw(renderer,dst,wide_?&view_:nullptr);return true;}
    alpha=(std::clamp)(alpha,alpha_,1.0f);
    if(alpha>alpha_){t3knative::Picture between;t3knative::InterpolatePicture(previous_,picture_,alpha,&between);renderer_.Draw(renderer,between,between.segs,dst,alpha-alpha_,wide_?&view_:nullptr,alpha>=1);alpha_=alpha;if(alpha>=1)pendingBlend_=false;}
    else renderer_.Redraw(renderer,dst,wide_?&view_:nullptr);
    return true;
}
bool NativeFrameRenderer::redraw(SDL_Renderer* renderer,const SDL_FRect& dst){if(!valid_)return false;renderer_.Redraw(renderer,dst,wide_?&view_:nullptr);return true;}
}
