#pragma once
#include "TempestSelectionMenu.h"
namespace tempest_native {
struct BootMenuEntryServices {
    virtual bool opening(const OpeningAnimationInput&)=0;
    virtual bool transition(const DriverTransitionInput&,const std::array<uint32_t,20>&)=0;
    virtual bool attract_asset(uint32_t source,uint32_t mode)=0;
    virtual bool selection(const SelectionMenuInput&,uint32_t& result)=0;
    virtual ~BootMenuEntryServices()=default;
};
struct BootMenuEntryResult {uint32_t selection;std::array<uint32_t,20> handoff;};
// C-main 800209C0..80020AE8. Stop after the first selection result, before
// dispatch to options/gameplay/attract modes. Includes the original 40-frame
// transition; it must not be replaced by a direct opening-to-menu jump.
inline bool enter_boot_menu(CommandMemory& m,const std::array<uint32_t,20>& incoming,BootMenuEntryServices& services,BootMenuEntryResult& output,bool firstEntry=true){
    auto context=incoming;uint32_t mode;
    if(firstEntry&&!m.write32(0x801133C8,0))return false;
    if(!m.read32(0x801133C8,mode))return false;
    if(!mode){context[7]=0x800177C6;if(!services.opening({context,0x800209F0}))return false;context[17]=0x800209F0;}
    context[7]=0x80016BE6;
    if(!services.transition({0x3E00,0x10808000,0x11000,0x200,40},context)||!m.read32(0x801133C8,mode))return false;
    context[17]=0x80020A18;
    if(!mode&&int32_t(context[4])>-1&&context[5]){uint32_t source;
        if(!m.read32(context[18]-0x140+(context[4]&3)*4,source))return false;context[7]=0x8001C7E6;
        if(!services.attract_asset(source,16))return false;context[17]=0x80020A60;
    }
    if(!m.write32(0x801133C8,0))return false;context[5]=3;context[4]=(context[4]&3)+1;
    if(!m.write32(0x800DC270,0)||!m.write32(0x801133F4,0))return false;
    context[17]=0x801133F4;context[1]=0;context[6]=0;context[16]=0;context[7]=0x8001817C;
    if(!m.write32(0x801133CC,0))return false;
    uint32_t selected;if(!services.selection({0x8010C8C0,2,0x8010C170,{context,0x80020AE6}},selected))return false;
    context[0]=selected;context[17]=0x80020AE6;output={selected,context};return true;
}
}
