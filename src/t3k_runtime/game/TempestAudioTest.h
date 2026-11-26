#pragma once
#include "TempestSession.h"
#include "TempestToolInput.h"
namespace tempest_native {
struct AudioTestServices {virtual bool volume_levels(uint32_t,uint32_t)=0;virtual ~AudioTestServices()=default;};
inline bool copy_audio_test_name(CommandMemory& m,uint32_t table,uint32_t index,uint32_t destination,unsigned padded){
    uint32_t source;if(!m.read32(table+index*4,source))return false;unsigned i=0;uint8_t byte;
    while(true){if(i>=128||!m.read8(source+i,byte))return false;if(!byte)break;if(!write_session_byte(m,destination+i,byte))return false;++i;}
    for(;i<=padded;++i)if(!write_session_byte(m,destination+i,' '))return false;return true;
}
inline bool prepare_audio_test_sound(CommandMemory& m,uint32_t index){return index<=54&&copy_audio_test_name(m,0x80113610,index,0x8010BAC9,29);}
inline bool prepare_audio_test_music(CommandMemory& m,uint32_t index){return index<=18&&copy_audio_test_name(m,0x80113578,index,0x8010BAFB,27)&&copy_audio_test_name(m,0x801135C4,index,0x8010BB24,19);}
inline bool prepare_audio_test_volumes(CommandMemory& m,uint32_t destination,AudioTestServices& services){
    uint32_t levels[2];for(unsigned row=0;row<2;++row){if(!m.read32(0x8011344C+row*4,levels[row])||levels[row]>16)return false;
        for(unsigned i=0;i<16;++i)if(!write_session_byte(m,destination+row*70+i*3,i<levels[row]?'+':' '))return false;}
    return services.volume_levels(levels[1]<<25,levels[0]<<25);
}
// Complete original 8001F2B0..8001F6D4 and its three text/volume helpers.
inline bool run_audio_test(CommandMemory& m,const OpeningAnimationInput& input,GameSessionServices& services,AudioTestServices& volume,unsigned budget=0xFFFFFFFF){
    SceneTimingScope cadence(services,SceneTimingKind::tool);if(!cadence.divisions)return false;uint64_t remainingBudget=cadence.budget(budget);
    auto c=input.incoming;uint32_t stack=c[19],base=stack-16;c[18]=base;c[19]=stack-128;c[17]=input.returnAddress;
    if(!services.transition({0x3400,0x10808000,0xFF00,0,1},c))return false;c[17]=0x8001F2E4;
    c[0]=0;c[1]=0;c[16]=0x8010BAA0;
    if(!m.write32(0x800B6BC4,0x8010C080)||!m.write32(0x800B6A90,0)||!m.write32(0x800B6A94,0x8000)||!m.write32(0x800B6AA0,0x8010BAA0)||!m.write32(0x800B850C,0))return false;
    for(auto offset:{0x58U,0x5CU,0x54U})if(!m.write32(base-offset,0))return false;
    uint32_t track;if(!prepare_audio_test_sound(m,0)||!m.read32(0x806B9958,track)||!prepare_audio_test_music(m,track)||!prepare_audio_test_volumes(m,0x8010BB52,volume))return false;
    c[17]=0x8001F378;if(!services.initialize_scene({6,c}))return false;c[17]=0x8001F388;
    uint32_t buttons;if(!m.read32(0x806B992C,buttons))return false;
    if(!(buttons&8)){c[16]=0x806B992C;c[2]=0x806B9958;
        do{
            if(!remainingBudget--)return false;uint32_t delta,value,active,port,table;uint8_t axis;
            if(!services.vertical_axis(base-0x54,0xA00,delta))return false;c[17]=0x8001F3D2;c[0]+=delta;if(int32_t(c[0])<0)c[0]=3;if(int32_t(c[0])>3)c[0]=0;
            if(!m.write32(0x800B6A90,c[0])||!m.read32(0x801133F0,active)||!m.read32(0x801133E8+active*4,port)||!m.read32(0x806B9910,table)||!m.read8(table+port*16+6,axis)||!m.read32(0x806B992C,buttons))return false;
            if(int8_t(axis)>40)buttons|=0x100;if(int8_t(axis)<-40)buttons|=0x400;if(!m.write32(0x806B992C,buttons)||!driver_raw_horizontal(m,base-0x58,0x500,delta))return false;c[17]=0x8001F498;
            if(delta){
                if(c[0]==0){c[1]+=delta;if(int32_t(c[1])<0)c[1]=54;if(c[1]>54)c[1]=0;if(!prepare_audio_test_sound(m,c[1]))return false;c[17]=0x8001F5CE;}
                else if(c[0]==1){if(!m.read32(0x806B9958,value))return false;value+=delta;if(int32_t(value)<0)value=18;value%=19;
                    if(!m.write32(0x806B9958,value)||!prepare_audio_test_music(m,value))return false;c[17]=0x8001F5CE;}
                else {uint32_t address=c[0]==2?0x8011344C:0x80113450;if(!m.read32(address,value))return false;int32_t next=int32_t(value+delta);value=next<0?0:next>15?15:uint32_t(next);
                    if(!m.write32(address,value)||!prepare_audio_test_volumes(m,0x8010BB52,volume))return false;c[17]=c[0]==2?0x8001F586:0x8001F5CE;
                    if(c[0]==2){if(!services.play_sound(c[1],0x1000))return false;c[17]=0x8001F5CE;}}
            }
            if(!m.read32(0x800B9778,value)||!m.write32(0x800B9778,value-uint32_t(cadence.delta(0x2000,2)))||!driver_button_edge(m,base-0x5C,0x4000,0x4000,value))return false;c[17]=0x8001F608;
            if(value&&c[0]<2){if(c[0]==0){if(!services.play_sound(c[1],0x1000))return false;}else{if(!m.read32(0x806B9958,track)||!services.select_music(track))return false;}c[17]=0x8001F644;}
            if(!m.read32(0x800B6A94,value)||!m.write32(0x800B6A94,int32_t(value)>0?value-uint32_t(cadence.delta(0x800,0)):0)||!services.menu_frame(c)||!m.read32(0x806B992C,buttons))return false;c[17]=0x8001F676;
        }while(!(buttons&8));
    }
    uint32_t random,cue;if(!m.write32(0x800B850C,1)||!next_driver_random(m,random)||!m.read32(0x80113410+(random&3)*4,cue)||!services.play_sound(cue,0x1000))return false;return true;
}
}
