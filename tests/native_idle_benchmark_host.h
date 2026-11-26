#pragma once
#include "native_menu_flow.h"
#include "native_media.h"
#include "native_controller_input.h"
#include "../src/settings.h"
#include <cstdio>
namespace tempest_native {
// Offline performance host: the production frame transaction, workers, mixer
// and renderer are unchanged. Both benchmark frontends draw once per original
// picture into an offscreen target without interpolated frames or presentation.
class NativeBenchmarkHost final:public NativeMenuHost {
public:
    NativeBenchmarkHost(NativeMedia& media,NativeAudioStreamQueue& stream,NativeAudioBackend& audio,SDL_Window*,SDL_Renderer* renderer,bool,bool=false):media_(media),stream_(stream),audio_(audio),renderer_(renderer){
        target_=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_XRGB8888,SDL_TEXTUREACCESS_TARGET,1440,1080);
        if(target_)SDL_SetRenderTarget(renderer,target_);SDL_SetRenderVSync(renderer,0);
        if(const char* rate=SDL_getenv("TEMPEST_BENCH_LOGIC_HZ"))logicHz_=std::atoi(rate)==60?60:24;
        if(const char* path=SDL_getenv("TEMPEST_BENCH_CSV")){csv_=std::fopen(path,"w");if(csv_)std::fprintf(csv_,"picture,frame_ms,audio_blocks\n");}
    }
    ~NativeBenchmarkHost(){if(csv_)std::fclose(csv_);SDL_SetRenderTarget(renderer_,nullptr);if(target_)SDL_DestroyTexture(target_);}
    bool ready()const{return target_&&csv_;}bool check()const{return true;}bool quit()const{return quit_;}bool failed()const{return failed_;}uint64_t frames()const{return frames_;}
    bool gpu_effects()const override{return true;}
    void script_button(uint16_t button,unsigned delay=65){button_=button;at_=frames_+delay;}
    void schedule_button(unsigned delay,uint16_t button){scheduled_[frames_+delay]=button;}
    void schedule_motion(unsigned,double){}
    bool begin_frame()override{if(active_||quit_||!media_.begin_frame())return false;auto reads=stream_.take_reads();pending_.insert(pending_.end(),reads.begin(),reads.end());active_=true;return true;}
    void commit_services()override{pending_.clear();media_.commit_frame();active_=false;}
    void commit_frame()override{commit_services();++frames_;audio_.take_audio();if(!SDL_SetRenderTarget(renderer_,nullptr)||!SDL_FlushRenderer(renderer_)||!SDL_SetRenderTarget(renderer_,target_))failed_=quit_=true;}
    void abort_frame()override{media_.abort_frame();active_=false;}
    bool poll_inputs(NativeFrameMemory& m)override{memory_=&m;for(auto& read:pending_)if(!media_.accept(read))return false;unsigned done;if(!media_.deliver(m,stream_,2048,done))return false;
        uint16_t buttons=frames_==at_?button_:0;if(auto it=scheduled_.find(frames_);it!=scheduled_.end())buttons|=it->second;
        auto record=native_pad_record({true});record.words[1]=uint32_t(buttons)<<16;NativeControllers controllers(m,{});return controllers.publish(1,record);}
    bool video_sync(int32_t request,uint32_t& field)override{field=uint32_t(frames_*60/logicHz_);return request==-1;}
    bool audio_target(uint64_t current,uint64_t& target)override{target=(std::max)(current,(frames_+1)*500/logicHz_);return true;}
    bool detect(CommandMemory&,uint32_t)override{return false;}
    void measured(double){uint64_t now=SDL_GetTicksNS();double ms=double(now-lastActiveEnd_)/1e6;lastActiveEnd_=now;if(++activeFrames_<=32)return;if(activeFrames_==33||activeFrames_==160){uint32_t total,used,reason,score;memory_->read32(0x800BA300,total);memory_->read32(0x800BA2FC,used);memory_->read32(0x800BA2F8,reason);memory_->read32(0x800DC240,score);std::printf("BENCH STATE: active=%u wave=%u used=%u exit=%u score=%u\n",activeFrames_,total,used,reason,score);}std::fprintf(csv_,"%u,%.9f,%llu\n",activeFrames_-32,ms,audio_.completed_blocks());
        if(activeFrames_==160){quit_=true;std::printf("BENCHMARK: 128 active gameplay pictures; 32-picture warmup; 1440x1080; interpolation off; no wall-clock pacing; virtual video/audio cadence=%u Hz.\n",logicHz_);}}
private:
    NativeMedia& media_;NativeAudioStreamQueue& stream_;NativeAudioBackend& audio_;SDL_Renderer* renderer_;SDL_Texture* target_=nullptr;FILE* csv_=nullptr;
    NativeFrameMemory* memory_=nullptr;
    std::vector<NiseStreamRead> pending_;std::map<uint64_t,uint16_t> scheduled_;uint64_t frames_=0,at_=0,lastActiveEnd_=0;uint16_t button_=0;unsigned activeFrames_=0,logicHz_=24;bool active_=false,quit_=false,failed_=false;
};
class MeasuredGameServices:public GameSessionServices {
public:
    MeasuredGameServices(NativeMenuFlow& f,NativeBenchmarkHost& h):flow(f),host(h){}
    bool initialize_scene(const SceneInitializationInput& in)override{return flow.initialize_scene(in);}
    bool menu_frame(const std::array<uint32_t,20>& c)override{return flow.menu_frame(c);}
    bool gameplay_frame(const std::array<uint32_t,20>& c)override{uint64_t begin=SDL_GetTicksNS();bool ok=flow.gameplay_frame(c);if(ok&&c[17]==0x8001DF40)host.measured(double(SDL_GetTicksNS()-begin)/1e6);return ok&&!host.quit();}
    bool play_sound(uint32_t a,uint32_t b)override{return flow.play_sound(a,b);}
    bool generate_level(uint32_t a)override{return flow.generate_level(a);}
    bool select_music(uint32_t a)override{return flow.select_music(a);}
    bool show_asset(uint32_t a,uint32_t b,const std::array<uint32_t,20>& c)override{return flow.show_asset(a,b,c);}
    bool play_request(const SoundRequest& a,uint32_t& b)override{return flow.play_request(a,b);}
    bool start_descriptor(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t& f)override{return flow.start_descriptor(a,b,c,d,e,f);}
    bool stop_voice(uint32_t a)override{return flow.stop_voice(a);}
    bool consume_sounds()override{return flow.consume_sounds();}
    bool game_over_banner(uint32_t a,uint32_t b,const std::array<uint32_t,20>& c)override{return flow.game_over_banner(a,b,c);}
    bool transition(const DriverTransitionInput& a,const std::array<uint32_t,20>& c)override{return flow.transition(a,c);}
    bool warp_bonus(const std::array<uint32_t,20>& c,uint32_t& a)override{return flow.warp_bonus(c,a);}
    bool warp_skip(const std::array<uint32_t,20>& c,uint32_t& a)override{return flow.warp_skip(c,a);}
    bool vertical_axis(uint32_t a,uint32_t b,uint32_t& c)override{return flow.vertical_axis(a,b,c);}
    bool high_score_entry(uint32_t a,uint32_t b,uint32_t c,uint32_t d,const std::array<uint32_t,20>& e)override{return flow.high_score_entry(a,b,c,d,e);}
private:NativeMenuFlow& flow;NativeBenchmarkHost& host;
};
}
