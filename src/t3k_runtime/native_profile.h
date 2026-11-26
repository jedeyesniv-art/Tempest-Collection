#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cstdlib>
#include <cstdio>
namespace tempest_native::profiling {
enum Stage {core,values,prepare2,prepare3,prepare6,prepare7,prepare10,prepare12,prepare17,
    audio,decode,platform,transaction,input_media,host_commit,render_submit,vectors,web,images,discs,feedback,present,gpu_drain,phase_checkpoint,media_read,media_delivery,count};
inline constexpr const char* names[]={"core","values","prepare2","prepare3","prepare6","prepare7","prepare10","prepare12","prepare17","audio","decode","platform","transaction","input_media","host_commit","render_submit","vectors","web","images","discs","feedback","present","gpu_drain","phase_checkpoint","media_read","media_delivery"};
inline bool enabled(){static bool value=std::getenv("T3K_PROFILE_CSV")!=nullptr;return value;}
inline bool fence(){static bool value=std::getenv("T3K_PROFILE_FENCE")!=nullptr;return value;}
struct Data {std::array<uint64_t,count> ns{},calls{};uint64_t reads=0,writes=0,newWords=0,uploads=0,uploadBytes=0,unchangedUploads=0,mediaBytes=0,cachedUploads=0;unsigned segments=0,blobs=0,glyphs=0,ops=0;};
inline thread_local Data data;
inline thread_local bool collecting=false;
inline thread_local unsigned phase=0,session=0,caller=0,lives=0,scene=0;
struct Scope;
inline thread_local Scope* parent=nullptr;
struct Scope {
    Stage stage;bool active;uint64_t start=0,children=0;Scope* previous=nullptr;
    explicit Scope(Stage s):stage(s),active(enabled()&&collecting){if(active){previous=parent;parent=this;start=SDL_GetTicksNS();}}
    ~Scope(){if(active){auto elapsed=SDL_GetTicksNS()-start;data.ns[stage]+=elapsed-children;++data.calls[stage];if(previous)previous->children+=elapsed;parent=previous;}}
};
inline Stage preparation(unsigned type){switch(type){case 2:return prepare2;case 3:return prepare3;case 6:return prepare6;case 7:return prepare7;case 10:return prepare10;case 12:return prepare12;default:return prepare17;}}
inline void upload(const void* bytes,size_t size,uint64_t& previous){
    if(!enabled()||!collecting)return;auto* p=static_cast<const unsigned char*>(bytes);uint64_t hash=1469598103934665603ULL;for(size_t i=0;i<size;++i)hash=(hash^p[i])*1099511628211ULL;
    ++data.uploads;data.uploadBytes+=size;if(hash==previous)++data.unchangedUploads;previous=hash;
}
inline void begin(){if(enabled())collecting=true;}
inline void header(std::FILE* file){std::fprintf(file,"frame,phase,session,caller,lives,divisions,substep,frame_ms,segments,blobs,glyphs,ops,reads,writes,new_words,uploads,upload_bytes,unchanged_slot_uploads,media_bytes,cached_uploads");for(auto name:names)std::fprintf(file,",%s_ms",name);std::fprintf(file,",scene\n");}
inline void emit(std::FILE* file,uint64_t frame,unsigned divisions,unsigned substep,double ms){
    std::fprintf(file,"%llu,%u,%u,%u,%u,%u,%u,%.6f,%u,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu",frame,phase,session,caller,lives,divisions,substep,ms,data.segments,data.blobs,data.glyphs,data.ops,data.reads,data.writes,data.newWords,data.uploads,data.uploadBytes,data.unchangedUploads,data.mediaBytes,data.cachedUploads);
    for(auto time:data.ns)std::fprintf(file,",%.6f",double(time)/1000000.);std::fprintf(file,",%u\n",scene);data={};
}
}
