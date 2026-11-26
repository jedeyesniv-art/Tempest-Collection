#include "t3k_runtime/native_frame_memory.h"
#include "t3k_runtime/game/TempestPreparation.h"
#include "t3k_runtime/game/TempestCallbacks.h"
#include "t3k_runtime/game/TempestContact.h"
#include "t3k_runtime/game/TempestDriver.h"
#include "t3k_runtime/game/TempestFrame.h"
#include "t3k_runtime/native_simulation_timing.h"
#include <cstdio>
#include "t3k_runtime/native_scene_clock.h"
#include "t3k_runtime/game/TempestOpeningAnimation.h"
#include "t3k_runtime/game/TempestMenuTransition.h"
#include "t3k_runtime/game/TempestAnimatedMenu.h"
#include <cstdlib>
#include <numeric>
#include "t3k_runtime/game/TempestSessionDeath.h"
#include "t3k_runtime/game/TempestSessionWarpBonus.h"
using namespace tempest_native;
static void require(bool value,const char* why){if(!value){std::fprintf(stderr,"cycle intent: %s\n",why);std::exit(1);}}
static uint32_t read(NativeFrameMemory& m,uint32_t at){uint32_t value=0;require(m.read32(at,value),"valid fixture address");return value;}
struct SceneFixture:GameSessionServices{
    SimulationStep clock;unsigned frames=0,transitions=0,sounds=0;explicit SceneFixture(unsigned den,unsigned num=1):clock{den,0,num}{}
    unsigned begin_scene_timing(SceneTimingKind)override{return clock.divisions;}
    SimulationStep scene_timing_step()const override{return clock;}SimulationStep simulation_step()const override{return clock;}
    bool initialize_scene(const SceneInitializationInput&)override{return true;}
    bool play_sound(uint32_t,uint32_t)override{++sounds;return true;}
    bool menu_frame(const std::array<uint32_t,20>&)override{++frames;clock.index=(clock.index+clock.numerator)%clock.divisions;return true;}
    bool gameplay_frame(const std::array<uint32_t,20>& c)override{return menu_frame(c);}
    bool transition(const DriverTransitionInput&,const std::array<uint32_t,20>&)override{++transitions;return true;}
    bool generate_level(uint32_t)override{return false;}bool select_music(uint32_t)override{return false;}
    bool show_asset(uint32_t,uint32_t,const std::array<uint32_t,20>&)override{return false;}
    bool play_request(const SoundRequest&,uint32_t&)override{return false;}
    bool start_descriptor(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t&)override{return false;}
    bool stop_voice(uint32_t)override{return true;}bool consume_sounds()override{return true;}
    bool game_over_banner(uint32_t,uint32_t,const std::array<uint32_t,20>&)override{return false;}
    bool warp_bonus(const std::array<uint32_t,20>&,uint32_t&)override{return false;}
    bool warp_skip(const std::array<uint32_t,20>&,uint32_t&)override{return false;}
    bool vertical_axis(uint32_t,uint32_t,uint32_t&)override{return false;}
    bool high_score_entry(uint32_t,uint32_t,uint32_t,uint32_t,const std::array<uint32_t,20>&)override{return false;}
};
int main(){
    {
        for(unsigned hz:{16u,20u,24u,30u,36u,48u}){
            unsigned divisor=std::gcd(120u,hz),den=120/divisor,num=hz/divisor;
            SceneCadence cadence{SceneTimingKind::bonus,hz,den,num};require(cadence.sample_hz()==120,"all agreed scene references retain exactly 120 Hz sampling");
            NativeFrameMemory memory;NativeSimulationTiming timing(memory);require(timing.reset(den,num),"rational timing reset");
            unsigned boundaries=0;int64_t forward=0,reverse=0;
            ObjectUpdateInput input{};input.parameters[0]=0x7FFFFFFF;input.parameters[1]=0x80000000;input.values.push_back({0,0x1000,0x10000,0x80000010});
            for(unsigned frame=0;frame<120;++frame){auto step=timing.simulation_step();require(step.valid(),"valid rational phase");boundaries+=step.legacy_boundary();forward+=step.delta(1001);reverse+=step.delta(-1001);
                require(timing.begin_values(),"rational value transaction");ObjectUpdateOutput output;require(timing.object_values(0x80020000,0x80020040,input,output),"fresh rational object integration");timing.commit_values();input.values=output.values;input.objectStatus=output.objectStatus;
                require(timing.advance(),"advance rational phase");
            }
            require(boundaries==hz&&forward==int64_t(hz)*1001&&reverse==-int64_t(hz)*1001&&timing.simulation_step().index==0,"exact positive/negative progression and authored event counts over one second");
            require(input.values[0].position==hz*0x1000,"actual object displacement follows reference speed at fixed 120 Hz");
        }
    }
    {
        NativeFrameMemory coarseMemory,fineMemory;SceneFixture coarse(1),fine(10,3);std::array<uint32_t,20> c{};c[19]=0x80080000;uint32_t a=0,b=0;
        require(run_session_warp_bonus(coarseMemory,c,coarse,a)&&run_session_warp_bonus(fineMemory,c,fine,b),"bonus presentation at authored and rational fine cadence");
        require(coarse.frames==60&&fine.frames==200&&a==b&&coarse.sounds==fine.sounds&&coarse.transitions==fine.transitions,"36 Hz bonus keeps 60 authored steps in 200 actual 120 Hz samples and conserves result/one-shot events");
    }
    {
        for(uint32_t word:{0x05000900u,0x7F000900u,0x80000700u}){
            NativeFrameMemory coarseMemory,fineMemory;PreparationShapeWriter coarseWriter{coarseMemory,0x201006F0,0x401A0000},fineWriter{fineMemory,0x201006F0,0x401A0000};
            PreparationDepth coarse{coarseMemory,coarseWriter},fine{fineMemory,fineWriter};
            require(coarseMemory.write32(0x201009C0,word)&&fineMemory.write32(0x201009C0,word),"packed bonus obstacle fixture");
            coarse.bonus_slot(0,0,0,0,0,0);
            for(unsigned phase=0;phase<5;++phase){fine.simulation={5,phase};fine.bonus_slot(0,0,0,0,0,0);if(phase<4)require(read(fineMemory,0x201009C0)==word,"fine drawing does not multiply packed obstacle physics");}
            require(coarse.good&&fine.good&&read(coarseMemory,0x201009C0)==read(fineMemory,0x201009C0),"bonus obstacle motion and both edge reversals conserve native authored state");
        }
    }
    for(unsigned reason:{1u,2u}){
        NativeFrameMemory coarseMemory,fineMemory;SceneFixture coarse(1),fine(5);GameSessionState a{},b{};
        a.phase=GameSessionPhase::lifeResolved;a.frameBase=0x80080000;a.context[19]=0x8007FC00;a.context[11]=3;b=a;
        for(auto* m:{&coarseMemory,&fineMemory})require(m->write32(0x800BA2F8,reason)&&m->write32(0x800BA2E4,0x80040000)&&m->write32(0x800BA2EC,0x1000000)&&m->write32(a.frameBase-0x380,0x10000),"death fixture state");
        require(resolve_session_death(coarseMemory,coarse,a)&&resolve_session_death(fineMemory,fine,b),"both complete death variants support fine cadence");
        require(coarse.frames==(reason==1?64:70)&&fine.frames==coarse.frames*5&&a.phase==b.phase&&a.context[11]==b.context[11]&&coarse.sounds==fine.sounds,"24 Hz death duration, life decrement, retry route and one-shot sound conserved at 120 Hz");
    }
    {
        NativeSceneClock clock;for(unsigned i=0;i<30;++i)require(clock.advance(30),"30 Hz clock");for(unsigned i=0;i<120;++i)require(clock.advance(120),"120 Hz clock");for(unsigned i=0;i<60;++i)require(clock.advance(60),"60 Hz clock");
        require(clock.events(500)==1500&&clock.events(60)==180,"mixed menu/game/transition clock conserves physical duration");auto ticks=clock.ticks();require(clock.next_events(120,500)==1504&&clock.ticks()==ticks,"audio prediction does not advance scene time");
        NativeSceneClock odd;for(unsigned i=0;i<1050;++i)require(odd.advance(105),"non-divisor reference cadence");require(odd.events(500)==5000,"per-rate remainder does not accumulate clock drift");
    }
    {
        struct Opening:OpeningAnimationServices{
            NativeFrameMemory& memory;unsigned divisions,numerator=1,frames=0,ends=0;std::vector<std::array<uint32_t,3>> samples;Opening(NativeFrameMemory& m,unsigned n):memory(m),divisions(n){}
            unsigned begin_scene_timing(SceneTimingKind)override{return divisions;}void end_scene_timing()override{++ends;}
            SimulationStep scene_timing_step()const override{return {divisions,(frames*numerator)%divisions,numerator};}
            bool initialize_scene(const SceneInitializationInput&)override{return true;}bool play_sound(uint32_t,uint32_t)override{return true;}
            bool menu_frame(const std::array<uint32_t,20>&)override{samples.push_back({read(memory,0x800B84B0),read(memory,0x800B77D0),read(memory,0x800B9528)});++frames;return true;}
        };
        NativeFrameMemory coarseMemory,fineMemory;Opening coarse(coarseMemory,1),fine(fineMemory,4);OpeningAnimationInput input{};input.incoming[19]=0x80020000;OpeningAnimationResult a,b;
        require(run_opening_animation(coarseMemory,input,coarse,a)&&run_opening_animation(fineMemory,input,fine,b),"opening at authored and fine cadence");
        require(a.movementFrames==62&&a.fadeFrames==64&&b.movementFrames==248&&b.fadeFrames==256&&coarse.ends==1&&fine.ends==1,"opening duration conserved at 30/120 Hz");
        for(unsigned i=0;i<62;++i)require(coarse.samples[i]==fine.samples[i*4],"opening motion uses same authored formula at matching times");
        for(unsigned i=0;i<64;++i)require(coarse.samples[62+i][2]==fine.samples[248+i*4][2],"opening fade duration and formula conserved");
        require(fine.samples[1][1]!=fine.samples[0][1]&&fine.samples[249][2]!=fine.samples[248][2],"opening has actual new positions and fade values between authored ticks");
        NativeFrameMemory rationalMemory;Opening rational(rationalMemory,24);rational.numerator=5;OpeningAnimationResult r;
        require(run_opening_animation(rationalMemory,input,rational,r),"opening accepts 25 ticks/s at 120 Hz");
        require(r.movementFrames+r.fadeFrames==(126*24+4)/5,"rational opening preserves phase carry and total duration");
        struct Transition:DriverSceneServices{unsigned n,frames=0;explicit Transition(unsigned d):n(d){}unsigned frame_divisions()const override{return n;}bool initialize_scene(uint32_t)override{return true;}bool menu_frame()override{++frames;return true;}} transition(2);
        NativeFrameMemory transitionMemory;require(run_driver_transition(transitionMemory,transition,{0,0,0,0,60})&&transition.frames==120,"one-second transition stays one second at 60/120 Hz");
    }
    {
        struct Precise:NativeFrameMemory{struct Point{uint32_t address,packed;double x,y;};std::vector<Point> points;bool precise_web_geometry()const override{return true;}void web_render_point(uint32_t a,uint32_t p,double x,double y)override{points.push_back({a,p,x,y});}void render_transform_point(uint32_t a,uint32_t p,double x,double y)override{points.push_back({a,p,x,y});}} precise;
        NativeFrameMemory original;PreparationShapeWriter fine{precise,0x201006F0,0x401A0000},coarse{original,0x201006F0,0x401A0000};
        for(unsigned i=0;i<70;++i){uint32_t x=0x12345+i*0x1000,y=0xFFFEABCD-i*0x800;require(fine.point(x,y)&&coarse.point(x,y),"generated fractional shape points");}
        require(fine.finish()&&coarse.finish()&&precise.points.size()==70,"shape precision survives a 64-word writer flush");
        for(const auto& p:precise.points)require(read(precise,p.address)==p.packed,"shape hint stamps actual external stream address");
        require(std::abs(precise.points[0].x-double(int32_t(0xFFFEABCD))/65536)<1e-9&&std::abs(precise.points[0].y-double(0x12345)/65536)<1e-9,"shape precision retains both fixed-point fractions before marker packing");
        require(precise.write32(0x20100A30,0x8004E040)&&original.write32(0x20100A30,0x8004E040),"projection output fixture");
        require(preparation_store_screen_position(precise,0x123456,0xFFEABCDE,0x10000)&&preparation_store_screen_position(original,0x123456,0xFFEABCDE,0x10000),"fractional projected object origin");
        require(precise.points.back().address==0x8004E040&&precise.points.back().packed==read(precise,0x20100030),"projected origin uses display-list stamp");
        uint32_t packedSize=(command_mul(0x123456,48,19)<<16)|(command_mul(0x123456,32,19)&65535);
        require(preparation_store_projected_size(precise,packedSize,0x00200030,0x123456)&&preparation_store_projected_size(original,packedSize,0x00200030,0x123456),"retain projected object scale before size packing");
        require(precise.points.back().address==0x8004E050&&precise.points.back().packed==packedSize&&std::abs(precise.points.back().x-double(0x123456)*32/524288)<1e-9&&std::abs(precise.points.back().y-double(0x123456)*48/524288)<1e-9,"projected scale metadata uses original full multiply and packed stamp");
        for(unsigned bank=0;bank<4;++bank)require(std::equal(precise.bank(NativeFrameMemory::Bank(bank)).begin(),precise.bank(NativeFrameMemory::Bank(bank)).end(),original.bank(NativeFrameMemory::Bank(bank)).begin()),"higher shape/origin precision changes no original game memory");
    }
    {
        struct Menu:AnimatedMenuServices{
            unsigned divisions,frames=0;explicit Menu(unsigned n):divisions(n){}
            unsigned begin_scene_timing(SceneTimingKind)override{return divisions;}
            SimulationStep scene_timing_step()const override{return {divisions,frames%divisions};}
            bool initialize_scene(const SceneInitializationInput&)override{return true;}bool play_sound(uint32_t,uint32_t)override{return true;}
            bool select_music(uint32_t)override{return true;}bool transition(const DriverTransitionInput&,const std::array<uint32_t,20>&)override{return true;}
            bool vertical_axis(uint32_t,uint32_t,uint32_t& result)override{result=0;return true;}
            bool menu_frame(const std::array<uint32_t,20>&)override{++frames;return true;}
        };
        NativeFrameMemory coarseMemory,fineMemory;Menu coarse(1),fine(4);SelectionMenuInput input{};input.choices=1;input.content=0x80060000;input.caller.incoming[19]=0x80080000;
        for(auto* m:{&coarseMemory,&fineMemory}){
            require(m->write32(0x801133D0,0xFFFFFFFF)&&m->write32(0x806B9918,0xA1B2C3D4),"menu idle fixture");
            for(unsigned i:{20u,21u})require(m->write32(0x8010DCB0+i*4,0x80060000),"menu preset fixture");
        }
        uint32_t a=0,b=0;require(run_animated_menu(coarseMemory,input,coarse,a)&&run_animated_menu(fineMemory,input,fine,b),"animated menu at reference/fine cadence");
        require(a==0xFFFFFFFF&&a==b&&fine.frames==coarse.frames*4,"menu entry, idle timeout and exit duration conserved at 30/120 Hz");
        require(read(coarseMemory,0x800DC270)==read(fineMemory,0x800DC270)&&read(coarseMemory,0x806B9918)==read(fineMemory,0x806B9918),"menu idle counter and random sequence do not accelerate with frame rate");
    }
    {
        WebRenderPrecision hints;double x=0,y=0;uint32_t at=0x80050000,stamp=0x12345678;
        hints.record(at,stamp,1.25,2.5);hints.record_transform(at,stamp,180.25,120.5);
        require(hints.read(at,stamp,x,y)&&x==1.25&&y==2.5,"vertex metadata has its own coordinate domain");
        require(hints.read_transform(at,stamp,x,y)&&x==180.25&&y==120.5,"transform metadata cannot alias vertex units");
        hints.invalidate(at);require(!hints.read(at,stamp,x,y)&&!hints.read_transform(at,stamp,x,y),"native rewrite invalidates both precision domains");
        hints.record(at,stamp,1,2);hints.record_transform(at,stamp,3,4);hints.invalidate_range(at-4,12);
        require(!hints.read(at,stamp,x,y)&&!hints.read_transform(at,stamp,x,y),"bulk publication invalidates both precision domains");
    }
    NativeFrameMemory memory;PreparationShapeWriter writer{memory,0x201006F0,0x80020000};
    {
        uint32_t program=0x80060000,oscillator=0x80060100;
        require(memory.write32(program,0x101)&&memory.write32(program+4,oscillator)&&memory.write32(program+8,0x80000000)&&memory.write32(0x20100BA8+4,0x203002B8)&&memory.write32(oscillator,0x10000005),"seed authored oscillator program");
        for(unsigned substep=0;substep<5;++substep){PreparationAnimation animation{memory};animation.source=program;animation.step={5,substep};
            require(animation.run()&&read(memory,0x20100B60)==((0x1000+substep+1)<<16|5),"oscillator evaluates fresh fractional phase");
            require(read(memory,oscillator)==(substep<4?0x10000005U:0x10050005U),"oscillator persists only one original phase increment");
        }
        require(memory.write32(0x20100A60,0x11223344)&&memory.write32(0x20100A64,0x55667788),"seed colour rotation");
        PreparationAnimation animation{memory};animation.left=16;animation.step={5,0};
        require(animation.rotate(0x0001F000)&&read(memory,0x20100A60)==0x11223344,"colour rotation is held between authored effect ticks");
        animation.step={5,4};require(animation.rotate(0x0001F000)&&read(memory,0x20100A60)==0x55667788&&read(memory,0x20100A64)==0x11223344,"colour rotation advances once at effect boundary");
    }
    require(memory.write32(0x800DC258,512)&&memory.write32(0x800DC25C,0)&&memory.write32(0x800DC254,0)&&memory.write32(0x8010F174,4096),"seed camera approach and web spring");
    for(unsigned substep=0;substep<5;++substep)require(frame_finish(memory,{5,substep}),"fine camera and occupancy pass");
    require(read(memory,0x800DC25C)==32,"camera approach advances one original increment in five fine steps");
    require((read(memory,0x8010F174)&65535)>3900&&(read(memory,0x8010F174)&65535)<4096,"web spring evolves without fivefold acceleration");
    require(memory.write32(0x806B9920,0)&&memory.write32(0x806B991C,100)&&memory.write32(0x80110AC4,0),"seed independent picture and buffer clocks");
    for(unsigned substep=0;substep<5;++substep)require(advance_driver_counters(memory,substep==4),"advance presentation ring with explicit picture boundary");
    require(read(memory,0x806B991C)==101&&read(memory,0x806B9920)==5,"five buffer advances contain one authored picture tick");
    {
        NativeSimulationTiming timing(memory);require(memory.write32(0x806B9930,0xFEFF0000),"seed held fire packet");
        unsigned firing=0,responses=0;
        for(unsigned step=0;step<10;++step){
            require(timing.begin_values(),"begin multi-rate policy");
            if(timing.fire_opportunity())++firing;
            require(!timing.fire_opportunity(),"one firing opportunity per real simulation sample");
            if(timing.surface_response(7,2))++responses;
            require(!timing.surface_response(7,2),"same contact is consumed once within a pass");
            timing.commit_values();require(timing.advance(),"advance explicit fine phase");
        }
        require(firing==3&&responses==3,"fresh edge/contact plus original-cadence held responses");
        require(timing.begin_values(),"contact departure");timing.commit_values();require(timing.advance(),"advance departure");
        require(timing.begin_values()&&timing.surface_response(7,2),"reentry responds before next legacy boundary");timing.abort_values();
        require(memory.write32(0x806B9930,0xFFFF0000)&&timing.begin_values(),"release fire");timing.commit_values();require(timing.advance(),"advance release");
        require(memory.write32(0x806B9930,0xFEFF0000)&&timing.begin_values()&&timing.fire_opportunity(),"fresh fire edge responds before legacy boundary");timing.abort_values();
        require(timing.begin_values()&&timing.fire_opportunity(),"aborted edge can be retried");timing.commit_values();
    }
    require(memory.write32(0x806B992C,0x100)&&memory.write32(0x801133D8,0),"seed digital ramp");
    for(unsigned step=0;step<5;++step)require(prepare_driver_controls(memory,5,step)&&read(memory,0x801133D8)==(step+1)*4,"fine input polling preserves digital ramp duration");
    require(memory.write32(0x806B992C,0x400)&&prepare_driver_controls(memory,5,0)&&int32_t(read(memory,0x801133D8))==-4,"digital reversal responds on next substep");
    uint32_t controllers=0x80050000;
    require(memory.write32(0x806B992C,0)&&memory.write32(0x801133F0,0)&&memory.write32(0x801133E8,1)&&memory.write32(0x806B9910,controllers)&&memory.write32(controllers+16,0x40000010)&&memory.write32(controllers+20,60U<<8),"seed analog rate input");
    require(prepare_driver_controls(memory,5,0)&&((read(memory,0x806B9930)>>8)&255)==60,"analog rate is not divided or multiplied with polling frequency");
    require(memory.write32(controllers+16,0x40001000)&&memory.write32(0x806B9948,0)&&memory.write32(0x801133B4,0)&&memory.write32(0x801133BC,1)&&memory.write32(0x801133C0,0)&&memory.write32(0x801133B8,0),"seed rotary displacement input");
    for(unsigned step=0;step<5;++step){uint32_t rate=0;require(memory.write32(controllers+24,(step+1)*2U<<8)&&sample_driver_rotary(memory,rate,5)&&rate==10,"rotary displacement is converted to rate once");}
    require(read(memory,0x801133B8)==10,"five polls retain total rotary displacement");
    // Module 17 advances hidden bonus-game state even when the slot is not
    // drawn (low byte zero). Rendering less often must not skip this update.
    PreparationDepth depth{memory,writer};uint32_t slot=0x201009C4;
    require(memory.write32(slot,0x00000900),"seed hidden moving bonus slot");
    depth.bonus_slot(2,0,0,0,0,0);require(depth.good&&read(memory,slot)==0x01000900,"bonus worker owns real slot movement before visibility check");
    for(unsigned i=1;i<5;++i)depth.bonus_slot(2,0,0,0,0,0);
    require(read(memory,slot)==0x05000900,"five unchanged bonus calls advance five times");
    // The web colour counter is spatial bookkeeping reloaded from immutable
    // module data for each job, not an independently evolving elapsed clock.
    uint32_t table=0x800B14A0+10*32;
    require(memory.write32(table+16,0x80010000)&&memory.write32(table+20,0x201008C0)&&memory.write32(table+24,0x1F8)&&memory.write32(0x80010068,7),"seed module upload contract");
    require(load_preparation_data(memory,10)&&read(memory,0x20100928)==7,"module upload initializes colour index");
    PreparationWeb web{memory,writer};web.advance_colour();web.advance_colour();
    require(web.good&&read(memory,0x20100928)==9,"colour index advances within one lane traversal");
    require(load_preparation_data(memory,10)&&read(memory,0x20100928)==7,"next web job resets spatial counter");
    // Lightning's persisted seed stream is distinct from callback/initializer
    // RNG. Preserve flicker cadence without freezing current endpoints.
    require(memory.write32(0x2010080C,3)&&memory.write32(0x20100810,5)&&memory.write32(0x80112C54,0x12345678),"seed lightning and gameplay streams");
    PreparationLightning lightning{memory,writer};require(lightning.seeds(),"first procedural realization");
    uint32_t first=read(memory,0x8010CFC4);require(first==preparation_rng(3),"lightning commits its own random state");
    require(lightning.seeds()&&read(memory,0x8010CFC4)==preparation_rng(first),"another geometry call changes lightning realization");
    require(read(memory,0x80112C54)==0x12345678,"lightning realization does not consume callback RNG");
    // Held fire has no timer inside the emitter: capacity and one invocation
    // per original frame supply its cadence. Calling unchanged code at 120 Hz
    // would add firing opportunities even if the picture clock were held.
    struct Sound:CallbackServices{unsigned calls=0;bool play_sound(uint32_t,uint32_t& handle)override{handle=calls++;return true;}} sound;
    require(memory.write32(0x800DC244,0)&&memory.write32(0x800DC274,0)&&memory.write32(0x800DC27C,0)&&memory.write32(0x800DC2C4,2)&&memory.write32(0x80113160,0xFEFF0000)&&memory.write32(0x80110AC4,0)&&memory.write32(0x801131B0,0x80020000),"seed held fire below projectile capacity");
    CallbackInput input{};input.object=0x80030000;
    require(callback_player_actions(memory,input,&sound)&&callback_player_actions(memory,input,&sound),"evaluate held fire twice without advancing time");
    require(sound.calls==2&&read(memory,0x80020000)==0x11301&&read(memory,0x80020008)==0x11301&&read(memory,0x801131B0)==0x80020010,"firing opportunity belongs to callback invocation, not an explicit countdown");
    // Surface response raises a lane by 16 on every qualifying invocation.
    // The status bit written by this function does not suppress the next
    // invocation. Fine collision sampling must not multiply wall growth/sound.
    uint32_t object=0x80040000,draw=0x80040100,height=0x800AF7B4+4;
    require(memory.write32(object+0x30,0x400000)&&memory.write32(object+0x38,0)&&memory.write32(object+0x10,0)&&memory.write32(draw+0x20,0x10000)&&memory.write32(draw+0x24,100U<<16)&&memory.write32(height,50)&&memory.write32(0x800DC254,0)&&memory.write32(0x801095A0,0),"seed non-player surface contact");
    LaneContactResult contact;unsigned sounds=sound.calls;
    require(resolve_lane_contact(memory,{object,draw},contact,&sound)&&contact.kind==2&&read(memory,height)==66,"first surface response raises lane");
    require(resolve_lane_contact(memory,{object,draw},contact,&sound)&&contact.kind==2&&read(memory,height)==82&&sound.calls==sounds+2,"repeated overlap repeats wall growth and sound despite status bit");
    std::puts("Cycle intent contracts: bonus simulation, spatial reset, lightning RNG, held-fire cadence, and repeated surface response passed.");
}
