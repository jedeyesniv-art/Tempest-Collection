#pragma once
#include "native_renderer_bridge.h"
#include "game/TempestCommands.h"
#include <array>
#include <map>
#include <vector>
namespace tempest_native {
// Write-through transactions let picture decoding see the just-built records
// while retaining rollback on a missing compute/audio/render dependency.
class NativeFrameMemory:public CommandMemory{
public:
    enum Bank:unsigned{system,main,gameLocal,rendererLocal};
    NativeFrameMemory(){for(auto& bank:banks_)bank.resize(0x800000);}
    bool load(Bank bank,std::span<const uint8_t> data){if(active_||data.size()!=0x800000)return false;banks_[bank].assign(data.begin(),data.end());checkpointBefore_.clear();checkpointActive_=false;return true;}
    std::span<const uint8_t> bank(Bank bank)const{return banks_[bank];}
    using Snapshot=std::array<std::vector<uint8_t>,4>;
    // A phase checkpoint is independent of a frame transaction: aborting a
    // frame must not discard the earlier phase's original words.
    bool begin_checkpoint(){if(active_)return false;checkpointBefore_.clear();checkpointActive_=true;return true;}
    bool checkpoint(Snapshot& out)const{
        if(active_||!checkpointActive_)return false;
        out=banks_;
        for(auto [key,value]:checkpointBefore_){auto* p=out[key>>23].data()+(key&0x7FFFFF);for(unsigned k=0;k<4;++k)p[k]=uint8_t(value>>(24-k*8));}
        return true;
    }
    bool restore(const Snapshot& in){
        if(active_)return false;for(const auto& bank:in)if(bank.size()!=0x800000)return false;
        banks_=in;before_.clear();checkpointBefore_.clear();checkpointActive_=false;return true;
    }
    template<class Edit> bool edit_words(Bank bank,uint32_t offset,uint32_t bytes,Edit edit){
        if(bank>rendererLocal||(offset&3)||(bytes&3)||offset>0x800000||bytes>0x800000-offset)return false;
        for(uint32_t at=offset;at<offset+bytes;at+=4)store(bank,at,word(bank,at),true);
        edit(std::span<uint8_t>(banks_[bank]).subspan(offset,bytes));return true;
    }
    bool in_transaction()const{return active_;}
    RendererMemory renderer_view()const{return {banks_[system],banks_[main],banks_[rendererLocal]};}
    bool begin(){if(active_)return false;before_.clear();active_=true;return true;}
    void commit(){before_.clear();active_=false;}
    void rollback(){for(auto [key,value]:before_)store(Bank(key>>23),key&0x7FFFFF,value,false);before_.clear();active_=false;}
    void track_accesses(){for(auto& a:access_)a.assign(0x200000,0);tracking_=true;}
    bool tracking_accesses()const{return tracking_;}
    std::vector<uint32_t> written_offsets(Bank bank)const{std::vector<uint32_t> result;for(auto [key,value]:before_)if((key>>23)==bank)result.push_back(key&0x7FFFFF);return result;}
    bool accessed(uint32_t address,uint32_t bytes,unsigned flags=3)const{Bank bank;uint32_t offset;if(!resolve(address,bank,offset)||bank>main||bytes>0x800000-offset)return true;if(!tracking_)return false;for(uint32_t i=offset>>2;i<(offset+bytes+3)/4;++i)if(access_[bank][i]&flags)return true;return false;}
    bool read8(uint32_t address,uint8_t& value)override{Bank bank;uint32_t offset;if(!resolve(address,bank,offset))return false;touch(bank,offset,1);value=banks_[bank][offset];return true;}
    bool read32(uint32_t address,uint32_t& value)override{address&=~3U;Bank bank;uint32_t offset;if(!resolve(address,bank,offset))return false;touch(bank,offset,1);value=word(bank,offset);return true;}
    bool write32(uint32_t address,uint32_t value)override{address&=~3U;Bank bank;uint32_t offset;if(!resolve(address,bank,offset))return false;touch(bank,offset,2);store(bank,offset,value,true);return true;}
    bool renderer_word(uint32_t offset,uint32_t value){if(offset>0x7FFFFC||offset&3)return false;store(rendererLocal,offset,value,true);return true;}
private:
    std::array<std::vector<uint8_t>,4> banks_;std::map<uint32_t,uint32_t> before_,checkpointBefore_;bool active_=false,checkpointActive_=false;
    std::array<std::vector<uint8_t>,2> access_;bool tracking_=false;
    void touch(Bank bank,uint32_t offset,unsigned flags){if(tracking_&&bank<=main)access_[bank][offset>>2]|=uint8_t(flags);}
    static bool resolve(uint32_t address,Bank& bank,uint32_t& offset){
        if(address>=0x80000000&&address<0x80800000)bank=system;
        else if(address>=0x40000000&&address<0x40800000)bank=main;
        else if(address>=0x20100000&&address<0x20200000)bank=gameLocal;
        else return false;offset=address&0x7FFFFF;return true;
    }
    uint32_t word(Bank bank,uint32_t offset)const{auto* p=banks_[bank].data()+offset;return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
    void store(Bank bank,uint32_t offset,uint32_t value,bool record){
        uint32_t key=(uint32_t(bank)<<23)|offset;if(checkpointActive_&&!checkpointBefore_.count(key))checkpointBefore_[key]=word(bank,offset);if(record&&active_&&!before_.count(key))before_[key]=word(bank,offset);
        auto* p=banks_[bank].data()+offset;for(unsigned k=0;k<4;++k)p[k]=uint8_t(value>>(24-k*8));
    }
};
}
