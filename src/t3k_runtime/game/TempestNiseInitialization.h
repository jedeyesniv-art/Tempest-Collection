#pragma once
#include "TempestCommands.h"
#include <array>
namespace tempest_native {
// Host services for SDK startup. Allocation addresses belong to the host
// allocator; none are taken from an emulator checkpoint. Uploading the original
// processor program is a platform contract, never permission to execute it.
struct NiseInitializationServices {
    virtual bool initialize_media_processor(uint32_t& engine)=0;
    virtual bool local_scratch(uint32_t& address,uint32_t& bytes)=0;
    virtual bool allocate_heap(uint32_t bytes,uint32_t& address)=0;
    virtual bool allocate_dma(uint32_t bytes,uint32_t alignment,uint32_t flags,uint32_t& address)=0;
    virtual bool processor_properties(uint32_t engine,uint32_t& properties)=0;
    virtual bool upload(uint32_t engine,uint32_t destination,uint32_t source,uint32_t bytes)=0;
    virtual bool start(uint32_t engine,const std::array<uint32_t,4>& message)=0;
    virtual bool configure_audio(uint32_t operation,uint32_t argument)=0;
    virtual bool install_interrupt(uint32_t interrupt,uint32_t handler)=0;
    virtual ~NiseInitializationServices()=default;
};
namespace nise_initialization_detail {
inline bool zero(CommandMemory& m,uint32_t address,uint32_t bytes){
    if((address|bytes)&3U)return false;
    for(uint32_t i=0;i<bytes;i+=4)if(!m.write32(address+i,0))return false;
    return true;
}
inline bool copy(CommandMemory& m,uint32_t source,uint32_t destination,uint32_t bytes){
    for(uint32_t i=0;i<bytes;i+=4){uint32_t value;if(!m.read32(source+i,value)||!m.write32(destination+i,value))return false;}
    return true;
}
}
// SDK 80021D38..800220C0, supported 8K processor path used by this game.
// Returns the SDK's 0x11 success token (also on repeat initialization) or zero
// for its checked allocation failures. false means an unsupported dependency
// or invalid memory; the surrounding transaction owns rollback in that case.
// The unexercised 4K path is deliberately unsupported: its original indirect
// upload call needs separate validation before it can be reconstructed safely.
inline bool initialize_nise(CommandMemory& m,NiseInitializationServices& services,
                            const std::array<uint32_t,4>* options,uint32_t& result){
    using namespace nise_initialization_detail;
    uint32_t initialized;if(!m.read32(0x804FB374,initialized))return false;
    result=0x11;if(initialized)return true;
    uint32_t stage,scratchBytes;
    if(!services.local_scratch(stage,scratchBytes)||!m.write32(0x8003C7AC,scratchBytes)||!m.write32(0x8003C7A8,stage))return false;
    for(unsigned i=0;i<4;++i)if(!m.write32(0x8003C7D4+i*4,options?(*options)[i]:0))return false;
    uint32_t engine,pool;
    if(!m.read32(0x8003C7D4,engine)||!m.read32(0x8003C7D8,pool)||
       !m.write32(0x8003C7C0,0)||!m.write32(0x804FB378,engine)||
       !m.write32(0x8003C7C4,0)||!m.write32(0x8003C7C8,0))return false;
    if(!pool){
        if(!services.allocate_heap(0xBD0,pool)||!m.write32(0x8003C7D8,pool)||!m.write32(0x8003C7B0,pool))return false;
        if(!pool){result=0;return true;}
        if(!m.write32(0x8003C7C8,1))return false;
    }else if(!m.write32(0x8003C7B0,pool))return false;
    if(!zero(m,stage,48)||!m.write32(stage+0x10,0xFFFF))return false;
    uint32_t work,aux;
    if(!services.allocate_heap(0x3200,work)||!m.write32(stage+0x20,work)||
       !m.write32(stage+0x24,100)||!m.write32(stage+0x2C,0x20000000)||
       !m.write32(0x8003C7CC,work)||!zero(m,work,0x3200)||
       !services.allocate_heap(0x800,aux)||!m.write32(stage+0x18,aux)||
       !m.write32(0x8003C7D0,aux)||!zero(m,aux,0x800))return false;
    // Host memory is coherent. SDK cache synchronization requires no mutation.
    if(!copy(m,stage,pool,48)||!zero(m,stage,96))return false;
    for(unsigned voice=0;voice<31;++voice)if(!copy(m,stage,pool+48+voice*96,96))return false;
    uint32_t pcm;if(!m.read32(0x8003C7DC,pcm)||!m.write32(0x8003C7B4,0x2000))return false;
    if(!pcm){
        if(!services.allocate_dma(0x2000,0x2000,1,pcm)||!m.write32(0x8003C7DC,pcm))return false;
        if(!pcm){result=0;return true;}
        if(!m.write32(0x8003C7C4,1))return false;
    }
    if(!zero(m,pcm,0x2000))return false;
    uint32_t properties;if(!services.processor_properties(engine,properties))return false;
    if(properties==0xFFFFFFFF)properties=engine==0?12:0;
    if((properties&12)!=12)return false;
    if(!services.upload(engine,0x20301000,0x806B14A0,0xA68)||
       !services.upload(engine,0x20101000,0x806B1F10,0x7AC)||
       !services.start(engine,{pool,pcm,0x2000,0x81})||
       !services.configure_audio(0x130,0)||!services.configure_audio(0x128,0x100)||
       !services.configure_audio(0x140,pcm)||!services.configure_audio(0x138,0)||
       !services.configure_audio(0x118,0x281)||!services.install_interrupt(4,0x80022D60)||
       !m.write32(0x804FB374,1))return false;
    return true;
}
// Default-options wrapper 80021CF0..80021D36.
inline bool initialize_default_nise(CommandMemory& m,NiseInitializationServices& services,uint32_t& result){
    std::array<uint32_t,4> options{};
    return services.initialize_media_processor(options[0])&&initialize_nise(m,services,&options,result);
}
}
