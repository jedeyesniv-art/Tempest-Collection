#include "t3k_runtime/native_audio_stream.h"
#include "t3k_runtime/native_continuation.h"
#include "t3k_runtime/native_state_archive.h"
#include "t3k_runtime/native_service_journal.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace tempest_native;
static void require(bool value,const char* why){if(!value){std::fprintf(stderr,"checkpoint test: %s\n",why);std::exit(1);}}
int main(){
    {
        t3knative::Picture previous,current;previous.flip=7;current.flip=8;t3knative::FeedbackState feedback;uint64_t serial=23;bool valid=true,retired=true;float phase=1;
        for(bool pending:{false,true}){StateWriter writer;StateEncode encode{writer};encode(previous,current,feedback,serial,valid,retired,pending,phase);StateReader reader{writer.bytes};StateDecode decode{reader};NativeFrameRenderer::PresentationState state;decode(state);
          require(reader.finished()==!pending,"settled legacy presentation states remain readable; unfinished generated frames are rejected");if(!pending)require(state.current.flip==8&&state.serial==23&&state.valid,"retired presentation flags cannot enable frame generation");}
        NativeFrameRenderer::PresentationState state;state.current=current;state.serial=23;state.valid=true;StateWriter writer;StateEncode encode{writer};encode(state);StateReader reader{writer.bytes};StateDecode decode{reader};bool pending=true;retired=true;phase=0;decode(previous,current,feedback,serial,valid,retired,pending,phase);require(reader.finished()&&previous.segs.empty()&&!retired&&!pending&&phase==1,"new presentation states preserve settled legacy wire layout");
    }

    {
        NativeFrameMemory bulk;NativeFrameMemory::Snapshot expected,original,phaseImage;
        for(unsigned bank=0;bank<4;++bank){auto data=bulk.bank(NativeFrameMemory::Bank(bank));expected[bank].assign(data.begin(),data.end());}original=expected;
        require(bulk.begin_checkpoint(),"bulk phase checkpoint");uint32_t random=0x154977AB;
        for(unsigned frame=0;frame<8;++frame){auto before=expected;require(bulk.begin(),"mixed bulk frame");
            for(unsigned op=0;op<80;++op){random=random*1664525+1013904223;unsigned bank=(random>>27)%3;uint32_t offset=((random>>5)%0x10000)&~3u;if(bank==2)offset+=0x100000;
                uint32_t base=bank==0?0x80000000:bank==1?0x40000000:0x20000000;
                if(op%3){uint32_t value=random^op;require(bulk.write32(base+offset,value),"interleaved word write");for(unsigned k=0;k<4;++k)expected[bank][offset+k]=uint8_t(value>>(24-k*8));}
                else{std::vector<uint8_t> bytes(8192+(op%5)*4);for(unsigned k=0;k<bytes.size();++k)bytes[k]=uint8_t(k+op+frame);require(bulk.write_bytes(base+offset,bytes),"cross-page bulk write");std::copy(bytes.begin(),bytes.end(),expected[bank].begin()+offset);}
            }
            std::array<uint8_t,8> tail{1,2,3,4,5,6,7,8};require(!bulk.write_bytes(0x807FFFFC,tail)&&!bulk.write_bytes(0x80000001,tail),"reject bulk range/unaligned address atomically");
            require(bulk.write_bytes(0x807FFFF8,tail),"bulk write at exact bank end");std::copy(tail.begin(),tail.end(),expected[0].end()-8);
            auto written=bulk.written_offsets(NativeFrameMemory::system);require(std::binary_search(written.begin(),written.end(),0x7FFFF8u)&&!std::binary_search(written.begin(),written.end(),0x7FFFF0u),"write masks retain exact word set within snapshotted pages");
            if(frame%2){bulk.commit();}else{bulk.rollback();expected=std::move(before);}
            for(unsigned bank=0;bank<4;++bank)require(std::equal(expected[bank].begin(),expected[bank].end(),bulk.bank(NativeFrameMemory::Bank(bank)).begin()),"mixed writes/rollback preserve every native bank byte");
        }
        require(bulk.checkpoint(phaseImage)&&phaseImage==original,"phase originals survive committed and aborted cross-page bulk writes");
        require(bulk.restore(phaseImage),"restore bulk phase image");
    }
    NativeFrameMemory memory;NativeFrameMemory::Snapshot phase;
    {
        double x=0,y=0;constexpr uint32_t point=0x40181800,packed=0x00010002;
        require(memory.write32(point,packed),"seed packed render point");memory.web_render_point(point,packed,2.125,1.25);
        require(memory.renderer_view().webPrecision->read(point,packed,x,y)&&x==2.125&&y==1.25,"fractional render metadata is retained separately");
        require(memory.begin(),"render metadata transaction");memory.web_render_point(point,packed,2.25,1.5);memory.rollback();
        require(!memory.renderer_view().webPrecision->read(point,packed,x,y),"rollback discards unpublished render metadata");
        memory.web_render_point(point,packed,2.125,1.25);memory.begin_render_precision_frame();
        require(!memory.renderer_view().webPrecision->read(point,packed,x,y),"new picture cannot inherit stale precision data");
    }
    uint32_t value=0;
    require(memory.write32(0x80000000,7),"initial write");
    require(memory.begin_checkpoint(),"phase start");
    require(memory.write32(0x80000000,8),"outside transaction write");
    require(memory.begin(),"frame start");
    require(!memory.checkpoint(phase),"must reject an in-flight frame");
    require(memory.write32(0x80000000,9)&&memory.write32(0x40000000,11),"frame writes");
    memory.rollback();
    require(memory.read32(0x80000000,value)&&value==8,"rollback retains pre-frame value");
    require(memory.begin(),"second frame");
    require(memory.renderer_word(12,0x12345678)&&memory.write32(0x20101000,19),"worker writes");
    memory.commit();
    require(memory.checkpoint(phase),"phase snapshot");
    require(phase[0][3]==7&&phase[1][3]==0&&phase[2][0x101003]==0&&phase[3][15]==0,"phase must include all bank originals");
    require(memory.restore(phase),"restore phase");
    require(memory.read32(0x80000000,value)&&value==7,"restored original RAM");
    auto bad=phase;bad[3].resize(1);
    require(!memory.restore(bad),"reject bad bank sizes atomically");
    require(memory.read32(0x80000000,value)&&value==7,"bad restore leaves RAM unchanged");
    NativeAudioBackend audio(memory);NativeAudioBackend::Checkpoint sound;
    require(audio.checkpoint(sound),"idle audio checkpoint");
    require(audio.begin_frame(),"audio transaction");
    require(!audio.checkpoint(sound)&&!audio.restore(sound),"reject in-flight audio checkpoint");
    audio.abort_frame();require(audio.restore(sound),"restore idle audio");
    sound.time.outputs=1;require(!audio.restore(sound),"reject unfinished audio timestamp");
    NativeAudioStreamQueue stream;NativeAudioStreamQueue::Checkpoint reads;
    require(stream.checkpoint(reads),"idle stream checkpoint");
    require(stream.begin_frame()&&!stream.checkpoint(reads)&&!stream.restore(reads),"reject in-flight stream checkpoint");
    stream.abort_frame();require(stream.restore(reads),"restore idle stream");
    NativeServiceJournal journal;std::array<uint32_t,8> event{1,2,3,4};
    require(journal.exchange(NativeServiceKind::input,event),"record input service");
    event={5,6};require(journal.exchange(NativeServiceKind::audio,event),"record audio clock");
    auto recorded=journal.events;journal.replay(recorded);event={};
    require(journal.exchange(NativeServiceKind::input,event)&&event[3]==4,"replay exact controller sample");
    require(!journal.exchange(NativeServiceKind::video,event),"reject divergent service order");
    journal.replay(recorded);require(journal.exchange(NativeServiceKind::input,event)&&journal.exchange(NativeServiceKind::audio,event)&&event[1]==6&&journal.at_boundary(),"resume only after all phase services");
    StateWriter writer;writer.u32(0x12345678);writer.u64(0xFEDCBA9876543210ULL);writer.flag(true);writer.text("native continuation");
    StateReader reader{writer.bytes};require(reader.u32()==0x12345678&&reader.u64()==0xFEDCBA9876543210ULL&&reader.flag(),"portable archive fields");
    std::string text;require(reader.text(text)&&text=="native continuation"&&reader.finished(),"portable archive text");
    StateReader truncated{std::span<const uint8_t>(writer.bytes).first(3)};truncated.u32();require(!truncated.valid,"reject truncated fields");
    StateReader bounded{std::span<const uint8_t>(writer.bytes).subspan(16)};require(!bounded.text(text,2),"bound allocation before reading strings");
    auto directory=std::filesystem::temp_directory_path()/("t3k-checkpoint-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto file=directory/"slot.native-state";std::string error;std::vector<uint8_t> payload{77};
    require(write_native_state(file,writer.bytes,123,error),"write state file");
    require(write_native_state(file,writer.bytes,123,error),"replace an existing state atomically");
    require(read_native_state(file,payload,123,error)&&payload==writer.bytes,"read compatible state");
    auto unchanged=payload;require(!read_native_state(file,payload,124,error)&&payload==unchanged,"reject wrong game without replacing current data");
    {std::fstream corrupt(file,std::ios::in|std::ios::out|std::ios::binary);corrupt.seekp(40);corrupt.put(0);}
    require(!read_native_state(file,payload,123,error)&&payload==unchanged,"reject damaged payload without replacing current data");
    std::filesystem::remove(file);std::filesystem::remove(directory);
    std::puts("Native phase memory/audio checkpoint checks passed.");
}
