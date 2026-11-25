#pragma once
#include "native_audio_stream.h"
#include "game/TempestDriver.h"
#include "game/TempestPauseMenu.h"
namespace tempest_native {
// The outer driver's real audio adapter. Its caller owns the surrounding
// memory/audio/stream transaction and supplies transport/clock progress.
class NativeDriverAudio final:public DriverSound,public DriverMusicServices,public DriverVolumeServices{
public:
    NativeDriverAudio(NativeFrameMemory& memory,NativeAudioBackend& audio,NativeAudioStreamQueue& stream,NiseStreamProgress& progress):memory_(memory),audio_(audio),stream_(stream),progress_(progress){}
    bool play_request(const SoundRequest& request,uint32_t& handle)override{return audio_.play_request(request,handle);}
    bool prepare_music(uint32_t first,uint32_t last)override{uint32_t result;return stream_.prepare_music(memory_,progress_,first,last,result);}
    bool activate_music()override{uint32_t result;return audio_.activate_music(progress_,result);}
    bool reset_music()override{return audio_.reset_music();}
    bool set_volumes(uint32_t stream,uint32_t voices)override{return audio_.set_volumes(stream,voices);}
    bool maintain_music(){return maintain_driver_music(memory_,*this);}
private:
    NativeFrameMemory& memory_;NativeAudioBackend& audio_;NativeAudioStreamQueue& stream_;NiseStreamProgress& progress_;
};
}
