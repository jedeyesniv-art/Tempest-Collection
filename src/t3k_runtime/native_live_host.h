#pragma once
#include "native_menu_flow.h"
#include "native_media.h"
#include "native_sdl_input.h"
#include "native_collection_config.h"
#include "native_service_journal.h"
#include <functional>
#include "../menu.h"
#include "../settings.h"
#include "../t3k_scores.h"
#include "../crt.h"
#include "../pixfont.h"
#include <filesystem>
namespace tempest_native {
class NativeLiveHost final:public NativeMenuHost {
public:
    NativeLiveHost(NativeMedia& media,NativeAudioStreamQueue& stream,NativeAudioBackend& audio,SDL_Window* window,SDL_Renderer* renderer,bool check,bool timed=false):
        media_(media),stream_(stream),audio_(audio),window_(window),renderer_(renderer),check_(check),timed_(timed){
        SDL_AudioSpec spec{SDL_AUDIO_S16,2,32000};device_=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
        if(device_&&!SDL_ResumeAudioStreamDevice(device_))failed_=true;
        crt::SetModeDefault(0,1);crt::Init(renderer_);
        epoch_=due_=SDL_GetTicksNS();
    }
    ~NativeLiveHost(){if(scene_)SDL_DestroyTexture(scene_);crt::Shutdown();if(device_)SDL_DestroyAudioStream(device_);}
    struct Checkpoint{
        uint64_t frames=0,elapsed=0;
        NativeControllerPort record{};NativeMouseSpinner::Checkpoint mouse{};
        std::vector<NiseStreamRead> pending;
        int32_t mouseDelta=0;
        bool sampled=false,mouseMode=false,reseed=false,naming=false;
        double rate=24,speedFrom=24,speedTo=24;uint64_t blendElapsed=0,blendDuration=0;
    };
    NativeServiceJournal journal;
    std::function<void()> resumeContinuation;
    std::function<void(int)> stateAction;
    bool checkpoint(Checkpoint& out)const{
        if(active_)return false;
        out={frames_,(pauseStart_?pauseStart_:SDL_GetTicksNS())-epoch_,record_,mouse_.checkpoint(),pending_,mouseDelta_,sampled_,mouseMode_,reseed_,naming_,rate_,speedFrom_,speedTo_,(pauseStart_?pauseStart_:SDL_GetTicksNS())-blendStart_,blendDuration_};return true;
    }
    bool restore(const Checkpoint& in){
        if(active_||in.rate<15||in.rate>60||!std::isfinite(in.speedFrom)||!std::isfinite(in.speedTo)||!mouse_.restore(in.mouse))return false;
        frames_=in.frames;record_=in.record;pending_=in.pending;mouseDelta_=in.mouseDelta;sampled_=in.sampled;mouseMode_=in.mouseMode;reseed_=in.reseed;naming_=in.naming;
        rateFrom_=rateFrames_=displayFrames_=0;pauseStart_=0;blendStart_=SDL_GetTicksNS()-in.blendElapsed;blendDuration_=in.blendDuration;rate_=in.rate;speedFrom_=in.speedFrom;speedTo_=in.speedTo;epoch_=SDL_GetTicksNS()-in.elapsed;due_=speedTime_=SDL_GetTicksNS();failed_=quit_=false;
        return true;
    }
    void resume_live(){SDL_ResumeAudioStreamDevice(device_);sampled_=false;suppress_=true;mouse_.clear_motion();SDL_ClearAudioStream(device_);}
    bool ready()const{return device_!=nullptr&&!failed_;}
    bool failed()const{return failed_;}
    bool quit()const{return quit_;}
    bool check()const{return check_;}
    uint64_t frames()const{return frames_;}
    void script_button(uint16_t buttons,unsigned delay=65){script_=buttons;scriptAt_=frames_+delay;}
    void schedule_button(unsigned delay,uint16_t buttons){scheduled_[frames_+delay]=buttons;}
    void schedule_motion(unsigned delay,double counts){motion_[frames_+delay]=counts;}
    void schedule_axis(unsigned delay,int8_t x){axes_[frames_+delay]=x;}
    bool begin_frame()override{
        if(active_||quit_)return false;
        if(journal.at_boundary()){if(resumeContinuation)resumeContinuation();journal.finish_replay();}
        if(!journal.replaying()&&!sampled_){if(!events())return false;sampled_=true;}
        std::array<uint32_t,8> input{};for(unsigned i=0;i<4;++i)input[i]=record_.words[i];input[4]=uint32_t(mouseDelta_);input[5]=reseed_;input[6]=mouseMode_;input[7]=sampled_;
        if(!journal.exchange(NativeServiceKind::input,input))return false;
        if(journal.replaying()){for(unsigned i=0;i<4;++i)record_.words[i]=input[i];mouseDelta_=int32_t(input[4]);reseed_=input[5]!=0;mouseMode_=input[6]!=0;sampled_=input[7]!=0;}
        if(!media_.begin_frame())return false;auto reads=stream_.take_reads();pending_.insert(pending_.end(),reads.begin(),reads.end());active_=true;return true;
    }
    void commit_services()override{pending_.clear();media_.commit_frame();active_=false;reseed_=false;}
    void commit_frame()override{
        commit_services();++frames_;sampled_=false;
        auto blocks=audio_.take_audio();std::vector<int16_t> pcm;pcm.reserve(blocks.size()*128);
        for(auto& block:blocks)for(auto word:block){pcm.push_back(int16_t(word>>16));pcm.push_back(int16_t(word));}
        if(journal.replaying())return;
        if((!check_||timed_)&&!pcm.empty())volume::Put(device_,pcm.data(),pcm.size());
        for(auto it=collection.screenshots.begin();it!=collection.screenshots.end();){
            if(it->first==frames_){auto* picture=SDL_RenderReadPixels(renderer_,nullptr);if(picture){SDL_SaveBMP(picture,it->second.c_str());SDL_DestroySurface(picture);}it=collection.screenshots.erase(it);}else ++it;
        }
        draw_overlay();
        if(!SDL_RenderPresent(renderer_))failed_=quit_=true;
        if(!check_||timed_){uint64_t interval=uint64_t(1000000000.0/rate_);due_+=interval;auto now=SDL_GetTicksNS();if(due_<now)due_=now;}
    }
    void abort_frame()override{media_.abort_frame();active_=false;}
    bool poll_inputs(NativeFrameMemory& memory)override{
        for(const auto& read:pending_)if(!media_.accept(read))return false;
        unsigned completed;if(!media_.deliver(memory,stream_,2048,completed))return false;
        if(reseed_){uint32_t position=uint32_t(int32_t(int8_t(record_.words[2]>>8)))&~1U;
            if(!memory.write32(0x806B9948,position)||!memory.write32(0x801133B4,0)||!memory.write32(0x801133B8,64)||!memory.write32(0x801133D8,0)||!memory.write32(0x806B992C,0))return false;}
        if(mouseMode_&&!memory.write32(0x801133B4,uint32_t(mouseDelta_)))return false;
        if(!write_session_byte(memory,0x8010BEF0,mouseMode_?32:0))return false;
        NativeControllers controllers(memory,{});return controllers.publish(1,record_);
    }
    bool video_sync(int32_t request,uint32_t& field)override{
        std::array<uint32_t,8> event{uint32_t(request),check_?uint32_t(frames_*60/24):uint32_t((SDL_GetTicksNS()-epoch_)*60/1000000000ULL)};
        if(!journal.exchange(NativeServiceKind::video,event)||event[0]!=uint32_t(request))return false;field=event[1];return request==-1;
    }
    bool audio_target(uint64_t current,uint64_t& target)override{
        target=check_&&!timed_?(frames_+1)*500/24:(SDL_GetTicksNS()-epoch_)*500/1000000000ULL+16;target=(std::max)(current,target);
        std::array<uint32_t,8> event{uint32_t(current),uint32_t(current>>32),uint32_t(target),uint32_t(target>>32)};
        if(!journal.exchange(NativeServiceKind::audio,event)||(uint64_t(event[0])|(uint64_t(event[1])<<32))!=current)return false;
        target=uint64_t(event[2])|(uint64_t(event[3])<<32);return true;
    }
    bool detect(CommandMemory& memory,uint32_t port)override{
        // This frontend exposes one sampled logical controller, backed by the
        // selected pad/keyboard or mouse path. Other ports are disconnected.
        NativeControllers controllers(memory,{});return controllers.publish(port,port==1?record_:NativeControllerPort{});
    }
    bool submit_picture(NativeFrameRenderer& pipeline,SDL_Renderer* renderer,t3knative::Picture picture,uint64_t serial,const SDL_FRect&,std::string& error)override{
        if(journal.replaying())return pipeline.accept_without_draw(std::move(picture),serial,error);
        int outW=0,outH=0;SDL_GetRenderOutputSize(renderer,&outW,&outH);
        if(outW<=0||outH<=0){error="Invalid render output size";return false;}
        bool original=settings::Num("native_view",1)==0;
        int cap=int(settings::Num("resolution",0)),height=cap>0?(std::min)(cap,outH):outH;
        int width=(std::max)(1,int(double(outW)*height/outH));
        if(original){width=360;height=240;}
        if(!scene_||sceneWidth_!=width||sceneHeight_!=height){
            if(scene_)SDL_DestroyTexture(scene_);
            scene_=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,width,height);
            sceneWidth_=width;sceneHeight_=height;
            if(!scene_){error=SDL_GetError();return false;}
            SDL_SetTextureBlendMode(scene_,SDL_BLENDMODE_NONE);SDL_SetTextureScaleMode(scene_,SDL_SCALEMODE_LINEAR);
        }
        t3knative::SetLineStyle(!original&&settings::Num("sharp_vectors",1)!=0,float(settings::Num("sharp_core",0.35)),float(settings::Num("sharp_glow",0.4)),float(settings::Num("sharp_max_size",100)));
        SDL_SetRenderTarget(renderer,scene_);SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
        float h=(std::min)(float(height),float(width)*0.75f),w=h*4/3;
        SDL_FRect dst{(width-w)/2,(height-h)/2,w,h},wide{0,(height-h)/2,float(width),h};
        pipeline.set_view(!original&&settings::Num("widescreen",1)!=0&&width>w+1?&wide:nullptr);
        pipeline.set_interpolation(!original&&(!check_||timed_)&&settings::Num("native_interpolate",1)!=0);
        bool ok=pipeline.submit_picture(renderer,std::move(picture),serial,dst,error);
        SDL_SetRenderTarget(renderer,nullptr);
        if(!ok)return false;
        presentation_=&pipeline;presentationDst_=dst;presentationOutput_=original;blendStart_=SDL_GetTicksNS();blendDuration_=uint64_t(1000000000.0/rate_);
        SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);crt::LoadSettings();
        SDL_FRect output{0,0,float(outW),float(outH)};
        if(original){float oh=(std::min)(float(outH),float(outW)*0.75f),ow=oh*4/3;output={(outW-ow)/2,(outH-oh)/2,ow,oh};}
        crt::DrawPicture(renderer,scene_,output,360,240);return true;
    }
    void published_picture(const t3knative::Picture& picture,NativeFrameMemory& memory)override{
        bool zoom=false,web=false;for(const auto& op:picture.ops){zoom|=op.kind==t3knative::Op::kZoom;web|=op.kind==t3knative::Op::kWeb;}
        uint32_t position=0,flags=0,slotPosition=0;
        bool claw=false;
        if(web&&memory.read32(0x800DC100,position)){
            for(uint32_t at=0x8003C000;at<0x80040000;at+=4){
                if(memory.read32(at+12,flags)&&flags==0x80000401&&memory.read32(at,slotPosition)&&slotPosition==position){claw=true;break;}
            }
        }
        auto clamp=[](double value){return (std::clamp)(value,15.0,60.0);};
        double play=clamp(settings::Num("game_speed",21)),warp=settings::Num("warp_speed",60),menus=settings::Num("menu_speed",30);
        double target=zoom&&!web?(warp>0?clamp(warp):play):web&&claw?play:menus>0?clamp(menus):play;
        auto now=SDL_GetTicksNS();
        if(!speedTime_){rate_=target;speedFrom_=target;speedTo_=target;}
        if(target!=speedTo_){speedFrom_=rate_;speedTo_=target;}
        double seconds=(std::clamp)(settings::Num("warp_ramp",0.5),0.0,2.0);
        double step=seconds>0?(std::max)(std::abs(speedTo_-speedFrom_),1.0)*(now-speedTime_)*1e-9/seconds:60;
        if(rate_<speedTo_)rate_=(std::min)(speedTo_,rate_+step);else rate_=(std::max)(speedTo_,rate_-step);
        speedTime_=now;
        if(!journal.replaying()&&web&&settings::Num("cheat_inf_lives",0)!=0){uint32_t lives;if(memory.read32(0x800BAC7C,lives)&&(lives==1||lives==2))memory.write32(0x800BAC7C,3);}
        // Scores are the only inherited helper that writes through a RAM view.
        // Protect both writable regions with the native phase journal first.
        std::vector<uint32_t> scores;std::array<uint32_t,8> event{};memory.read32(0x800BAC7C,event[0]);
        if(!journal.replaying()){
            memory.edit_words(NativeFrameMemory::system,0x113420,32,[](auto){});
            memory.edit_words(NativeFrameMemory::system,0x10C3A0,0x1A0,[&](auto){auto bank=memory.bank(NativeFrameMemory::system);t3kscores::AfterField(const_cast<uint8_t*>(bank.data()),bank.size());});
            uint32_t v;for(uint32_t at=0x80113420;at<0x80113440;at+=4){memory.read32(at,v);scores.push_back(v);}
            for(uint32_t at=0x8010C3A0;at<0x8010C540;at+=4){memory.read32(at,v);scores.push_back(v);}
        }
        if(!journal.exchange(NativeServiceKind::scores,event,&scores)||scores.size()!=112){failed_=quit_=true;return;}
        if(journal.replaying()){
            memory.write32(0x800BAC7C,event[0]);
            unsigned i=0;for(uint32_t at=0x80113420;at<0x80113440;at+=4)memory.write32(at,scores[i++]);
            for(uint32_t at=0x8010C3A0;at<0x8010C540;at+=4)memory.write32(at,scores[i++]);
        }
    }
    void name_entry(bool active)override{naming_=active;if(active&&check_)std::printf("Native original name entry at frame %llu\n",frames_);}
private:
    bool events(){
        if(collection.restartAt&&frames_==*collection.restartAt){collection.restartAt.reset();std::fprintf(stderr,"Native restart at frame %llu\n",frames_);throw NativeRestart{};}
        for(auto it=collection.stateTests.begin();it!=collection.stateTests.end();){
            if(it->first==frames_){int action=it->second;it=collection.stateTests.erase(it);if(stateAction)stateAction(action);}
            else ++it;
        }
        if(timed_&&frames_>=700){quit_=true;std::printf("Live clock/audio check: %llu frames, %llu audio blocks, %d bytes queued.\n",frames_,audio_.completed_blocks(),SDL_GetAudioStreamQueued(device_));return false;}
        for(;;){
            SDL_Event event;while(SDL_PollEvent(&event)){
                pads::HandleEvent(event);
                if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat){if(event.key.scancode==SDL_SCANCODE_F11)toggle_fullscreen();if(event.key.scancode==SDL_SCANCODE_F5)settings::Reload();
                    if(event.key.scancode==SDL_SCANCODE_F4)settings::Save("show_rates",settings::Num("show_rates",0)==0?"1":"0");
                    if(event.key.scancode==SDL_SCANCODE_F6)settings::Save("native_interpolate",settings::Num("native_interpolate",1)==0?"1":"0");
                    if(event.key.scancode==SDL_SCANCODE_F7)settings::Save("native_view",settings::Num("native_view",1)==0?"1":"0");}
                if(event.type==SDL_EVENT_QUIT)quit_=true;
                if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&event.key.scancode==SDL_SCANCODE_ESCAPE){if(menu::IsOpen())menu::Close();else menu::Open();}
                if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN&&(event.gbutton.button==SDL_GAMEPAD_BUTTON_GUIDE||event.gbutton.button==SDL_GAMEPAD_BUTTON_BACK)){if(menu::IsOpen())menu::Close();else menu::Open();}
                if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST&&!check_&&!collection.menuTest)menu::Open();
            }
            if(collection.menuTest&&!menuTestOpened_&&frames_>=30){menuTestOpened_=true;menu::Open(collection.menuTest==2);}
            pad_=pads::Current();
            if(quit_)return false;
            if(menu::IsOpen()){
                auto start=SDL_GetTicksNS();pauseStart_=start;SDL_SetWindowRelativeMouseMode(window_,false);SDL_PauseAudioStreamDevice(device_);
                bool waitRelease=true;menu::Update({},SDL_GetTicksNS());
                while(menu::IsOpen()&&!quit_){
                    while(SDL_PollEvent(&event)){pads::HandleEvent(event);if(event.type==SDL_EVENT_QUIT)quit_=true;
                        if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN&&event.gbutton.button==SDL_GAMEPAD_BUTTON_GUIDE)menu::Close();
                        if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&event.key.scancode==SDL_SCANCODE_F11)toggle_fullscreen();
                    }pad_=pads::Current();
                    auto nav=menu::ReadNav(pad_,SDL_GetKeyboardState(nullptr));
                    if(waitRelease){bool held=nav.up||nav.down||nav.left||nav.right||nav.ok||nav.back||nav.cancel||controls::Held(pad_)!=0;nav={};waitRelease=held;}
                    uint32_t held=controls::Held(pad_),pressed=waitRelease?0:held&~bindHeld_;bindHeld_=held;
                    for(int i=0;i<controls::kButtonCount;++i)if(pressed&(1U<<i)){nav.new_button=i;break;}
                    int action=menu::Update(nav,SDL_GetTicksNS());
                    if(action==menu::kToggleFullscreen)toggle_fullscreen();
                    if((action==menu::kSaveState||action==menu::kLoadState)&&stateAction)stateAction(action);
                    if(action==t3k_menu::kRestart)throw NativeRestart{};
                    if(action==menu::kQuit)quit_=true;
                    int w,h;SDL_GetRenderOutputSize(renderer_,&w,&h);menu::Draw(renderer_,w,h);
                    // --menu tests: --winshot=N counts the menu's presents, then quits
                    if(collection.menuTest){++menuPresents_;for(auto it=collection.screenshots.begin();it!=collection.screenshots.end();){
                        if(it->first==menuPresents_){auto* picture=SDL_RenderReadPixels(renderer_,nullptr);if(picture){SDL_SaveBMP(picture,it->second.c_str());SDL_DestroySurface(picture);}it=collection.screenshots.erase(it);if(collection.screenshots.empty())quit_=true;}else ++it;}}
                    SDL_RenderPresent(renderer_);SDL_Delay(8);
                }
                auto paused=SDL_GetTicksNS()-start;pauseStart_=0;epoch_+=paused;due_+=paused;blendStart_+=paused;displayDue_+=paused;SDL_ResumeAudioStreamDevice(device_);suppress_=true;mouse_.clear_motion();reseed_=true;
                if(quit_)return false;
            }
            if((check_&&!timed_)||SDL_GetTicksNS()>=due_)break;
            auto now=SDL_GetTicksNS();
            if(presentation_&&scene_&&settings::Num("native_interpolate",1)!=0&&now>=displayDue_){
                SDL_SetRenderTarget(renderer_,scene_);
                presentation_->advance_presentation(renderer_,presentationDst_,blendDuration_?float(double(now-blendStart_)/blendDuration_):1);
                SDL_SetRenderTarget(renderer_,nullptr);int w=0,h=0;SDL_GetRenderOutputSize(renderer_,&w,&h);
                SDL_SetRenderDrawColor(renderer_,0,0,0,255);SDL_RenderClear(renderer_);
                SDL_FRect dst{0,0,float(w),float(h)};
                if(presentationOutput_){float dh=(std::min)(float(h),float(w)*0.75f),dw=dh*4/3;dst={(w-dw)/2,(h-dh)/2,dw,dh};}
                crt::DrawPicture(renderer_,scene_,dst,360,240);draw_overlay();SDL_RenderPresent(renderer_);displayDue_=SDL_GetTicksNS()+8000000;
            }else SDL_Delay(1);
        }
        bool mouse=settings::Str("control_device","gamepad")=="mouse";
        if(mouse!=mouseMode_){mouseMode_=mouse;suppress_=true;mouse_.clear_motion();reseed_=true;}
        SDL_SetWindowRelativeMouseMode(window_,mouseMode_&&!check_);
        if(mouseMode_){uint8_t previous=uint8_t(record_.words[2]>>8)&0xFE;if(!sample_sdl_mouse(mouse_,!check_,record_))return false;
            // Relative host motion already has an unambiguous direction. Seed
            // the hardware decoder's direction latch to avoid its zero-history
            // wrap quirk; gameplay still uses its original sensitivity/clamp.
            mouseDelta_=native_relative_direction(previous,uint8_t(record_.words[2]>>8));
            if(check_){if(auto it=motion_.find(frames_);it!=motion_.end())mouse_.add_motion(it->second);record_=mouse_.sample({true});mouseDelta_=native_relative_direction(previous,uint8_t(record_.words[2]>>8));}
            const bool* keys=SDL_GetKeyboardState(nullptr);uint32_t buttons=record_.words[1]>>16;
            if(keys[SDL_SCANCODE_RETURN])buttons|=0x2000;
            if(keys[SDL_SCANCODE_UP])buttons|=0x200;if(keys[SDL_SCANCODE_DOWN])buttons|=0x800;
            if(keys[SDL_SCANCODE_LEFT])buttons|=0x400;if(keys[SDL_SCANCODE_RIGHT])buttons|=0x100;
            record_.words[1]=(record_.words[1]&65535)|(buttons<<16);
        }else {
            // Preserve native signed analog axes and keyboard / d-pad (Back opens the collection menu; the Nuon
            // button is a bindable one, LB by default). The collection remaps only its eleven bindable buttons.
            auto keyboard=native_pad_record(sample_sdl_pad(nullptr,SDL_GetKeyboardState(nullptr)));
            auto input=sample_sdl_pad(pad_);input.held&=pad_button(PadButton::up)|pad_button(PadButton::down)|pad_button(PadButton::left)|pad_button(PadButton::right);
            input.connected=true;record_=native_pad_record(input);
            record_.words[1]|=keyboard.words[1]|uint32_t(t3k_menu::NuonButtons(controls::Held(pad_)))<<16;
        }
        if(frames_==scriptAt_)record_.words[1]|=uint32_t(script_)<<16;
        if(auto it=scheduled_.find(frames_);it!=scheduled_.end())record_.words[1]|=uint32_t(it->second)<<16;
        if(auto it=axes_.find(frames_);it!=axes_.end())record_.words[1]=(record_.words[1]&~0xFF00U)|(uint32_t(uint8_t(it->second))<<8);
        if(check_&&naming_&&frames_%4==1)record_.words[1]|=0x40000000;
        if(suppress_){bool held=(record_.words[1]>>16)!=0;record_.words[1]=0;mouseDelta_=0;suppress_=held;}
        return true;
    }
    void draw_overlay(){
        auto now=SDL_GetTicksNS();++displayFrames_;
        if(!rateFrom_){rateFrom_=now;rateFrames_=frames_;}
        if(now-rateFrom_>=1000000000ULL){
            char label[100];double seconds=(now-rateFrom_)*1e-9;
            std::snprintf(label,sizeof(label),"%.1f pictures/s  %.1f presents/s",(frames_-rateFrames_)/seconds,displayFrames_/seconds);
            rateLabel_=label;rateFrom_=now;rateFrames_=frames_;displayFrames_=0;
        }
        int w=0,h=0;SDL_GetRenderOutputSize(renderer_,&w,&h);
        if(settings::Num("show_rates",0)!=0)pixfont::Text(renderer_,rateLabel_,10,10,2,{1,0.3f,1},1,-1,0);
        menu::DrawToast(renderer_,w,h,now);
    }
    uint64_t rateFrom_=0,rateFrames_=0,displayFrames_=0;std::string rateLabel_="Native Tempest 3000";
    void toggle_fullscreen(){bool value=(SDL_GetWindowFlags(window_)&SDL_WINDOW_FULLSCREEN)==0;if(SDL_SetWindowFullscreen(window_,value))settings::Save("fullscreen",value?"1":"0");}
    NativeFrameRenderer* presentation_=nullptr;SDL_FRect presentationDst_{};bool presentationOutput_=false;uint64_t blendStart_=0,blendDuration_=0,displayDue_=0;
    SDL_Texture* scene_=nullptr;int sceneWidth_=0,sceneHeight_=0;
    uint32_t bindHeld_=0;double rate_=24,speedFrom_=24,speedTo_=24;uint64_t speedTime_=0,pauseStart_=0;
    NativeMedia& media_;NativeAudioStreamQueue& stream_;NativeAudioBackend& audio_;SDL_Window* window_;SDL_Renderer* renderer_;SDL_AudioStream* device_=nullptr;SDL_Gamepad* pad_=nullptr;bool menuTestOpened_=false;uint64_t menuPresents_=0;
    NativeMouseSpinner mouse_;NativeControllerPort record_{};std::vector<NiseStreamRead> pending_;
    std::map<uint64_t,uint16_t> scheduled_;
    std::map<uint64_t,double> motion_;
    std::map<uint64_t,int8_t> axes_;
    uint64_t frames_=0,epoch_=0,due_=0,scriptAt_=0;uint16_t script_=0;int mouseDelta_=0;bool check_=false,timed_=false,active_=false,quit_=false,sampled_=false,mouseMode_=false,suppress_=false,reseed_=false,naming_=false,failed_=false;
};
}
