#pragma once
#include "TempestSelectionMenu.h"
#include <charconv>
namespace tempest_native {
struct LevelDescriptionInput {uint32_t level,inheritedRetry;};
// 8001A884..8001AF94. Immutable original string/table layout; original decimal
// scratch and temporary RNG reseeding/restoration remain observable RAM effects.
inline bool prepare_level_description(CommandMemory& m,const LevelDescriptionInput& input,unsigned budget=65536){
    if(input.level>128)return false;
    auto store=[&](uint32_t a,uint8_t v){uint32_t w;unsigned shift=24-(a&3)*8;return m.read32(a,w)&&m.write32(a,(w&~(255U<<shift))|(uint32_t(v)<<shift));};
    auto decimal=[&](uint32_t value,uint32_t destination,unsigned width){
        char number[11];auto result=std::to_chars(number,number+11,value);if(result.ec!=std::errc())return false;unsigned length=unsigned(result.ptr-number);
        for(unsigned i=0;i<length;++i)if(!store(0x8003C788+i,uint8_t(number[length-1-i]))||!store(destination+i,uint8_t(number[i])))return false;
        for(unsigned i=length;i<width;++i)if(!store(destination+i,' '))return false;return true;
    };
    const uint8_t format[]{'%', 'u',0};for(unsigned i=0;i<3;++i){uint8_t v;if(!m.read8(0x806A25B4+i,v)||v!=format[i])return false;}
    if(!decimal(input.level+1,0x8010CCB9,4))return false;
    uint32_t name,seed,maximum,flags;if(!m.read32(0x80114D4C+input.level*4,name)||!m.read32(0x806B9918,seed)||!m.read32(0x801133B0,maximum))return false;
    bool generated=int32_t(input.level)>int32_t(maximum);
    if(!generated){if(!m.read32(0x801133A8,flags))return false;generated=bool(flags&3);}
    uint32_t random,retry=input.inheritedRetry;
    if(generated){uint32_t a,b,d,source=0x800AF7F8+input.level*52;
        if(!m.read32(source+12,a)||!m.read32(source+8,b)||!m.read32(source+16,d)||!m.write32(0x806B9918,(((a<<2)^b)*input.level)^d))return false;
        name=0x8010BF20;
        bool complete=false;
        while(!complete){if(!budget--)return false;for(unsigned i=0;i<20;++i)if(!store(name+i,' '))return false;
            uint32_t first,second,third,position=0;uint8_t initial;
            if(!next_driver_random(m,random)||!m.read32(0x80113A84+(random%455)*4,first)||!m.read8(first,initial)||!next_driver_random(m,random))return false;
            if((random&255)<=15)retry=1000;
            auto append=[&](uint32_t text){for(unsigned i=0;i<256;++i){uint8_t v;if(!m.read8(text+i,v))return false;if(!v)return true;if(!store(name+position,v))return false;if(++position>29)return true;}return false;};
            if(!append(first))return false;if(position>29)continue;
            if(!store(name+position,' '))return false;if(++position>29)continue;
            auto choose=[&](uint32_t table,uint32_t count,uint32_t& text){
                do{if(!budget--||!next_driver_random(m,random)||!m.read32(table+(random%count)*4,text))return false;--retry;uint8_t v;if(!m.read8(text,v))return false;
                    if(int32_t(retry)<=0||v==initial)return true;
                }while(true);
            };
            if(!choose(0x801141A0,442,second)||!append(second))return false;if(position>29)continue;
            if(!store(name+position,' '))return false;if(++position>29)continue;
            if(!next_driver_random(m,random))return false;if((random&255)<=63)retry=0;
            if(!choose(0x8011488C,302,third)||!append(third))return false;if(position>29)continue;
            if(!store(name+position,0))return false;complete=true;
        }
        if(!m.write32(0x806B9918,seed))return false;
    }
    unsigned length=0;for(;length<256;++length){uint8_t v;if(!m.read8(name+length,v))return false;if(!v)break;if(!store(0x8010CCCC+length,v))return false;}
    if(!length||length==256)return false;
    for(unsigned i=length;i<30;++i)if(!store(0x8010CCCC+i,' '))return false;
    for(unsigned i=0;i<8;++i){uint8_t code;if(!m.read8(0x80114F4C+input.level*16+i,code))return false;uint32_t text;
        if(!code){if(!m.read32(0x801137D8,text))return false;}
        else{if(!m.read8(0x80114F4C+input.level*16+i+8,code))return false;unsigned k=0;for(;k<171;++k){uint32_t key;if(!m.read32(0x8011352C+k*4,key))return false;if(key==code)break;}
            if(k==171||!m.read32(0x801137D8+(k+1)*4,text))return false;}
        unsigned n=0;for(;n<256;++n){uint8_t v;if(!m.read8(text+n,v))return false;if(!v)break;if(!store(0x8010CC90+150+i*24+n,v))return false;}
        if(n==256)return false;for(;n<19;++n)if(!store(0x8010CC90+150+i*24+n,' '))return false;
    }
    return decimal(input.level*0x494C,0x8010CCFE,11);
}
}
