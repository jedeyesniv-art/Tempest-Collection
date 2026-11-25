#pragma once
#include "native_frame_platform.h"
#include "native_audio_stream.h"
#include <deque>
#include <memory>
#include <cctype>
namespace tempest_native {
// Native read-only media frontend. Disc reads stage immutable host bytes;
// explicit delivery advances publish sectors and call the recovered completion
// handler. There is no emulated processor clock or presumed I/O completion.
class NativeMedia {
public:
    struct Job{NiseStreamRead request{};std::shared_ptr<const std::vector<uint8_t>> data;uint32_t delivered=0;bool error=false;};
    struct Checkpoint{std::array<std::string,17> handles{};std::deque<Job> jobs;};
    bool checkpoint(Checkpoint& out)const{if(active_)return false;out={handles_,jobs_};return true;}
    bool restore(const Checkpoint& in){
        if(active_||in.jobs.size()>4096)return false;
        for(const auto& j:in.jobs){
            if(!j.data||j.request.handle<3||j.request.handle>19||in.handles[j.request.handle-3].empty()||
               j.data->size()%2048||j.data->size()>uint64_t(j.request.blocks)*2048||j.delivered>j.data->size()/2048||
               j.request.blocks>4096||!j.request.blocks||!external_range(j.request.destination,uint64_t(j.request.blocks)*2048))return false;
        }
        handles_=in.handles;jobs_=in.jobs;beforeJobs_.clear();return true;
    }
    explicit NativeMedia(NativeDiscImage& disc):disc_(disc){}
    bool begin_frame(){if(active_)return false;beforeHandles_=handles_;beforeJobs_=jobs_;active_=true;return true;}
    void commit_frame(){if(!active_)return;beforeJobs_.clear();active_=false;}
    void abort_frame(){if(!active_)return;handles_=beforeHandles_;jobs_=beforeJobs_;beforeJobs_.clear();active_=false;}
    bool open(uint32_t device,std::string path,uint32_t mode,uint32_t& handle,uint32_t& sectorBytes){
        if(!active_)return false;handle=0;
        if(device!=1&&device!=2)return true;
        // This game opens read-only media. Other modes require a separate
        // implementation, rather than silently writing to the supplied image.
        if(mode!=0)return false;
        for(char& c:path){if(c=='\\')c='/';c=char(std::toupper(uint8_t(c)));}
        if(path.starts_with("/ISO9660/"))path.erase(0,9);else if(path.starts_with("/UDF/"))path.erase(0,5);
        uint32_t bytes;if(path.empty()||!disc_.file_size(path,bytes))return true;
        for(unsigned i=0;i<handles_.size();++i)if(handles_[i].empty()){handles_[i]=std::move(path);handle=i+3;sectorBytes=2048;break;}
        return true;
    }
    bool close(uint32_t handle){if(!active_)return false;if(handle>=3&&handle<=19)handles_[handle-3].clear();return true;}
    bool accept(const NiseStreamRead& request){
        if(!active_||request.handle<3||request.handle>19||handles_[request.handle-3].empty()||
           request.mode!=0x40000000||request.callback!=0x800219DA||!request.blocks||request.blocks>4096||
           (request.destination&3)||!external_range(request.destination,uint64_t(request.blocks)*2048))return false;
        uint32_t size;if(!disc_.file_size(handles_[request.handle-3],size))return false;
        uint32_t sectors=size/2048,available=request.sector<sectors?sectors-request.sector:0;
        uint32_t count=(std::min)(available,request.blocks);auto data=std::make_shared<std::vector<uint8_t>>();
        bool read=count==0||disc_.read(handles_[request.handle-3],request.sector,count*2048,*data);
        if(!read)data->clear();jobs_.push_back({request,data,0,!read||count!=request.blocks});return true;
    }
    bool deliver(NativeFrameMemory& memory,NativeAudioStreamQueue& stream,unsigned sectorBudget,unsigned& completed){
        if(!active_)return false;completed=0;
        while(!jobs_.empty()){
            auto& job=jobs_.front();uint32_t sectors=uint32_t(job.data->size()/2048);
            while(job.delivered<sectors&&sectorBudget){uint32_t source=job.delivered*2048,destination=job.request.destination+source;
                for(unsigned i=0;i<2048;i+=4){const auto* p=job.data->data()+source+i;uint32_t value=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];if(!memory.write32(destination+i,value))return false;}
                ++job.delivered;--sectorBudget;
            }
            if(job.delivered!=sectors)break;
            uint32_t status=job.error?0xC0000000:0x40000000,block=job.error?sectors-1:job.request.blocks;
            if(!stream.completed(memory,status,block))return false;
            jobs_.pop_front();++completed;
        }
        return true;
    }
    size_t pending()const{return jobs_.size();}
private:
    NativeDiscImage& disc_;std::array<std::string,17> handles_{},beforeHandles_{};
    std::deque<Job> jobs_,beforeJobs_;bool active_=false;
    static bool external_range(uint32_t a,uint64_t bytes){return (a>=0x40000000&&a<0x40800000&&bytes<=0x40800000ULL-a)||(a>=0x80000000&&a<0x80800000&&bytes<=0x80800000ULL-a);}
};
// Synchronous SDK waits still have to complete actual disc transfers. The
// surrounding audio/media transaction owns rollback and publication.
class NativeStreamProgress final:public NiseStreamProgress {
public:
    NativeStreamProgress(NativeFrameMemory& memory,NativeMedia& media,NativeAudioStreamQueue& stream):memory_(memory),media_(media),stream_(stream){}
    bool await_read_ready(CommandMemory& memory)override{return progress(memory);}
    bool await_stream_state(CommandMemory& memory)override{return progress(memory);}
private:
    NativeFrameMemory& memory_;NativeMedia& media_;NativeAudioStreamQueue& stream_;
    bool progress(CommandMemory& memory){
        if(&memory!=&memory_)return false;
        std::vector<NiseStreamRead> reads;if(!stream_.take_pending_reads(reads))return false;
        for(auto& request:reads)if(!media_.accept(request))return false;
        unsigned completed=0;return media_.deliver(memory_,stream_,4096,completed)&&completed!=0;
    }
};
}
