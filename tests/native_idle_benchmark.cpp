// Benchmark-only derivative of native_play.cpp: identical startup and pipeline; offline host and active-frame timing.
#include "native_boot_image.h"
#include "native_runtime_platform.h"
#include "native_sdk_drawing.h"
#include "native_boot_music.h"
#include "native_boot_renderer.h"
#include "native_idle_benchmark_host.h"
#include "game/TempestBootDriver.h"
#include "game/TempestBitmapTransfer.h"
#include "game/TempestSessionEntry.h"
#include "game/TempestMainDispatch.h"
#include "game/TempestGameSetupEntry.h"
#include "game/TempestGameSession.h"
#include "game/TempestPassword.h"
#include "game/TempestMainContinuation.h"
#include "game/TempestOptions.h"
#include "game/TempestVectorInspector.h"
#include "game/TempestAudioTest.h"
#include "game/TempestControllerCalibration.h"
#include "game/TempestRotaryCalibration.h"
#include <cstdio>
#include <cstring>
using namespace tempest_native;
struct RuntimeCalls:BootMenuEntryServices,MainDispatchServices,GameSetupEntryServices,SessionEntryServices,MainContinuationServices,OriginalOptionsServices,InspectorInputServices,AudioTestServices,ControllerCalibrationServices {
    NativeFrameMemory& memory;NativeMenuFlow& flow;NativeBenchmarkHost& host;std::array<uint32_t,20> last{};OpeningAnimationResult openingResult{};unsigned sessions=0;std::string scenario;
    RuntimeCalls(NativeFrameMemory& m,NativeMenuFlow& f,NativeBenchmarkHost& h):memory(m),flow(f),host(h){}
    bool opening(const OpeningAnimationInput& input)override{return run_opening_animation(memory,input,static_cast<AnimatedMenuServices&>(flow),openingResult);}
    bool transition(const DriverTransitionInput& input,const std::array<uint32_t,20>& c)override{return flow.transition(input,c);}
    bool attract_asset(uint32_t source,uint32_t scene)override{return flow.show_asset(source,scene,last);}
    bool selection(const SelectionMenuInput& input,uint32_t& result)override{last=input.caller.incoming;host.script_button(host.check()?0x4000:0);bool ok=run_selection_menu(memory,input,flow,result,0xFFFFFFFF);host.script_button(0);return ok;}
    bool animated_menu(const SelectionMenuInput& input,uint32_t& result)override{last=input.caller.incoming;host.script_button(host.check()?0x4000:0);if(host.check()&&scenario=="--check-two-player")host.schedule_button(63,0x800);bool ok=run_animated_menu(memory,input,flow,result,0xFFFFFFFF);host.script_button(0);return ok;}
    bool level_selection(const LevelSelectionInput& input,uint32_t& result)override{host.script_button(host.check()?0x4000:0,scenario=="--check-level"?95:35);if(host.check()&&scenario=="--check-level")host.schedule_button(33,0x200);bool ok=run_level_selection(memory,input,flow,result,0xFFFFFFFF);host.script_button(0);return ok;}
    bool exit_process(uint32_t,bool& returned)override{returned=false;return true;}
    bool select_music(uint32_t track)override{return flow.select_music(track);}
    bool show_asset(uint32_t source,uint32_t scene,const std::array<uint32_t,20>& c)override{return flow.show_asset(source,scene,c);}
    bool horizontal_axis(uint32_t latch,uint32_t mask,uint32_t& result)override{return flow.axis(latch,mask,true,result);}
    bool menu_action(uint32_t latch,uint32_t mask,uint32_t& result)override{return flow.menu_action(latch,mask,result);}
    bool volume_levels(uint32_t stream,uint32_t voices)override{return flow.set_volume_levels(stream,voices);}
    bool rotary_calibration(uint32_t player,const std::array<uint32_t,20>& c)override{return run_rotary_calibration(memory,player,{c,0x8001C228},flow);}
    bool tool(uint32_t selected,const std::array<uint32_t,20>& c)override{
        return selected==4?run_audio_test(memory,{c,0x80020E20},flow,*this):selected==5?run_vector_inspector(memory,{c,0x80020E20},flow,*this):run_controller_calibration(memory,{c,0x80020E20},flow,*this);
    }
    bool game_session(const std::array<uint32_t,4>& args,const std::array<uint32_t,20>& c,uint32_t& result)override{
        ++sessions;MeasuredGameServices measured(flow,host);
        return run_game_session(memory,{args,{c,0x80020BEE}},measured,result);
    }
};
int run(const char* image,const std::string& scenario){bool check=!scenario.empty();
    NativeDiscImage disc(image);std::vector<uint8_t> program;NativeBootImage boot;std::string error;
    if(!disc.read_file("NUON.RUN",program)||!load_native_boot_image(program,boot,error)){std::fprintf(stderr,"Disc load failed: %s\n",error.c_str());return 1;}
    NativeFrameMemory memory;for(unsigned i=0;i<3;++i)if(!memory.load(NativeFrameMemory::Bank(i),boot.banks[i]))return 1;
    uint32_t stack;if(!memory.read32(0x804FBA4C,stack)||!initialize_driver_bss(memory))return 1;
    NativeRuntimePlatform platform(memory,stack);NativeAudioBackend audio(memory,0);NativeNiseInitialization initializer(memory,audio,platform);NativeSdkDrawing drawing(memory);
    std::array<NativeControllerPort,9> ports{};ports[1]=native_pad_record({true});NativeControllers controllers(memory,ports);BootMainArguments args;
    if(!prepare_boot_main(memory,controllers,args))return 1;
    struct Drawing:BootDrawingServices{
        NativeFrameMemory& memory;NativeRuntimePlatform& platform;NativeNiseInitialization& nise;NativeSdkDrawing& drawing;uint32_t stack;
        Drawing(NativeFrameMemory& m,NativeRuntimePlatform& p,NativeNiseInitialization& n,NativeSdkDrawing& d,uint32_t s):memory(m),platform(p),nise(n),drawing(d),stack(s){}
        bool constructors()override{return run_driver_constructors(memory,platform);}
        bool initialize_audio()override{uint32_t result;return initialize_default_nise(memory,nise,result)&&result==0x11;}
        bool initialize_environment(uint32_t e)override{return initialize_graphics_environment(memory,platform,e);}
        bool initialize_context(uint32_t c,uint32_t e)override{return initialize_graphics_context(memory,c,e);}
        bool copy_bitmap(uint32_t c,uint32_t s,uint32_t d,uint32_t r,uint32_t o)override{NativeBitmapTransfer transfer(memory,drawing,stack-0x220);return prepare_bitmap_blit(memory,transfer,c,s,d,r,o);}
    } startup(memory,platform,initializer,drawing,stack);
    BootDrawingStage stage;if(!memory.begin()||!begin_boot_drawing(memory,startup,stack,stage)||!finish_boot_bitmap(memory)||!initializer.commit_processor()){std::fprintf(stderr,"Startup drawing/audio failed: %s\n",drawing.failure().c_str());return 1;}memory.commit();
    NativeAudioStreamQueue stream;NativeMedia media(disc);
    NativeStreamProgress progress(memory,media,stream);
    audio.set_stream(&stream);NativeDriverAudio driver(memory,audio,stream,progress);NativeBootMusic music(memory,media,audio,driver);
    if(!memory.begin()||!audio.begin_frame()||!media.begin_frame()||!begin_boot_music(memory,music,stack))return 1;memory.commit();audio.commit_frame();media.commit_frame();
    auto reads=stream.take_reads();if(!memory.begin()||!stream.begin_frame()||!media.begin_frame())return 1;for(auto& read:reads)if(!media.accept(read))return 1;unsigned completed;
    if(!media.deliver(memory,stream,4096,completed))return 1;memory.commit();stream.commit_frame();media.commit_frame();
    SDL_Window* window=SDL_CreateWindow("Tempest 3000 Native",1440,1080,check?SDL_WINDOW_HIDDEN:0);SDL_Renderer* renderer=window?t3knative::CreateRenderer(window):nullptr;if(!renderer)return 1;
    int status=1;
    {
        NativeFrameRenderer pipeline;NativeBootRenderer worker(memory,platform,pipeline,renderer);uint32_t selected;
        if(!begin_boot_worker(memory,worker,stack)||!select_boot_controllers(memory,selected))return 1;
        NativeBenchmarkHost host(media,stream,audio,window,renderer,check,scenario=="--live-check");if(scenario=="--check-mouse")settings::Set("control_device","mouse");else if(check)settings::Set("control_device","gamepad");if(!host.ready()){std::fprintf(stderr,"Audio device failed: %s\n",SDL_GetError());return 1;}
        NativePreparationBackend compute(memory);NativeMenuFlow flow(memory,compute,audio,driver,host,pipeline,renderer,{0,0,1440,1080});RuntimeCalls calls(memory,flow,host);calls.scenario=scenario;
        // Native C ABI boot values, not a captured frame or a diagnostic poison
        // pattern. Defined C-main locals and stack ownership are retained;
        // opaque caller words start at the native host's zero-initialized ABI.
        std::array<uint32_t,20> c{};c[1]=0x80000388;c[2]=2;c[3]=14;c[4]=0xFFFFFFFF;c[7]=0x800177C6;c[16]=0x800B1700;c[18]=stack-16;c[19]=stack-0x1C0;
        if(scenario=="--check-tools"){
            bool good=true;for(unsigned tool=4;tool<=6&&good;++tool){
                if(tool==6){for(unsigned press:{30U,40U,50U})host.schedule_button(press,0x4000);host.schedule_button(270,8);}
                else{host.schedule_button(90,8);}
                good=calls.tool(tool,c);std::printf("Native original tool %u: %s, frames=%llu\n",tool,good?"passed":"FAILED",host.frames());if(good)good=flow.menu_frame(c);
            }
            if(good){settings::Set("control_device","mouse");good=flow.menu_frame(c);
                for(unsigned press=30;press<=33;++press)host.schedule_button(press,0x4000);
                host.schedule_button(45,0x1000);host.schedule_button(60,8);host.schedule_button(61,8);host.schedule_button(90,8);
                if(good)good=calls.tool(6,c);std::printf("Native original rotary calibration: %s, frames=%llu\n",good?"passed":"FAILED",host.frames());}
            if(!good)std::fprintf(stderr,"Tool error: %s\n",flow.failure().c_str());status=good?0:1;
        }else{
        bool ok=true,opening=true,firstOpening=true;uint32_t state=0;
        while(ok&&!host.quit()){
            if(opening){BootMenuEntryResult entry;calls.last=c;ok=enter_boot_menu(memory,c,calls,entry,firstOpening);firstOpening=false;if(!ok)break;c=entry.handoff;MainDispatchResult route;ok=route_primary_menu_result(memory,c,calls,route);if(!ok)break;
                if(route.destination==MainDispatchDestination::firmwareExit){status=0;break;}c=route.handoff;state=route.state;opening=route.destination==MainDispatchDestination::openingLoop;if(opening)continue;
            }
            if(state==0){uint32_t choice;c[16]=0;c[7]=0x8001817C;ok=memory.write32(0x801133CC,0)&&calls.selection({0x8010C8C0,2,0x8010C170,{c,0x80020AE6}},choice);if(!ok)break;c[0]=choice;c[17]=0x80020AE6;MainDispatchResult route;ok=route_primary_menu_result(memory,c,calls,route);if(!ok)break;if(route.destination==MainDispatchDestination::firmwareExit){status=0;break;}c=route.handoff;state=route.state;opening=route.destination==MainDispatchDestination::openingLoop;continue;}
            if(state==1){c[17]=0x801133F4;GameSetupEntryResult setup;ok=enter_game_setup(memory,c,calls,setup);if(!ok)break;c=setup.handoff;
                if(int32_t(setup.selection)<0){state=0;c[17]=0x801133F4;c[7]=0;continue;}
                if(setup.selection==2){uint32_t password;c[7]=0x8001B0DC;ok=memory.write32(0x800DC270,c[16])&&run_level_password(memory,{c,0x80020B92},flow,password);if(!ok)break;c[17]=0x801133F4;c[16]=password;
                    if(int32_t(password)>int32_t(c[3])){c[3]=password;state=1;memory.write32(0x801133F4,1);}else state=0;c[0]=0xFFFFFFFF;continue;}
                SessionStartRequest request;ok=prepare_session_entry(memory,c,calls,request);if(!ok)break;uint32_t result;ok=calls.game_session(request.arguments,request.incoming,result);if(!ok)break;
                ok=continue_after_session(memory,request.incoming,result,calls,c);if(!ok)break;memory.read32(0x801133F4,state);std::printf("Completed native session %u: result=%u frames=%llu state=%u\n",calls.sessions,result,host.frames(),state);
                if(check){status=0;break;}continue;
            }
            if(state==2){ok=run_original_options(memory,c,calls,c)&&memory.read32(0x801133F4,state);continue;}
            if(int32_t(state)<0){c[7]=0x80016BE6;ok=flow.transition({0x3E00,0x10808000,0x10200,0,60},c);c[17]=0x80020E70;opening=true;continue;}
            std::fprintf(stderr,"Unknown C-main state %u\n",state);ok=false;
        }
        if(host.quit())status=host.failed()?1:0;if(host.failed())std::fprintf(stderr,"Live device error: %s\n",SDL_GetError());if(!ok&&!host.quit())std::fprintf(stderr,"Native flow stopped at frame %llu: %s / %s\n",host.frames(),flow.failure().c_str(),audio.failure().c_str());
        }
    }
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);return status;
}
int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);if(argc<2||argc>3){std::fprintf(stderr,"Usage: tempest_native.exe disc.iso [--check]\n");return 2;}
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMEPAD)){std::fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
    std::string scenario=argc==3?argv[2]:"";if(!scenario.empty()&&scenario!="--check"&&scenario!="--check-two-player"&&scenario!="--check-level"&&scenario!="--check-mouse"&&scenario!="--check-tools"&&scenario!="--live-check"){std::fprintf(stderr,"Unknown argument\n");SDL_Quit();return 2;}
    int result=run(argv[1],"--benchmark");SDL_Quit();return result;
}

