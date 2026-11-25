#pragma once
#include "native_audio_backend.h"
#include "game/TempestNiseStream.h"
namespace tempest_native {
// A transactional driver outbox: each accepted item is a real read to fulfill,
// not fabricated PCM or a completed-I/O notification.
class NativeAudioStreamQueue final:public NativeAudioStream,private NiseStreamServices{
public:
    using Checkpoint=std::vector<NiseStreamRead>;
    bool checkpoint(Checkpoint& out)const{if(active_||!pending_.empty())return false;out=ready_;return true;}
    bool restore(const Checkpoint& in){if(active_)return false;ready_=in;pending_.clear();return true;}
    bool begin_frame()override{if(active_)return false;active_=true;pending_.clear();return true;}
    void commit_frame()override{if(!active_)return;ready_.insert(ready_.end(),pending_.begin(),pending_.end());pending_.clear();active_=false;}
    void abort_frame()override{pending_.clear();active_=false;}
    bool boundary(NativeFrameMemory& memory,unsigned half)override{return active_&&handle_nise_stream_message(memory,0xA0,half,*this);}
    bool completed(NativeFrameMemory& memory,uint32_t status,uint32_t blocks){return active_&&complete_nise_stream_read(memory,status,blocks,*this);}
    bool request(NativeFrameMemory& memory,uint32_t sector,uint32_t blocks,uint32_t destination){return active_&&request_nise_stream_read(memory,sector,blocks,destination,*this);}
    bool prepare_music(NativeFrameMemory& memory,NiseStreamProgress& progress,uint32_t first,uint32_t last,uint32_t& result){return active_&&prepare_nise_music_stream(memory,*this,progress,first,last,result);}
    std::vector<NiseStreamRead> take_reads(){auto result=std::move(ready_);ready_.clear();return result;}
    bool take_pending_reads(std::vector<NiseStreamRead>& reads){if(!active_)return false;reads=std::move(pending_);pending_.clear();return true;}
private:
    bool active_=false;std::vector<NiseStreamRead> pending_,ready_;
    bool enqueue(const NiseStreamRead& request)override{if(!active_)return false;pending_.push_back(request);return true;}
};
}
