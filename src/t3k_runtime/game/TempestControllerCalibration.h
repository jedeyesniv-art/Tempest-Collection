#pragma once
#include "TempestSession.h"
namespace tempest_native {
struct ControllerCalibrationServices {
    virtual bool rotary_calibration(uint32_t,const std::array<uint32_t,20>&)=0;
    virtual ~ControllerCalibrationServices()=default;
};
// Original 8001C05A..8001C7E4. Preserve the original staged analog calibration
// and the rotary branch, rather than replacing either with host movement edits.
inline bool run_controller_calibration(CommandMemory& m,const OpeningAnimationInput& input,GameSessionServices& services,ControllerCalibrationServices& rotary,unsigned budget=0xFFFFFFFF){
    SceneTimingScope cadence(services,SceneTimingKind::tool);if(!cadence.divisions)return false;uint64_t remainingBudget=cadence.budget(budget);
    auto c=input.incoming;uint32_t stack=c[19];c[18]=stack-16;c[19]=stack-64;c[17]=input.returnAddress;
    uint32_t value,random,table,port,record,buttons;uint8_t byte;
    auto write=[&](uint32_t a,uint32_t v){return m.write32(a,v);};
    auto frame=[&](uint32_t link){if(!remainingBudget--||!services.menu_frame(c))return false;c[17]=link;return true;};
    auto rotate=[&](uint32_t delta){return m.read32(0x800B92D4,value)&&write(0x800B92D4,value+uint32_t(cadence.delta(int32_t(delta),0)));};
    auto controller=[&](uint32_t player){return player<2&&m.read32(0x801133E8+player*4,port)&&m.read32(0x806B9910,table)&&(record=table+port*16,true);};
    auto held=[&](uint32_t player,uint32_t mask,bool& result){if(!controller(player)||!m.read8(record+4,byte))return false;buttons=uint32_t(byte)<<8;if(!m.read8(record+5,byte))return false;buttons|=byte;result=bool(buttons&mask);return true;};
    c[16]=0x800B6A90;c[0]=0x80015500;c[1]=0;
    if(!write(0x800B6A90,0xFFFFFFFF)||!write(0x800B6A94,0)||!next_driver_random(m,random)||!write(0x800B6A98,random)||!next_driver_random(m,random)||!m.read32(0x800B6A9C,value)||!write(0x800B6A9C,value^(random&32)))return false;
    c[16]=0x800B6A9C;c[17]=0x8001C09E;if(!services.transition({0x3E00,0x10808000,0x10400,0,20},c))return false;c[17]=0x8001C0D0;
    bool initialize=true;
    while(true){
        if(initialize){c[16]=1;if(!write(0x800B850C,1)||!services.initialize_scene({16,c}))return false;c[17]=0x8001C0EA;
            c[8]=1;c[11]=0x800B6AA0;c[10]=0x801133E8;c[5]=0x806B9910;c[6]=0x4000;c[9]=0xFFFFFFE8;initialize=false;}
        if(!write(0x800B6AA0,0x8010C200))return false;c[2]=0xFFFFFFFF;
        do{
            c[0]=0;
            do{bool b,a;if(!held(c[0],8,b))return false;if(b){c[8]=0;c[2]=c[0];}
                if(!held(c[0],0x4000,a))return false;if(a){c[2]=c[0];c[0]=3;}++c[0];
            }while(c[0]<=1);
            if(!frame(0x8001C184)||!rotate(0x2000))return false;
        }while(int32_t(c[2])<0);
        c[17]=0x8010C262;if(!write_session_byte(m,c[17],' ')||!controller(c[2]))return false;
        c[0]=0x801133E8+c[2]*4;c[1]=0x806B9910;c[17]=record;
        uint32_t config;if(!m.read32(record,config))return false;bool pressed;
        if(!held(c[2],0x4000,pressed))return false;
        if((config&0x1000)&&pressed){if(!rotary.rotary_calibration(c[2],c))return false;c[17]=0x8001C228;
            if(!write(0x800B6AA0,0x8010C350)||!held(c[2],8,pressed))return false;c[1]=table;
            if(pressed){c[16]=0x800B92D4;c[0]=0x801133E8+c[2]*4;
                do{if(!frame(0x8001C272)||!rotate(0x2000)||!held(c[2],8,pressed))return false;}while(pressed);}
            initialize=true;continue;
        }
        if(!held(c[2],0x4008,pressed))return false;
        if(pressed){c[17]=0x8010C262;c[0]=0x800B92D4;c[1]=0x801133E8+c[2]*4;
            do{if(!frame(0x8001C304)||!rotate(0x2000)||!held(c[2],0x4008,pressed))return false;}while(pressed);}
        c[17]=0x8010C262;if(!write_session_byte(m,c[17],0))return false;
        if(!c[8])return services.transition({0x3E00,0x10808000,0x10400,0,20},c);
        for(uint32_t label:{0x8010C27DU,0x8010C2EDU,0x8010C35DU}){if(!write_session_byte(m,label,uint8_t('1'+c[2])))return false;c[17]=label;}
        if(!write(0x800B6AA0,0x8010C270)||!controller(c[2]))return false;
        uint8_t x,y;if(!m.read8(record+6,x)||!m.read8(record+7,y))return false;c[1]=uint32_t(int32_t(int8_t(x)));c[0]=uint32_t(int32_t(int8_t(y)));c[3]=c[1];c[16]=c[0];c[17]=c[2]*4;
        if(!held(c[2],0x4000,pressed))return false;
        if(!pressed){c[7]=0x800B76E4;c[4]=255;
            do{if(!controller(c[2])||!m.read8(record+6,x)||!m.read8(record+7,y))return false;c[17]=uint32_t(int32_t(int8_t(x)));uint32_t sy=uint32_t(int32_t(int8_t(y)));
                if(int32_t(c[17])<int32_t(c[1]))c[1]=c[17];if(int32_t(c[17])>int32_t(c[3]))c[3]=c[17];if(int32_t(sy)<int32_t(c[0]))c[0]=sy;if(int32_t(sy)>int32_t(c[16]))c[16]=sy;
                if(!write(0x800B76E4,(uint32_t(x)<<16)|y)||!frame(0x8001C4CE)||!rotate(0x2000)||!held(c[2],0x4000,pressed))return false;c[17]=c[2]*4;
            }while(!pressed);
        }
        c[17]=0x8010C2CD;if(!write_session_byte(m,c[17],' ')||!held(c[2],0x4000,pressed))return false;
        if(pressed){c[0]=0x800B92D4;c[1]=0x801133E8+c[2]*4;do{if(!frame(0x8001C59C)||!rotate(0x2000)||!held(c[2],0x4000,pressed))return false;}while(pressed);}
        if(!write(0x800B76E4,0x00600060)||!write_session_byte(m,0x8010C2CD,0)||!write(0x800B6AA0,0x8010C2E0)||!held(c[2],0x4000,pressed))return false;c[17]=0x8010C2CD;
        if(!pressed){c[17]=0x8010C342;c[0]=0x800B92D4;c[1]=0x801133E8+c[2]*4;do{if(!frame(0x8001C65C)||!rotate(0x2000)||!held(c[2],0x4000,pressed))return false;}while(!pressed);}
        c[17]=0x8010C342;if(!write_session_byte(m,c[17],' ')||!controller(c[2])||!m.read8(record+6,x)||!m.read8(record+7,y)||!write(0x806B9944+c[2]*4,uint32_t(int32_t(int8_t(x))))||!write(0x806B994C+c[2]*4,uint32_t(int32_t(int8_t(y))))||!held(c[2],0x4000,pressed))return false;
        c[17]=0x806B994C+c[2]*4;
        if(pressed){c[0]=0x800B92D4;c[1]=0x801133E8+c[2]*4;do{if(!frame(0x8001C732)||!rotate(0x2000)||!held(c[2],0x4000,pressed))return false;}while(pressed);}
        c[17]=0x8010C342;if(!write_session_byte(m,c[17],0)||!write(0x800B6AA0,0x8010C350))return false;c[0]=0;c[1]=0x800B92D4;
        do{++c[0];if(!frame(0x8001C7A4)||!rotate(0xFFFFC000))return false;}while(c[0]<=199);
    }
}
}
