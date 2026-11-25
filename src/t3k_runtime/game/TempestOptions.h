#pragma once
#include "TempestAnimatedMenu.h"
#include "TempestSessionUtilities.h"
namespace tempest_native {
struct OriginalOptionsServices {
    virtual bool animated_menu(const SelectionMenuInput&,uint32_t&)=0;
    virtual bool show_asset(uint32_t,uint32_t,const std::array<uint32_t,20>&)=0;
    virtual bool tool(uint32_t,const std::array<uint32_t,20>&)=0;
    virtual ~OriginalOptionsServices()=default;
};
// C-main 80020C4E..80020E46. The tool functions are separate original callers.
inline bool run_original_options(CommandMemory& m,const std::array<uint32_t,20>& incoming,OriginalOptionsServices& services,std::array<uint32_t,20>& output){
    auto c=incoming;uint8_t rotary;uint32_t selected,value;
    if(!m.write32(0x801133CC,3)||!m.read8(0x8010BEF0,rotary))return false;c[7]=0x8001867E;
    if(!services.animated_menu({0x8010BE30,rotary==32?7U:6U,0x8010C130,{c,0x80020C8C}},selected))return false;c[0]=selected;c[17]=0x80020C8C;
    uint32_t state=2;
    if(int32_t(selected)<0)state=0;
    else if(selected<2){uint32_t bit=selected==0?16:2;if(!m.read32(0x801133A8,value)||!m.write32(0x801133A8,value^bit))return false;value^=bit;
        c[0]=selected==0?(value&16?3:0):(value&2?0:16);c[2]=0;
        uint32_t source=selected==0?0x8010BE00:0x8010BE10,destination=selected==0?0x8010BE58:0x8010BE71,length=selected==0?3:16;
        for(;c[2]<length;++c[2]){uint8_t ch;if(!m.read8(source+c[0]+c[2],ch)||!write_session_byte(m,destination+c[2],ch))return false;}
    }else if(selected==2){c[16]=0x8001C7E6;for(uint32_t asset:{0x8010C500U,0x8010C640U,0x8010C750U}){if(!services.show_asset(asset,16,c))return false;c[17]=asset==0x8010C500?0x80020DBC:asset==0x8010C640?0x80020DCA:0x80020DD8;}}
    else if(selected==3){c[7]=0x8001C7E6;if(!services.show_asset(0x8010C3A0,16,c))return false;c[17]=0x80020DF6;}
    else if(selected<=6){c[7]=selected==4?0x8001F2B0:selected==5?0x80019FC2:0x8001C05A;if(!services.tool(selected,c))return false;c[17]=0x80020E20;}
    if(selected<=6||int32_t(selected)<0){if(!m.write32(0x801133F4,state))return false;}
    if(int32_t(selected)>=0&&!m.write32(0x800DC270,0))return false;c[17]=0x801133F4;if(!m.read32(0x801133F4,c[7]))return false;output=c;return true;
}
}
