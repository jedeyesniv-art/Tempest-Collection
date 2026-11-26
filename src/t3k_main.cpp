#include <SDL3/SDL_main.h>
#include "t3k_runtime/native_boot_image.h"
#include "t3k_runtime/native_runtime_platform.h"
#include "t3k_runtime/native_sdk_drawing.h"
#include "t3k_runtime/native_boot_music.h"
#include "t3k_runtime/native_boot_renderer.h"
#include "t3k_runtime/native_live_host.h"
#include "t3k_runtime/game/TempestBootDriver.h"
#include "t3k_runtime/game/TempestBitmapTransfer.h"
#include "t3k_runtime/game/TempestSessionEntry.h"
#include "t3k_runtime/game/TempestMainDispatch.h"
#include "t3k_runtime/game/TempestGameSetupEntry.h"
#include "t3k_runtime/game/TempestGameSession.h"
#include "t3k_runtime/game/TempestPassword.h"
#include "t3k_runtime/game/TempestMainContinuation.h"
#include "t3k_runtime/game/TempestOptions.h"
#include "t3k_runtime/game/TempestVectorInspector.h"
#include "t3k_runtime/game/TempestAudioTest.h"
#include "t3k_runtime/game/TempestControllerCalibration.h"
#include "t3k_runtime/game/TempestRotaryCalibration.h"
#include "t3k_runtime/native_collection_config.h"
#include "pixfont.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
using namespace tempest_native;
namespace fs=std::filesystem;
std::string FindDisc(const std::string& given) {
  if (!given.empty()) return given;
  fs::path base(SDL_GetBasePath());
  std::vector<fs::path> dirs = {base, base / "game", fs::current_path(), fs::current_path() / "Nuance_067",
                                base.parent_path().parent_path() / "Nuance_067"};
  // the compressed image (tools/t3k_compress.py) wins over an .iso / .mdf in the same folder
  for (auto& d : dirs) {
    std::error_code ec;
    std::string found;
    for (auto& e : fs::directory_iterator(d, ec)) {
      auto name = e.path().filename().string();
      auto ext = e.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      std::transform(name.begin(), name.end(), name.begin(), ::tolower);
      if (name.find("tempest") == std::string::npos && name.find("t3k") == std::string::npos) continue;
      if (ext == ".nuz") return e.path().string();
      // a DVD .mdf (Alcohol 120%) / .img is the same 2048-byte sectors as an .iso
      if ((ext == ".iso" || ext == ".mdf" || ext == ".img") && found.empty()) found = e.path().string();
    }
    if (!found.empty()) return found;
  }
  return {};
}
struct RuntimeCalls:BootMenuEntryServices,MainDispatchServices,GameSetupEntryServices,SessionEntryServices,MainContinuationServices,OriginalOptionsServices,InspectorInputServices,AudioTestServices,ControllerCalibrationServices {
    NativeFrameMemory& memory;NativeMenuFlow& flow;NativeLiveHost& host;std::array<uint32_t,20> last{};OpeningAnimationResult openingResult{};unsigned sessions=0;std::string scenario;
    RuntimeCalls(NativeFrameMemory& m,NativeMenuFlow& f,NativeLiveHost& h):memory(m),flow(f),host(h){}
    bool opening(const OpeningAnimationInput& input)override{return run_opening_animation(memory,input,static_cast<AnimatedMenuServices&>(flow),openingResult);}
    bool transition(const DriverTransitionInput& input,const std::array<uint32_t,20>& c)override{return flow.transition(input,c);}
    bool attract_asset(uint32_t source,uint32_t scene)override{return flow.show_asset(source,scene,last);}
    bool selection(const SelectionMenuInput& input,uint32_t& result)override{last=input.caller.incoming;unsigned delay=65;if(profiling::enabled())if(const auto* hold=std::getenv("T3K_PROFILE_MENU_HOLD"))delay=unsigned((std::clamp)(std::strtoul(hold,nullptr,10),65UL,600UL));host.script_button(host.check()?0x4000:0,delay);bool ok=run_selection_menu(memory,input,flow,result,0xFFFFFFFF);host.script_button(0);return ok;}
    bool animated_menu(const SelectionMenuInput& input,uint32_t& result)override{last=input.caller.incoming;unsigned delay=65;if(host.check())if(const auto* hold=std::getenv("T3K_CHECK_PLAYER_MENU_HOLD"))delay=unsigned((std::clamp)(std::strtoul(hold,nullptr,10),65UL,500UL));host.script_button(host.check()?0x4000:0,delay);if(host.check()&&scenario=="--check-two-player")host.schedule_button(63,0x800);bool ok=run_animated_menu(memory,input,flow,result,0xFFFFFFFF);host.script_button(0);return ok;}
    bool level_selection(const LevelSelectionInput& input,uint32_t& result)override{
        bool combat=scenario=="--check-combat";unsigned level=8;
        if(combat)if(auto* value=std::getenv("TEMPEST_CHECK_LEVEL"))level=unsigned(std::strtoul(value,nullptr,10));
        if(combat&&level>14)return false;
        unsigned at=33;
        if(combat){if(level>=8){host.schedule_button(at,0x400);at+=45;}for(unsigned i=0;i<level%8;++i){host.schedule_button(at,0x200);at+=45;}}
        host.script_button(host.check()?0x4000:0,combat?at+5:scenario=="--check-level"?95:35);
        if(host.check()&&scenario=="--check-level")host.schedule_button(33,0x200);
        bool ok=run_level_selection(memory,input,flow,result,0xFFFFFFFF);host.script_button(0);
        if(host.check())std::printf("Native selected level %u\n",result+1);
        return ok&&(!combat||result==level);
    }
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
    void prepare_session_input(){
        ++sessions;
        if(host.check()&&collection.logicHz==120){unsigned pattern=0;if(auto* value=std::getenv("TEMPEST_CHECK_PATTERN"))pattern=unsigned(std::strtoul(value,nullptr,10));host.combat_pattern(pattern);return;}
        if(host.check()){host.schedule_button(180,0x2000);host.schedule_button(190,0x2000);for(unsigned i=210;i<218;++i)host.schedule_button(i,0x100);if(scenario=="--check-mouse")for(unsigned i=220;i<260;++i)host.schedule_motion(i,i<240?8:-8);
            if(scenario=="--check-combat"&&collection.logicHz==24)for(unsigned i=220;i<6000;++i){
                unsigned pattern=0;if(auto* value=std::getenv("TEMPEST_CHECK_PATTERN"))pattern=unsigned(std::strtoul(value,nullptr,10));
                uint16_t buttons=uint16_t((pattern==1?0:0x4000)|(pattern==2?0:(i%180<90?0x100:0x400))|(i%120<45?0x10:0)|(pattern==3&&i%240==0?8:0));
                if(pattern==4){buttons=uint16_t(0x4000|(i%120<60?0x10:0));host.schedule_axis(i,int8_t(i%360<180?80:-80));}
                host.schedule_button(i,buttons);
            }}
    }
    bool game_session(const std::array<uint32_t,4>& args,const std::array<uint32_t,20>& c,uint32_t& result)override{
        prepare_session_input();
        return run_game_session(memory,{args,{c,c[17]==0x80020BC8?0x80020BEEU:0x80020B34U}},flow,result);
    }
};
#include "t3k_runtime/native_continuation.h"
int run(const char* image,const std::string& scenario){bool check=!scenario.empty();
    activeTiming=NativeTimingProfile::read();
    NativeDiscImage disc(image);std::vector<uint8_t> program;NativeBootImage boot;std::string error;
    if(!disc.read_file("NUON.RUN",program)||!load_native_boot_image(program,boot,error)){std::fprintf(stderr,"Disc load failed: %s\n",error.c_str());return 1;}
    NativeFrameMemory memory;for(unsigned i=0;i<3;++i)if(!memory.load(NativeFrameMemory::Bank(i),boot.banks[i]))return 1;
    uint32_t stack;if(!memory.read32(0x804FBA4C,stack)||!initialize_driver_bss(memory))return 1;
    struct ScoreGuard{NativeFrameMemory& memory;~ScoreGuard(){auto bank=memory.bank(NativeFrameMemory::system);t3kscores::Flush(bank.data(),bank.size());}} scoreGuard{memory};
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
    SDL_Window* window=SDL_CreateWindow("Tempest 3000",collection.width,collection.height,(check?SDL_WINDOW_HIDDEN:0)|SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> windowOwner(window,SDL_DestroyWindow);
    SDL_Renderer* renderer=window?t3knative::CreateRenderer(window):nullptr;
    auto destroyRenderer=[](SDL_Renderer* value){pixfont::Shutdown();t3knative::ShutdownPipeline();SDL_DestroyRenderer(value);};
    std::unique_ptr<SDL_Renderer,decltype(destroyRenderer)> rendererOwner(renderer,destroyRenderer);
    if(!renderer)return 1;
    if(!check&&settings::Num("fullscreen",1)!=0&&!collection.windowSize)SDL_SetWindowFullscreen(window,true);
    SDL_SetRenderVSync(renderer,collection.logicHz==24&&!check&&!collection.novsync&&settings::Num("vsync",1)!=0?1:0);
    int status=1;
    {
        NativeFrameRenderer pipeline;NativeBootRenderer worker(memory,platform,pipeline,renderer);uint32_t selected;
        if(!begin_boot_worker(memory,worker,stack)||!select_boot_controllers(memory,selected))return 1;
        NativeLiveHost host(media,stream,audio,window,renderer,check,scenario=="--live-check");if(scenario=="--check-mouse")settings::Set("controller","mouse");else if(check)settings::Set("controller","auto");if(!host.ready()){std::fprintf(stderr,"Audio device failed: %s\n",SDL_GetError());return 1;}
        NativePreparationBackend compute(memory);NativeMenuFlow flow(memory,compute,audio,driver,host,pipeline,renderer,{0,0,960,640});RuntimeCalls calls(memory,flow,host);calls.scenario=scenario;
        // Native C ABI boot values, not a captured frame or a diagnostic poison
        // pattern. Defined C-main locals and stack ownership are retained;
        // opaque caller words start at the native host's zero-initialized ABI.
        std::array<uint32_t,20> c{};c[1]=0x80000388;c[2]=2;c[3]=14;c[4]=0xFFFFFFFF;c[7]=0x800177C6;c[16]=0x800B1700;c[18]=stack-16;c[19]=stack-0x1C0;
        if(scenario=="--check-password"){
            host.name_entry(true);uint32_t passwordResult=0;
            bool good=run_level_password(memory,{c,0},flow,passwordResult);host.name_entry(false);
            std::printf("Native password validation: %s, result=%u, pictures=%llu\n",good?"passed":"FAILED",passwordResult,host.frames());
            if(!good)std::fprintf(stderr,"Password error: %s\n",flow.failure().c_str());status=good?0:1;
        }else if(scenario=="--check-tools"){
            bool good=true;for(unsigned tool=4;tool<=6&&good;++tool){
                if(tool==6){for(unsigned press:{30U,40U,50U})host.schedule_button(press,0x4000);host.schedule_button(270,8);}
                else{host.schedule_button(90,8);}
                good=calls.tool(tool,c);std::printf("Native original tool %u: %s, frames=%llu\n",tool,good?"passed":"FAILED",host.frames());if(good)good=flow.menu_frame(c);
            }
            if(good){settings::Set("controller","mouse");good=flow.menu_frame(c);
                for(unsigned press=30;press<=33;++press)host.schedule_button(press,0x4000);
                host.schedule_button(45,0x1000);host.schedule_button(60,8);host.schedule_button(61,8);host.schedule_button(90,8);
                if(good)good=calls.tool(6,c);std::printf("Native original rotary calibration: %s, frames=%llu\n",good?"passed":"FAILED",host.frames());}
            if(!good)std::fprintf(stderr,"Tool error: %s\n",flow.failure().c_str());status=good?0:1;
        }else{
        auto state=initial_collection_driver(stack);bool ok=true,replayingPhase=false;
        NativeContinuation phaseCheckpoint;
        std::optional<NativeContinuation> loaded;
        const uint64_t identity=state_checksum(program);
        auto statePath=[&]{int slot=(std::clamp)(int(settings::Num("state_slot",1)),1,5);return collection.userdata/"states"/("t3k_slot"+std::to_string(slot)+".native-state");};
        menu::SetSlotInfo([&]{std::error_code ec;return fs::exists(statePath(),ec)?"Native state":"Empty native slot";});
        host.stateAction=[&](int action){
            if(collection.logicHz!=24){menu::Toast("Save/load is unavailable with scene timing enabled",SDL_GetTicksNS());return;}
            std::string why;
            if(action==menu::kSaveState){
                auto saved=phaseCheckpoint;
                if(!memory.checkpoint(saved.memory)||!host.checkpoint(saved.currentHost)||!pipeline.capture_presentation(renderer,saved.presentation,why)){menu::Toast(why.empty()?"Cannot checkpoint this native boundary":why,SDL_GetTicksNS());return;}
                saved.events=host.journal.events;saved.expectedMemory=native_memory_checksum(memory);saved.expectedServices=native_services_checksum(audio,compute,media,stream,flow);
                StateWriter writer;StateEncode encode{writer};encode(saved);
                if(!write_native_state(statePath(),writer.bytes,identity,why)){menu::Toast(why,SDL_GetTicksNS());return;}
                std::fprintf(stderr,"Native state saved at frame %llu phase %u session %u\n",host.frames(),unsigned(state.phase),unsigned(state.session.phase));
                menu::Toast("Native state saved",SDL_GetTicksNS());
            }else{
                std::vector<uint8_t> payload;if(!read_native_state(statePath(),payload,identity,why)){menu::Toast(why,SDL_GetTicksNS());return;}
                NativeContinuation saved;StateReader reader{payload};StateDecode decode{reader};decode(saved);
                bool valid=reader.finished()&&uint32_t(saved.driver.phase)<=uint32_t(CollectionPhase::finished)&&uint32_t(saved.driver.session.phase)<=uint32_t(GameSessionPhase::finished);
                for(const auto& bank:saved.memory)valid&=bank.size()==0x800000;
                for(const auto& event:saved.events)valid&=uint32_t(event.kind)<=uint32_t(NativeServiceKind::scores);
                if(collection.timingResearch&&(saved.presentation.interpolate||saved.presentation.pending)){menu::Toast("Timing research requires a state saved without interpolation",SDL_GetTicksNS());return;}
                if(!valid){menu::Toast("Invalid native continuation",SDL_GetTicksNS());return;}
                throw NativeLoad{std::move(saved)};
            }
        };
        host.resumeContinuation=[&]{
            if(!loaded)throw std::runtime_error("Missing native continuation");
            std::string why;
            if(native_memory_checksum(memory)!=loaded->expectedMemory)throw std::runtime_error("Native continuation RAM did not reproduce exactly");
            if(native_services_checksum(audio,compute,media,stream,flow)!=loaded->expectedServices)throw std::runtime_error("Native continuation services did not reproduce exactly");
            if(!pipeline.restore_presentation(renderer,loaded->presentation,why)||!host.restore(loaded->currentHost))throw std::runtime_error(why.empty()?"Cannot restore native host":why);
            t3knative::FeedbackState verify;
            if(!pipeline.capture_feedback(renderer,verify,why)||verify.width!=loaded->presentation.feedback.width||verify.height!=loaded->presentation.feedback.height||verify.format!=loaded->presentation.feedback.format||verify.pixels!=loaded->presentation.feedback.pixels)throw std::runtime_error("Native feedback image did not restore exactly");
            host.resume_live();menu::Close();t3kscores::Restart();loaded.reset();
            std::fprintf(stderr,"Native continuation resumed exactly at frame %llu\n",host.frames());
        };
        NativeSimulationTiming simulationTiming(memory);bool fineTiming=false;
        while(ok&&!host.quit()&&state.phase!=CollectionPhase::finished){
            try{
                // Validation route: reach a live original session before invoking
                // the complete bonus-game caller with its genuine initialized data.
                if(scenario=="--check-warp"&&flow.simulation_frames()>=500&&state.phase==CollectionPhase::sessionStep&&state.session.phase==GameSessionPhase::active){
                    if(!flow.configure_timing(nullptr))throw std::runtime_error("Cannot release gameplay timing for warp validation");
                    for(unsigned delay=20;delay<800;delay+=8)host.schedule_button(delay,0x4000);
                    uint32_t result=0;ok=flow.warp_skip(state.session.context,result);
                    std::printf("Native warp-game validation: %s, result=%u, pictures=%llu\n",ok?"passed":"FAILED",result,host.frames());
                    if(ok)state.phase=CollectionPhase::finished;break;
                }
                bool requested=collection.logicHz==120&&state.phase==CollectionPhase::sessionStep&&(state.session.phase==GameSessionPhase::playing||state.session.phase==GameSessionPhase::active);
                if(requested!=fineTiming){
                    auto cadence=collection.sceneHz?activeTiming.cadence(SceneTimingKind::none,true):SceneCadence{SceneTimingKind::none,24,5,1};
                    if(requested&&!simulationTiming.reset(cadence.divisions,cadence.numerator))throw std::runtime_error("Cannot reset native simulation timing");
                    if(!flow.configure_timing(requested?&simulationTiming:nullptr))throw std::runtime_error("Cannot change native simulation timing");
                    fineTiming=requested;
                }
                if(profiling::enabled()){profiling::phase=unsigned(state.phase);profiling::session=unsigned(state.session.phase);}
                auto simulationBefore=flow.simulation_frames();
                if(!replayingPhase){
                    profiling::Scope profile(profiling::phase_checkpoint);
                    phaseCheckpoint={};phaseCheckpoint.driver=state;phaseCheckpoint.callerLast=calls.last;phaseCheckpoint.sessions=calls.sessions;
                    auto local=compute.local_data();std::copy(local.begin(),local.end(),phaseCheckpoint.preparation.begin());
                    if(!audio.checkpoint(phaseCheckpoint.audio)||!media.checkpoint(phaseCheckpoint.media)||!stream.checkpoint(phaseCheckpoint.stream)||!flow.checkpoint(phaseCheckpoint.flow)||!host.checkpoint(phaseCheckpoint.host)||!memory.begin_checkpoint())throw std::runtime_error("Native phase cannot checkpoint its services");
                    host.journal.begin_phase();
                }
                replayingPhase=false;auto phase=state.phase;
                ok=step_collection_driver(memory,flow,calls,state);
                if(fineTiming&&flow.simulation_frames()!=simulationBefore&&!simulationTiming.advance())throw std::runtime_error("Cannot advance native simulation phase");
                if(check&&ok&&phase==CollectionPhase::continuation){status=0;break;}
            }catch(NativeLoad& load){
                loaded=std::move(load.saved);auto& saved=*loaded;
                if(!memory.restore(saved.memory)||!compute.restore_worker_data(saved.preparation)||!audio.restore(saved.audio)||!media.restore(saved.media)||!stream.restore(saved.stream)||!flow.restore(saved.flow)||!host.restore(saved.host)||!memory.begin_checkpoint())throw std::runtime_error("Native continuation service restore failed");
                state=saved.driver;calls.last=saved.callerLast;calls.sessions=saved.sessions;
                phaseCheckpoint=saved;phaseCheckpoint.memory={};phaseCheckpoint.events.clear();phaseCheckpoint.presentation={};
                pipeline.restore_sequence(saved.flow.serial);host.journal.replay(saved.events);replayingPhase=true;
            }
        }
        menu::SetSlotInfo({});
        if(ok&&state.phase==CollectionPhase::finished)status=0;
        if(host.quit())status=host.failed()?1:0;if(host.failed())std::fprintf(stderr,"Live device error: %s\n",SDL_GetError());if(!ok&&!host.quit())std::fprintf(stderr,"Native flow stopped at frame %llu: %s / %s\n",host.frames(),flow.failure().c_str(),audio.failure().c_str());
        }
    }
    return status;
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMEPAD)){std::fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
    collection.userdata=fs::path(SDL_GetBasePath())/"userdata";
    std::string disc,scenario;std::vector<std::pair<std::string,std::string>> overrides;
    for(int i=1;i<argc;++i){std::string arg=argv[i];
        if(arg=="--from-launcher")collection.fromLauncher=true;
        else if(arg=="--novsync")collection.novsync=true;
        else if(arg.rfind("--logic-hz=",0)==0){auto value=arg.substr(11);if(value!="24"&&value!="120")return 2;collection.logicHz=unsigned(std::strtoul(value.c_str(),nullptr,10));}
        else if(arg.rfind("--scene-hz=",0)==0){collection.sceneHz=unsigned(std::strtoul(arg.c_str()+11,nullptr,10));if(collection.sceneHz!=0&&collection.sceneHz!=120)return 2;}
        else if(arg.rfind("--fine-check-limit=",0)==0)collection.fineCheckLimit=std::strtoull(arg.c_str()+19,nullptr,10);
        else if(arg.rfind("--present-hz=",0)==0){collection.presentHz=unsigned(std::strtoul(arg.c_str()+13,nullptr,10));if(collection.presentHz<15||collection.presentHz>240)return 2;}
        else if(arg.rfind("--timing-trace=",0)==0)collection.timingTrace=arg.substr(15);
        else if(arg=="--menu"||arg=="--menu=all")collection.menuTest=arg=="--menu"?1:2;
        else if(arg.rfind("--userdata=",0)==0)collection.userdata=arg.substr(11);
        else if(arg.rfind("--winsize=",0)==0){auto v=arg.substr(10);auto x=v.find('x');if(x==std::string::npos)return 2;collection.width=std::atoi(v.c_str());collection.height=std::atoi(v.c_str()+x+1);if(collection.width<64||collection.height<64)return 2;collection.windowSize=true;}
        else if(arg.rfind("--restarttest=",0)==0)collection.restartAt=std::strtoull(arg.c_str()+14,nullptr,10);
        else if(arg.rfind("--winshot=",0)==0){auto v=arg.substr(10);auto colon=v.find(':');if(colon==std::string::npos)return 2;collection.screenshots.emplace_back(std::strtoull(v.c_str(),nullptr,10),v.substr(colon+1));}
        else if(arg.rfind("--statetest=",0)==0){auto v=arg.substr(12);auto colon=v.find(':');if(colon==std::string::npos)return 2;auto action=v.substr(colon+1);if(action!="save"&&action!="load")return 2;collection.stateTests.emplace_back(std::strtoull(v.c_str(),nullptr,10),action=="save"?menu::kSaveState:menu::kLoadState);}
        else if(arg.rfind("--set=",0)==0){auto v=arg.substr(6);auto eq=v.find('=');if(eq==std::string::npos)return 2;overrides.emplace_back(v.substr(0,eq),v.substr(eq+1));}
        else if(arg=="--check"||arg=="--check-two-player"||arg=="--check-level"||arg=="--check-mouse"||arg=="--check-tools"||arg=="--check-combat"||arg=="--check-warp"||arg=="--check-password"||arg=="--live-check")scenario=arg;
        else if(arg.rfind("--",0)==0){std::fprintf(stderr,"Unsupported option: %s\n",arg.c_str());SDL_Quit();return 2;}
        else if(disc.empty())disc=arg;else return 2;
    }
    if(collection.logicHz==120&&!collection.timingResearch){std::fprintf(stderr,"120 Hz updates require a build with native scene timing enabled\n");SDL_Quit();return 2;}
    bool explicitScene=false;for(int i=1;i<argc;++i)if(std::string(argv[i]).rfind("--scene-hz=",0)==0)explicitScene=true;
    if(collection.logicHz==120&&!explicitScene)collection.sceneHz=120;
    if(collection.sceneHz&&collection.logicHz!=120){std::fprintf(stderr,"Scene timing requires 120 Hz internal updates\n");SDL_Quit();return 2;}
    if(collection.fineCheckLimit&&(scenario.empty()||collection.logicHz!=120)){std::fprintf(stderr,"A fine-step check limit requires a 120 Hz validation scenario\n");SDL_Quit();return 2;}
    if(collection.presentHz&&collection.logicHz!=120){std::fprintf(stderr,"An independent presentation cap requires 120 Hz internal updates\n");SDL_Quit();return 2;}
    load_collection_settings();for(const auto& [key,value]:overrides)settings::Set(key,value);
    t3kscores::Init((collection.userdata/"t3k_highscores.txt").string(),settings::Num("keep_high_scores",1)!=0);
    disc=FindDisc(disc);
    if(disc.empty()){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Tempest 3000","Put your Tempest 3000 disc image (.nuz, .iso or .mdf) in the game folder next to the program.",nullptr);SDL_Quit();return 1;}
    if(collection.timingResearch){
        if(collection.logicHz==120&&collection.sceneHz){auto profile=NativeTimingProfile::read();
            std::fprintf(stderr,"Native scene timing: high framerate %s; gameplay reference %u ticks/s; interpolation disabled\n",profile.highFramerate?(profile.presentationHz==60?"60 Hz display / 120 Hz updates":"120 Hz"):"Original (full reference ticks)",profile.rates[0]);}
        else std::fprintf(stderr,collection.logicHz==120?"Native timing research: interpolation disabled; gameplay-only 120 Hz diagnostic\n":"Native timing research: interpolation disabled; original gameplay cadence retained\n");
    }
    int result=1;bool restart;
    do{restart=false;try{result=run(disc.c_str(),scenario);}catch(const NativeRestart&){t3kscores::Restart();restart=true;}catch(const std::exception& e){std::fprintf(stderr,"Native runtime: %s\n",e.what());result=1;}}while(restart);
    SDL_Quit();return result;
}
