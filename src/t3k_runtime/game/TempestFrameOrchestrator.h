#pragma once
#include "TempestFrameLoop.h"
namespace tempest_native {
struct FrameRenderRequest{uint32_t zero,picture,control,list;};
struct FrameHostServices:FrameServices{
    virtual bool initialize_workers(const std::array<uint32_t,48>&)=0;
    virtual bool wait_compute()=0;
    virtual bool submit_render(const FrameRenderRequest&)=0;
    virtual bool wait_render()=0;
};
struct GameFrameInput{
    std::array<uint32_t,4> frame;
    // Original saved engine data at 800BAC50. This is a transitional data ABI,
    // not an instruction/register interpreter; only four words feed the pass.
    std::array<uint32_t,20> engineContext;
};
enum class GameFrameStage:uint32_t{setup,objects,computeFence,queue,render,renderFence,complete};
struct GameFrameResult{GameFrameStage stage;FramePassResult pass;QueueConsumption queue;FrameRenderRequest render;};
static_assert(sizeof(GameFrameInput)==96&&sizeof(GameFrameResult)==56);
// Complete semantic ordering of 800102F4..80010424. Hardware module uploads,
// communication and busy waits are replaced by explicit host service contracts.
// Failure leaves partial data writes: use a transaction and publish no picture.
inline bool prepare_game_frame(CommandMemory& m,const GameFrameInput& in,GameFrameResult& out,FrameHostServices& services){
    out={GameFrameStage::setup,{},{},{}};
    for(unsigned i=0;i<20;++i)if(!m.write32(0x800BAC50+i*4,in.engineContext[i]))return false;
    if(!m.write32(0x800DACB0,0x40181800)||!m.write32(0x80110AB4,0xFFFFFFFF)||!frame_vector_store(m,0x80110AA0,in.frame))return false;
    std::array<uint32_t,48> environment;
    for(unsigned i=0;i<48;++i)if(!m.read32(0x800B1720+i*4,environment[i]))return false;
    if(!services.initialize_workers(environment)||!m.write32(0x80110AB4,0)||!m.write32(0x800DC230,0)||
       !m.write32(0x800DC3A0,0x800DC3B0)||!m.write32(0x800DC3B0,0x80000000)||!m.write32(0x80110AB8,0x8004E040))return false;
    out.stage=GameFrameStage::objects;
    if(!frame_begin(m)||!run_frame_object_pass(m,{in.engineContext[3],in.engineContext[9],in.engineContext[10],in.engineContext[11]},out.pass,services))return false;
    out.stage=GameFrameStage::computeFence;
    if(!m.write32(0x800DC230,1)||!services.wait_compute())return false;
    out.stage=GameFrameStage::queue;
    if(!m.write32(0x800DC230,2)||!consume_object_queue(m,0x800BA7CC,out.queue)||!m.write32(0x800DC230,3))return false;
    // Exact original boundary 800103D0: preparation and queue consumption have
    // finished; the renderer has not touched the work buffer or framebuffers.
    out.stage=GameFrameStage::render;return true;
}
inline bool complete_game_frame(CommandMemory& m,GameFrameResult& out,FrameHostServices& services){
    if(out.stage!=GameFrameStage::render)return false;
    // Worker 0 clears the shared 2048-word depth/work buffer to the terminal
    // queue word (-1). The GPU backend need not use it, but its data effect is
    // retained rather than silently dropping a frame operation.
    for(unsigned i=0;i<2048;++i)if(!m.write32(0x80110C40+i*4,0xFFFFFFFF))return false;
    out.stage=GameFrameStage::render;out.render.zero=0;
    for(unsigned i=0;i<3;++i)if(!m.write32(0x8003DE50+i*4,1))return false;
    if(!m.read32(0x80110AA0,out.render.picture)||!m.read32(0x80110AA8,out.render.control)||!m.read32(0x80110AB8,out.render.list)||
       !frame_vector_store(m,0x800B18D0,{0,out.render.picture,out.render.control,out.render.list})||!services.submit_render(out.render))return false;
    out.stage=GameFrameStage::renderFence;
    if(!m.write32(0x800DC230,4)||!services.wait_render())return false;
    for(unsigned i=0;i<3;++i)if(!m.write32(0x8003DE50+i*4,0))return false;
    if(!m.write32(0x800DC230,5))return false;
    out.stage=GameFrameStage::complete;return true;
}
inline bool run_game_frame(CommandMemory& m,const GameFrameInput& in,GameFrameResult& out,FrameHostServices& services){
    return prepare_game_frame(m,in,out,services)&&complete_game_frame(m,out,services);
}
}
