#pragma once
#include "TempestCommands.h"
#include <array>
#include <bit>
namespace tempest_native {
// Supported semantic branch of BiCopy 806B3960 -> copDefault 806B4A40:
// unscaled format-4 source to normalized 32-bit destination, 64x1 tiles. Local DMA
// scratch and CPU register state are implementation-private, not API outputs.
// Other formats, transparency, scaling, continuation or overlapping storage
// fail explicitly. The caller owns rollback if a required memory access fails.
inline bool copy_bitmap32_unscaled(CommandMemory& m,uint32_t parameters,uint32_t& result){
    std::array<uint32_t,31> p;for(unsigned i=0;i<p.size();++i)if(!m.read32(parameters+i*4,p[i]))return false;
    if(p[0]||p[1]||p[5]!=4||p[12]!=0x840||p[17]!=1||p[18]!=1||p[19]!=1||p[20]!=1||p[29]!=2||p[26]!=1||p[25]!=64||p[27]!=64)return false;
    const uint32_t width=p[9],height=p[8],sourceLeft=p[7],sourceTop=p[6],destLeft=p[14],destTop=p[13];
    if(!width||!height||width>65536||height>65536||width!=p[16]||height!=p[15]||!p[23]||!p[24]||!p[25]||!p[26]||p[25]>64||
       !p[27]||p[27]>64||p[25]!=std::bit_ceil(p[27])||p[28]!=p[26]||p[23]!=(uint64_t(width)+p[27]-1)/p[27]||p[24]!=(uint64_t(height)+p[26]-1)/p[26]||
       uint64_t(p[23])*p[24]!=p[2]||p[2]>0x7FFFFFFF)return false;
    if((p[3]&3)||(p[10]&3)||(p[4]&3)||!p[4]||(p[11]&7)||p[11]>2040||uint64_t(sourceLeft)+width>p[4]/4||
       uint64_t(destLeft)+width>p[11]||uint64_t(destLeft)+width>2048||uint64_t(destTop)+height>2048)return false;
    auto valid=[](uint64_t begin,uint64_t end){return end>begin&&((begin>=0x80000000&&end<=0x80800000)||(begin>=0x40000000&&end<=0x40800000));};
    uint64_t sb=uint64_t(p[3])+uint64_t(sourceTop)*p[4]+uint64_t(sourceLeft)*4,se=sb+uint64_t(height-1)*p[4]+uint64_t(width)*4;
    uint64_t db=uint64_t(p[10])+(uint64_t(destTop)*p[11]+destLeft)*4,de=db+uint64_t(height-1)*p[11]*4+uint64_t(width)*4;
    if(!valid(p[3],uint64_t(p[3])+4)||!valid(sb,se)||p[10]<0x40000000||db<0x40000000||de>0x40800000||de<=db||(sb<de&&db<se)||!valid(parameters,uint64_t(parameters)+124)||
       (uint64_t(parameters)<de&&db<uint64_t(parameters)+124)||(uint64_t(parameters)<se&&sb<uint64_t(parameters)+124))return false;
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){uint32_t value;
        if(!m.read32(uint32_t(sb+uint64_t(y)*p[4]+x*4),value)||!m.write32(uint32_t(db+uint64_t(y)*p[11]*4+x*4),value))return false;}
    if(!m.write32(parameters,p[23])||!m.write32(parameters+4,p[2]))return false;
    result=0;return true;
}
}
