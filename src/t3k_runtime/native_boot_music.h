#pragma once
#include "native_media.h"
#include "native_driver_audio.h"
#include "game/TempestBootDriver.h"
#include "game/TempestMenuInput.h"
namespace tempest_native {
// The owner supplies one memory/audio/media transaction. Queuing the first
// read does not complete it; delivery remains the native media owner's job.
class NativeBootMusic final:public BootMusicServices {
public:
    NativeBootMusic(NativeFrameMemory& m,NativeMedia& media,NativeAudioBackend& audio,NativeDriverAudio& driver):memory_(m),media_(media),audio_(audio),driver_(driver){}
    bool open_media(uint32_t device,uint32_t path,uint32_t mode,uint32_t output,uint32_t& handle)override{
        std::string name;uint8_t byte;
        for(unsigned i=0;i<4096;++i){if(!memory_.read8(path+i,byte))return false;if(!byte){uint32_t bytes=0;
            if(!media_.open(device,name,mode,handle,bytes))return false;return !handle||memory_.write32(output,bytes);}name.push_back(char(byte));}
        return false;
    }
    bool initialize_stream(uint32_t handle,uint32_t buffer,uint32_t blocks)override{return audio_.initialize_stream(handle,buffer,blocks);}
    bool select_music(uint32_t track)override{return select_driver_music_track(memory_,driver_,track);}
    bool set_volumes(uint32_t stream,uint32_t voices)override{return driver_.set_volumes(stream,voices);}
    bool play_ui_sound(uint32_t index)override{uint32_t handle;return play_driver_ui_sound(memory_,driver_,index,handle);}
private:
    NativeFrameMemory& memory_;NativeMedia& media_;NativeAudioBackend& audio_;NativeDriverAudio& driver_;
};
}
