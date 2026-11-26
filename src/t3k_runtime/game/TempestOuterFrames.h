#pragma once
#include "TempestDriver.h"
#include "TempestControllerDiscovery.h"
#include "TempestFrameOrchestrator.h"
namespace tempest_native {
struct DriverMenuFrameServices:DriverSync,DriverPresentationServices,ControllerDiscoveryServices{
    virtual bool game_frame(const GameFrameInput&)=0;
};
// Complete C caller 80015C74..80015D66. Incoming engine data is explicit;
// three saved ABI words reflect this original caller's own C frame. No host
// pointer or NUON execution is used to derive those guest data values.
inline bool run_driver_menu_frame(CommandMemory& m,const std::array<uint32_t,20>& incoming,DriverMenuFrameServices& services){
    if(!begin_driver_frame(m,services))return false;
    GameFrameInput frame{};frame.engineContext=incoming;
    frame.engineContext[17]=0x806B9934;
    frame.engineContext[18]=incoming[19]-16;
    frame.engineContext[19]=incoming[19]-32;
    if(!m.read32(0x806B991C,frame.frame[0])||!m.read32(0x806B9938,frame.frame[1])||!m.read32(0x806B9934,frame.frame[2]))return false;
    frame.frame[3]=0xFFFF0000;
    if(!services.game_frame(frame)||!finish_driver_frame(m,services))return false;
    uint32_t active,port,base,config;
    if(!m.read32(0x801133F0,active)||!m.read32(0x801133E8+active*4,port)||!m.read32(0x806B9910,base)||!m.read32(base+port*16,config))return false;
    if(config&0x1000){uint32_t delta;if(!sample_driver_rotary(m,delta,services.simulation_step().divisions,services.simulation_step().numerator))return false;}
    uint32_t flags;if(!m.read32(0x80110AC4,flags))return false;
    if(flags&4)return true;
    for(unsigned slot=1;slot<=8;++slot){uint8_t status;
        if(!m.read32(0x806B9910,base)||!m.read8(base+slot*16,status))return false;
        if((status&128)&&!discover_driver_controllers(m,services,slot))return false;
    }
    return true;
}
struct DriverGameFrameServices:DriverMenuFrameServices,DriverSound{
    virtual bool pause_menu(const std::array<uint32_t,20>& incoming)=0;
};
// Complete C caller 80016704..80016AE6, using the native control decoder,
// discovery, deferred sounds and finish functions. The pause service receives
// exact caller data and can route it to the recovered run_driver_pause_menu.
struct DriverGameFramePreparation {bool pause=false;std::array<uint32_t,20> context{};};
inline bool prepare_driver_game_frame(CommandMemory& m,const std::array<uint32_t,20>& incoming,DriverGameFrameServices& services,DriverGameFramePreparation& preparation,SimulationStep step={}){
    preparation={};
    if(!step.valid()||!prepare_driver_controls(m,step.divisions,step.index,step.numerator))return false;
    uint32_t base;
    for(unsigned slot=1;slot<=8;++slot){uint8_t status;if(!m.read32(0x806B9910,base)||!m.read8(base+slot*16,status))return false;if((status&128)&&!discover_driver_controllers(m,services,slot))return false;}
    uint32_t active,port;uint8_t hi,lo;
    if(!m.read32(0x801133F0,active)||!m.read32(0x801133E8+active*4,port)||!m.read32(0x806B9910,base)||!m.read8(base+port*16+4,hi)||!m.read8(base+port*16+5,lo))return false;
    bool chord=((uint32_t(hi)<<8)|lo)==0x2030;
    if(!m.write32(0x801133C8,chord?1:0))return false;
    uint32_t buttons;if(!m.read32(0x806B992C,buttons))return false;
    if((buttons&0x2000)&&!chord){uint32_t flags;if(!m.read32(0x80110AC4,flags))return false;if(!(flags&2)){
        auto context=incoming;context[0]=0x806B9910;context[16]=9;context[17]=base+port*16+4;
        context[18]=incoming[19]-16;context[19]=incoming[19]-32;
        preparation.pause=true;preparation.context=context;
    }}
    else if(!m.write32(0x80110AC4,0))return false;
    return true;
}
inline bool finish_driver_game_frame(CommandMemory& m,const std::array<uint32_t,20>& incoming,DriverGameFrameServices& services){
    if(!begin_driver_frame(m,services))return false;
    GameFrameInput frame{};frame.engineContext=incoming;
    frame.engineContext[0]=0x806B9910;frame.engineContext[16]=9;frame.engineContext[17]=0x806B9934;
    frame.engineContext[18]=incoming[19]-16;frame.engineContext[19]=incoming[19]-32;
    if(!m.read32(0x806B991C,frame.frame[0])||!m.read32(0x806B9938,frame.frame[1])||!m.read32(0x806B9934,frame.frame[2])||!m.read32(0x806B9930,frame.frame[3]))return false;
    return services.game_frame(frame)&&consume_driver_sounds(m,services)&&finish_driver_frame(m,services);
}
inline bool run_driver_game_frame(CommandMemory& m,const std::array<uint32_t,20>& incoming,DriverGameFrameServices& services){
    DriverGameFramePreparation preparation;if(!prepare_driver_game_frame(m,incoming,services,preparation))return false;
    if(preparation.pause&&!services.pause_menu(preparation.context))return false;
    return finish_driver_game_frame(m,incoming,services);
}
}
