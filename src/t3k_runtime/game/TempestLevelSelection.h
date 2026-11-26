#pragma once
#include "TempestLevelDefinition.h"
#include "TempestLevelDescription.h"
namespace tempest_native {
struct LevelSelectionInput {uint32_t level,mode,maximum;OpeningAnimationInput caller;};
struct LevelSelectionServices:OpeningAnimationServices,LevelDefinitionServices {
    virtual bool transition(const DriverTransitionInput&,const std::array<uint32_t,20>&)=0;
    virtual bool axis(uint32_t latch,uint32_t mask,bool horizontal,uint32_t& delta)=0;
};
// 8001CA1A..8001D074. Original scene-9 level selector, including full slide,
// fade, level wrapping, packed definition/text preparation and returned level.
inline bool run_level_selection(CommandMemory& m,const LevelSelectionInput& input,LevelSelectionServices& services,uint32_t& output,unsigned budget=8192){
    SceneTimingScope cadence(services,SceneTimingKind::levelSelect);if(!cadence.divisions)return false;uint64_t remainingBudget=cadence.budget(budget);
    uint32_t stack=input.caller.incoming[19];if((stack&15)||stack<0x60||input.level>128||input.maximum>128)return false;
    auto c=input.caller.incoming;c[13]=input.mode;c[14]=input.maximum;c[3]=input.level;c[5]=0;c[17]=input.caller.returnAddress;c[18]=stack-16;c[19]=stack-0x60;
    auto write=[&](uint32_t a,uint32_t v){return m.write32(a,v);};
    auto frame=[&](uint32_t link){if(!remainingBudget)return false;--remainingBudget;if(!services.menu_frame(c))return false;c[17]=link;return true;};
    auto turn=[&](uint32_t increment){uint32_t angle;return m.read32(0x800B92D4,angle)&&write(0x800B92D4,angle+increment);};
    uint32_t flags,value,random;
    if(!write(stack-20,0)||!write(stack-24,0)||!write(0x800DC240,c[3]*0x494C)||!write(0x800B9520,0x30000)||!write(0x800B9524,0x20000)||!write(0x800B9528,65535)||!write(0x800B952C,0x00580084))return false;
    c[16]=0x800B6A90;c[0]=0x80015500;
    if(!write(0x800B6A90,0xFFFFFFFF)||!next_driver_random(m,random)||!write(0x800B6A98,random)||!next_driver_random(m,random)||!m.read32(0x800B6A9C,value)||!write(0x800B6A9C,value^(random&32))||!write(0x800B6AA0,0x8010CC90))return false;
    c[16]=0x800B6AA0;c[17]=0x800DC100;for(unsigned i=0;i<9;++i)if(!write(0x800DC100+i*4,0))return false;
    if(!m.read32(0x800AF700,c[6])||!services.transition({0x3400,0x10808000,0x10400,0,1},c)||!m.read32(0x801133A8,flags)||!write(0x801133A8,flags|4))return false;c[17]=0x8001CB2E;
    auto definition=[&](uint32_t link){if(!load_level_definition(m,c[6],c[3]+c[13],services))return false;c[17]=link;return true;};
    auto colors=[&](){uint32_t offset=((c[3]>>4)&7)*16;
        return m.read32(0x800B2AE8+offset,value)&&write(0x8010D04C,value)&&m.read32(0x800B2AEC+offset,value)&&write(0x800BA5A8,value);};
    auto describe=[&](uint32_t link){if(!prepare_level_description(m,{c[3],c[3]}))return false;c[17]=link;return true;};
    auto scene=[&](uint32_t link){if(!services.initialize_scene({9,c}))return false;c[17]=link;return true;};
    if(!definition(0x8001CB50)||!colors()||!m.read32(0x801133B0,value))return false;if(int32_t(c[3])>int32_t(value))c[3]=128;
    c[0]=0x10000000;if(!describe(0x8001CBAC))return false;c[16]=0x800B6A94;
    if(!scene(0x8001CBC0)||!write(0x800DC254,0)||!write(0x800DC25C,0)||!write_level_halfword(m,c[6],0)||!read_level_halfword(m,c[6]+40,value))return false;
    c[17]=(c[6]>>1)<<4;c[4]=0x800B907C;c[7]=value;c[2]=uint32_t(int32_t(int16_t(value)));c[1]=0x800B92D4;
    do{if(!m.read32(0x800B6A94,value)||!write(0x800B6A94,int32_t(value)>0?value-uint32_t(cadence.delta(0x800,0)):0))return false;
        c[0]=int32_t(c[0])>0?c[0]-uint32_t(cadence.delta(0x800000,1)):0;
        if(!write(0x800B907C,(c[2]<<16)+c[0])||!frame(0x8001CC4E)||!turn(uint32_t(cadence.delta(0x2000,3))))return false;
    }while(int32_t(c[0])>0);
    uint32_t buttons;if(!write(0x801133B8,0x40)||!m.read32(0x806B992C,buttons))return false;
    if(!(buttons&0x4000)){
        c[5]=0;c[11]=0x8010D04C;c[10]=0x800B2AE0;c[12]=0x800BA5A8;c[9]=(c[6]>>1)<<4;c[8]=c[9];
        while(!(buttons&0x4000)&&int32_t(c[5])<=599){
            c[5]=0;c[4]=1;if(!next_driver_random(m,random))return false;c[17]=0x8001CCC0;uint32_t delta;
            if(!services.axis(stack-20,0xA00,false,delta))return false;c[17]=0x8001CCD4;
            if(!delta){if(!services.axis(stack-24,0x500,true,delta))return false;c[17]=0x8001CCEC;c[4]=8;}
            if(delta){bool forward=int32_t(delta)<0;uint32_t step=c[4];c[16]=0;c[2]=c[7]<<16;c[5]=0x800B907C;c[1]=0x800B92D4;
                unsigned count=forward?8:20;for(uint64_t i=0;i<uint64_t(count)*cadence.divisions;i+=cadence.step().numerator){c[0]+=uint32_t(cadence.delta(forward?-0x800000:0x800000,2));
                    if(!write(0x800B907C,c[0]+c[2])||!frame(forward?0x8001CD20:0x8001CE46)||!turn(uint32_t(cadence.delta(0x2000,3))))return false;++c[16];}
                if(forward){c[3]+=step;if(int32_t(c[3])>int32_t(c[14]))c[3]=0;
                    if(!m.read32(0x801133B0,value))return false;if(int32_t(c[3])>int32_t(value))c[3]=128;
                    if(!definition(0x8001CD60)||!colors()||!scene(0x8001CD8E))return false;c[0]=0x10000000;if(!describe(0x8001CDA0))return false;
                }else{c[3]-=step;if(int32_t(c[3])<0)c[3]=c[14];if(!describe(0x8001CE70)||!m.read32(0x801133B0,value))return false;
                    if(int32_t(c[3])>int32_t(value))c[3]=128;if(!definition(0x8001CE94)||!colors()||!scene(0x8001CEC2))return false;c[0]=0xFC000000;}
                if(!write_level_halfword(m,c[6],0)||!read_level_halfword(m,c[6]+40,value))return false;
                c[2]=0x800B907C;c[7]=value;c[1]=uint32_t(int32_t(int16_t(value)))<<16;c[16]=0x800B92D4;
                do{c[0]+=uint32_t(cadence.delta(forward?-0x800000:0x800000,2));
                    if(!write(0x800B907C,c[0]+c[1])||!frame(forward?0x8001CDFE:0x8001CF20)||!turn(uint32_t(cadence.delta(0x2000,3))))return false;
                }while(forward?int32_t(c[0])>0:int32_t(c[0])<0);
                if(!write(0x801133B8,0x40))return false;c[5]=0;
            }else{c[16]=0;c[2]=c[7]<<16;
                if(!m.read32(0x800B6A94,value)||!write(0x800B6A94,int32_t(value)>0?value-uint32_t(cadence.delta(0x800,0)):0)||!turn(uint32_t(cadence.delta(0x2000,3)))||!frame(0x8001CF82))return false;
            }
            if(!m.read32(0x806B992C,buttons))return false;
        }
    }
    if(!services.play_sound(11,0x1000))return false;c[17]=0x8001CFA8;
    if(!m.read32(0x800B6A94,value))return false;
    if(int32_t(value)<=65535){c[17]=65535;c[16]=0x800B6A94;c[2]=0x800B92D4;c[4]=0x800B907C;c[1]=uint32_t(int32_t(int16_t(c[7])));
        do{if(!m.read32(0x800B6A94,value)||!write(0x800B6A94,value+uint32_t(cadence.delta(0x400,4))))return false;c[0]-=uint32_t(cadence.delta(0x40000,5));
            if(!frame(0x8001D00C)||!turn(uint32_t(cadence.delta(-0x8000,6)))||!write(0x800B907C,c[0]+(c[1]<<16))||!m.read32(0x800B6A94,value))return false;
        }while(int32_t(value)<=65535);
    }
    if(int32_t(c[5])>599)c[3]=0xFFFFFFFF;if(!m.read32(0x801133A8,flags)||!write(0x801133A8,flags&~4U))return false;output=c[3];return true;
}
}
