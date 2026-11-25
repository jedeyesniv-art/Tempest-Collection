#pragma once
#include "TempestCommands.h"
#include <bit>
#include <array>
namespace tempest_native {
// NISE's 64-sample software mixer on MPE0. The supported entry points are
// reconstructed algorithms, not decoded NUON instructions. Unsupported effect
// modes fail explicitly until their own differential reconstruction is ready.
struct NiseBlockResult {unsigned voices=0;uint32_t unsupported=0;unsigned streamBoundary=0;};
namespace nise_detail {
inline uint32_t scalar_mul(uint32_t a,uint32_t b,unsigned shift){return uint32_t((int64_t(int32_t(a))*int32_t(b))>>shift);}
inline uint32_t short_mul(uint32_t a,uint32_t b){return uint32_t((int32_t(a)>>16)*(int32_t(b)>>16))*4U;}
inline bool copy(CommandMemory& m,uint32_t source,uint32_t target,unsigned bytes){for(unsigned i=0;i<bytes;i+=4){uint32_t v;if(!m.read32(source+i,v)||!m.write32(target+i,v))return false;}return true;}
inline bool clear(CommandMemory& m,uint32_t target,unsigned bytes){for(unsigned i=0;i<bytes;i+=4)if(!m.write32(target+i,0))return false;return true;}
inline bool sample(CommandMemory& m,uint32_t address,uint32_t& value){uint8_t a,b;if(!m.read8(address,a)||!m.read8(address+1,b))return false;value=(uint32_t(a)<<24)|(uint32_t(b)<<16);return true;}
inline bool shorts(CommandMemory& m,uint32_t address,const std::array<uint32_t,64>& data){for(unsigned i=0;i<64;i+=2)if(!m.write32(address+i*2,(data[i]&0xffff0000)|(data[i+1]>>16)))return false;return true;}
// 203019AA / 2030161C: nearest and linear PCM16 sampling. Interpolation uses
// NUON short-vector multiplies, whose operands discard their low 16 bits.
inline bool resample(CommandMemory& m,uint32_t phase,uint32_t rate,uint32_t offset,bool linear,std::array<uint32_t,64>& out,uint32_t& next,uint32_t& advance){
    uint32_t step=rate>>13;
    auto read=[&](unsigned i,uint32_t& value){uint32_t position=(phase>>16)+(offset<<16)+i*step,a,b;if(!sample(m,0x20101240+(position>>16)*2,a))return false;
        if(linear){if(!sample(m,0x20101240+(position>>16)*2+2,b))return false;uint32_t fraction=(position&0xffff)<<14;value=(short_mul(fraction,b)&0xffff0000)+(short_mul(0x40000000-(fraction&0xffff0000),a)&0xffff0000);}else value=a;return true;};
    auto store=[&](unsigned i){return m.write32(0x20101350+i*2,(out[i]&0xffff0000)|(out[i+1]>>16))&&m.write32(0x20101354+i*2,(out[i+2]&0xffff0000)|(out[i+3]>>16));};
    if(linear){
        if(!read(0,out[0])||!read(1,out[1]))return false;
        // The original pipeline prefetches the next two samples before each
        // four-sample store, including its final unused lookahead. High rates
        // can alias source and mono output, so this order is observable.
        for(unsigned i=0;i<64;i+=4){uint32_t a,b;if(!read(i+2,out[i+2])||!read(i+3,out[i+3])||!read(i+4,a)||!read(i+5,b)||!store(i))return false;if(i+4<64){out[i+4]=a;out[i+5]=b;}}
    }else for(unsigned i=0;i<64;i+=4){for(unsigned k=0;k<4;++k)if(!read(i+k,out[i+k]))return false;if(!store(i))return false;}
    // The nearest path shifts r0 down twice, so its returned phase deliberately
    // drops the incoming fraction. Preserve that original asymmetry.
    uint32_t total=(linear?(phase>>16):0)+(linear?(step&0x3ffff):step)*64;next=total<<16;advance=total>>16;return true;
}
// 203018AC: accumulate mono into the interleaved stereo bus. Each pass preserves
// the original high-halfword rounding between vector operations and stores.
inline bool pan(CommandMemory& m,const std::array<uint32_t,64>& mono,uint32_t pan,uint32_t panGain,uint32_t gain,uint32_t master,uint32_t output=0x201015D0){
    uint32_t g=scalar_mul(master,gain,30);g=scalar_mul(panGain,g,30);
    uint32_t left=short_mul(g,pan),right=short_mul(g,0x40000000-pan);
    for(unsigned i=0;i<64;++i){uint32_t old;if(!m.read32(output+i*4,old))return false;
        uint32_t l=(old&0xffff0000)+short_mul(mono[i],left),r=(old<<16)+short_mul(mono[i],right);
        if(!m.write32(output+i*4,(l&0xffff0000)|(r>>16)))return false;
    }return true;
}
}
inline bool mix_nise_block(CommandMemory& m,NiseBlockResult& result){
    using namespace nise_detail;uint32_t masterAddress;
    if(!m.read32(0x20101010,masterAddress)||!copy(m,masterAddress,0x20101040,48))return false;
    std::array<uint32_t,12> master{};for(unsigned i=0;i<12;++i)if(!m.read32(0x20101040+i*4,master[i]))return false;
    if(master[1]&0x30){result.unsupported=master[1]&0x30;return false;}
    if(master[1]&0x80){
        // 20301440: streamed stereo ring, independently scaled by the upper
        // half of master volume. Its cursor belongs to the original mixer.
        uint32_t gain=(master[4]>>16)<<14;
        for(unsigned i=0;i<64;++i){uint32_t a;if(!m.read32(master[5]+i*4,a))return false;uint32_t l=short_mul(a,gain),r=short_mul(a<<16,gain);if(!m.write32(0x201015D0+i*4,(l&0xffff0000)|(r>>16)))return false;}
        master[5]+=256;if(master[5]==master[2]+(master[3]>>1))result.streamBoundary=1;
        if(master[5]==master[2]+master[3]){master[5]=master[2];result.streamBoundary=2;}
        if(!m.write32(0x20101054,master[5]))return false;
    }else if(!clear(m,0x201015D0,256))return false;
    if(!clear(m,0x201013D0,128)||!m.write32(0x20101030,masterAddress+48)||!m.write32(0x20101028,master[0]))return false;
    // Effect-ring prefetch exists even while its send is disabled.
    if(!copy(m,master[8]+master[10]*128,0x20101450,128))return false;
    uint32_t remaining=master[0],active=master[0];
    while(remaining){unsigned index=31-std::countl_zero(remaining);remaining&=~(1U<<index);uint32_t address=masterAddress+48+index*96;
        if(!m.write32(0x20101020,index)||!m.write32(0x20101024,index)||!m.write32(0x20101028,remaining)||!copy(m,address,0x20101070,96))return false;
        std::array<uint32_t,24> v{};for(unsigned i=0;i<24;++i)if(!m.read32(address+i*4,v[i]))return false;
        if((v[6]&3)!=1||(v[6]&0x1f70)){result.unsupported=v[6];return false;}
        uint32_t needed=((((v[5]>>13)*uint64_t(0x400000))>>16)+0x8000)>>15;needed+=8;
        uint32_t cursor=v[1],left=needed,write=0x20101240;bool ended=false;
        for(unsigned guard=0;left&&guard<128;++guard){uint32_t available=v[2]-cursor+(cursor&3);unsigned amount=left<available?left:available;
            unsigned rounded=(amount+3)&~3U;if(rounded&&!copy(m,cursor&~3U,write,rounded))return false;write+=amount;left-=amount;
            if(!left)break;if(!v[3]){if(!clear(m,write,(left/4+1)*4))return false;ended=true;left=0;break;}cursor=v[3];
        }
        if(left)return false;
        if(ended){active&=~(1U<<index);v[1]=v[0];v[4]=0;}
        if(!copy(m,0x20101070,0x201010D0,96))return false;
        std::array<uint32_t,64> samples{};uint32_t advance;
        uint32_t offset=(v[1]&3)>>1;
        if(!resample(m,v[4],v[5],offset,bool(v[6]&0x80),samples,v[4],advance))return false;
        v[1]+=advance*2;
        for(unsigned guard=0;v[1]>=v[2]&&guard<128;++guard){if(!v[3])break;v[1]=v[1]-v[2]+v[3];}
        if(!pan(m,samples,v[20],v[21],v[23],(master[4]&0xffff)<<14))return false;
        for(unsigned i=0;i<24;++i)if(!m.write32(0x20101070+i*4,v[i]))return false;
        if(!copy(m,0x20101070,address,96))return false;++result.voices;
    }
    if(!m.write32(0x20101040,active)||!copy(m,0x20101040,masterAddress,48))return false;
    return true;
}
}
