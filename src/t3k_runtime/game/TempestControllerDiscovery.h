#pragma once
#include "TempestCommands.h"
namespace tempest_native {
struct ControllerDiscoveryServices {
    virtual bool detect(uint32_t port)=0;
    virtual bool menu_frame()=0;
    virtual ~ControllerDiscoveryServices()=default;
};
// Complete original 80015E4E..8001619C. Device detection and menu frames are
// real dependencies, never successful no-ops in the production driver.
inline bool discover_driver_controllers(CommandMemory& m,ControllerDiscoveryServices& services,uint32_t changedPort,unsigned budget=4096){
    auto byte_store=[&](uint32_t a,uint8_t b){uint32_t w;unsigned shift=24-8*(a&3);return m.read32(a,w)&&m.write32(a,(w&~(255U<<shift))|(uint32_t(b)<<shift));};
    auto controller=[&](uint32_t port,uint32_t& address){uint32_t base;if(!m.read32(0x806B9910,base))return false;address=base+port*16;return true;};
    auto combined_buttons=[&](uint32_t& buttons){buttons=0;for(unsigned k=0;k<2;++k){uint32_t port,address;uint8_t hi,lo;if(!m.read32(0x801133E8+k*4,port)||!controller(port,address)||!m.read8(address+4,hi)||!m.read8(address+5,lo))return false;buttons|=(uint32_t(hi)<<8)|lo;}return true;};
    auto frame=[&](){if(!budget)return false;--budget;return services.menu_frame();};
    const uint32_t objects[]{0x800B84B0,0x800B6864,0x800B50A8,0x800B6AA4};uint32_t saved[4];
    if(!m.write32(0x800B3394,0x00180020)||!m.write32(0x800B3398,2)||!m.write32(0x800B33A0,0x00B40078))return false;
    for(unsigned k=0;k<4;++k)if(!m.read32(objects[k],saved[k])||!m.write32(objects[k],0x1000))return false;
    if(!m.write32(0x80110AC4,7)||!services.detect(changedPort))return false;
    uint32_t buttons;
    do {
        unsigned count=0,scan=1;
        for(;scan<=8;++scan){uint32_t address,config;
            if(!controller(scan,address)||!m.read32(address,config))return false;
            if((config&0x80000000)&&!services.detect(scan))return false;
            if(!controller(scan,address)||!m.read32(address,config))return false;
            if((config&0x40000000)&&(config&0x1012)){
                // The third match is stored before the original limit check.
                if(!m.write32(0x801133E8+count*4,scan))return false;
                if(count>1){if(!m.write32(0x800B33A4,0x8010CBE0))return false;scan=9;}
                ++count;
            }
        }
        if(!count){if(!m.write32(0x800B33A4,0x8010CB30)||!m.write32(0x801133E8,0))return false;scan=0;}
        else if(!m.write32(0x800B33A4,0x8010CBE0))return false;
        if(!frame())return false;
        // Preserve the actual loop-register condition, including remote fallback.
        if(scan<=1){uint32_t first;if(!m.read32(0x801133E8,first)||!m.write32(0x801133EC,first))return false;}
        if(!combined_buttons(buttons))return false;
    }while(!(buttons&0x2000));
    // The release test is sampled BEFORE each menu call, so a final call is
    // retained even on the iteration which first sees Start released.
    do {if(!combined_buttons(buttons)||!frame())return false;}while(buttons&0x2000);
    if(!byte_store(0x8010BEF0,0))return false;
    for(unsigned player=0;player<2;++player){uint32_t port,address,config;uint8_t raw;
        if(!m.read32(0x801133E8+player*4,port)||!controller(port,address)||!m.read32(address,config))return false;
        if(config&0x1000){
            if(!m.read8(address+10,raw)||!m.write32(0x806B9944+player*4,uint32_t(int32_t(int8_t(raw))))||!m.write32(0x801133B4,0)||!byte_store(0x8010BEF0,0x20))return false;
        }else{
            if(!m.read8(address+6,raw)||!m.write32(0x806B9944+player*4,uint32_t(int32_t(int8_t(raw))))||!m.read8(address+7,raw)||!m.write32(0x806B994C+player*4,uint32_t(int32_t(int8_t(raw)))))return false;
        }
    }
    if(!m.write32(objects[0],saved[0])||!m.write32(0x800B33A4,0x8010BBD0)||!m.write32(0x800B3394,0x00200020))return false;
    for(unsigned k=1;k<4;++k)if(!m.write32(objects[k],saved[k]))return false;
    return m.write32(0x800B3398,3)&&m.write32(0x800B33A0,0x00B40098)&&m.write32(0x80110AC4,0);
}
}
