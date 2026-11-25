#include "native_frame_platform.h"
#include <algorithm>
#include <cctype>
namespace tempest_native {
static uint32_t little(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
static std::string normalized(std::string s){for(char& c:s){if(c=='\\')c='/';c=char(std::toupper(uint8_t(c)));}while(!s.empty()&&s.front()=='/')s.erase(s.begin());return s;}
bool NativeDiscImage::bytes(uint64_t offset,uint8_t* out,size_t count){return offset<=imageBytes_&&count<=imageBytes_-offset&&image_.read(offset,out,count)==count;}
NativeDiscImage::NativeDiscImage(const std::string& path){
    if(!image_.open(path.c_str()))return;imageBytes_=image_.size();std::array<uint8_t,2048> pvd{};
    if(!bytes(16*2048,pvd.data(),pvd.size())||pvd[0]!=1||std::string(reinterpret_cast<char*>(pvd.data()+1),5)!="CD001")return;
    valid_=directory(little(pvd.data()+158),little(pvd.data()+166),"",0);
}
bool NativeDiscImage::directory(uint32_t sector,uint32_t size,const std::string& prefix,unsigned depth){
    if(depth>8||size>0x1000000)return false;std::vector<uint8_t> data(size);if(!bytes(uint64_t(sector)*2048,data.data(),size))return false;
    for(size_t at=0;at<size;){unsigned length=data[at];if(!length){at=(at/2048+1)*2048;continue;}if(length<34||at+length>size||33U+data[at+32]>length)return false;
        unsigned nameLength=data[at+32];if(nameLength==1&&data[at+33]<2){at+=length;continue;}
        std::string name(reinterpret_cast<char*>(data.data()+at+33),nameLength);auto semi=name.find(';');if(semi!=std::string::npos)name.resize(semi);name=normalized(name);
        uint32_t lba=little(data.data()+at+2),bytesInFile=little(data.data()+at+10);std::string path=prefix+name;
        if(data[at+25]&2){if(!directory(lba,bytesInFile,path+"/",depth+1))return false;}else files_[path]={lba,bytesInFile};at+=length;
    }
    return true;
}
const NativeDiscImage::Entry* NativeDiscImage::find(const std::string& file)const{
    if(!valid_)return nullptr;std::string name=normalized(file);auto it=files_.find(name);if(it==files_.end())it=files_.find("NUON/"+name);
    if(it==files_.end()){for(auto candidate=files_.begin();candidate!=files_.end();++candidate)if(candidate->first.ends_with("/"+name)){if(it!=files_.end())return nullptr;it=candidate;}}
    return it==files_.end()?nullptr:&it->second;
}
bool NativeDiscImage::file_size(const std::string& file,uint32_t& size)const{const auto* entry=find(file);if(!entry)return false;size=entry->size;return true;}
bool NativeDiscImage::read_file(const std::string& file,std::vector<uint8_t>& out){uint32_t size;return file_size(file,size)&&read(file,0,size,out);}
bool NativeDiscImage::read(const std::string& file,uint32_t block,uint32_t count,std::vector<uint8_t>& out){
    const auto* entry=find(file);if(!entry||count>0x800000||uint64_t(block)*2048+count>entry->size)return false;out.resize(count);return bytes((uint64_t(entry->sector)+block)*2048,out.data(),out.size());
}
bool NativeFramePlatform::settle(NativeFrameMemory& memory,std::string& error){
    if(settled_||committed_||!memory.tracking_accesses()){error="platform settlement requires a fresh tracked frame";return false;}
    if(elapsedFields_>0xFFFFFFFF){error="invalid platform field interval";return false;}
    if(elapsedFields_&&memory.accessed(0x807FFDC0,4)){error="frame accessed the asynchronous BIOS field counter";return false;}
    for(const auto& d:deliveries_)if(d.engine>3||!d.bytes||(d.address&3)||(d.bytes&3)||memory.accessed(d.address,d.bytes)){error="disc arrival overlaps gameplay reads/writes at "+std::to_string(d.address);return false;}
    std::vector<std::vector<uint8_t>> payloads;payloads.reserve(deliveries_.size());
    for(const auto& d:deliveries_){payloads.emplace_back();if(!disc_.read(d.path,d.block,d.bytes,payloads.back())){error="cannot read original disc asset "+d.path;return false;}
        uint64_t hash=14695981039346656037ULL;for(uint8_t byte:payloads.back())hash=(hash^byte)*1099511628211ULL;if(hash!=d.hash){error="disc asset differs from reference arrival "+d.path;return false;}}
    for(size_t i=0;i<deliveries_.size();++i){const auto& d=deliveries_[i];const auto& p=payloads[i];for(unsigned offset=0;offset<d.bytes;offset+=4){uint32_t value=uint32_t(p[offset])<<24|uint32_t(p[offset+1])<<16|uint32_t(p[offset+2])<<8|p[offset+3];if(!memory.write32(d.address+offset,value))return false;}deliveredBytes_+=d.bytes;}
    if(elapsedFields_){auto b=memory.bank(NativeFrameMemory::system);unsigned a=0x7FFDC0;uint32_t old=uint32_t(b[a])<<24|uint32_t(b[a+1])<<16|uint32_t(b[a+2])<<8|b[a+3];if(!memory.write32(0x807FFDC0,old+uint32_t(elapsedFields_)))return false;}
    settled_=true;return true;
}
}
