#pragma once
#include "TempestSession.h"
namespace tempest_native {
// Complete 8001AF96..8001B0DA, including its zero-score row-relative store.
inline bool prepare_high_score_numbers(CommandMemory& m){
    const uint8_t format[]{'%', 'u',0};for(unsigned i=0;i<3;++i){uint8_t v;if(!m.read8(0x806A25B4+i,v)||v!=format[i])return false;}
    for(unsigned row=0;row<8;++row){uint32_t score;if(!m.read32(0x80113420+row*4,score))return false;
        char number[11];auto converted=std::to_chars(number,number+11,score);if(converted.ec!=std::errc())return false;unsigned n=unsigned(converted.ptr-number),destination=0x8010C3DE+row*38;
        for(unsigned i=0;i<n;++i)if(!write_session_byte(m,0x8003C788+i,uint8_t(number[n-1-i])))return false;
        if(!score){if(!write_session_byte(m,destination+row,'0'))return false;}
        else for(unsigned i=0;i<n;++i)if(!write_session_byte(m,destination+i,uint8_t(number[i])))return false;
        for(unsigned i=n;i<10;++i)if(!write_session_byte(m,destination+i,' '))return false;
    }
    return true;
}
struct HighScoreEntryInput {uint32_t content,scene,scores,player;OpeningAnimationInput caller;};
// Complete 8001B4D6..8001BD48: ranking/insertion and the original eight-character
// name editor. The alphabetical controller UI is preserved, not replaced by a
// host text box or a canned name. Private C formatting buffers are native data.
inline bool run_high_score_entry(CommandMemory& m,const HighScoreEntryInput& input,GameSessionServices& services,uint32_t& output,unsigned budget=8192){
    uint32_t stack=input.caller.incoming[19];if(input.player>1||(stack&15)||stack<128)return false;auto c=input.caller.incoming;c[18]=stack-16;c[19]=stack-128;c[17]=input.caller.returnAddress;
    uint32_t alphabet=stack-60,latch=stack-64,score,value,random;uint8_t byte;
    c[8]=input.content;c[13]=input.scene;c[0]=input.scores;c[16]=input.player;c[10]=3;c[5]=0;c[12]=1;c[9]=16;c[3]=0x806A25D8;c[4]=0x806A25E0;c[14]=0x80033AA0;
    if(!m.write32(latch,0))return false;for(unsigned i=0;i<42;++i){if(!m.read8(0x806A25E8+i,byte)||!write_session_byte(m,alphabet+i,byte))return false;}
    c[17]=0x8001B54C;if(!m.write32(0x800B6B04,0x8010C1A8)||!m.read32(input.scores+input.player*4,score)||!m.read32(0x801133F0,value)||value>1||!m.read32(0x801133A0+value*4,c[11]))return false;
    c[0]=0;for(;c[0]<8;++c[0]){if(!m.read32(0x80113420+c[0]*4,value))return false;if(int32_t(value)<=int32_t(score))break;}
    if(c[0]==8){output=0;return true;}
    uint32_t rank=c[0];c[16]=rank;c[6]=0x8010C4B2;
    if(rank==7)c[6]=0x8010C4D8;
    else{c[2]=c[6]+38;c[7]=7;
        do{for(unsigned i=0;i<8;++i){if(!m.read8(c[6]+i,byte)||!write_session_byte(m,c[2]+i,byte))return false;}
            if(!m.read32(0x80113420+(c[7]-1)*4,value)||!m.write32(0x80113420+c[7]*4,value))return false;--c[7];++c[16];c[2]-=38;c[6]-=38;
        }while(c[16]<=6);c[6]+=38;
    }
    if(!m.write32(0x80113420+rank*4,score)||!prepare_high_score_numbers(m))return false;c[14]=0x8001AF96;c[17]=0x8001B658;c[2]=0x8010BF83;
    auto find_letter=[&](uint8_t letter){for(c[1]=0;c[1]<256;++c[1]){uint8_t v;if(!m.read8(alphabet+c[1],v))return false;if(v==letter)return true;}return false;};
    if(!m.read8(c[11],byte)||!find_letter(byte)||!next_driver_random(m,random)||!m.read32(0x80113A40+(random&7)*4,value))return false;c[17]=0x8001B6AE;c[0]=0;c[16]=0;
    auto append=[&](uint32_t text){c[16]=0;for(;c[16]<256;++c[16]){uint8_t v;if(!m.read8(text+c[16],v))return false;if(!v)return true;if(!write_session_byte(m,c[2]+c[0],v))return false;++c[0];}return false;};
    if(!append(value)||!next_driver_random(m,random)||!m.read32(0x80113A60+(random%9)*4,value)||!append(value)||!append(c[3]))return false;
    for(;c[0]<=39;++c[0])if(!write_session_byte(m,c[2]+c[0],';'))return false;c[7]=0;
    c[16]=0x800B6A90;c[0]=0x80015500;
    if(!m.write32(0x800B6A90,0xFFFFFFFF)||!m.write32(0x800B6A94,65535)||!next_driver_random(m,random)||!m.write32(0x800B6A98,random)||!next_driver_random(m,random)||!m.read32(0x800B6A9C,value)||!m.write32(0x800B6A9C,value^(random&32))||!m.write32(0x800B6AA0,input.content))return false;
    c[16]=0x800B6AA0;c[14]=0x80015734;if(!services.select_music(17))return false;c[17]=0x8001B876;c[14]=0x80016BE6;
    if(!services.transition({0x3E00,0x10808000,0x10400,0,20},c)||!m.read8(c[11],byte)||!write_session_byte(m,0x8010C1AB,byte))return false;c[17]=0x8001B89C;c[0]=6;c[16]=0;
    do{if(!write_session_byte(m,0x8010C1A8+c[0],'<'))return false;++c[16];c[0]+=3;}while(c[16]<=6);
    c[17]=0x8010C1A8+c[0]-3;c[14]=0x80010200;
    if(!m.write32(0x800B850C,1)||!services.initialize_scene({input.scene,c}))return false;c[17]=0x8001B91A;
    auto frame=[&](uint32_t link){c[14]=0x80015C74;if(!budget--||!services.menu_frame(c))return false;c[17]=link;return true;};
    if(!m.read32(0x800B6A94,value))return false;
    if(int32_t(value)>0){c[16]=0x800B6A94;c[0]=0x800B92D4;
        do{if(!m.read32(0x800B6A94,value)||!m.write32(0x800B6A94,int32_t(value)>0?value-0x400:0)||!frame(0x8001B95E)||!m.read32(0x800B92D4,value)||!m.write32(0x800B92D4,value+0x2000)||!m.read32(0x800B6A94,c[14]))return false;}while(int32_t(c[14])>0);
    }
    if(!m.write32(0x801133B8,64))return false;c[8]=latch;c[4]=alphabet;c[13]=0x806B992C;
    while(c[12]){uint32_t delta,held,buttons;c[14]=0x80017A04;
        if(!services.vertical_axis(latch,0xA00,delta)||!m.read32(latch,held))return false;c[17]=0x8001B9B8;c[0]=delta;
        if(!held){if(!delta)c[9]=16;}else{--c[9];if(int32_t(c[9])<=0){if(!m.write32(latch,0))return false;c[9]=4;}}
        c[1]-=delta;c[16]=40;if(int32_t(c[1])<0)c[1]=40;if(int32_t(c[1])>40)c[1]=0;
        c[16]=0x8010C1A8+c[10];c[17]=alphabet+c[1];c[0]=c[16]<<3;c[2]=0-c[0];c[3]=0xFFFFFFE8;
        if(!m.read8(alphabet+c[1],byte)||!write_session_byte(m,c[16],byte)||!m.read32(0x806B992C,buttons))return false;
        if(buttons&0x4000){
            if(byte=='='){c[14]=0x80015602;if(!services.play_sound(8,0x1000))return false;c[17]=0x8001BA7E;
                if(int32_t(c[5])>0){if(!write_session_byte(m,c[16],'<'))return false;--c[5];c[10]-=3;}c[1]=0;
            }else if(byte=='>'){c[12]=0;c[1]=0;}
            else{if(!write_session_byte(m,c[11]+c[5],byte))return false;++c[5];c[10]+=3;if(c[5]==8)c[12]=0;
                c[14]=0x80015602;if(!services.play_sound(49,0x1000))return false;c[17]=0x8001BAF2;c[1]=0;}
            c[17]=c[11]+c[5];if(!m.read8(c[17],byte)||!find_letter(byte))return false;
            if(!m.read32(0x806B992C,buttons))return false;
            if(buttons&0x4000){c[0]=0x806B992C;c[16]=0x4000;
                do{if(!frame(0x8001BB60)||!m.read32(0x806B992C,buttons))return false;}while(buttons&0x4000);}
        }
        if(!frame(0x8001BB76)||!m.read32(0x800B92D4,c[14])||!m.write32(0x800B92D4,c[14]+0x2000)||!m.read32(0x806B992C,buttons))return false;
        if(buttons&0xFF3F){if(!m.read32(0x800DC274,c[14])||!m.write32(0x800DC274,c[14]|2))return false;}else c[14]=4;
    }
    c[14]=0x80015602;if(!services.play_sound(46,0x1000)||!m.read32(0x800B6A94,value))return false;c[17]=0x8001BBD0;
    if(int32_t(value)<=65535){c[17]=65535;c[16]=0x800B6A94;c[0]=0x800B92D4;
        do{if(!m.read32(0x800B6A94,value)||!m.write32(0x800B6A94,value+0x400)||!frame(0x8001BC24)||!m.read32(0x800B92D4,value)||!m.write32(0x800B92D4,value-0x8000)||!m.read32(0x800B6A94,c[14]))return false;}while(int32_t(c[14])<=65535);
    }
    c[16]=0;c[14]=0x80016BE6;if(!m.write32(0x800B850C,0)||!services.transition({0x3F00,0x10808000,0x10100,0,40},c))return false;
    for(unsigned i=0;i<8;++i){if(!m.read8(0x8010C1AB+i*3,byte)||!write_session_byte(m,c[11]+i,byte)||!write_session_byte(m,c[6]+i,(byte>='<'&&byte<='>')?' ':byte))return false;}
    output=c[7];return true;
}
}
