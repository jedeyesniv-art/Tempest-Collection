#include "t3k_menu.h"
#include "controls.h"
#include "settings.h"
#include "t3k_runtime/native_controller_input.h"
#include "t3k_runtime/native_mouse_input.h"
#include "t3k_runtime/native_input_response.h"
#include <limits>
#include "t3k_runtime/native_timing_profile.h"
#include "t3k_runtime/native_hud_layout.h"
#include "t3k_runtime/native_aspect.h"
#include <cstdio>
#include <cstdlib>
static void require(bool v,const char* why){if(!v){std::fprintf(stderr,"control contract: %s\n",why);std::exit(1);}}
int main(){
    t3k_menu::Install(false);
    require(t3k_menu::NuonButtons(1)==0x4000,"south button must be native fire, not RetroPad A");
    require(t3k_menu::NuonButtons(2)==8,"east button must be native zapper");
    require(t3k_menu::NuonButtons(1<<5)==0x10,"right shoulder must be native jump");
    require(t3k_menu::NuonButtons(1<<7)==1,"default right trigger is C-right");
    require(t3k_menu::NuonButtons(1<<4)==0x1000,"left shoulder is the NUON button (Back opens the collection menu)");
    settings::Set("bind_rt","r");require(t3k_menu::NuonButtons(1<<7)==0x10,"existing right-trigger jump remap is preserved");
    settings::Set("bind_a","nuon");require(t3k_menu::NuonButtons(1)==0x1000,"NUON is remappable using the hardware bit");
    settings::Set("controller","mouse");menu::Item choice;t3k_menu::FillControllerChoice(choice);
    unsigned mice=0;for(size_t i=0;i<choice.values.size();++i)if(choice.values[i]=="mouse"){++mice;require(choice.names[i]=="Mouse / trackball","selected mouse is not labelled disconnected");}
    require(mice==1&&choice.values.front()=="auto"&&choice.values[1]=="mouse","unified controller selector lists mouse exactly once alongside automatic gamepads");
    settings::Set("controller","auto");t3k_menu::FillControllerChoice(choice);mice=0;for(const auto& v:choice.values)mice+=v=="mouse";
    require(mice==1,"mouse remains available with no connected gamepad");
    using namespace tempest_native;
    activeTiming=NativeTimingProfile::read();require(!timing_changes_pending(),"fresh run has no pending timing changes");
    settings::Set("native_gameplay_speed","25");settings::Set("native_high_framerate","0");
    require(timing_changes_pending(),"pending timing and picture settings require restart");
    require(activeTiming.highFramerate&&activeTiming.rates[0]==24,"pending settings cannot change active run");
    auto pending=NativeTimingProfile::read();auto coarse=pending.cadence(SceneTimingKind::none,true);
    require(coarse.referenceHz==25&&coarse.divisions==1&&coarse.numerator==1,"off performs a complete original-strength tick at configured speed");
    activeTiming=pending;require(!timing_changes_pending(),"restart applies all timing settings together");
    settings::Set("native_high_framerate","1");auto fine=NativeTimingProfile::read().cadence(SceneTimingKind::none,true);
    require(fine.referenceHz==25&&fine.divisions==24&&fine.numerator==5&&fine.sample_hz()==120,"custom gameplay speeds maintain actual 120 Hz sampling");
    activeTiming=NativeTimingProfile::read();settings::Set("native_high_framerate","60");auto sixty=NativeTimingProfile::read();
    require(sixty.highFramerate&&sixty.presentationHz==60&&sixty.cadence(SceneTimingKind::none,true).sample_hz()==120,"60 Hz mode caps presentation and retains native high-rate integration");
    require(timing_changes_pending(),"120 to 60 Hz requires restart even though simulation cadence is unchanged");
    activeTiming=sixty;require(!timing_changes_pending(),"60 Hz snapshot is active after restart");
    settings::Set("native_high_framerate","1");auto full=NativeTimingProfile::read();
    require(full.highFramerate&&full.presentationHz==0&&timing_changes_pending(),"legacy On and 120 Hz draw every simulation update");
    for(const auto& field:timingFields)for(unsigned hz=15;hz<=60;++hz){settings::Set(field.key,std::to_string(hz));auto profile=NativeTimingProfile::read();
      for(unsigned k=0;k<=unsigned(SceneTimingKind::settle);++k){auto c=profile.cadence(SceneTimingKind(k));require(c.sample_hz()==120&&c.numerator<=c.divisions,"all configurable scene rates form valid 120 Hz rational steps");}}
    settings::Set("native_menu_speed","nan");require(NativeTimingProfile::read().rates[1]==24,"invalid timing uses agreed default");
    NativePadSample sample{true};sample.leftX=-32768;sample.leftY=-32768;
    auto record=native_pad_record(sample);
    require((record.words[1]>>16)==0,"axes alone press no button");
    require((record.words[1]&65535)==0x807F,"native signed axes and inverted Y saturate correctly");
    require(nuon_axis(-1)==-1&&nuon_axis(32767)==127,"axis quantization retains negative high-byte behavior");
    for(int v=-32768;v<=32767;++v)require(shape_stick_axis(int16_t(v),1,1)==v,"linear response preserves every signed input exactly");
    for(double curve:{2.5,5.0}){int16_t previous=-32768;
        for(int v=-32768;v<=32767;++v){auto shaped=shape_stick_axis(int16_t(v),curve,1);require(shaped>=previous,"default and maximum curves stay monotonic across full axis");previous=shaped;}}
    require(shape_stick_axis(14080,2,1)==9600,"higher curve widens middle stick travel");
    require(nuon_axis(shape_stick_axis(-32768,5,1))==-90&&nuon_axis(shape_stick_axis(32767,5,1))==90,"curve retains full speed at both endpoints");
    require(shape_stick_axis(32767,1,3)==32767&&shape_stick_axis(-32768,1,3)==-32768,"sensitivity saturates without wrapping");
    require(shape_stick_axis(12345,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity())==shape_stick_axis(12345,2.5,1),"invalid preferences fall back to default response");
    NativeMouseSpinner gained;require(gained.add_motion(4*input_gain(.5)),"mouse gain accepted");auto gainedRecord=gained.sample({true});require(uint8_t(gainedRecord.words[2]>>8)==2,"mouse gain scales displacement before native wrapping");
    NativeMouseSpinner spinner;auto rotary=spinner.sample({true,true,true,true});
    require((rotary.words[1]>>16)==0x5008,"mouse fire, NUON jump, zapper coexist");
    require((rotary.words[0]&0x1000)!=0&&(record.words[0]&0x1000)==0,"rotary and pad are separate device identities");
    require(native_hud_anchor(0x800B2FD0,0x800B94F0,0,0)==NativeHudAnchor::left,"score identity comes from native source and parameters");
    require(native_hud_anchor(0x800B3010,0x800B6A90,0,0x8010CDF0)==NativeHudAnchor::left,"lives group anchors left regardless of transformed screen origin");
    require(native_hud_anchor(0x800B3010,0x800B6A90,0,0x8010CE10)==NativeHudAnchor::right,"web number anchors right");
    require(native_hud_anchor(0x800B3010,0,0x800B4F50,0)==NativeHudAnchor::right,"status icon anchors right");
    require(native_hud_anchor(0x800B3010,0,0,0)==NativeHudAnchor::none,"generic vector shape is not guessed to be HUD");
    require(!native_hud_layout_scene(SceneTimingKind::opening)&&!native_hud_layout_scene(SceneTimingKind::levelSelect)&&native_hud_layout_scene(SceneTimingKind::levelClear),"title/menu composition stays original while score echoes remain anchored");
    for(float margin:{0.f,60.f,135.f}){
      t3knative::Picture pic;pic.segs={ {100,20,110,30,{1,1,1,1},{1,1,1,1}}, {40,200,50,210,{1,1,1,1},{1,1,1,1}} };
      t3knative::Op world{t3knative::Op::kSegs};world.count=1;pic.ops.push_back(world);
      t3knative::Op life{t3knative::Op::kSegs};life.first=1;life.count=1;life.hudAnchor=unsigned(NativeHudAnchor::left);pic.ops.push_back(life);
      t3knative::Glyph score;score.x=32;score.y=32;score.w=16;score.h=20;pic.glyphs.push_back(score);
      t3knative::Op text{t3knative::Op::kText};text.count=1;text.hudAnchor=unsigned(NativeHudAnchor::left);pic.ops.push_back(text);
      apply_native_hud_layout(pic,margin);
      require(pic.segs[0].x0==100&&pic.segs[0].x1==110,"wide HUD does not shift world geometry");
      require(pic.segs[1].x0==40-margin&&pic.segs[1].y0==200,"lives shift as a group with fixed vertical placement");
      require(pic.glyphs[0].x==32-margin&&pic.glyphs[0].w==16&&pic.glyphs[0].h==20,"score keeps edge inset and scale at 4:3, 16:9 and 21:9");
    }
    settings::Set("widescreen","0");require(native_aspect_choice()=="4:3","old original-aspect preference is retained");
    settings::Set("widescreen","1");require(native_aspect_choice()=="16:9","old wide preference maps to 16:9");
    settings::Set("native_aspect","21:9");require(native_aspect_choice()=="21:9","explicit aspect overrides old toggle");
    for(auto size:{std::pair{720.f,540.f},std::pair{960.f,540.f},std::pair{1260.f,540.f}})
      for(float ratio:{4.f/3,16.f/9,21.f/9}){auto r=native_aspect_fit(size.first,size.second,ratio);
        require(std::abs(r.w/r.h-ratio)<.00001f&&r.w<=size.first+.001f&&r.h<=size.second+.001f,"all selected aspects fit each window without cropping or stretch");
        require(std::abs(r.x*2+r.w-size.first)<.001f&&std::abs(r.y*2+r.h-size.second)<.001f,"aspect framing is centered with symmetric bars");}
    // Exercise actual overlay navigation, including dynamically visible pending
    // restart rows. Opening each fold with Right avoids relying on row indices.
    for(const auto& field:timingFields)settings::Set(field.key,std::to_string(field.initial));
    settings::Set("native_high_framerate","1");settings::Set("game_speed","21");settings::Set("menu_speed","30");
    activeTiming=NativeTimingProfile::read();auto before=activeTiming;
    t3k_menu::Install(false,true);menu::Open(true);uint64_t now=1000000000;
    auto press=[&](menu::Nav nav){menu::Update({},++now);return menu::Update(nav,++now);};
    std::array<bool,timingFields.size()> reached{};bool toggleReached=false;
    for(unsigned row=0;row<100;++row){
      auto previous=NativeTimingProfile::read();menu::Nav right;right.right=true;press(right);auto changed=NativeTimingProfile::read();
      for(size_t i=0;i<reached.size();++i)reached[i]=reached[i]||previous.rates[i]!=changed.rates[i];
      toggleReached|=previous.highFramerate!=changed.highFramerate||previous.presentationHz!=changed.presentationHz;
      menu::Nav down;down.down=true;press(down);
    }
    for(bool visited:reached)require(visited,"every timing field is reachable through the actual overlay");
    require(toggleReached,"high-framerate toggle is reachable through the actual overlay");
    require(settings::Num("game_speed",0)==21&&settings::Num("menu_speed",0)==30,"retired game-section pacing rows are absent in profile mode");
    require(activeTiming==before&&timing_changes_pending(),"overlay edits leave active timing untouched until restart");
    menu::Close();
    std::puts("Native controller and collection binding contracts passed.");
}
