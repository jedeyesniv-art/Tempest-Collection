#pragma once
#include <array>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>
namespace tempest_native {
// First-write snapshots, independently owned by a frame or phase. Page copies
// preserve all original bytes; word masks retain precise write-set diagnostics.
// Slot storage is reused, so commit does not free thousands of tree nodes.
class NativeMemoryJournal {
    static constexpr uint32_t pageBytes=4096,pagesPerBank=2048;
    struct Page {
        uint32_t key;
        std::array<uint8_t,pageBytes> bytes;
        std::array<uint64_t,16> words{};
        Page(uint32_t k,const uint8_t* source):key(k){std::memcpy(bytes.data(),source,pageBytes);}
    };
    std::array<uint32_t,4*pagesPerBank> slots_{};
    std::vector<Page> pages_;
    Page& page(unsigned bank,uint32_t offset,const uint8_t* data){
        uint32_t key=bank*pagesPerBank+offset/pageBytes;
        if(!slots_[key]){pages_.emplace_back(key,data+(offset&~(pageBytes-1)));slots_[key]=uint32_t(pages_.size());}
        return pages_[slots_[key]-1];
    }
public:
    void clear(){for(const auto& p:pages_)slots_[p.key]=0;pages_.clear();}
    bool capture_word(unsigned bank,uint32_t offset,const uint8_t* data){
        auto& p=page(bank,offset,data);unsigned bit=(offset&(pageBytes-1))/4;uint64_t mask=uint64_t(1)<<(bit%64);
        bool fresh=(p.words[bit/64]&mask)==0;p.words[bit/64]|=mask;return fresh;
    }
    uint32_t capture_range(unsigned bank,uint32_t offset,uint32_t bytes,const uint8_t* data){
        uint32_t fresh=0,end=offset+bytes;
        while(offset<end){auto& p=page(bank,offset,data);uint32_t finish=(std::min)(end,(offset&~(pageBytes-1))+pageBytes);
            unsigned first=(offset&(pageBytes-1))/4,last=first+(finish-offset)/4;
            while(first<last){unsigned count=(std::min)(last-first,64-first%64);uint64_t mask=count==64?~uint64_t(0):((uint64_t(1)<<count)-1)<<(first%64);
                fresh+=std::popcount(mask&~p.words[first/64]);p.words[first/64]|=mask;first+=count;}
            offset=finish;
        }return fresh;
    }
    template<class Banks> void restore(Banks& banks)const{for(const auto& p:pages_)std::memcpy(banks[p.key/pagesPerBank].data()+(p.key%pagesPerBank)*pageBytes,p.bytes.data(),pageBytes);}
    std::vector<uint32_t> written(unsigned bank)const{
        std::vector<uint32_t> out;for(const auto& p:pages_)if(p.key/pagesPerBank==bank)for(unsigned i=0;i<16;++i){uint64_t bits=p.words[i];while(bits){unsigned bit=std::countr_zero(bits);out.push_back((p.key%pagesPerBank)*pageBytes+(i*64+bit)*4);bits&=bits-1;}}
        std::sort(out.begin(),out.end());return out;
    }
};
}
