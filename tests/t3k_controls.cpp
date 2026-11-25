#include "t3k_menu.h"
#include "controls.h"
#include "settings.h"
#include "t3k_runtime/native_controller_input.h"
#include "t3k_runtime/native_mouse_input.h"
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
    using namespace tempest_native;
    NativePadSample sample{true};sample.leftX=-32768;sample.leftY=-32768;
    auto record=native_pad_record(sample);
    require((record.words[1]>>16)==0,"axes alone press no button");
    require((record.words[1]&65535)==0x807F,"native signed axes and inverted Y saturate correctly");
    require(nuon_axis(-1)==-1&&nuon_axis(32767)==127,"axis quantization retains negative high-byte behavior");
    NativeMouseSpinner spinner;auto rotary=spinner.sample({true,true,true,true});
    require((rotary.words[1]>>16)==0x5008,"mouse fire, NUON jump, zapper coexist");
    require((rotary.words[0]&0x1000)!=0&&(record.words[0]&0x1000)==0,"rotary and pad are separate device identities");
    std::puts("Native controller and collection binding contracts passed.");
}
