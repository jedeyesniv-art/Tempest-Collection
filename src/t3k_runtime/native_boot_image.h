#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <algorithm>
namespace tempest_native {
struct BootSection{std::string name;uint32_t physical,virtualAddress,size,fileOffset,flags;bool loaded;};
struct NativeBootImage{
    uint32_t entry=0,coffOffset=0;
    std::vector<BootSection> sections;
    // Original program/data bytes, never executed as instructions. The native
    // boot functions consume the same data layout as the recovered frame code.
    std::array<std::vector<uint8_t>,3> banks;
};
inline bool load_native_boot_image(std::span<const uint8_t> file,NativeBootImage& out,std::string& error){
    auto fits=[&](uint64_t offset,uint64_t bytes){return offset<=file.size()&&bytes<=file.size()-offset;};
    auto u16=[&](size_t at){return uint32_t(file[at])<<8|file[at+1];};
    auto u32=[&](size_t at){return uint32_t(file[at])<<24|uint32_t(file[at+1])<<16|uint32_t(file[at+2])<<8|file[at+3];};
    auto magic=[&](size_t at,const char* text,size_t n){return fits(at,n)&&std::equal(text,text+n,file.begin()+at);};
    size_t at=0;unsigned wrappers=0;
    while(true){
        if(++wrappers>8){error="nested program wrapper limit";return false;}
        if(magic(at,"Bles",4)){if(!fits(at,0x54)||u16(at+4)!=1){error="unsupported BLES header";return false;}uint32_t relative=u16(at+0x52);if(relative<0x54||!fits(at,relative)){error="invalid BLES program offset";return false;}at+=relative;continue;}
        if(magic(at,"NUONROM-DISK",12)){if(!fits(at,96)||(!magic(at+64,"cd_app.cof",10)&&!magic(at+64,"nuon.run",8))){error="unsupported program archive entry";return false;}uint32_t relative=u32(at+80),length=u32(at+84);if(relative<96||!fits(relative,length)){error="invalid program archive extent";return false;}at=relative;continue;}
        break;
    }
    if(!fits(at,24)||u16(at)!=0x120){error="missing NUON COFF header";return false;}
    uint32_t count=u16(at+2),optional=u16(at+16);if(!count||count>4096||optional<4||!fits(at+20,uint64_t(optional)+uint64_t(count)*44)){error="invalid COFF section table";return false;}
    NativeBootImage image;image.entry=u32(at+20);image.coffOffset=uint32_t(at);for(auto& bank:image.banks)bank.resize(0x800000);
    for(unsigned i=0;i<count;++i){size_t section=at+20+optional+i*44;std::string name;for(unsigned k=0;k<8&&file[section+k];++k)name+=char(file[section+k]);
        BootSection s{name,u32(section+8),u32(section+12),u32(section+16),u32(section+20),u32(section+40),false};
        // No-load/BSS sections are metadata for native initialization. They do
        // not contain payload to copy; initialization itself is recovered from
        // the original startup functions, not invented from a gameplay snapshot.
        if(!(s.flags&0x282)){
            unsigned bank;uint32_t offset=s.physical&0x7fffff;
            if(s.physical>=0x80000000&&s.physical<0x80800000)bank=0;else if(s.physical>=0x40000000&&s.physical<0x40800000)bank=1;else if(s.physical>=0x20000000&&s.physical<0x20800000)bank=2;else{error="unsupported program section address";return false;}
            if(s.size>0x800000-offset||!fits(at+s.fileOffset,s.size)){error="program section exceeds file or memory";return false;}
            std::copy_n(file.begin()+at+s.fileOffset,s.size,image.banks[bank].begin()+offset);s.loaded=true;
            // Preserve PATCH payload exactly. Nuance zeros its first 16 bytes
            // as a firmware workaround; native startup must classify hardware
            // patch requests explicitly rather than silently modifying game data.
        }
        image.sections.push_back(std::move(s));
    }
    out=std::move(image);error.clear();return true;
}
}
