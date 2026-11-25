#pragma once
#include "TempestCommands.h"
#include <charconv>
namespace tempest_native {
inline bool write_session_byte(CommandMemory& m,uint32_t address,uint8_t value){uint32_t word;unsigned shift=24-(address&3)*8;return m.read32(address,word)&&m.write32(address,(word&~(255U<<shift))|(uint32_t(value)<<shift));}
// 8001A39A..8001A42C. Display spare-life markers (maximum eight), then NUL.
inline bool prepare_session_lives(CommandMemory& m,uint32_t lives){
    uint32_t count=lives-1;if(int32_t(count)>8)count=8;if(int32_t(count)<0)count=0;
    for(unsigned i=0;i<count;++i)if(!write_session_byte(m,0x8010CDF0+i,'$'))return false;
    return write_session_byte(m,0x8010CDF0+count,0);
}
// 8001A42E..8001A500. Two original scene lists; the optional-layer flag affects
// the first list after entry zero, while the second retains every selected layer.
inline bool prepare_session_layers(CommandMemory& m,uint32_t mask){
    for(unsigned i=0;i<5;++i)if(!m.write32(0x800B2BA0+i*4,0x00320001))return false;
    unsigned count=0;
    for(unsigned bit=0;bit<32;++bit)if(mask&(1U<<bit)){
        uint32_t descriptor,flags;if(count){if(!m.read32(0x801133A8,flags))return false;}
        if(!count||(flags&16)){if(!m.read32(0x800BA798+bit*4,descriptor)||!m.write32(0x800B2BA0+count*4,descriptor))return false;}
        if(!m.read32(0x800BA798+bit*4,descriptor)||!m.write32(0x800B2C3C+count*4,descriptor))return false;++count;
    }
    while(count<5){if(!m.write32(0x800B2BA0+count*4,0x00320001)||!m.write32(0x800B2C3C+count*4,0x00320001))return false;++count;}
    return true;
}
// 8001A5A8..8001A5F2. This variant has an unmodified factor and no countdown.
inline bool blend_session_parameters(CommandMemory& m,uint32_t factor,uint32_t first,uint32_t second,uint32_t destination){
    for(unsigned i=0;i<64;++i){uint32_t a,b;if(!m.read32(first+i*4,a)||!m.read32(second+i*4,b)||!m.write32(destination+i*4,(b+uint32_t(int32_t((a-b)*factor)>>16))&65535))return false;}
    return true;
}
// 8001A7A4..8001A882. Session level label, including the shared decimal scratch.
inline bool prepare_session_level_label(CommandMemory& m,uint32_t level){
    const uint8_t format[]{'%', 'u',0};for(unsigned i=0;i<3;++i){uint8_t v;if(!m.read8(0x806A25B4+i,v)||v!=format[i])return false;}
    char number[11];auto result=std::to_chars(number,number+11,level+1);if(result.ec!=std::errc())return false;unsigned n=unsigned(result.ptr-number);
    for(unsigned i=0;i<n;++i)if(!write_session_byte(m,0x8003C788+i,uint8_t(number[n-1-i]))||!write_session_byte(m,0x8010CE12+i,uint8_t(number[i])))return false;
    return write_session_byte(m,0x8010CE12+n,0);
}
}
