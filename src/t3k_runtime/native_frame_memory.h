#pragma once
#include "native_renderer_bridge.h"
#include "game/TempestCommands.h"
#include <array>
#include "native_profile.h"
#include "native_memory_journal.h"
#include <vector>
namespace tempest_native {
// Write-through transactions let picture decoding see the just-built records
// while retaining rollback on a missing compute/audio/render dependency.
class NativeFrameMemory:public CommandMemory{
public:
    enum Bank:unsigned{system,main,gameLocal,rendererLocal};
    NativeFrameMemory(){for(auto& bank:banks_)bank.resize(0x800000);}
    bool load(Bank bank,std::span<const uint8_t> data){if(active_||data.size()!=0x800000)return false;banks_[bank].assign(data.begin(),data.end());webPrecision_.clear();drawSources_.clear();checkpointBefore_.clear();checkpointActive_=false;return true;}
    std::span<const uint8_t> bank(Bank bank)const{return banks_[bank];}
    using Snapshot=std::array<std::vector<uint8_t>,4>;
    // A phase checkpoint is independent of a frame transaction: aborting a
    // frame must not discard the earlier phase's original words.
    bool begin_checkpoint(){if(active_)return false;checkpointBefore_.clear();checkpointActive_=true;return true;}
    bool checkpoint(Snapshot& out)const{
        if(active_||!checkpointActive_)return false;
        out=banks_;
        checkpointBefore_.restore(out);
        return true;
    }
    bool restore(const Snapshot& in){
        if(active_)return false;for(const auto& bank:in)if(bank.size()!=0x800000)return false;
        banks_=in;webPrecision_.clear();drawSources_.clear();before_.clear();checkpointBefore_.clear();checkpointActive_=false;return true;
    }
    template<class Edit> bool edit_words(Bank bank,uint32_t offset,uint32_t bytes,Edit edit){
        if(bank>rendererLocal||(offset&3)||(bytes&3)||offset>0x800000||bytes>0x800000-offset)return false;
        capture_range(bank,offset,bytes);
        edit(std::span<uint8_t>(banks_[bank]).subspan(offset,bytes));return true;
    }
    bool in_transaction()const{return active_;}
    RendererMemory renderer_view()const{return {banks_[system],banks_[main],banks_[rendererLocal],&webPrecision_,&drawSources_};}
    void render_transform_point(uint32_t address,uint32_t packed,double x,double y)override{webPrecision_.record_transform(address,packed,x,y);}
    void web_render_point(uint32_t address,uint32_t packed,double x,double y)override{webPrecision_.record(address,packed,x,y);}
    void render_draw_source(uint32_t draw,uint32_t object,uint32_t source,uint32_t parameters)override{
        NativeDrawSource v{draw,object,source,parameters};
        if(!read32(0x80113164,v.globals))return;
        if(v.globals>=0x80000000&&v.globals<0x807FFFE8)read32(v.globals+20,v.content);
        v.anchor=native_hud_anchor(v.source,v.parameters,v.globals,v.content);
        auto at=std::lower_bound(drawSources_.begin(),drawSources_.end(),draw,[](const NativeDrawSource& a,uint32_t b){return a.draw<b;});
        if(at!=drawSources_.end()&&at->draw==draw)*at=v;else drawSources_.insert(at,v);
    }
    void begin_render_precision_frame(){webPrecision_.clear();drawSources_.clear();}
    bool begin(){if(active_)return false;before_.clear();active_=true;return true;}
    void commit(){before_.clear();active_=false;}
    void rollback(){before_.restore(banks_);webPrecision_.clear();drawSources_.clear();before_.clear();active_=false;}
    void track_accesses(){for(auto& a:access_)a.assign(0x200000,0);tracking_=true;}
    bool tracking_accesses()const{return tracking_;}
    std::vector<uint32_t> written_offsets(Bank bank)const{return before_.written(bank);}
    bool accessed(uint32_t address,uint32_t bytes,unsigned flags=3)const{Bank bank;uint32_t offset;if(!resolve(address,bank,offset)||bank>main||bytes>0x800000-offset)return true;if(!tracking_)return false;for(uint32_t i=offset>>2;i<(offset+bytes+3)/4;++i)if(access_[bank][i]&flags)return true;return false;}
    bool read8(uint32_t address,uint8_t& value)override{if(profiling::enabled()&&profiling::collecting)++profiling::data.reads;Bank bank;uint32_t offset;if(!resolve(address,bank,offset))return false;touch(bank,offset,1);value=banks_[bank][offset];return true;}
    bool read32(uint32_t address,uint32_t& value)override{if(profiling::enabled()&&profiling::collecting)++profiling::data.reads;address&=~3U;Bank bank;uint32_t offset;if(!resolve(address,bank,offset))return false;touch(bank,offset,1);value=word(bank,offset);return true;}
    bool write32(uint32_t address,uint32_t value)override{if(profiling::enabled()&&profiling::collecting)++profiling::data.writes;address&=~3U;Bank bank;uint32_t offset;if(!resolve(address,bank,offset))return false;webPrecision_.invalidate(address);touch(bank,offset,2);store(bank,offset,value,true);return true;}
    // Publish immutable disc/audio bytes without per-word dispatch or allocation.
    bool write_bytes(uint32_t address,std::span<const uint8_t> bytes){
        Bank bank,lastBank;uint32_t offset,lastOffset;
        if((address&3)||(bytes.size()&3)||!resolve(address,bank,offset)||bytes.size()>0x800000-offset)return false;
        if(!bytes.empty()&&(!resolve(address+uint32_t(bytes.size())-1,lastBank,lastOffset)||lastBank!=bank))return false;
        capture_range(bank,offset,uint32_t(bytes.size()));
        webPrecision_.invalidate_range(address,uint32_t(bytes.size()));
        if(tracking_)for(uint32_t at=offset;at<offset+bytes.size();at+=4)touch(bank,at,2);
        if(profiling::enabled()&&profiling::collecting)profiling::data.writes+=bytes.size()/4;
        if(!bytes.empty())std::memmove(banks_[bank].data()+offset,bytes.data(),bytes.size());return true;
    }
    bool renderer_word(uint32_t offset,uint32_t value){if(offset>0x7FFFFC||offset&3)return false;store(rendererLocal,offset,value,true);return true;}
private:
    WebRenderPrecision webPrecision_{};
    std::vector<NativeDrawSource> drawSources_;
    std::array<std::vector<uint8_t>,4> banks_;NativeMemoryJournal before_,checkpointBefore_;bool active_=false,checkpointActive_=false;
    std::array<std::vector<uint8_t>,2> access_;bool tracking_=false;
    void touch(Bank bank,uint32_t offset,unsigned flags){if(tracking_&&bank<=main)access_[bank][offset>>2]|=uint8_t(flags);}
    static bool resolve(uint32_t address,Bank& bank,uint32_t& offset){
        if(address>=0x80000000&&address<0x80800000)bank=system;
        else if(address>=0x40000000&&address<0x40800000)bank=main;
        else if(address>=0x20100000&&address<0x20200000)bank=gameLocal;
        else return false;offset=address&0x7FFFFF;return true;
    }
    uint32_t word(Bank bank,uint32_t offset)const{auto* p=banks_[bank].data()+offset;return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
    void capture_range(Bank bank,uint32_t offset,uint32_t bytes){
        if(checkpointActive_)checkpointBefore_.capture_range(bank,offset,bytes,banks_[bank].data());
        if(active_){auto fresh=before_.capture_range(bank,offset,bytes,banks_[bank].data());if(profiling::enabled()&&profiling::collecting)profiling::data.newWords+=fresh;}
    }
    void store(Bank bank,uint32_t offset,uint32_t value,bool record){
        if(checkpointActive_)checkpointBefore_.capture_word(bank,offset,banks_[bank].data());
        if(record&&active_){bool fresh=before_.capture_word(bank,offset,banks_[bank].data());if(fresh&&profiling::enabled()&&profiling::collecting)++profiling::data.newWords;}
        auto* p=banks_[bank].data()+offset;for(unsigned k=0;k<4;++k)p[k]=uint8_t(value>>(24-k*8));
    }
};
}
