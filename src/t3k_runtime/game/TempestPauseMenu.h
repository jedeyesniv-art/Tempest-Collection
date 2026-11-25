#pragma once
#include "TempestMenuInput.h"
#include "TempestOuterFrames.h"
namespace tempest_native {
struct DriverVolumeServices{virtual bool set_volumes(uint32_t stream,uint32_t voices)=0;virtual ~DriverVolumeServices()=default;};
namespace pause_detail {
inline bool byte_store(CommandMemory& m,uint32_t a,uint8_t value){uint32_t word;unsigned shift=24-8*(a&3);return m.read32(a,word)&&m.write32(a,(word&~(255U<<shift))|(uint32_t(value)<<shift));}
// A native C local latch. Only its private token is intercepted; game memory
// and sound resources still go through the ordinary data interface. No guest
// C-stack image is needed to hold the recovered function's two local variables.
struct LatchMemory:CommandMemory{
    CommandMemory& parent;uint32_t latch=0;explicit LatchMemory(CommandMemory& m):parent(m){}
    bool read8(uint32_t a,uint8_t& v)override{return parent.read8(a,v);}
    bool read32(uint32_t a,uint32_t& v)override{if(!a){v=latch;return true;}return parent.read32(a,v);}
    bool write32(uint32_t a,uint32_t v)override{if(!a){latch=v;return true;}return parent.write32(a,v);}
};
}
// Complete 8001F15E..8001F2AE: two rows of sixteen text markers, then the
// original SDK volume arguments. Input levels are reread during each fill.
inline bool update_driver_volume_bars(CommandMemory& m,uint32_t text,DriverVolumeServices& services,unsigned budget=4096){
    for(unsigned row=0;row<2;++row){uint32_t count=0,level,address=0x8011344C+row*4;
        if(!m.read32(address,level))return false;
        while(int32_t(count)<int32_t(level)){if(!budget--||!pause_detail::byte_store(m,text+row*0x46+count*3,'+'))return false;++count;if(!m.read32(address,level))return false;}
        for(;count<=15;++count)if(!pause_detail::byte_store(m,text+row*0x46+count*3,' '))return false;
    }
    uint32_t stream,voices;return m.read32(0x80113450,stream)&&m.read32(0x8011344C,voices)&&services.set_volumes(stream<<25,voices<<25);
}
struct DriverPauseServices:DriverMenuFrameServices,DriverSound,DriverVolumeServices{};
// Complete 8001619E..800165FA. Menu frames execute through the recovered
// native menu caller; only its explicit platform/gameplay dependencies remain.
template<class Services,class Frame> inline bool run_driver_pause_menu_frames(CommandMemory& m,const std::array<uint32_t,20>& incoming,Services& services,Frame&& pauseFrame,unsigned budget=4096){
    std::array<uint8_t,6> labels;for(unsigned k=0;k<6;++k)if(!m.read8(0x806A25AC+k,labels[k]))return false;
    const uint32_t objects[]{0x800B84B0,0x800B6864,0x800B50A8};uint32_t saved[3];
    for(unsigned k=0;k<3;++k)if(!m.read32(objects[k],saved[k])||!m.write32(objects[k],0x1000))return false;
    for(unsigned k=0;k<3;++k)if(!pause_detail::byte_store(m,0x8010BBFF+k,labels[k]))return false;
    if(!m.write32(0x800B6A90,0)||!m.write32(0x80110AC4,3)||!pause_detail::byte_store(m,0x8010BBD0,'_')||!update_driver_volume_bars(m,0x8010BC24,services))return false;
    uint32_t selection=0,toggle=0,flags;pause_detail::LatchMemory vertical(m),horizontal(m);
    if(!m.read32(0x80110AC4,flags))return false;
    while(flags&3){
        if(!budget--)return false;uint32_t delta;
        if(!poll_driver_menu_axis(vertical,services,0,0xA00,MenuAxis::vertical,delta))return false;
        selection+=delta;if(int32_t(selection)>2)selection=0;if(int32_t(selection)<0)selection=2;
        if(!m.write32(0x800B6A90,selection)||!poll_driver_menu_axis(horizontal,services,0,0x500,MenuAxis::horizontal,delta))return false;
        if(delta){
            if(!selection){toggle^=1;for(unsigned k=0;k<3;++k)if(!pause_detail::byte_store(m,0x8010BBFF+k,labels[toggle*3+k]))return false;}
            else {uint32_t address=0x8011344C+(selection-1)*4,level;
                if(!m.read32(address,level)||!m.write32(address,level+delta))return false;level+=delta;
                if(int32_t(level)>15){level=15;if(!m.write32(address,level))return false;}
                if(int32_t(level)<0&&!m.write32(address,0))return false;
                if(!update_driver_volume_bars(m,0x8010BC24,services))return false;
            }
        }
        uint32_t buttons,quit;if(!m.read32(0x806B992C,buttons))return false;
        if((buttons&0x4000)&&!m.write32(0x801133C8,toggle))return false;
        if(!m.read32(0x801133C8,quit))return false;
        if(quit){if(!m.write32(0x80110AC4,0)||!pause_detail::byte_store(m,0x8010BBD0,0))return false;break;}
        auto context=incoming;
        context[0]=selection;context[1]=toggle;context[2]=0;context[3]=0x8010BBD0;
        context[4]=saved[1];context[5]=saved[2];context[6]=saved[0];context[16]=0x80110AC4;
        context[17]=uint32_t(0x8010BBD0U<<3);context[18]=incoming[19]-16;context[19]=incoming[19]-64;
        if(!pauseFrame(context)||!m.read32(0x806B992C,buttons)||!m.read32(0x80110AC4,flags))return false;
        if(buttons&0x2000){if(!(flags&2)){flags^=1;if(!m.write32(0x80110AC4,flags)||!m.write32(0x80110AC4,flags|2))return false;flags|=2;}}
        else {flags&=0xFD;if(!m.write32(0x80110AC4,flags))return false;}
        if(!m.read32(0x80110AC4,flags))return false;
    }
    if(!pause_detail::byte_store(m,0x8010BBD0,0))return false;
    for(unsigned k=0;k<3;++k)if(!m.write32(objects[k],saved[k]))return false;
    return true;
}
inline bool run_driver_pause_menu(CommandMemory& m,const std::array<uint32_t,20>& incoming,DriverPauseServices& services,unsigned budget=4096){
    return run_driver_pause_menu_frames(m,incoming,services,[&](const auto& context){return run_driver_menu_frame(m,context,services);},budget);
}

}
