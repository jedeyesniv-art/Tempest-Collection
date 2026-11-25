#pragma once
#include "TempestAnimatedMenu.h"
#include "TempestGameMenuLabels.h"
namespace tempest_native {
struct GameSetupEntryServices {
    virtual bool animated_menu(const SelectionMenuInput&,uint32_t& result)=0;
    virtual ~GameSetupEntryServices()=default;
};
struct GameSetupEntryResult {uint32_t selection;std::array<uint32_t,20> handoff;};
// C-main state-1 entry from 80020A88 through 80020B7C. The boundary precedes
// password/level selection; no gameplay or password result is synthesized.
inline bool enter_game_setup(CommandMemory& m,const std::array<uint32_t,20>& incoming,GameSetupEntryServices& services,GameSetupEntryResult& output){
    uint32_t state;if(!m.read32(incoming[17],state)||state!=1)return false;auto c=incoming;c[16]=0;c[7]=0x8001A5F4;
    if(!m.write32(0x801133CC,0)||!prepare_game_menu_labels(m,c[3]))return false;c[17]=0x80020B56;c[7]=0x8001867E;
    SelectionMenuInput menu{0x8010BD40,3,0x8010C130,{c,0x80020B72}};uint32_t result;
    if(!services.animated_menu(menu,result)||!m.write32(0x801133F4,0))return false;c[0]=result;c[17]=0x80020B72;output={result,c};return true;
}
}
