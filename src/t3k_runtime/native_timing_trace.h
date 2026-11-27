#pragma once
#include "native_frame_memory.h"
#include "game/TempestSceneTiming.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
namespace tempest_native {
// Read-only evidence at the complete native-picture boundary. No synthetic
// timestamps or modified counters are fed back into game memory.
class NativeTimingTrace {
    std::string capturePrefix_;uint64_t captureFrame_=0;
    std::ofstream frames_,effects_,values_,temporal_,motion_,motionInfo_;
    uint64_t motionStart_=0,motionCount_=0,motionWritten_=0;
    std::vector<uint8_t> previousPixels_;unsigned previousScene_=~0U;
    static uint32_t word(std::span<const uint8_t> bank,uint32_t at){auto* p=bank.data()+(at&0x7FFFFF);return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
    static bool peek(const NativeFrameMemory& memory,uint32_t address,uint32_t& value){
        NativeFrameMemory::Bank bank;
        if(address>=0x80000000&&address<=0x807FFFFC)bank=NativeFrameMemory::system;
        else if(address>=0x40000000&&address<=0x407FFFFC)bank=NativeFrameMemory::main;
        else if(address>=0x20100000&&address<=0x201FFFFC)bank=NativeFrameMemory::gameLocal;
        else return false;
        if(address&3)return false;value=word(memory.bank(bank),address);return true;
    }
public:
    explicit NativeTimingTrace(const std::string& prefix){
        if(const char* path=std::getenv("T3K_RENDER_CAPTURE")){capturePrefix_=path;if(const char* frame=std::getenv("T3K_RENDER_CAPTURE_FRAME"))captureFrame_=std::strtoull(frame,nullptr,10);}
        if(const char* path=std::getenv("T3K_TEMPORAL_TRACE")){
            temporal_.open(path);if(!temporal_)throw std::runtime_error("Cannot open temporal comparison trace");
            temporal_<<"host_frame,scene_kind,substep,divisions,numerator,mean_rgb,mean_absolute_change\n";
        }
        if(const char* path=std::getenv("T3K_MOTION_CAPTURE")){
            motion_.open(path,std::ios::binary);motionInfo_.open(std::string(path)+".csv");
            if(!motion_||!motionInfo_)throw std::runtime_error("Cannot open motion capture");
            motionCount_=630;if(const char* v=std::getenv("T3K_MOTION_START"))motionStart_=std::strtoull(v,nullptr,10);
            if(const char* v=std::getenv("T3K_MOTION_COUNT"))motionCount_=std::strtoull(v,nullptr,10);
            motionInfo_<<"capture_index,host_frame,width,height,scene_kind,reference_hz,divisions,numerator\n";
        }
        if(prefix.empty())return;
        std::filesystem::path base(prefix);if(!base.parent_path().empty())std::filesystem::create_directories(base.parent_path());
        auto f=base;f+=".frames.csv";auto e=base;e+=".effects.csv";frames_.open(f);effects_.open(e);auto v=base;v+=".values.csv";values_.open(v);
        if(!frames_||!effects_||!values_)throw std::runtime_error("Cannot open native timing trace");
        frames_<<"host_frame,gameplay,driver_picture,engine_picture,renderer_job,video_field,control,player_position,camera0,camera1,camera2,packed_lifetime,session_flags,ops,segments,blobs,webs,divisions,substep,score,lives,scene_kind,reference_hz,sample_hz,menu_fade,opening_phase,step_numerator\n";
        effects_<<"host_frame,op_index,type,kind,fade,zoom_x,zoom_y,angle,count\n";
        effects_.precision(9);
        values_<<"host_frame,object,descriptor,value_index,position,velocity,scale,flags\n";
    }
    // Diagnostic GPU readback: intentionally excluded from performance runs.
    void display(uint64_t serial,SDL_Renderer* renderer,SimulationStep step,SceneCadence cadence){
        bool capture=motion_.is_open()&&serial>=motionStart_&&serial-motionStart_<motionCount_;
        if(!capture&&(!temporal_.is_open()||!cadence.referenceHz))return;
        auto* raw=SDL_RenderReadPixels(renderer,nullptr);if(!raw)throw std::runtime_error("Temporal readback failed");
        auto* rgba=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(raw);if(!rgba)throw std::runtime_error("Temporal conversion failed");
        std::vector<uint8_t> bytes(size_t(rgba->w)*rgba->h*4);
        for(int y=0;y<rgba->h;++y)std::copy_n(static_cast<const uint8_t*>(rgba->pixels)+y*rgba->pitch,rgba->w*4,bytes.data()+size_t(y)*rgba->w*4);
        if(capture){
            motion_.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
            motionInfo_<<motionWritten_++<<','<<serial<<','<<rgba->w<<','<<rgba->h<<','<<unsigned(cadence.kind)<<','<<cadence.referenceHz<<','<<step.divisions<<','<<step.numerator<<'\n';
            if(!motion_)throw std::runtime_error("Motion capture write failed");
        }
        SDL_DestroySurface(rgba);if(!temporal_.is_open())return;double energy=0,delta=0;
        bool comparable=previousScene_==unsigned(cadence.kind)&&previousPixels_.size()==bytes.size();
        for(size_t i=0;i<bytes.size();++i)if(i%4!=3){energy+=bytes[i];if(comparable)delta+=std::abs(int(bytes[i])-int(previousPixels_[i]));}
        double count=bytes.size()*0.75;
        temporal_<<serial<<','<<unsigned(cadence.kind)<<','<<step.index<<','<<step.divisions<<','<<step.numerator<<','<<energy/count<<','<<delta/count<<'\n';
        previousScene_=unsigned(cadence.kind);previousPixels_=std::move(bytes);
    }
    void picture(uint64_t serial,bool gameplay,const t3knative::Picture& pic,const NativeFrameMemory& memory,SimulationStep step={},SceneCadence cadence={}){
        if(!capturePrefix_.empty()&&serial==captureFrame_){
            std::filesystem::path prefix(capturePrefix_);if(!prefix.parent_path().empty())std::filesystem::create_directories(prefix.parent_path());
            const char* names[]{"system","main","local"};unsigned index=0;
            for(auto bank:{NativeFrameMemory::system,NativeFrameMemory::main,NativeFrameMemory::rendererLocal}){
                auto bytes=memory.bank(bank);std::ofstream output(capturePrefix_+"-"+names[index++]+".bin",std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!output)throw std::runtime_error("Cannot save rendering coverage capture");
            }
            std::ofstream sources(capturePrefix_+"-sources.csv");sources<<"draw,object,template,parameters,globals,content\n";
            const auto view=memory.renderer_view();if(view.drawSources)for(const auto& source:*view.drawSources)sources<<source.draw<<','<<source.object<<','<<source.source<<','<<source.parameters<<','<<source.globals<<','<<source.content<<'\n';
            if(!sources)throw std::runtime_error("Cannot save rendering source provenance");
        }
        if(!frames_.is_open())return;
        auto s=memory.bank(NativeFrameMemory::system),r=memory.bank(NativeFrameMemory::rendererLocal);
        frames_<<serial<<','<<gameplay<<','<<word(s,0x806B991C)<<','<<word(s,0x80110AA0)<<','<<word(r,0x100000)<<','<<word(s,0x806B9924)<<','<<word(s,0x806B9930)<<','<<word(s,0x800DC100);
        for(unsigned i=0;i<3;++i)frames_<<','<<word(s,0x800DAD80+i*4);
        frames_<<','<<word(s,0x800DC250)<<','<<word(s,0x800DC274)<<','<<pic.ops.size()<<','<<pic.segs.size()<<','<<pic.blobs.size()<<','<<pic.webs.size()<<','<<step.divisions<<','<<step.index<<','<<word(s,0x800DC240)<<','<<word(s,0x800BAC7C)<<','<<unsigned(cadence.kind)<<','<<cadence.referenceHz<<','<<(cadence.referenceHz?cadence.sample_hz():step.divisions>1?120:0)<<','<<word(s,0x800B6A94)<<','<<word(s,0x800B77CC)<<','<<step.numerator<<'\n';
        uint32_t object=word(s,0x800BA470);unsigned visited=0;
        while(object){
            if(++visited>65536)throw std::runtime_error("Timing trace object list exceeded its bound");
            uint32_t descriptor,pointer,next;
            if(!peek(memory,object+8,descriptor)||!peek(memory,object+12,pointer)||!peek(memory,object+4,next))throw std::runtime_error("Timing trace encountered invalid object memory");
            uint32_t base=(pointer?pointer:object+0x40)&~15U,count=(descriptor>>16)&255;
            for(uint32_t i=0;i<count;++i){uint32_t v[4];for(unsigned k=0;k<4;++k)if(!peek(memory,base+i*16+k*4,v[k]))throw std::runtime_error("Timing trace encountered invalid value memory");
                if(v[3]&0x80000000U)values_<<serial<<','<<object<<','<<descriptor<<','<<i<<','<<v[0]<<','<<v[1]<<','<<v[2]<<','<<v[3]<<'\n';
            }
            object=next;
        }
        for(size_t i=0;i<pic.ops.size();++i){const auto& op=pic.ops[i];
            if(op.kind==t3knative::Op::kZoom||op.kind==t3knative::Op::kRipple||op.kind==t3knative::Op::kWeb||op.kind==t3knative::Op::kBlendRect||op.kind==t3knative::Op::kBlobs)
                effects_<<serial<<','<<i<<','<<op.type<<','<<unsigned(op.kind)<<','<<op.fade<<','<<op.zx<<','<<op.zy<<','<<op.angle<<','<<op.count<<'\n';
        }
        // Diagnostic evidence must fail visibly rather than accept a partial
        // file as a valid timing trace (for example on an interrupted write).
        if((serial&255)==255){frames_.flush();effects_.flush();values_.flush();}
        if(!frames_||!effects_||!values_)throw std::runtime_error("Cannot write native timing trace");
    }
};
}
