#pragma once
#include "native_controllers.h"
#include <algorithm>
namespace tempest_native {
// Physical positions, independent of Xbox/PlayStation face-button lettering.
enum class PadButton:unsigned {south,east,west,north,leftShoulder,rightShoulder,
    leftTrigger,rightTrigger,start,back,leftStick,rightStick,up,down,left,right,count};
struct NativePadSample {
    bool connected=false;
    uint32_t held=0;
    int16_t leftX=0,leftY=0; // SDL convention: positive Y points down.
};
inline constexpr uint32_t pad_button(PadButton b){return 1U<<unsigned(b);}
// Keep device quantization separate from the game's own deadzone and clamp.
// A negative division must round down to retain the signed high-byte ABI.
inline int8_t nuon_axis(int32_t value){
    value=(std::clamp)(value,-32768,32767);
    return int8_t(value>=0?value/256:-((-value+255)/256));
}
inline NativeControllerPort native_pad_record(const NativePadSample& input){
    NativeControllerPort result{};if(!input.connected)return result;
    constexpr std::array<uint16_t,unsigned(PadButton::count)> mapping{
        0x4000,0x0008,0x0004,0x0002,0x0020,0x0010,0x8000,0x0001,
        0x2000,0x1000,0x8000,0x0001,0x0200,0x0800,0x0400,0x0100};
    uint32_t buttons=0;for(unsigned i=0;i<mapping.size();++i)if(input.held&(1U<<i))buttons|=mapping[i];
    // Connected, standard/D-pad/shoulder/extended buttons, analog stick 1.
    // Do not advertise a spinner too: the original ORs both axis paths.
    result.words[0]=0x4000001F;
    result.words[1]=(buttons<<16)|(uint32_t(uint8_t(nuon_axis(input.leftX)))<<8)|uint8_t(nuon_axis(-int32_t(input.leftY)));
    return result;
}
// A spinner supplies a wrapping absolute counter, not a per-frame delta.
// Its hardware button mask includes NUON (0x1000), used for jet and UI axes.
inline NativeControllerPort native_spinner_record(bool connected,uint16_t buttons,uint8_t position){
    if(!connected)return {};
    return {{{0x4000100F,uint32_t(buttons)<<16,uint32_t(position)<<8,0}}};
}
}
