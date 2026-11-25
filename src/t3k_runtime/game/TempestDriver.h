#pragma once
#include "TempestCommands.h"
#include "TempestSound.h"
#include <utility>
#include <initializer_list>
namespace tempest_native {
// CRT 800100E0..80010128: these are the original two zero-fill loops,
// not a blanket clearing of program data or an imported gameplay checkpoint.
inline bool initialize_driver_bss(CommandMemory& m){
    for(auto range:{std::pair<uint32_t,uint32_t>{0x8003C750,0x130},{0x806B9550,0x450}})
        for(uint32_t k=0;k<range.second;k+=4)if(!m.write32(range.first+k,0))return false;
    return true;
}
// 8001586E..8001597E. VidSync is an explicit platform service. It must not
// advance gameplay itself; the original caller advances one logic frame.
struct DriverSync {virtual bool video_sync(int32_t request,uint32_t& field)=0;virtual ~DriverSync()=default;};
inline bool begin_driver_frame(CommandMemory& m,DriverSync& platform){
    uint32_t mode;if(!m.read32(0x801133E8,mode))return false;
    if(!mode){
        uint32_t controllers,remote,buttons;
        if(!m.read32(0x806B9910,controllers)||!m.read32(controllers+12,remote)||!m.read32(controllers+4,buttons))return false;
        uint32_t translated=((remote&0x100000)?0x4000:0)|((remote&0x200000)?8:0)|((remote&0x800000)?16:0);
        if(!m.write32(controllers+4,buttons|(translated<<16)))return false;
    }
    uint32_t field,previous,index;
    if(!platform.video_sync(-1,field)||!m.write32(0x806B9924,field)||!m.read32(0x806B9934,previous)||!m.write32(0x806B9938,previous)||!m.read32(0x806B9920,index))return false;
    return m.write32(0x806B9934,0x40010000+index*0x54600);
}
// 800159BA..800159F0, after display/music services, before ring advancement.
// NUON's ld_w returns the selected big-endian halfword in the upper 16 bits.
inline bool sample_driver_controller(CommandMemory& m){
    uint32_t active,port,controllers;uint8_t high,low;
    if(!m.read32(0x801133F0,active)||!m.read32(0x801133E8+active*4,port)||!m.read32(0x806B9910,controllers)||!m.read8(controllers+port*16+4,high)||!m.read8(controllers+port*16+5,low))return false;
    return m.write32(0x806B992C,(uint32_t(high)<<8)|low);
}
// 800165FC: signed rotary byte, discontinuity suppression, sensitivity,
// clamping, optional inversion, and persistent position/phase state.
inline bool sample_driver_rotary(CommandMemory& m,uint32_t& result){
    uint32_t active,port,controllers,previous,last,multiplier,invert,phase;uint8_t raw;
    if(!m.read32(0x801133F0,active)||!m.read32(0x801133E8+active*4,port)||!m.read32(0x806B9910,controllers)||!m.read8(controllers+port*16+10,raw)||!m.read32(0x806B9944+port*4,previous)||!m.read32(0x801133B4,last))return false;
    uint32_t current=uint32_t(int32_t(int8_t(raw)))&~1U,delta=current-previous;
    if((delta^last)&0x80000000){uint32_t magnitude=int32_t(delta)<0?0U-delta:delta;
        if(int32_t(magnitude)>112)delta=last;
        else if(!m.write32(0x801133B4,delta))return false;
    }
    if(!m.read32(0x801133BC,multiplier))return false;
    delta=command_mul(multiplier,delta,0);
    if(int32_t(delta)<-80)delta=uint32_t(-80);if(int32_t(delta)>80)delta=80;
    if(!m.write32(0x806B9944+port*4,current)||!m.read32(0x801133C0,invert))return false;
    if(invert)delta=0U-delta;
    if(!m.read32(0x801133B8,phase)||!m.write32(0x801133B8,phase+delta))return false;
    result=delta;return true;
}
// 80016714..80016982: gameplay control-packet preparation, before connection
// enumeration and pause handling in the complete outer gameplay-frame caller.
inline bool prepare_driver_controls(CommandMemory& m){
    uint32_t buttons;if(!m.read32(0x806B992C,buttons))return false;
    uint32_t flags=((buttons&0x4000)?0x100:0)|((buttons&8)?0x800:0)|((buttons&0x2000)?0x10:0)|((buttons&16)?0x20:0)|((buttons&32)?0x40:0);
    if(!m.write32(0x806B9930,(flags<<16)^0xFFFF0000))return false;
    auto merge=[&](uint32_t bits){uint32_t packet;return m.read32(0x806B9930,packet)&&m.write32(0x806B9930,packet|bits);};
    if(buttons&0x500){
        uint32_t phase;
        if(buttons&0x400){
            if(!m.read32(0x801133D8,phase))return false;
            if(int32_t(phase)>0){phase=0;if(!m.write32(0x801133D8,phase))return false;}
            phase=int32_t(phase)<=-80?uint32_t(-80):phase-20;
            if(!m.write32(0x801133D8,phase)||!merge((phase<<8)&0xFE00))return false;
        }
        if(buttons&0x100){
            if(!m.read32(0x801133D8,phase))return false;
            if(int32_t(phase)<0){phase=0;if(!m.write32(0x801133D8,phase))return false;}
            phase=int32_t(phase)>79?80:phase+20;
            if(!m.write32(0x801133D8,phase)||!merge((phase<<8)&0xFE00))return false;
        }
        return true;
    }
    if(!m.write32(0x801133D8,0))return false;
    auto controller=[&](uint32_t& address,uint32_t& capabilities){uint32_t active,port,base;if(!m.read32(0x801133F0,active)||!m.read32(0x801133E8+active*4,port)||!m.read32(0x806B9910,base))return false;address=base+port*16;return m.read32(address,capabilities);};
    uint32_t address,capabilities;if(!controller(address,capabilities))return false;
    if(capabilities&0x1000){uint32_t delta;if(!sample_driver_rotary(m,delta)||!merge((delta<<8)&0xFC00))return false;}
    if(!controller(address,capabilities))return false;
    if(capabilities&16){uint8_t raw;if(!m.read8(address+6,raw))return false;int32_t axis=int8_t(raw);if(axis>=-19&&axis<=19)axis=0;if(axis<-90)axis=-90;if(axis>90)axis=90;if(!merge((uint32_t(axis)<<8)&0xFF00))return false;}
    return true;
}
// 80015DFA: step the optional scene color stream. The original advances its
// pointer before testing the word, then clears it again on the zero terminator.
inline bool advance_driver_color(CommandMemory& m){
    uint32_t cursor,value;if(!m.read32(0x800B84CC,cursor))return false;
    if(!cursor)return true;
    if(!m.read32(cursor,value)||!m.write32(0x800B84CC,cursor+4))return false;
    if(!value)return m.write32(0x800B84CC,0)&&m.write32(0x800B84B4,0x10808000);
    return m.write32(0x800B84B4,value);
}
// 800159F0..80015A44, before advance_driver_color. The pending pointer's
// address load is in a branch delay slot, so it applies for both flag paths.
inline bool advance_driver_counters(CommandMemory& m){
    uint32_t index,flags,value;const uint32_t address=0x800DC2DC;
    if(!m.read32(0x806B9920,index)||!m.write32(0x806B9920,index+1)||!m.read32(0x80110AC4,flags))return false;
    if(!(flags&1)){
        uint32_t frame;
        if(!m.read32(0x806B991C,frame)||!m.write32(0x806B991C,frame+1))return false;
    }
    if(!m.read32(address,value))return false;
    if(value&&(!m.write32(0x800B84CC,value)||!m.write32(address,0)))return false;
    return true;
}
inline bool finish_driver_ring(CommandMemory& m){uint32_t index;if(!m.read32(0x806B9920,index))return false;return index<=2||m.write32(0x806B9920,0);}
struct DriverPresentation{uint32_t buffer,flags,width,height,extra;};
struct DriverPresentationServices{
    virtual bool present(const DriverPresentation&)=0;
    virtual bool maintain_music()=0;
    virtual ~DriverPresentationServices()=default;
};
struct DriverMusicServices{
    virtual bool prepare_music(uint32_t firstSector,uint32_t lastSector)=0;
    virtual bool activate_music()=0;
    virtual bool reset_music(){return false;}
    virtual ~DriverMusicServices()=default;
};
// Complete 80015734..800157CC: reset the previous ring, preload a changed
// selection, then retain the SDK's actual readiness status for maintenance.
inline bool select_driver_music_track(CommandMemory& m,DriverMusicServices& services,uint32_t track){
    uint32_t media,current,first,last,status;
    if(!m.read32(0x806B9940,media))return false;if(!media)return true;
    if(!m.read32(0x80113444,current))return false;if(current==track)return true;
    if(!m.write32(0x80113444,track)||!services.reset_music()||!m.write32(0x80113440,0)||!m.read32(0x80113494+track*8,first)||!m.read32(0x80113498+track*8,last)||!services.prepare_music(first,last)||!m.read32(0x804FB370,status)||!m.write32(0x80113440,status)||!m.write32(0x806B9958,track))return false;
    return true;
}
// Complete 800157CE..8001586C. The SDK status read at 800223FE is a plain
// read of 804FB370. Its result replaces the game's control word even when
// neither a preload nor activation is needed (store in a branch delay slot).
inline bool maintain_driver_music(CommandMemory& m,DriverMusicServices& services){
    uint32_t enabled,control,status;
    if(!m.read32(0x801133D4,enabled))return false;if(!enabled)return true;
    if(!m.read32(0x80113440,control))return false;if(control==0xFFFFFFFE)return true;
    if(!m.read32(0x804FB370,status)||!m.write32(0x80113440,status))return false;
    if(status==0xFFFFFFFF){uint32_t track,first,last;
        if(!m.read32(0x806B9958,track)||!m.read32(0x80113494+track*8,first)||!m.read32(0x80113498+track*8,last))return false;
        return services.prepare_music(first,last);
    }
    if(status==1)return services.activate_music()&&m.write32(0x80113440,0xFFFFFFFE);
    return true;
}
// Complete 80015980..80015A70. Preserve presentation/music before input
// sampling, then counters/color/ring. Presentation does not advance game logic.
inline bool finish_driver_frame(CommandMemory& m,DriverPresentationServices& services){
    uint32_t buffer;if(!m.read32(0x806B9934,buffer)||!services.present({buffer,0x002DC840,360,240,0})||!services.maintain_music())return false;
    return sample_driver_controller(m)&&advance_driver_counters(m)&&advance_driver_color(m)&&finish_driver_ring(m);
}
struct DriverSound {virtual bool play_request(const SoundRequest&,uint32_t& handle)=0;virtual ~DriverSound()=default;};
// 80015D68: gameplay's deferred sound queue uses a six-bit resource index,
// unlike the immediate object's eight-bit cue index. Even its zero terminator
// is cleared before returning. The SDK handle is intentionally discarded.
inline bool consume_driver_sounds(CommandMemory& m,DriverSound& sound,unsigned budget=4096){
    for(unsigned k=0;k<budget;++k){
        uint32_t cue;if(!m.read32(0x800AF708+k*4,cue)||!m.write32(0x800AF708+k*4,0))return false;
        if(!cue)return true;
        uint32_t resource,word,handle;uint32_t table=0x80115B10+(cue&63)*8;
        if(!m.read32(table,resource)||!m.read32(table+4,word)||!sound.play_request({resource,0x1000+(((cue>>16)&255)<<6),word,0,0,0},handle))return false;
    }
    return false;
}
struct DriverSceneServices{
    virtual bool initialize_scene(uint32_t scene)=0;
    virtual bool menu_frame()=0;
    virtual ~DriverSceneServices()=default;
};
struct DriverTransitionInput{uint32_t flags,color,parameter,argument,frames;};
// Complete 80016BE6..80016CC0. Scene setup and menu frames remain explicit
// calls to their recovered implementations; a missing service must fail.
inline bool run_driver_transition(CommandMemory& m,DriverSceneServices& services,const DriverTransitionInput& in,unsigned budget=4096){
    uint32_t saved;
    if(!m.write32(0x800B84B0,in.flags)||!m.write32(0x800B84B4,in.color)||!m.write32(0x800B84B8,in.parameter)||!m.write32(0x800B84C8,in.parameter)||!m.write32(0x800B84BC,in.argument)||!m.read32(0x800B850C,saved)||!m.write32(0x800B850C,0)||!services.initialize_scene(2))return false;
    uint32_t frame=0;
    while(int32_t(frame)<int32_t(in.frames)){
        if(!budget--)return false;uint32_t flags,buttons;
        if(!m.read32(0x800DC274,flags))return false;
        if((flags&3)==3)break;
        if(!m.read32(0x806B992C,buttons))return false;
        if((buttons&0xFF3F)&&!m.write32(0x800DC274,flags|2))return false;
        if(!services.menu_frame())return false;
        ++frame;
    }
    return m.write32(0x800B850C,saved);
}
}
