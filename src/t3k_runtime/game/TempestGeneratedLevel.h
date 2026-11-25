#pragma once
#include "TempestLevelDefinition.h"
#include "TempestSelectionMenu.h"
namespace tempest_native {
// Complete original 80018F22..80019248. Preserve its nibble construction,
// partial-word behavior and symmetry variants; other packed fields are retained.
inline bool generate_session_level(CommandMemory& m,uint32_t destination,unsigned budget=65536){
    if(destination&1)return false;uint32_t produced=0,nibbles=0,packed=0,index=4,random;
    do{if(!budget--||!next_driver_random(m,random))return false;uint32_t repeats=random%5;
        if(!next_driver_random(m,random))return false;uint32_t step=(random&3)+1;
        if(!next_driver_random(m,random))return false;if(random&0x1000)step=0-step;
        if(!next_driver_random(m,random))return false;if((random&3)==3)step=0;
        while(int32_t(repeats)>0){
            packed|=step;if(nibbles<=2){packed<<=4;++nibbles;}
            else{if(!write_level_halfword(m,destination+index*2,packed))return false;nibbles=packed=0;++index;produced+=4;}
            if(produced<=16)--repeats;else repeats=0;
        }
    }while(produced<=15);
    if(!write_level_halfword(m,destination+16,0)||!write_level_halfword(m,destination+18,0x1000)||!write_level_halfword(m,destination+4,16)||!next_driver_random(m,random)||!write_level_halfword(m,destination+6,random)||!next_driver_random(m,random))return false;
    uint32_t variant=random&3;
    if(variant==1){uint32_t mirror=0;
        for(unsigned i=0;i<2;++i){uint32_t word;if(!read_level_halfword(m,destination+8+i*2,word))return false;
            for(unsigned n=0;n<4;++n){mirror=(mirror<<4)|((16-(word&15))&15);word=uint32_t(int32_t(word<<16)>>20);}
            if(!write_level_halfword(m,destination+(7-i)*2,mirror))return false;}
    }else if(variant==2){for(unsigned i=0;i<2;++i){uint32_t word;if(!read_level_halfword(m,destination+8+i*2,word)||!write_level_halfword(m,destination+12+i*2,word))return false;}}
    else{if(!next_driver_random(m,random))return false;uint32_t offset=(random&3)<<2;if(!offset)offset=2;
        if(variant==3){for(unsigned i=0;i<4;++i){uint32_t word;if(!read_level_halfword(m,destination+8,word)||!write_level_halfword(m,destination+8+i*2,(word&0xFFF0)|((word+offset)&15)))return false;offset=0-offset;}}
        else{uint32_t word;if(!read_level_halfword(m,destination+10,word)||!write_level_halfword(m,destination+10,(word&0xFFF0)|((word+offset)&15)))return false;}
    }
    return true;
}
}
