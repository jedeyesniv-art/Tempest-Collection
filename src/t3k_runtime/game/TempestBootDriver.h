#pragma once
#include "TempestCommands.h"
namespace tempest_native {
struct BootDrawingServices {
    virtual bool constructors()=0;
    virtual bool initialize_audio()=0;
    virtual bool initialize_environment(uint32_t)=0;
    virtual bool initialize_context(uint32_t,uint32_t)=0;
    virtual bool copy_bitmap(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t)=0;
    virtual ~BootDrawingServices()=default;
};
enum class BootDrawingStage:uint32_t {constructors,audio,environment,context,bitmap,complete};
// C-main 80020428..80020640, ending immediately after its first bitmap call.
// entryStack identifies the original caller-owned local data, not a CPU state.
// Register spills are private and are deliberately not reconstructed. The
// caller must own a transaction across this function and its required services.
inline bool begin_boot_drawing(CommandMemory& m,BootDrawingServices& services,uint32_t entryStack,BootDrawingStage& stage){
    if((entryStack&15)||entryStack<0x80000200||entryStack>0x80800000)return false;
    stage=BootDrawingStage::constructors;if(!services.constructors())return false;
    const uint32_t assets[]{0x8010C3A0,0x8010C500,0x8010C640,0x8010C750};
    for(unsigned i=0;i<4;++i)if(!m.write32(entryStack-0x150+i*4,assets[i]))return false;
    if(!m.write32(0x806B9918,0x31415926)||!m.write32(0x806B9954,0xD532FE7C)||!m.write32(0x806B992C,0))return false;
    stage=BootDrawingStage::audio;if(!services.initialize_audio())return false;
    const uint32_t environment=entryStack-0x40,context=entryStack-0x118;
    stage=BootDrawingStage::environment;if(!services.initialize_environment(environment))return false;
    stage=BootDrawingStage::context;if(!services.initialize_context(context,environment))return false;
    const uint32_t source=entryStack-0x140,destination=entryStack-0x12C,rectangle=entryStack-0x194;
    const uint32_t sourceWords[]{0x01680040,0x8005E040,0x016800F0,0x40};
    const uint32_t destinationWords[]{0x002DCC40,0x4012D200,0x016800F0,0x44};
    for(unsigned i=0;i<4;++i)if(!m.write32(source+i*4,sourceWords[i]))return false;
    for(unsigned i=0;i<4;++i)if(!m.write32(destination+i*4,destinationWords[i]))return false;
    if(!m.write32(rectangle,0)||!m.write32(rectangle+4,0x016700EF))return false;
    stage=BootDrawingStage::bitmap;if(!services.copy_bitmap(context,source,destination,rectangle,0))return false;
    stage=BootDrawingStage::complete;return true;
}
// Original continuation 80020640..8002066A. Deliberately modifies 230 rows,
// not all 240 rows of the bitmap. Keep the exact observed scalar-loop bound.
inline bool finish_boot_bitmap(CommandMemory& m){
    for(uint32_t i=0;i<0x14370;++i){uint32_t word,address=0x4012D200+i*4;
        if(!m.read32(address,word)||!m.write32(address,word|0x1F))return false;}
    return true;
}
struct BootMusicServices {
    virtual bool open_media(uint32_t device,uint32_t path,uint32_t mode,uint32_t sizeOutput,uint32_t& handle)=0;
    virtual bool initialize_stream(uint32_t handle,uint32_t buffer,uint32_t blocks)=0;
    virtual bool select_music(uint32_t track)=0;
    virtual bool set_volumes(uint32_t stream,uint32_t voices)=0;
    virtual bool play_ui_sound(uint32_t index)=0;
    virtual ~BootMusicServices()=default;
};
// C-main 8002066A..80020740, after the bitmap's memory result is available.
// A missing music file skips stream initialization but retains volume/UI work.
inline bool begin_boot_music(CommandMemory& m,BootMusicServices& services,uint32_t entryStack){
    if((entryStack&15)||entryStack<0x80000200||entryStack>0x80800000)return false;
    uint32_t sizeOutput=entryStack-0x198,handle,streamLevel,voiceLevel;
    if(!m.write32(0x806B993C,0x40400000)||!m.write32(sizeOutput,0)||!m.write32(0x801133D4,0)||
       !services.open_media(2,0x806A2614,0,sizeOutput,handle)||!m.write32(0x806B9940,handle))return false;
    // The handle store is in the original branch delay slots, including zero.
    if(handle){uint32_t ring;if(!m.read32(0x806B993C,ring)||
        !services.initialize_stream(handle,ring,0x800)||!services.select_music(0)||!m.write32(0x801133D4,1))return false;}
    if(!m.read32(0x80113450,streamLevel)||!m.read32(0x8011344C,voiceLevel)||!services.set_volumes(streamLevel<<25,voiceLevel<<25)||
       !m.write32(0x800DC274,0)||!m.write32(0x806B9920,0)||!m.write32(0x806B991C,0)||!m.write32(0x806B9934,0x40064600))return false;
    return services.play_ui_sound(0);
}
}
