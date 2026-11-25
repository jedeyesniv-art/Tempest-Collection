#pragma once
#include "native_state_archive.h"
#include "native_collection_driver.h"
#include "native_live_host.h"
#include <bit>
#include <type_traits>
namespace tempest_native {
struct StateEncode {
    static constexpr bool reading=false;StateWriter& stream;
    void operator()(){}
    template<class T,class... Rest> void operator()(T& value,Rest&... rest){
        if constexpr(std::is_same_v<T,bool>)stream.flag(value);
        else if constexpr(std::is_enum_v<T>)stream.u32(uint32_t(value));
        else if constexpr(std::is_floating_point_v<T>){if constexpr(sizeof(T)==8)stream.u64(std::bit_cast<uint64_t>(value));else stream.u32(std::bit_cast<uint32_t>(value));}
        else if constexpr(std::is_integral_v<T>){if constexpr(sizeof(T)>4)stream.u64(uint64_t(value));else stream.u32(uint32_t(value));}
        else state_fields(*this,value);
        (*this)(rest...);
    }
};
struct StateDecode {
    static constexpr bool reading=true;StateReader& stream;
    void operator()(){}
    template<class T,class... Rest> void operator()(T& value,Rest&... rest){
        if constexpr(std::is_same_v<T,bool>)value=stream.flag();
        else if constexpr(std::is_enum_v<T>)value=T(stream.u32());
        else if constexpr(std::is_floating_point_v<T>){if constexpr(sizeof(T)==8)value=std::bit_cast<T>(stream.u64());else value=std::bit_cast<T>(stream.u32());}
        else if constexpr(std::is_integral_v<T>){if constexpr(sizeof(T)>4)value=T(stream.u64());else value=T(stream.u32());}
        else state_fields(*this,value);
        (*this)(rest...);
    }
};
template<class A,class T,size_t N> void state_fields(A& a,std::array<T,N>& v){for(auto& item:v)a(item);}
template<class A> void state_fields(A& a,std::vector<uint8_t>& v){if constexpr(A::reading)a.stream.data(v,256*1024*1024);else a.stream.data(v);}
template<class A> void state_fields(A& a,std::string& v){if constexpr(A::reading)a.stream.text(v);else a.stream.text(v);}
template<class A,class Container> void state_sequence(A& a,Container& v){
    uint32_t count=uint32_t(v.size());a(count);
    if constexpr(A::reading){if(!a.stream.valid||count>1048576||count>a.stream.bytes.size()-a.stream.at){a.stream.valid=false;return;}v.resize(count);}
    for(auto& item:v){a(item);if constexpr(A::reading)if(!a.stream.valid)return;}
}
template<class A,class T> void state_fields(A& a,std::vector<T>& v){state_sequence(a,v);}
template<class A,class T> void state_fields(A& a,std::deque<T>& v){state_sequence(a,v);}
template<class A> void state_fields(A& a,AudioClockState& v){a(v.cores,v.outputs,v.voices);}
template<class A> void state_fields(A& a,NativeAudioBackend::Checkpoint& v){a(v.local,v.time,v.paused,v.continuationSafe,v.ready);}
template<class A> void state_fields(A& a,NiseStreamRead& v){a(v.handle,v.mode,v.sector,v.blocks,v.destination,v.callback);}
template<class A> void state_fields(A& a,NativeMedia::Job& v){
    a(v.request,v.delivered,v.error);
    if constexpr(A::reading){auto data=std::make_shared<std::vector<uint8_t>>();a(*data);v.data=std::move(data);}
    else{auto data=*v.data;a(data);}
}
template<class A> void state_fields(A& a,NativeMedia::Checkpoint& v){a(v.handles,v.jobs);}
template<class A> void state_fields(A& a,NativeControllerPort& v){a(v.words);}
template<class A> void state_fields(A& a,NativeMouseSpinner::Checkpoint& v){a(v.position,v.pending,v.fraction);}
template<class A> void state_fields(A& a,NativeLiveHost::Checkpoint& v){a(v.frames,v.elapsed,v.record,v.mouse,v.pending,v.mouseDelta,v.sampled,v.mouseMode,v.reseed,v.naming,v.rate,v.speedFrom,v.speedTo,v.blendElapsed,v.blendDuration);}
template<class A> void state_fields(A& a,NativeMenuFlow::Checkpoint& v){a(v.serial,v.jobs,v.callbacks,v.incoming,v.preparedContext,v.prepared,v.gameplay);}
template<class A> void state_fields(A& a,SessionStartRequest& v){a(v.arguments,v.incoming);}
template<class A> void state_fields(A& a,GameSessionState& v){a(v.context,v.frameBase,v.phase);}
template<class A> void state_fields(A& a,CollectionDriverState& v){a(v.phase,v.c,v.mainState,v.sessionResult,v.firstOpening,v.request,v.session);}
template<class A> void state_fields(A& a,NativeServiceEvent& v){a(v.kind,v.words,v.data);}
template<class A> void state_fields(A& a,t3knative::FeedbackState& v){a(v.width,v.height,v.format,v.pixels);}
template<class A> void state_fields(A& a,SDL_FColor& v){a(v.r,v.g,v.b,v.a);}
template<class A> void state_fields(A& a,t3knative::Seg& v){a(v.x0,v.y0,v.x1,v.y1,v.c0,v.c1,v.r);}
template<class A> void state_fields(A& a,t3knative::Item& v){a(v.shape,v.x,v.y,v.first,v.count);}
template<class A> void state_fields(A& a,t3knative::Web::Vertex& v){a(v.x,v.y,v.u,v.v);}
template<class A> void state_fields(A& a,t3knative::Web& v){a(v.quads);for(auto& word:v.texture)a(word);}
template<class A> void state_fields(A& a,t3knative::Glyph& v){a(v.x,v.y,v.w,v.h,v.pw,v.ph,v.rgba);}
template<class A> void state_fields(A& a,t3knative::Blob& v){a(v.x,v.y,v.r,v.colour);}
template<class A> void state_fields(A& a,t3knative::Plane& v){a(v.cols,v.rows,v.grid);for(auto& word:v.texture)a(word);}
template<class A> void state_fields(A& a,t3knative::Op& v){a(v.kind,v.x,v.y,v.w,v.h,v.colour,v.cx,v.cy,v.zx,v.zy,v.angle,v.fade,v.first,v.count,v.type);}
template<class A> void state_fields(A& a,t3knative::Picture& v){a(v.flip,v.segs,v.items,v.ops,v.webs,v.glyphs,v.blobs,v.planes,v.complete,v.first_unknown);for(auto& word:v.unknown_entry)a(word);}
template<class A> void state_fields(A& a,NativeFrameRenderer::PresentationState& v){a(v.previous,v.current,v.feedback,v.serial,v.valid,v.interpolate,v.pending,v.alpha);}
struct NativeContinuation {
    NativeFrameMemory::Snapshot memory;
    std::array<uint8_t,8192> preparation{};
    NativeAudioBackend::Checkpoint audio;
    NativeMedia::Checkpoint media;
    NativeAudioStreamQueue::Checkpoint stream;
    NativeMenuFlow::Checkpoint flow;
    NativeLiveHost::Checkpoint host,currentHost;
    CollectionDriverState driver;
    std::array<uint32_t,20> callerLast{};
    uint32_t sessions=0;
    std::vector<NativeServiceEvent> events;
    NativeFrameRenderer::PresentationState presentation;
    uint64_t expectedMemory=0,expectedServices=0;
};
template<class A> void state_fields(A& a,NativeContinuation& v){a(v.memory,v.preparation,v.audio,v.media,v.stream,v.flow,v.host,v.currentHost,v.driver,v.callerLast,v.sessions,v.events,v.presentation,v.expectedMemory,v.expectedServices);}
inline uint64_t native_memory_checksum(const NativeFrameMemory& memory){
    uint64_t hash=14695981039346656037ULL;
    for(unsigned i=0;i<4;++i)for(auto byte:memory.bank(NativeFrameMemory::Bank(i))){hash^=byte;hash*=1099511628211ULL;}return hash;
}
inline uint64_t native_services_checksum(NativeAudioBackend& audio,NativePreparationBackend& compute,NativeMedia& media,NativeAudioStreamQueue& stream,NativeMenuFlow& flow){
    NativeAudioBackend::Checkpoint sound;NativeMedia::Checkpoint disc;NativeAudioStreamQueue::Checkpoint reads;NativeMenuFlow::Checkpoint driver;
    if(!audio.checkpoint(sound)||!media.checkpoint(disc)||!stream.checkpoint(reads)||!flow.checkpoint(driver))return 0;
    StateWriter writer;StateEncode encode{writer};encode(sound,disc,reads,driver);writer.data(compute.local_data());return state_checksum(writer.bytes);
}
struct NativeLoad{NativeContinuation saved;};
}
