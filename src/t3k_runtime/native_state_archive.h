#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace tempest_native {
// A pointer-free, little-endian archive. Reads are bounded before allocating;
// no object representation, native pointer, or compiler padding goes on disk.
struct StateWriter {
    std::vector<uint8_t> bytes;
    void u32(uint32_t v){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(v>>(i*8)));}
    void u64(uint64_t v){u32(uint32_t(v));u32(uint32_t(v>>32));}
    void flag(bool v){u32(v?1:0);}
    void data(std::span<const uint8_t> v){u64(v.size());bytes.insert(bytes.end(),v.begin(),v.end());}
    void text(const std::string& v){data({reinterpret_cast<const uint8_t*>(v.data()),v.size()});}
};
struct StateReader {
    std::span<const uint8_t> bytes;size_t at=0;bool valid=true;
    uint32_t u32(){if(!valid||at>bytes.size()||bytes.size()-at<4){valid=false;return 0;}uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(bytes[at++])<<(i*8);return v;}
    uint64_t u64(){uint64_t low=u32(),high=u32();return low|(high<<32);}
    bool flag(){auto v=u32();if(v>1)valid=false;return v==1;}
    bool data(std::vector<uint8_t>& out,size_t maximum){auto n=u64();if(!valid||n>maximum||at>bytes.size()||n>bytes.size()-at){valid=false;return false;}out.assign(bytes.begin()+at,bytes.begin()+at+size_t(n));at+=size_t(n);return true;}
    bool text(std::string& out,size_t maximum=4096){std::vector<uint8_t> v;if(!data(v,maximum))return false;out.assign(v.begin(),v.end());return true;}
    bool finished()const{return valid&&at==bytes.size();}
};
inline uint64_t state_checksum(std::span<const uint8_t> bytes){uint64_t hash=14695981039346656037ULL;for(auto v:bytes){hash^=v;hash*=1099511628211ULL;}return hash;}
inline bool write_native_state(const std::filesystem::path& path,std::span<const uint8_t> payload,uint64_t gameIdentity,std::string& error){
    if(payload.size()>256ULL*1024*1024){error="Native state exceeds the supported size";return false;}
    StateWriter header;header.u64(0x3154534B33545054ULL);header.u32(1);header.u32(40);header.u64(gameIdentity);header.u64(payload.size());header.u64(state_checksum(payload));
    std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);if(ec){error="Cannot create native state folder";return false;}
    auto tmp=path;tmp+=L".tmp";
    {std::ofstream out(tmp,std::ios::binary|std::ios::trunc);if(!out){error="Cannot create native state";return false;}
     out.write(reinterpret_cast<const char*>(header.bytes.data()),header.bytes.size());out.write(reinterpret_cast<const char*>(payload.data()),payload.size());out.flush();if(!out){error="Cannot write native state";return false;}}
#ifdef _WIN32
    if(!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){error="Cannot replace native state";return false;}
#else
    std::filesystem::rename(tmp,path,ec);if(ec){error="Cannot replace native state";return false;}
#endif
    error.clear();return true;
}
inline bool read_native_state(const std::filesystem::path& path,std::vector<uint8_t>& payload,uint64_t gameIdentity,std::string& error){
    constexpr uint64_t maximum=256ULL*1024*1024;
    std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in){error="Native slot is empty or unreadable";return false;}
    auto size=in.tellg();if(size<40||uint64_t(size)>maximum+40){error="Invalid native state size";return false;}
    in.seekg(0);std::vector<uint8_t> header(40);in.read(reinterpret_cast<char*>(header.data()),40);StateReader r{header};
    auto magic=r.u64();auto version=r.u32();auto headerBytes=r.u32();auto identity=r.u64();auto count=r.u64();auto checksum=r.u64();
    if(!in||magic!=0x3154534B33545054ULL||version!=1||headerBytes!=40||identity!=gameIdentity||count!=uint64_t(size)-40){error="Native state is incompatible with this game or version";return false;}
    std::vector<uint8_t> next(size_t(count),uint8_t{});in.read(reinterpret_cast<char*>(next.data()),next.size());
    if(!in||state_checksum(next)!=checksum){error="Native state is truncated or damaged";return false;}
    payload=std::move(next);error.clear();return true;
}
}
