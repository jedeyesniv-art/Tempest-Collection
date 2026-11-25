#include "native_preparation_backend.h"
#include "native_frame_platform.h"
#include "native_audio_backend.h"
#include "native_audio_stream.h"
#include "game/TempestSound.h"
#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>
#include <memory>
using namespace tempest_native;
struct CaptureSound{uint32_t cue,handle;SoundRequest request;};
struct Binary{
    FILE* file;bool good=true;
    explicit Binary(const std::filesystem::path& p):file(std::fopen(p.string().c_str(),"rb")){good=file!=nullptr;}
    ~Binary(){if(file)std::fclose(file);}
    void bytes(void* p,size_t n){if(good)good=std::fread(p,1,n,file)==n;}
    template<class T>void value(T& v){bytes(&v,sizeof(v));}
    template<class T>void list(std::vector<T>& v,unsigned max=65536){uint32_t count=0;value(count);if(count>max){good=false;return;}v.resize(count);if(count)bytes(v.data(),sizeof(T)*count);}
};
static std::vector<uint8_t> load(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
static bool deltas(Binary& file,NativeFrameMemory& m){uint32_t count=0;file.value(count);if(count>0x400000)return false;for(unsigned i=0;i<count&&file.good;++i){uint32_t p[2]{};file.bytes(p,8);if(!m.write32(p[0],p[1]))return false;}return file.good;}
static uint32_t word(std::span<const uint8_t> b,unsigned off){return uint32_t(b[off])<<24|uint32_t(b[off+1])<<16|uint32_t(b[off+2])<<8|b[off+3];}
struct PlatformInput{
    bool available=false,clock=false,exact=false;std::array<uint32_t,50> context{};uint32_t exitPC=0;std::vector<std::array<uint32_t,2>> stacks;std::array<uint64_t,6> blocks{};std::vector<std::pair<uint32_t,AudioClockState>> events;
    bool read(const std::filesystem::path& path){if(!std::filesystem::exists(path))return true;Binary input(path);uint32_t magic=0;input.value(magic);if(magic<0x504c3031||magic>0x504c3033)return false;input.value(context);input.value(exitPC);input.list(stacks,1024);if(magic>=0x504c3032){clock=true;exact=magic==0x504c3033;input.bytes(blocks.data(),(exact?6:4)*8);uint32_t count=0;input.value(count);if(count>4096)return false;for(unsigned i=0;i<count;++i){uint32_t kind;AudioClockState block;input.value(kind);input.value(block.cores);if(exact){input.value(block.outputs);input.value(block.voices);}else block.outputs=block.cores;events.push_back({kind,block});}}
        for(auto range:stacks)if(range[0]>range[1]||uint64_t(range[0])+4096<range[1])return false;available=input.good;return input.good;
    }
};
struct TraceAudioClock:NativeAudioClock{
    const PlatformInput& input;size_t cursor=0,before=0;explicit TraceAudioClock(const PlatformInput& value):input(value){}
    bool begin_frame()override{before=cursor;return true;}void abort_frame()override{cursor=before;}
    bool target_blocks(AudioClockEvent kind,AudioClockState& blocks)override{if(cursor>=input.events.size()||uint32_t(kind)!=input.events[cursor].first){std::fprintf(stderr," audio clock event differs at %zu expected-kind=%u actual-kind=%u\n",cursor,cursor<input.events.size()?input.events[cursor].first:~0U,uint32_t(kind));return false;}blocks=input.events[cursor++].second;return true;}
};
struct ObservedCompute:FrameComputeBackend{
    NativePreparationBackend backend;const std::vector<FrameWorkerRequest>& requests;size_t cursor=0,before=0;bool good=true;
    ObservedCompute(NativeFrameMemory& m,const std::vector<FrameWorkerRequest>& r):backend(m),requests(r){}
    bool initialize(const std::array<uint32_t,48>& v)override{before=cursor;return backend.initialize(v);}
    bool submit(const FrameWorkerRequest& v)override{if(cursor>=requests.size()||v.type!=requests[cursor].type||v.context!=requests[cursor].context){good=false;return false;}++cursor;return backend.submit(v);}
    bool complete()override{return backend.complete();}
    void commit_frame()override{backend.commit_frame();}
    void abort_frame()override{backend.abort_frame();cursor=before;}
};
// Observe requests and independently check results; allocation and all memory
// effects execute in the production native backend. Captured handles are never
// supplied to gameplay as substitutes for an unimplemented audio service.
struct ObservedAudio:FrameAudioBackend{
    NativeFrameMemory& memory;NativeAudioBackend backend;NativeAudioStreamQueue stream;const std::vector<CaptureSound>& sounds;size_t cursor=0,before=0;bool good=true;
    ObservedAudio(NativeFrameMemory& m,const std::vector<CaptureSound>& s):memory(m),backend(m),sounds(s){backend.set_stream(&stream);}
    bool begin_frame()override{before=cursor;return backend.begin_frame();}void commit_frame()override{backend.commit_frame();}void abort_frame()override{backend.abort_frame();cursor=before;}
    bool play_sound(uint32_t cue,uint32_t& handle)override{
        SoundRequest request{};
        if(cursor>=sounds.size()||sounds[cursor].cue!=cue||!prepare_sound_request(memory,cue,request)||std::memcmp(&request,&sounds[cursor].request,sizeof(request))){good=false;return false;}
        if(!backend.play_sound(cue,handle)||handle!=sounds[cursor].handle){std::fprintf(stderr," native audio cue=%08X handle=%u reference=%u\n",cue,handle,sounds[cursor].handle);good=false;return false;}++cursor;return true;
    }
};
struct Checkpoint:NativeFrameCheckpoint{
    const NativeFrameMemory& expected;NativePreparationBackend& compute;std::span<const uint8_t> worker;
    bool called=false,good=true,rejectOnce=true;uint64_t differences=0,privateStackWords=0;std::map<uint32_t,unsigned> pages;const PlatformInput& platform;
    Checkpoint(const NativeFrameMemory& e,NativePreparationBackend& c,std::span<const uint8_t> w,const PlatformInput& p):expected(e),compute(c),worker(w),platform(p){}
    bool prepared(const NativeFrameMemory& actual,const GameFrameResult&,std::string& error)override{
        called=true;good=true;differences=privateStackWords=0;pages.clear();
        for(unsigned bank=0;bank<2;++bank){auto a=actual.bank(NativeFrameMemory::Bank(bank)),b=expected.bank(NativeFrameMemory::Bank(bank));uint32_t base=bank?0x40000000:0x80000000;
            for(unsigned off=0;off<a.size();off+=4)if(word(a,off)!=word(b,off)){
                uint32_t address=base+off;bool privateStack=false;for(auto range:platform.stacks)if(address>=range[0]&&uint64_t(address)+4<=range[1])privateStack=true;
                // Original C ABI spill bytes are not native game state. This
                // exception requires a measured SDK call-stack range and proof
                // that native gameplay neither read nor wrote the word.
                if(privateStack&&!actual.accessed(address,4)){++privateStackWords;continue;}
                if(differences<24)std::fprintf(stderr," whole word %08X native=%08X expected=%08X\n",base+off,word(a,off),word(b,off));
                ++differences;++pages[(base+off)&~0xFFFU];good=false;
            }
        }
        auto a=actual.bank(NativeFrameMemory::gameLocal),b=expected.bank(NativeFrameMemory::gameLocal);
        for(uint32_t off:actual.written_offsets(NativeFrameMemory::gameLocal))if(word(a,off)!=word(b,off)){good=false;++differences;if(differences<32)std::fprintf(stderr," whole local %08X native=%08X expected=%08X\n",0x20000000+off,word(a,off),word(b,off));}
        auto data=compute.local_data();
        for(unsigned off=0;off<0x6F0;off+=4){bool check=off==12||off==32||(off>=0xF0&&off<0x2F0&&(word(data,12)&1))||(off>=0x2F0&&(word(data,12)&2));
            if(check&&word(data,off)!=word(worker,off)){good=false;++differences;if(differences<32)std::fprintf(stderr," whole worker %08X native=%08X expected=%08X\n",0x20100000+off,word(data,off),word(worker,off));}}
        // Continue decoding even on a mismatch so diagnostics can distinguish
        // a protocol difference from different gameplay drawing records.
        if(rejectOnce){rejectOnce=false;error="injected rejection after complete preparation and platform settlement";return false;}return true;
    }
};
struct ObservedPlatform:FramePlatformBackend{
    NativeFramePlatform base;NativeAudioBackend& audio;const PlatformInput& input;
    ObservedPlatform(NativeDiscImage& disc,const std::vector<NativeMediaDelivery>& d,uint64_t fields,NativeAudioBackend& a,const PlatformInput& p):base(disc,d,fields),audio(a),input(p){}
    bool settle(NativeFrameMemory& memory,std::string& error)override{if(input.clock&&!audio.advance_to({input.blocks[1],input.blocks[3],input.exact?input.blocks[5]:~uint64_t(0)})){error="native audio could not reach the observed sample boundary: "+audio.failure();return false;}return base.settle(memory,error);}
    void commit_frame()override{base.commit_frame();}void abort_frame()override{base.abort_frame();}
    uint64_t delivered_bytes()const{return base.delivered_bytes();}
};
struct Hash{
    uint64_t value=14695981039346656037ULL;
    void add(uint32_t v){for(unsigned i=0;i<4;++i){value^=uint8_t(v>>(i*8));value*=1099511628211ULL;}}
    void add(float v){add(std::bit_cast<uint32_t>(v));}
    void colour(const SDL_FColor& c){add(c.r);add(c.g);add(c.b);add(c.a);}
    template<class T>void size(const std::vector<T>& v){add(uint32_t(v.size()));}
    void vertices(const std::vector<t3knative::Web::Vertex>& v){size(v);for(auto a:v){add(a.x);add(a.y);add(a.u);add(a.v);}}
};
static uint64_t picture_hash(const t3knative::Picture& p){
    Hash h;h.add(p.flip);h.add(uint32_t(p.complete));h.add(p.first_unknown);for(auto v:p.unknown_entry)h.add(v);
    h.size(p.segs);for(auto v:p.segs){h.add(v.x0);h.add(v.y0);h.add(v.x1);h.add(v.y1);h.colour(v.c0);h.colour(v.c1);h.add(v.r);}
    h.size(p.items);for(auto v:p.items){h.add(v.shape);h.add(v.x);h.add(v.y);h.add(v.first);h.add(v.count);}
    h.size(p.ops);for(auto v:p.ops){h.add(uint32_t(v.kind));h.add(v.x);h.add(v.y);h.add(v.w);h.add(v.h);h.colour(v.colour);h.add(v.cx);h.add(v.cy);h.add(v.zx);h.add(v.zy);h.add(v.angle);h.add(v.fade);h.add(v.first);h.add(v.count);}
    h.size(p.webs);for(const auto& v:p.webs){h.vertices(v.quads);for(auto t:v.texture)h.add(t);}
    h.size(p.glyphs);for(const auto& v:p.glyphs){h.add(v.x);h.add(v.y);h.add(v.w);h.add(v.h);h.add(uint32_t(v.pw));h.add(uint32_t(v.ph));h.size(v.rgba);for(auto t:v.rgba)h.add(t);}
    h.size(p.blobs);for(auto v:p.blobs){h.add(v.x);h.add(v.y);h.add(v.r);h.colour(v.colour);}
    h.size(p.planes);for(const auto& v:p.planes){h.add(uint32_t(v.cols));h.add(uint32_t(v.rows));h.vertices(v.grid);for(auto t:v.texture)h.add(t);}
    return h.value;
}
static void picture_difference(const t3knative::Picture& a,const t3knative::Picture& b){
    std::fprintf(stderr," picture sizes ops=%zu/%zu segs=%zu/%zu items=%zu/%zu webs=%zu/%zu glyphs=%zu/%zu blobs=%zu/%zu planes=%zu/%zu\n",a.ops.size(),b.ops.size(),a.segs.size(),b.segs.size(),a.items.size(),b.items.size(),a.webs.size(),b.webs.size(),a.glyphs.size(),b.glyphs.size(),a.blobs.size(),b.blobs.size(),a.planes.size(),b.planes.size());
    for(unsigned i=0;i<a.ops.size()&&i<b.ops.size();++i){t3knative::Picture x,y;x.ops={a.ops[i]};y.ops={b.ops[i]};if(picture_hash(x)!=picture_hash(y)){auto p=a.ops[i],q=b.ops[i];std::fprintf(stderr," op %u kind=%u/%u first=%u/%u count=%u/%u fade=%g/%g\n",i,p.kind,q.kind,p.first,q.first,p.count,q.count,p.fade,q.fade);break;}}
    for(unsigned i=0;i<a.items.size()&&i<b.items.size();++i){t3knative::Picture x,y;x.items={a.items[i]};y.items={b.items[i]};if(picture_hash(x)!=picture_hash(y)){auto p=a.items[i],q=b.items[i];std::fprintf(stderr," item %u shape=%08X/%08X first=%u/%u count=%u/%u xy=%g,%g/%g,%g\n",i,p.shape,q.shape,p.first,q.first,p.count,q.count,p.x,p.y,q.x,q.y);break;}}
    for(unsigned i=0;i<a.segs.size()&&i<b.segs.size();++i){t3knative::Picture x,y;x.segs={a.segs[i]};y.segs={b.segs[i]};if(picture_hash(x)!=picture_hash(y)){auto p=a.segs[i],q=b.segs[i];std::fprintf(stderr," segment %u xy=%g,%g,%g,%g/%g,%g,%g,%g radius=%g/%g\n",i,p.x0,p.y0,p.x1,p.y1,q.x0,q.y0,q.x1,q.y1,p.r,q.r);break;}}
    for(unsigned i=0;i<a.webs.size()&&i<b.webs.size();++i){t3knative::Picture x,y;x.webs={a.webs[i]};y.webs={b.webs[i]};if(picture_hash(x)!=picture_hash(y)){std::fprintf(stderr," web %u differs\n",i);break;}}
}
static std::vector<uint8_t> gpu_pixels(SDL_Renderer* renderer,const std::filesystem::path& save={}){
    SDL_Surface* surface=SDL_RenderReadPixels(renderer,nullptr);if(!surface)return {};std::vector<uint8_t> bytes(size_t(surface->pitch)*surface->h);std::memcpy(bytes.data(),surface->pixels,bytes.size());if(!save.empty())SDL_SaveBMP(surface,save.string().c_str());SDL_DestroySurface(surface);return bytes;
}
int main(int argc,char** argv){
    if(argc!=3&&(argc!=4||std::string(argv[3])!="--gpu"))return 2;bool gpu=argc==4;NativeDiscImage disc(argv[2]);std::filesystem::path folder=argv[1];auto system=load(folder/"base-system.bin"),main=load(folder/"base-main.bin");if(system.size()!=0x800000||main.size()!=0x800000)return 2;
    if(!SDL_Init(SDL_INIT_VIDEO))return 2;SDL_Window* window=SDL_CreateWindow("Native whole-frame validation",720,480,SDL_WINDOW_HIDDEN);SDL_Renderer* renderer=window?t3knative::CreateRenderer(window):nullptr;if(!renderer)return 2;
    auto pipeline=std::make_unique<NativeFrameRenderer>(),referencePipeline=std::make_unique<NativeFrameRenderer>();std::string error;if(!pipeline->initialize(renderer,error)||(gpu&&!referencePipeline->initialize(renderer,error))){std::fprintf(stderr,"%s\n",error.c_str());return 2;}
    std::vector<std::filesystem::path> cases;for(const auto& e:std::filesystem::directory_iterator(folder)){auto name=e.path().filename().string();if(name.starts_with("frame-")&&!name.ends_with("-platform.bin")&&e.path().extension()==".bin")cases.push_back(e.path());}std::sort(cases.begin(),cases.end());
    unsigned failures=0,completed=0,rollbacks=0,gpuChecks=0,framebufferChecks=0,readOnlyChecks=0;
    for(const auto& path:cases){
        Binary file(path);uint32_t magic=0,index=0;uint64_t field=0;GameFrameInput input{};file.value(magic);file.value(index);file.value(field);file.value(input);if(magic!=0x57463032)return 2;
        std::array<std::array<uint8_t,0x2000>,3> local{};file.bytes(local.data(),sizeof(local));
        NativeFrameMemory actual,expected;actual.load(NativeFrameMemory::system,system);actual.load(NativeFrameMemory::main,main);
        if(!deltas(file,actual))return 2;
        expected.load(NativeFrameMemory::system,actual.bank(NativeFrameMemory::system));expected.load(NativeFrameMemory::main,actual.bank(NativeFrameMemory::main));
        for(unsigned k:{0U,2U}){std::vector<uint8_t> bank(0x800000);std::copy(local[k].begin(),local[k].end(),bank.begin()+0x100000);actual.load(k==0?NativeFrameMemory::gameLocal:NativeFrameMemory::rendererLocal,bank);expected.load(k==0?NativeFrameMemory::gameLocal:NativeFrameMemory::rendererLocal,bank);}
        if(!deltas(file,expected))return 2;std::array<std::array<uint8_t,0x2000>,2> afterLocal{};file.bytes(afterLocal.data(),sizeof(afterLocal));uint64_t endField=0;file.value(endField);
        std::vector<uint8_t> afterGame(0x800000);std::copy(afterLocal[0].begin(),afterLocal[0].end(),afterGame.begin()+0x100000);expected.load(NativeFrameMemory::gameLocal,afterGame);
        std::vector<FrameWorkerRequest> requests;std::vector<CaptureSound> sounds;file.list(requests);file.list(sounds);
        uint32_t deliveryCount=0;file.value(deliveryCount);if(deliveryCount>65536)return 2;std::vector<NativeMediaDelivery> deliveries(deliveryCount);
        for(auto& d:deliveries){file.value(d.engine);file.value(d.address);file.value(d.bytes);file.value(d.block);file.value(d.hash);uint32_t length=0;file.value(length);if(length>4096)return 2;d.path.resize(length);file.bytes(d.path.data(),length);}if(!file.good||endField<field)return 2;
        PlatformInput platformInput;if(!platformInput.read(path.parent_path()/(path.stem().string()+"-platform.bin")))return 2;TraceAudioClock audioClock(platformInput);
        ObservedCompute compute(actual,requests);ObservedAudio audio(actual,sounds);Checkpoint checkpoint(expected,compute.backend,afterLocal[1],platformInput);ObservedPlatform platform(disc,deliveries,endField-field,audio.backend,platformInput);NativeFrameIntegration integration(compute,audio,&checkpoint,&platform);
        if(!compute.backend.restore_worker_data(local[1]))return 2;
        if(platformInput.clock){if(!audio.backend.restore_processor_checkpoint(local[2],platformInput.context[0],platformInput.context[41],{platformInput.blocks[0],platformInput.blocks[2],platformInput.exact?platformInput.blocks[4]:~uint64_t(0)})||!audio.backend.set_clock(&audioClock))return 2;}
        else if(!audio.backend.restore_processor_data(local[2]))return 2;
        std::array<std::vector<uint8_t>,4> beforeMemory;for(unsigned bank=0;bank<4;++bank){auto bytes=actual.bank(NativeFrameMemory::Bank(bank));beforeMemory[bank].assign(bytes.begin(),bytes.end());}
        std::vector<uint8_t> beforeAudio(audio.backend.processor_data().begin(),audio.backend.processor_data().end());auto beforeClock=audio.backend.clock_state();
        actual.track_accesses();
        bool rejected=!integration.step(actual,input,uint64_t(index)+1,error)&&!integration.take()&&checkpoint.called&&checkpoint.good;
        for(unsigned bank=0;bank<4;++bank)rejected=rejected&&std::equal(beforeMemory[bank].begin(),beforeMemory[bank].end(),actual.bank(NativeFrameMemory::Bank(bank)).begin());
        auto afterClock=audio.backend.clock_state();rejected=rejected&&std::equal(beforeAudio.begin(),beforeAudio.end(),audio.backend.processor_data().begin())&&std::equal(local[1].begin(),local[1].end(),compute.backend.local_data().begin())&&beforeClock.cores==afterClock.cores&&beforeClock.outputs==afterClock.outputs&&beforeClock.voices==afterClock.voices&&audio.backend.take_audio().empty()&&audio.stream.take_reads().empty();if(rejected)++rollbacks;
        actual.track_accesses();
        bool step=integration.step(actual,input,uint64_t(index)+1,error);auto result=integration.take();
        auto nativeReads=audio.stream.take_reads();bool readRequestsOK=true;uint64_t linkedBytes=0;
        char requestName[64];std::snprintf(requestName,sizeof(requestName),"media-requests-%06u.bin",index);auto requestPath=folder/requestName;
        if(std::filesystem::exists(requestPath)){Binary requestsFile(requestPath);std::vector<std::array<uint32_t,7>> expectedRequests;requestsFile.list(expectedRequests,4096);if(!requestsFile.good||expectedRequests.size()!=nativeReads.size())readRequestsOK=false;
            if(expectedRequests.size()==nativeReads.size())for(unsigned i=0;i<nativeReads.size();++i){const auto& e=expectedRequests[i];const auto& n=nativeReads[i];if(e[0]!=3||std::memcmp(e.data()+1,&n,sizeof(n)))readRequestsOK=false;}
            for(const auto& d:deliveries)for(const auto& r:nativeReads)if(d.engine==3&&d.address>=r.destination&&uint64_t(d.address)+d.bytes<=uint64_t(r.destination)+uint64_t(r.blocks)*2048&&d.block==r.sector+(d.address-r.destination)/2048){linkedBytes+=d.bytes;break;}
        }
        uint32_t referenceClock=0;expected.read32(0x80110AA0,referenceClock);expected.renderer_word(0x100000,referenceClock);t3knative::Picture reference;std::string referenceError;
        bool referenceOK=build_renderer_picture(expected.renderer_view(),referenceClock,reference,referenceError);
        bool pictures=referenceOK&&result&&picture_hash(result->picture)==picture_hash(reference);
        if(referenceOK&&result&&!pictures)picture_difference(result->picture,reference);
        bool independent=result&&!actual.accessed(0x40010000,0xFD200),readOnly=result.has_value();
        if(result){auto view=actual.renderer_view();std::vector<uint8_t> originalSystem(view.system.begin(),view.system.end()),originalLocal(view.local.begin(),view.local.end()),poisonedMain(view.main.begin(),view.main.end());t3knative::Picture repeated;std::string auditError;
            readOnly=build_renderer_picture(view,result->picture.flip,repeated,auditError)&&picture_hash(repeated)==picture_hash(result->picture)&&std::equal(originalSystem.begin(),originalSystem.end(),view.system.begin())&&std::equal(originalLocal.begin(),originalLocal.end(),view.local.begin())&&std::equal(poisonedMain.begin(),poisonedMain.end(),view.main.begin());
            std::fill(poisonedMain.begin()+0x10000,poisonedMain.begin()+0x10d200,0xa5);auto poisonBefore=poisonedMain;view.main=poisonedMain;t3knative::Picture withoutFramebuffers;
            independent=independent&&build_renderer_picture(view,result->picture.flip,withoutFramebuffers,auditError)&&picture_hash(withoutFramebuffers)==picture_hash(result->picture);
            readOnly=readOnly&&poisonedMain==poisonBefore&&std::equal(originalSystem.begin(),originalSystem.end(),view.system.begin())&&std::equal(originalLocal.begin(),originalLocal.end(),view.local.begin());
        }
        if(independent)++framebufferChecks;
        if(readOnly)++readOnlyChecks;
        bool gpuOK=true;if(gpu&&result&&referenceOK){SDL_FRect destination{0,0,720,480};SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);gpuOK=pipeline->submit_picture(renderer,result->picture,uint64_t(index)+1,destination,error);auto nativePixels=gpu_pixels(renderer,folder/"native-last.bmp");SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);gpuOK=gpuOK&&referencePipeline->submit_picture(renderer,reference,uint64_t(index)+1,destination,error);auto expectedPixels=gpu_pixels(renderer);gpuOK=gpuOK&&!nativePixels.empty()&&nativePixels==expectedPixels;if(gpuOK)++gpuChecks;}
        bool ok=rejected&&step&&result&&checkpoint.called&&checkpoint.good&&compute.good&&audio.good&&compute.cursor==requests.size()&&audio.cursor==sounds.size()&&pictures&&gpuOK&&independent&&readOnly&&readRequestsOK&&(!platformInput.clock||audioClock.cursor==platformInput.events.size());
        if(!ok){++failures;std::fprintf(stderr,"whole frame index=%u field=%llu step=%u checkpoint=%u differences=%llu jobs=%zu/%zu sounds=%zu/%zu picture=%u rollback=%u gpu=%u framebuffer=%u read-only=%u read-requests=%u error=%s ref=%s\n",index,field,step,checkpoint.called,checkpoint.differences,compute.cursor,requests.size(),audio.cursor,sounds.size(),pictures,rejected,gpuOK,independent,readOnly,readRequestsOK,error.c_str(),referenceError.c_str());unsigned shown=0;for(auto [page,count]:checkpoint.pages){std::fprintf(stderr," page %08X: %u differing words\n",page,count);if(++shown==12)break;}}
        else std::printf("whole frame %u: %zu workers, %zu sound requests, %zu ops, %zu segments, %llu verified disc bytes, %zu native stream reads (%llu linked arrival bytes), %llu disjoint SDK-private stack words; matched\n",index,requests.size(),sounds.size(),result->picture.ops.size(),result->picture.segs.size(),platform.delivered_bytes(),nativeReads.size(),linkedBytes,checkpoint.privateStackWords);
        ++completed;
    }
    pipeline.reset();referencePipeline.reset();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();std::printf("native whole-frame replay: %u cases, %u mismatches, %u rollback checks, %u GPU comparisons, %u framebuffer-independence checks, %u read-only decoder checks\n",completed,failures,rollbacks,gpuChecks,framebufferChecks,readOnlyChecks);return !completed||failures?1:0;
}
