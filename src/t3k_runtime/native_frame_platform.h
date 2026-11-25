#pragma once
#include "native_frame_integration.h"
#include <fstream>
#include "disc_image.h"
namespace tempest_native {
struct NativeMediaDelivery{uint32_t engine,address,bytes,block;uint64_t hash;std::string path;};
// Disc bytes come from the user's image, not captured output-memory patches.
class NativeDiscImage{
public:
    explicit NativeDiscImage(const std::string& path);
    bool read(const std::string& file,uint32_t block,uint32_t bytes,std::vector<uint8_t>& out);
    bool file_size(const std::string& file,uint32_t& size)const;
    bool read_file(const std::string& file,std::vector<uint8_t>& out);
private:
    DiscImage image_;uint64_t imageBytes_=0;bool valid_=false;
    struct Entry{uint32_t sector,size;};std::map<std::string,Entry> files_;
    const Entry* find(const std::string& file)const;
    bool directory(uint32_t sector,uint32_t bytes,const std::string& prefix,unsigned depth);
    bool bytes(uint64_t offset,uint8_t* out,size_t count);
};
// A batch of independently observed platform arrivals. Deferring delivery until
// the preparation fence is permitted only after proving that gameplay neither
// read nor wrote any arrival range during this frame. Overlaps fail explicitly.
class NativeFramePlatform final:public FramePlatformBackend{
public:
    NativeFramePlatform(NativeDiscImage& disc,const std::vector<NativeMediaDelivery>& deliveries,uint64_t elapsedFields):disc_(disc),deliveries_(deliveries),elapsedFields_(elapsedFields){}
    bool settle(NativeFrameMemory&,std::string& error)override;
    void commit_frame()override{committed_=true;}
    void abort_frame()override{settled_=false;deliveredBytes_=0;}
    uint64_t delivered_bytes()const{return deliveredBytes_;}
private:
    NativeDiscImage& disc_;const std::vector<NativeMediaDelivery>& deliveries_;uint64_t elapsedFields_,deliveredBytes_=0;bool settled_=false,committed_=false;
};
}
