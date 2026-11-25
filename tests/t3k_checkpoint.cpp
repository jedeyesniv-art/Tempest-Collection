#include "t3k_runtime/native_audio_stream.h"
#include "t3k_runtime/native_state_archive.h"
#include "t3k_runtime/native_service_journal.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace tempest_native;
static void require(bool value,const char* why){if(!value){std::fprintf(stderr,"checkpoint test: %s\n",why);std::exit(1);}}
int main(){
    NativeFrameMemory memory;NativeFrameMemory::Snapshot phase;
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
