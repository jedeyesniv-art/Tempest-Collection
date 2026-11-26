#pragma once
#include "TempestFrame.h"
#include "TempestContact.h"
#include "TempestLifecycle.h"
namespace tempest_native {
struct FrameWorkerRequest{uint32_t type;std::array<uint32_t,4> context;};
struct FrameServices:CallbackServices,ObjectValueServices{
    // The recovered dispatcher forces preparation onto worker 1 and waits for
    // its previous job before installing the next. Submission may return with
    // this job pending; the final frame barrier completes it. A backend must
    // preserve that single-worker ordering and reject unknown preparation work.
    virtual bool prepare_worker_slot()=0;
    virtual bool submit_worker(const FrameWorkerRequest&)=0;
    virtual bool frame_failure(const char*,uint32_t,uint32_t){return false;}
};
struct FramePassInput{uint32_t accumulator,scratch21,scratch22,scratch23;};
struct FramePassResult{uint32_t drawEnd,objects,colliders,callbacks,workers,trap,trapValue;};
inline bool frame_vector_store(CommandMemory& m,uint32_t address,const std::array<uint32_t,4>& v){
    for(unsigned k=0;k<4;++k)if(!m.write32((address&~15U)+k*4,v[k]))return false;return true;
}
inline bool frame_dispatch(CommandMemory& m,uint32_t selector,uint32_t object,uint32_t draw,const FrameObjectLayout& layout,
                           const std::array<uint32_t,8>& args,FrameServices& services,FramePassResult& out,bool argumentWordsKnown=false,bool eventDispatch=false){
    selector&=255;
    if(selector&128){FrameWorkerRequest request{selector&127,{}};if(!services.prepare_worker_slot()||!callback_vector(m,0x80112C40,request.context)||!services.submit_worker(request))return services.frame_failure("preparation worker",object,request.type);++out.workers;return true;}
    uint32_t offset;if(!m.read32(0x801131E4+selector*4,offset))return false;
    CallbackInput input{0x800113B4+offset,object,draw,layout.values,layout.parameters,args};
    if(!services.callback_opportunity(input.entry,eventDispatch))return true;
    // These entries preserve incoming volatile words in opaque event payloads.
    // Normal event dispatch establishes them below. Do not fabricate zeros for
    // an unobserved unconditional caller with no recovered argument contract.
    if(!argumentWordsKnown){
        uint32_t flags,forwarded,pointer,count;
        if(input.entry==0x800128FE){if(!m.read32(0x800DC274,flags)||(flags&0x60))return services.frame_failure("unrecovered callback arguments",object,input.entry);}
        else if(input.entry==0x80014CD6){if(!m.read32(0x800DC274,flags)||!m.read32(0x801131B4,forwarded)||(flags&0x20)||(forwarded&0x200))return services.frame_failure("unrecovered callback arguments",object,input.entry);}
        else if(input.entry==0x800131DA){
            if(!m.read32(0x801131B4,forwarded))return false;
            if(!(forwarded&0x100000)&&(forwarded&0x2000000)){
                if(!m.read32(0x80113164,pointer)||!m.read32(pointer+0x18,count)||!callback_counter_positive(count))return false;
            }
        }
    }
    // The table dispatcher supplies the resolved address and relative base.
    input.arguments[0]=input.entry;input.arguments[1]=0x800113B4;
    CallbackResult result;if(!execute_callback(m,input,result,&services))return services.frame_failure("object callback",object,input.entry);
    ++out.callbacks;if(result.trap){out.trap=1;out.trapValue=result.trapValue;}return true;
}
inline bool frame_object_events(CommandMemory& m,uint32_t object,uint32_t draw,const FrameObjectLayout& layout,
                                FrameServices& services,FramePassResult& out,CommandScratch* scratch=nullptr){
    EventInput input{};uint32_t status;
    if(!m.read32(0x80110AC4,input.paused))return false;
    if(input.paused&1){if(scratch)scratch->second=input.paused;return true;}
    if(!m.write32(0x801131B4,0)||!m.read32(object+0x38,status)||!m.read32(object+0x30,input.interest)||!m.read32(object+0x24,input.descriptor))return false;
    input.status=status;auto selection=select_event(input);
    if(scratch){scratch->second=selection.route?status&65535:status;scratch->third=status>>16;}
    if(!selection.route)return true;
    if(!m.write32(0x801131B4,status)||!m.write32(object+0x38,selection.status))return false;
    std::array<uint32_t,8> args{};args[2]=status&65535;args[3]=status>>16;
    return frame_dispatch(m,selection.index|(selection.route==2?128:0),object,draw,layout,args,services,out,true,true);
}
inline bool frame_countdown(CommandMemory& m,uint32_t object){
    uint32_t state;if(!m.read32(0x800DC250,state))return false;
    uint32_t remaining=state&255,upper=state&0xFFFF0000;
    if(!remaining)return true;
    --remaining;uint32_t next=upper|remaining;
    auto expired=[&](){return m.write32(0x800DC250,0x80000);};
    if(!remaining)return expired();
    if(!(next&0x40000)){
        next|=0x40000;
        if(!append_event_packet(m,0x30004,{0x8010CF30,next,0x800DBCD0,upper})||!append_object_event(m,0x12D05,object))return false;
    }
    uint32_t timer=(state>>8)&255;
    if(timer){--timer;if(!timer&&(next&0x20000))return expired();
        return m.write32(0x80112D34,0x80000000)&&m.write32(0x800DC250,next|(timer<<8));
    }
    return m.write32(0x800DC250,next|0x10000);
}
// Complete MPE3 object/frame pass 8001169A..80010392, with explicit asynchronous
// worker submissions. Pixel drawing is not performed inside gameplay traversal.
inline bool run_frame_object_pass(CommandMemory& m,const FramePassInput& input,FramePassResult& out,FrameServices& services,unsigned budget=65536){
    out={};uint32_t object,draw,countdown;
    if(!m.read32(0x80113150,object)||!m.read32(0x80113154,draw)||!m.write32(0x800DBCD0,0xFFFFFFFF)||
       !m.write32(0x801131B0,0x800DBCD0)||!append_selected_object_event(m,object)||!m.write32(0x801131B0,0x800DBCD0)||!m.read32(0x800DC250,countdown)||
       (services.simulation_step().legacy_boundary()&&!frame_countdown(m,object)))return false;
    uint32_t colliders=0,records=0x80112D50,accumulator=input.accumulator,scratch22=input.scratch22;
    // 800116EC establishes r3 before the loop; the active timer path replaces
    // it with the selected-record word's address (80011768).
    CommandScratch arguments{0x801131B0,countdown&0xFFFF0000U};
    if((countdown&255)>1&&((countdown>>8)&255)&&!(((countdown>>8)&255)==1&&(countdown&0x20000)))arguments={0x80000000,0x80112D34};
    // The original enters the body unconditionally, even for an invalid null
    // head. The memory adapter, rather than an invented empty-scene path, rejects it.
    for(unsigned count=0;count<budget;++count){
        FrameObjectLayout layout;if(!prepare_frame_object(m,object,false,layout,&services))return services.frame_failure("object attributes",object,0);
        uint32_t source;if(!m.read32(object+0x18,source))return false;
        if(source){for(unsigned i=0;i<4;++i){std::array<uint32_t,4> words;if(!callback_vector(m,source+i*16,words)||!frame_vector_store(m,draw+i*16,words))return false;}arguments.second=0;}
        // The shared save vector's r21/r23 are opaque scratch. The pass assigns
        // them below only where their values are semantically established.
        std::array<uint32_t,4> save{records,input.scratch21,scratch22,input.scratch23};
        if(!frame_vector_store(m,0x80113180,save))return false;
        CommandOutput command;if(!execute_commands(m,{object,draw,layout.script,layout.values,layout.parameters,accumulator},command,4096,&arguments))return services.frame_failure("object commands",object,layout.script);accumulator=command.accumulator;
        std::array<uint32_t,4> restored;if(!callback_vector(m,0x80113180,restored))return false;records=restored[0];
        std::array<uint32_t,4> header;if(!callback_vector(m,draw,header)||!frame_vector_store(m,0x80112C40,header))return false;
        uint32_t status;if(!m.read32(object+0x38,status))return false;
        if(status&32){
            if(status&1){
                uint32_t descriptor;if(!m.read32(object+0x34,descriptor))return false;
                uint32_t offset;if(!m.read32(0x801131CC+(descriptor&255)*4,offset)||0x800113B4+offset!=0x80014A0C)return false;
                CollisionRecord record;if(!prepare_collision_record(m,{object,draw,status},record))return false;
                if(!frame_vector_store(m,records,{record.packedPosition,record.draw,record.status,record.object})||
                   !m.write32(records+16,0x8010A178)||!m.write32(records+20,0x8011316C))return false;
                records+=32;++colliders;
            }else{
                scratch22=colliders;
                CollisionBatchResult result;if(!collide_record_batch(m,{object,draw,records,colliders,status,restored[1],restored[3]},result))return services.frame_failure("collision batch",object,draw);
                if(colliders)arguments.second=result.status;
            }
        }
        uint32_t descriptor;if(!m.read32(object+8,descriptor))return false;
        uint32_t selector=descriptor;
        if(descriptor&128){if(!m.read32(object+0x24,selector))return false;selector>>=16;}
        auto dispatch=[&](uint32_t selected){std::array<uint32_t,8> words{};words[2]=arguments.second;words[3]=arguments.third;return frame_dispatch(m,selected,object,draw,layout,words,services,out,true);};
        if(!dispatch(selector&255)||out.trap)return false;
        if(!m.read32(object+0x38,status))return false;
        if(!(status&1)&&(!frame_object_events(m,object,draw,layout,services,out,&arguments)||out.trap))return false;
        if(!m.read32(object+0x18,source))return false;
        if(source){if(!callback_vector(m,draw,header)||!frame_vector_store(m,0x80112C40,header))return false;draw+=64;}
        if(!m.read32(object+8,descriptor))return false;
        if(descriptor&128){
            if(!dispatch((descriptor>>8)&127)||out.trap)return false;
            if(!m.read32(object+8,descriptor)||!dispatch(descriptor&255)||out.trap)return false;
        }
        ++out.objects;
        if(!m.read32(object+4,object))return false;
        if(!object)break;if(count+1==budget)return false;
    }
    if(!m.write32(0x80113174,draw))return false;
    for(uint32_t i=0;i<colliders;++i){
        uint32_t recordAddress=0x80112D50+i*32;std::array<uint32_t,4> record;
        if(!callback_vector(m,recordAddress,record)||!m.write32(0x8011316C,recordAddress))return false;object=record[3];
        FrameObjectLayout layout;if(!prepare_frame_object(m,object,true,layout)||!m.write32(object+0x38,record[2]))return false;
        LaneContactResult contact;if(!resolve_lane_contact(m,{object,record[1]},contact,&services))return services.frame_failure("lane contact",object,record[1]);
        if(!frame_object_events(m,object,record[1],layout,services,out)||out.trap)return false;
    }
    if(!m.read32(0x80113174,draw)||!m.write32(draw+60,0xFF)||!append_object_event(m,0xFFFFFFFF,object)||!frame_finish(m,services.simulation_step()))return false;
    out.drawEnd=draw;out.colliders=colliders;return true;
}
}
