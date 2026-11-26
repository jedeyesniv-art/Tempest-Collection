#pragma once
#include "native_frame_integration.h"
#include "game/TempestNise.h"
#include "game/TempestNiseVoice.h"
#include "game/TempestNiseDescriptor.h"
#include "game/TempestNiseStreamControl.h"
#include "game/TempestNiseProcessorControl.h"
#include "game/TempestNiseStreamInitialization.h"
namespace tempest_native {
enum class AudioClockEvent:uint32_t{sound_request,pause_acknowledgment};
struct AudioClockState{uint64_t cores=0,outputs=0,voices=~uint64_t(0);};
struct NativeAudioClock{
    virtual bool target_blocks(AudioClockEvent,AudioClockState&)=0;
    virtual bool begin_frame(){return true;}
    virtual void commit_frame(){}
    virtual void abort_frame(){}
    virtual ~NativeAudioClock()=default;
};
struct NativeAudioStream{
    virtual bool boundary(NativeFrameMemory&,unsigned half)=0;
    virtual bool begin_frame(){return true;}
    virtual void commit_frame(){}
    virtual void abort_frame(){}
    virtual ~NativeAudioStream()=default;
};
// Native SDK allocation and NISE block mixing. The caller supplies the original
// sample clock and requests 64-sample blocks; this backend imposes no gameplay
// rate, interpolation of motion, or new timing policy. PCM is published only
// when its surrounding frame/audio transaction commits.
class NativeAudioBackend final:public FrameAudioBackend,private NiseVoiceDevice{
    struct Group{uint32_t address;std::vector<uint32_t> values;};
    struct Block{std::array<uint8_t,8192> after{};std::array<uint32_t,64> pcm{};std::vector<Group> groups;size_t cursor=0;bool corePublished=false;};
public:
    struct Checkpoint{
        std::array<uint8_t,8192> local{};
        AudioClockState time{};
        bool paused=false,continuationSafe=false;
        std::vector<std::array<uint32_t,64>> ready;
    };
    bool checkpoint(Checkpoint& out)const{
        // A partial mixer block contains an in-flight DMA continuation. Phase
        // checkpoints are taken only after both core and output have settled.
        if(active_||failed_||block_||!pending_.empty()||time_.cores!=time_.outputs)return false;
        out={local_,time_,paused_,continuationSafe_,ready_};return true;
    }
    bool restore(const Checkpoint& in){
        if(active_||in.time.cores!=in.time.outputs)return false;
        local_=in.local;time_=in.time;paused_=in.paused;continuationSafe_=in.continuationSafe;
        ready_=in.ready;pending_.clear();block_.reset();beforeBlock_.reset();failed_=false;failure_.clear();return true;
    }
    explicit NativeAudioBackend(NativeFrameMemory& memory,unsigned engine=0):memory_(memory),engine_(engine){}
    // Boot from the executable's immutable .nised payload and the original A0
    // startup message. Unlike restore_processor_checkpoint, this accepts no
    // captured runtime state, PC, register file or audio clock continuation.
    bool initialize_processor(std::span<const uint8_t> data,unsigned engine,const std::array<uint32_t,4>& message){
        if(active_||engine_>3||engine!=engine_||data.size()!=0x7AC||message[2]!=0x2000||message[3]!=0x81)return false;
        auto read=[&](unsigned at){return uint32_t(data[at])<<24|uint32_t(data[at+1])<<16|uint32_t(data[at+2])<<8|data[at+3];};
        if(read(0)!=0x203014B0||read(4)!=0x203015D4)return false;
        std::array<uint8_t,8192> fresh{};std::copy(data.begin(),data.end(),fresh.begin()+0x1000);
        // The immutable payload is loaded first; the recovered message handler
        // installs pool/output/size/mode and starts at the first DMA half.
        struct InitialMemory:CommandMemory{
            std::array<uint8_t,8192>& bytes;explicit InitialMemory(std::array<uint8_t,8192>& value):bytes(value){}
            bool read8(uint32_t a,uint8_t& v)override{if(a<0x20100000||a>=0x20102000)return false;v=bytes[a-0x20100000];return true;}
            bool read32(uint32_t a,uint32_t& v)override{a&=~3U;v=0;for(unsigned k=0;k<4;++k){uint8_t b;if(!read8(a+k,b))return false;v=(v<<8)|b;}return true;}
            bool write32(uint32_t a,uint32_t v)override{a&=~3U;if(a<0x20100000||a>0x20101FFC)return false;for(unsigned k=0;k<4;++k)bytes[a-0x20100000+k]=uint8_t(v>>(24-8*k));return true;}
        } initialMemory(fresh);
        struct InitialDevice:NiseProcessorControlServices{bool enabled=false;bool enable_audio_interrupt()override{enabled=true;return true;}bool acknowledge_pause()override{return false;}} initialDevice;
        if(!handle_nise_processor_message(initialMemory,initialDevice,0xA0,message)||!initialDevice.enabled)return false;
        local_=fresh;paused_=word(local_,0x102C)!=0;continuationSafe_=word(local_,0x100C)==0;
        block_.reset();time_={0,0,0};pending_.clear();return true;
    }
    bool restore_processor_data(std::span<const uint8_t> data){if(active_||data.size()!=local_.size())return false;std::copy(data.begin(),data.end(),local_.begin());paused_=word(local_,0x102c)!=0;continuationSafe_=word(local_,0x100c)==0;block_.reset();time_={0,0,0};return true;}
    bool restore_processor_checkpoint(std::span<const uint8_t> data,uint32_t pc,uint32_t returnPC,AudioClockState time){
        if(!restore_processor_data(data)||time.outputs>time.cores||time.cores-time.outputs>1)return false;time_=time;if(time_.voices==~uint64_t(0))time_.voices=0;
        if(time.cores>time.outputs){Block block;block.after=local_;for(unsigned i=0;i<64;++i)block.pcm[i]=word(local_,0x15d0+i*4);block.corePublished=true;block_=std::move(block);continuationSafe_=true;return true;}
        if(word(local_,0x100c)){
            continuationSafe_=pc==0x20301000;
            if(pc==0x203013F6&&returnPC==0x203010B4)continuationSafe_=true;
            if(pc==0x203013F6&&returnPC==0x203010FE){uint32_t mask;if(!memory_.read32(word(local_,0x1010),mask))return false;
                if(mask){unsigned highest=31-std::countl_zero(mask);continuationSafe_=word(local_,0x1024)==highest&&word(local_,0x1028)==(mask&~(1U<<highest));}}
        }return true;
    }
    bool set_clock(NativeAudioClock* clock){if(active_)return false;clock_=clock;return true;}
    bool set_stream(NativeAudioStream* stream){if(active_)return false;stream_=stream;return true;}
    bool begin_frame()override{if(active_||engine_>3)return false;if(clock_&&!clock_->begin_frame())return false;if(stream_&&!stream_->begin_frame()){if(clock_)clock_->abort_frame();return false;}before_=local_;beforePaused_=paused_;beforeTime_=time_;beforeBlock_=block_;beforeContinuation_=continuationSafe_;active_=true;failed_=false;failure_.clear();pending_.clear();return true;}
    bool play_sound(uint32_t cue,uint32_t& handle)override{SoundRequest request;if(!active_||failed_||!prepare_sound_request(memory_,cue,request))return fail();return play_request(request,handle);}
    bool play_request(const SoundRequest& request,uint32_t& handle){if(!active_||failed_)return false;if(!synchronize(AudioClockEvent::sound_request)||!start_nise_sound(memory_,*this,request,handle))return fail();return true;}
    bool play_descriptor(uint32_t requested,uint32_t descriptor,uint32_t spatial,uint32_t volume,uint32_t auxiliary,uint32_t& handle){if(!active_||failed_)return false;return (synchronize(AudioClockEvent::sound_request)&&start_nise_descriptor(memory_,*this,requested,descriptor,spatial,volume,auxiliary,handle))||fail("native descriptor voice failed");}
    bool stop_voice(uint32_t handle){if(!active_||failed_)return false;return stop_nise_voice(memory_,*this,handle)||fail("native voice stop failed");}
    bool activate_music(NiseStreamProgress& progress,uint32_t& result){if(!active_||failed_)return false;return activate_nise_music_stream(memory_,*this,progress,result)||fail("native stream activation dependency failed");}
    bool reset_music(){if(!active_||failed_)return false;return reset_nise_music_stream(memory_,*this)||fail("native stream reset dependency failed");}
    bool initialize_stream(uint32_t handle,uint32_t ring,uint32_t blocks){if(!active_||failed_)return false;return initialize_nise_stream(memory_,*this,handle,ring,blocks)||fail("native stream initialization dependency failed");}
    bool set_volumes(uint32_t stream,uint32_t voices){if(!active_||failed_)return false;return set_nise_mix_volumes(memory_,*this,stream,voices)||fail("native volume dependency failed");}
    bool mix_block(NiseBlockResult& result){
        if(!active_||failed_||paused_)return fail();View view(*this);if(!mix_nise_block(view,result))return fail();
        if(result.streamBoundary&&(!stream_||!stream_->boundary(memory_,result.streamBoundary-1)))return fail("stream callback requires a native implementation");
        std::array<uint32_t,64> pcm{};for(unsigned i=0;i<64;++i)pcm[i]=word(local_,0x15d0+i*4);pending_.push_back(pcm);return true;
    }
    bool advance_to(uint64_t blocks){return advance_to({blocks,blocks,~uint64_t(0)});}
    // A function-level coroutine: complete voice transfers, then master state,
    // then PCM output. It never decodes or executes a NUON instruction. Separating
    // these observable events also permits exact checks at asynchronous fences.
    bool advance_to(AudioClockState target){profiling::Scope profile(profiling::audio);
        bool exactVoices=target.voices!=~uint64_t(0);
        if(!active_||failed_||target.cores<time_.cores||target.outputs<time_.outputs||target.outputs>target.cores||target.cores-target.outputs>1||target.cores-time_.cores>4096||(exactVoices&&target.voices<time_.voices))return fail("inconsistent audio event timestamp");
        while(time_.cores<target.cores||time_.outputs<target.outputs||(exactVoices&&time_.voices<target.voices)){
            if(paused_)return fail();if(!block_&&!prepare_block())return false;
            if(block_->corePublished){if(time_.outputs>=target.outputs||!publish_output())return fail();continue;}
            auto& group=block_->groups[block_->cursor];bool master=block_->cursor+1==block_->groups.size();
            if(master?time_.cores>=target.cores:(exactVoices&&time_.voices>=target.voices))return fail("audio event order differs from the supplied clock");
            for(unsigned i=0;i<group.values.size();++i)if(!memory_.write32(group.address+i*4,group.values[i]))return fail();
            ++block_->cursor;if(master){local_=block_->after;block_->corePublished=true;++time_.cores;}else ++time_.voices;
        }return true;
    }
    void commit_frame()override{if(!active_||failed_)return;ready_.insert(ready_.end(),pending_.begin(),pending_.end());pending_.clear();active_=false;if(clock_)clock_->commit_frame();if(stream_)stream_->commit_frame();}
    void abort_frame()override{if(active_){local_=before_;paused_=beforePaused_;time_=beforeTime_;block_=beforeBlock_;continuationSafe_=beforeContinuation_;if(clock_)clock_->abort_frame();if(stream_)stream_->abort_frame();}active_=failed_=false;pending_.clear();}
    std::vector<std::array<uint32_t,64>> take_audio(){auto out=std::move(ready_);ready_.clear();return out;}
    std::span<const uint8_t> processor_data()const{return local_;}
    uint64_t completed_blocks()const{return time_.cores;}
    AudioClockState clock_state()const{return time_;}
    const std::string& failure()const{return failure_;}
private:
    NativeFrameMemory& memory_;unsigned engine_;std::array<uint8_t,8192> local_{},before_{};bool active_=false,failed_=false,paused_=false,beforePaused_=false,continuationSafe_=false,beforeContinuation_=false;
    NativeAudioClock* clock_=nullptr;NativeAudioStream* stream_=nullptr;AudioClockState time_{0,0,0},beforeTime_{0,0,0};std::optional<Block> block_,beforeBlock_;
    std::vector<std::array<uint32_t,64>> pending_,ready_;
    std::string failure_;
    bool fail(const char* why="native audio operation failed"){if(active_)failed_=true;if(failure_.empty())failure_=why;return false;}
    static uint32_t word(const std::array<uint8_t,8192>& data,unsigned at){return uint32_t(data[at])<<24|uint32_t(data[at+1])<<16|uint32_t(data[at+2])<<8|data[at+3];}
    static void put(std::array<uint8_t,8192>& data,unsigned at,uint32_t value){for(unsigned i=0;i<4;++i)data[at+i]=uint8_t(value>>(24-i*8));}
    bool synchronize(AudioClockEvent event){if(!clock_)return true;AudioClockState target;if(!clock_->target_blocks(event,target))return fail();return advance_to(target);}
    bool pause(uint32_t engine)override{if(engine!=engine_||paused_||!synchronize(AudioClockEvent::pause_acknowledgment))return false;paused_=true;put(local_,0x102c,1);return true;}
    bool resume(uint32_t engine)override{if(engine!=engine_||!paused_)return false;paused_=false;put(local_,0x102c,0);return true;}
    static bool external(uint32_t a){return (a>=0x40000000&&a<0x40800000)||(a>=0x80000000&&a<0x80800000);}
    struct View:CommandMemory{
        NativeAudioBackend& owner;explicit View(NativeAudioBackend& value):owner(value){}
        bool read8(uint32_t a,uint8_t& value)override{if(a>=0x20100000&&a<0x20102000){value=owner.local_[a-0x20100000];return true;}return external(a)&&owner.memory_.read8(a,value);}
        bool read32(uint32_t a,uint32_t& value)override{a&=~3U;if(a>=0x20100000&&a<0x20102000){value=word(owner.local_,a-0x20100000);return true;}return external(a)&&owner.memory_.read32(a,value);}
        bool write32(uint32_t a,uint32_t value)override{a&=~3U;if(a>=0x20100000&&a<0x20102000){put(owner.local_,a-0x20100000,value);return true;}return external(a)&&owner.memory_.write32(a,value);}
    };
    struct StagingView:View{
        // The supported mixer publishes 32 voice records and a 12-word master
        // record. Keep an indexed overlay; order retains every original write.
        static constexpr unsigned words=12+32*24;
        uint32_t pool;std::array<uint32_t,words> staged;std::array<uint8_t,words> valid{};
        std::vector<std::pair<uint32_t,uint32_t>> order;
        explicit StagingView(NativeAudioBackend& value):View(value),pool(word(value.local_,0x1010)){order.reserve(words);}
        const uint32_t* find(uint32_t a)const{a&=~3U;if(a<pool||uint64_t(a)-pool>=words*4||(pool&3))return nullptr;unsigned i=(a-pool)/4;return valid[i]?&staged[i]:nullptr;}
        bool read8(uint32_t a,uint8_t& value)override{if(auto* p=find(a)){value=uint8_t(*p>>(24-(a&3)*8));return true;}return View::read8(a,value);}
        bool read32(uint32_t a,uint32_t& value)override{a&=~3U;if(auto* p=find(a)){value=*p;return true;}return View::read32(a,value);}
        bool write32(uint32_t a,uint32_t value)override{a&=~3U;if(a>=0x20100000&&a<0x20102000)return View::write32(a,value);
            if(!external(a)||a<pool||uint64_t(a)-pool>=words*4||(pool&3))return false;
            unsigned i=(a-pool)/4;staged[i]=value;valid[i]=1;order.push_back({a,value});return true;}
    };
    bool prepare_block(){
        uint32_t remaining=word(local_,0x100c);
        if(!remaining){uint32_t bytes=word(local_,0x1018),half=word(local_,0x1034);if(bytes<512||bytes>0x20000||(bytes&511)||half>1)return fail();put(local_,0x1034,half^1);put(local_,0x1008,word(local_,0x1014)+half*(bytes/2));put(local_,0x100c,bytes/512);continuationSafe_=true;}
        if(!continuationSafe_)return fail("snapshot requires an unrecovered mixer continuation");auto original=local_;StagingView view(*this);NiseBlockResult result{};bool ok=mix_nise_block(view,result);
        Block block;block.after=local_;for(unsigned i=0;i<64;++i)block.pcm[i]=word(local_,0x15d0+i*4);local_=original;
        if(!ok)return fail("unsupported mixer mode or audio source memory");if(result.streamBoundary&&(!stream_||!stream_->boundary(memory_,result.streamBoundary-1)))return fail("stream callback requires a native implementation");
        for(auto [address,value]:view.order){if(block.groups.empty()||address!=block.groups.back().address+block.groups.back().values.size()*4)block.groups.push_back({address,{}});block.groups.back().values.push_back(value);}
        uint32_t pool=word(local_,0x1010);if(block.groups.empty()||block.groups.back().address!=pool||block.groups.back().values.size()!=12)return fail();
        for(unsigned i=0;i+1<block.groups.size();++i){const auto& g=block.groups[i];if(g.values.size()!=24||g.address<pool+48||uint64_t(g.address)>=uint64_t(pool)+48+32*96||(g.address-pool-48)%96)return fail();}
        block_=std::move(block);return true;
    }
    bool publish_output(){
        uint32_t destination=word(local_,0x1008),remaining=word(local_,0x100c);
        if(!block_||!block_->corePublished||!remaining||destination<0x40000000||uint64_t(destination)+256>0x40800000)return fail();
        for(unsigned i=0;i<64;++i)if(!memory_.write32(destination+i*4,block_->pcm[i]))return fail();
        put(local_,0x1008,destination+256);put(local_,0x100c,remaining-1);++time_.outputs;pending_.push_back(block_->pcm);block_.reset();return true;
    }
};
}
