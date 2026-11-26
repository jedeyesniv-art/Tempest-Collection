#include "t3k_fractional_motion.h"
#include "t3k_runtime/native_fractional_values.h"
#include "t3k_runtime/native_jump_dynamics.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace tempest_native;
using namespace tempest_native::timing_research;
static void check(bool value,const char* why){if(!value){std::fprintf(stderr,"fractional motion: %s\n",why);std::exit(1);}}
int main(){
    {
        FractionalMovement movement;MovementOutput output{};
        MovementInput input{950,500,65536,(3U<<8)|(3U<<24),0,1000,0,0,0,0};unsigned events=0;
        for(unsigned step=0;step<10;++step){check(movement.advance(input,5,output),"sustained clamp contact");if(output.objectStatus&0x200000)++events;input.position=output.position;input.velocity=output.velocity;input.flags=output.flags;input.objectStatus=0;}
        check(events==3,"sustained boundary contact repeats at original cadence rather than each fine step");
    }
    {
        for(unsigned divisions:{1U,2U,5U,7U}){
            NativeJumpDynamics jump;NativeJumpState state{0x200000,0x100000,10};bool thrust=false;
            for(unsigned step=0;step<divisions;++step)check(jump.advance(state,true,48,divisions,thrust)&&thrust,"jet thrust integrates each fine sample");
            check(state.velocity==0x120000&&std::abs(int64_t(state.position)-0x320000)<=2&&state.fuel==9,"subdivision retains original thrust trajectory and fuel duration");
            jump.reset();state={0xA00000,0,10};
            for(unsigned step=0;step<divisions;++step)check(jump.advance(state,false,48,divisions,thrust)&&!thrust,"released jet integrates gravity");
            check(state.velocity==uint32_t(-0x40000)&&std::abs(int64_t(state.position)-0x9C0000)<=2,"subdivision retains original ballistic gravity trajectory");
        }
        NativeJumpDynamics jump;NativeJumpState state{0,0,0};bool thrust=false;
        for(unsigned step=0;step<25;++step)check(jump.advance(state,false,48,5,thrust),"recharge sample");
        check(state.fuel==5,"fuel recharges at original elapsed rate");
        check(jump.advance(state,true,48,5,thrust)&&thrust&&state.position>0,"new jump responds before next legacy boundary");
        check(jump.advance(state,false,48,5,thrust)&&!thrust,"jump release responds on next fine step");
        jump.reset();state={0,0,10};
        for(unsigned step=0;step<25;++step)check(jump.advance(state,step%5<4,48,5,thrust),"short repeated jet pulse");
        check(state.fuel==6,"short pulses cannot evade fuel use by releasing at a legacy boundary");
    }
    {
        FractionalMovement movement;MovementOutput out{};
        MovementInput input{0,0,65536,0,uint32_t(-100000),100000,0,0,0,0};
        for(unsigned step=0;step<5;++step){input.velocity+=200;check(movement.advance(input,5,out),"fine force accepted");input.position=out.position;input.velocity=out.velocity;}
        check(out.velocity==1000&&out.position==600,"sub-cutoff force increments accumulate before the legacy-time cutoff");
    }
    {
        NativeFractionalValues registry;ObjectUpdateInput input{};input.parameters[0]=100;input.values={{0,4,0,0x80001000}};ObjectUpdateOutput output;
        check(!registry.object_values(1,64,input,output),"fractional registry requires a frame transaction");
        check(registry.begin_values()&&!registry.begin_values()&&!registry.set_divisions(1),"fractional transaction cannot nest or change cadence");
        check(registry.object_values(1,64,input,output)&&output.values[0].position==0,"first fraction retained");registry.abort_values();
        check(registry.begin_values()&&registry.object_values(1,64,input,output)&&output.values[0].position==0,"aborted frame cannot leak fractional remainder");registry.commit_values();
        check(registry.begin_values()&&registry.object_values(1,64,input,output)&&output.values[0].position==1,"accepted frame preserves remainder");registry.abort_values();
        check(registry.begin_values(),"new pool transaction");registry.object_instantiated(1);
        check(registry.object_values(1,64,input,output)&&output.values[0].position==0,"pool instantiation clears old incarnation");registry.abort_values();
        check(registry.begin_values()&&registry.object_values(1,64,input,output)&&output.values[0].position==1,"aborted pool reuse restores prior incarnation state");registry.commit_values();
    }
    {
        ObjectUpdateInput input{};input.parameters[0]=100000;input.parameters[1]=uint32_t(-100000);
        input.parameterAliases[2]=0;
        input.values={{0,1000,65536,0x80000010},{0,500,0,0x80001002}};
        FractionalObjectValues values;ObjectUpdateOutput output;
        check(values.update(input,5,output)&&output.values[0].position==200&&output.values[1].position==100,"later target alias observes earlier fine-step write");
        input.values=output.values;auto saved=values;auto valid=input;
        input.values.push_back({0,1,65536,0x80003000});ObjectUpdateOutput sentinel{123,{}};
        check(!values.update(input,5,sentinel)&&sentinel.objectStatus==123&&sentinel.values.empty(),"unsupported later record publishes no partial result");
        ObjectUpdateOutput control;check(values.update(valid,5,output)&&saved.update(valid,5,control)&&output.values[0].position==control.values[0].position&&output.values[1].position==control.values[1].position,"failure rolls fractional state back as well as value output");
        // Reset represents a new pool incarnation at the same memory address.
        ObjectUpdateInput tiny{};tiny.values={{0,4,0,0x80001000}};tiny.parameters[0]=100;
        values.reset();check(values.update(tiny,5,output)&&output.values[0].position==0,"new incarnation starts empty remainder");
        values.reset();check(values.update(tiny,5,output)&&output.values[0].position==0,"pool reuse cannot inherit fractional movement");
    }
    // Every native boundary/topology/grid/pause branch stays bit-exact at 1x.
    for(unsigned mode=0;mode<6;++mode)for(unsigned topology=0;topology<4;++topology)
    for(unsigned paused=0;paused<2;++paused)for(int direction:{-1,1})for(bool grid:{false,true}){
        MovementInput in{uint32_t(direction*65000),uint32_t(direction*1000),65536,
            uint32_t((mode<<8)|(grid?1U<<26:0)|(3U<<24)|(1U<<27)),uint32_t(-65536),65536,paused,topology,0,0};
        FractionalMovement movement;MovementOutput actual{};auto expected=movement_step(in);
        check(movement.advance(in,1,actual),"all original boundary branches supported at 1x");
        check(actual.position==expected.position&&actual.velocity==expected.velocity&&actual.flags==expected.flags&&actual.objectStatus==expected.objectStatus,"1x boundary/topology/grid/pause parity");
    }
    for(unsigned mode=0;mode<6;++mode)for(unsigned topology=0;topology<2;++topology)for(int direction:{-1,1}){
        FractionalMovement movement;MovementOutput out{};
        MovementInput in{uint32_t(direction<0?50:950),uint32_t(direction*500),65536,uint32_t((mode<<8)|(3U<<24)|(1U<<27)),0,1000,0,topology,0,0};
        check(movement.advance(in,5,out),"fine-step bounded movement accepted");
        check((out.flags&(direction<0?0x10000:0x20000))&&(out.objectStatus&0x200000),"fine-step crossing publishes directional and interest events");
        unsigned effective=mode<4?mode:topology?1:mode==4?3:2;
        if(effective==1)check(out.position==(direction<0?950U:50U),"wrap retains fine-step overshoot");
        if(effective==2)check(out.position==in.position&&out.velocity==0U-in.velocity,"bounce undoes actual fine-step displacement");
        if(effective==3)check(out.position==(direction<0?0U:1000U)&&out.velocity==(direction<0?0U:500U),"clamp retains native asymmetric velocity rule");
    }
    {
        FractionalMovement movement;MovementOutput out{};
        MovementInput in{0,1000,65536,0,uint32_t(-100000),100000,0,0,0,0};
        check(movement.advance(in,5,out)&&out.position==200,"fine movement initial sample");
        in.position=out.position;in.velocity=uint32_t(-1000);
        check(movement.advance(in,5,out)&&out.position==0,"new force reverses on next actual substep");
        in.position=65000;in.velocity=1000;in.flags=1U<<26;
        check(movement.advance(in,5,out)&&out.position==65200&&!(out.objectStatus&0x800000),"grid snap uses fine displacement");
        in.position=out.position;in.velocity=out.velocity;check(movement.advance(in,5,out)&&out.position==65400,"grid crossing not premature");
        in.position=out.position;check(movement.advance(in,5,out)&&out.position==65536&&(out.objectStatus&0x800000),"grid crossing reports actual fine-step crossing");
    }
    unsigned cases=0;
    for(uint32_t scale:{0U,4096U,61440U,65536U,69632U})
    for(int32_t velocity:{-100000,-1274,-255,-254,-1,0,1,254,255,1274,100000})
    for(unsigned divisions:{1U,2U,5U,7U,120U}){
        MovementInput input{0,uint32_t(velocity),scale,0x80000000U, uint32_t(-10000000),10000000,0,0,0,0};
        auto expected=movement_step(input);FreeFlightSegment segment;
        check(segment.begin(input,divisions),"supported free flight accepted");
        MovementOutput sample{};int32_t previous=0;
        for(unsigned i=0;i<divisions;++i){
            check(segment.advance(sample),"all requested substeps available");
            check(int32_t(expected.velocity)<0?int32_t(sample.position)<=previous:int32_t(sample.position)>=previous,"monotonic free flight");
            previous=int32_t(sample.position);
        }
        check(sample.position==expected.position && sample.velocity==expected.velocity && sample.flags==expected.flags && sample.objectStatus==expected.objectStatus,"exact original endpoint including integer damping and cutoff");
        check(!segment.advance(sample),"segment cannot advance beyond its owned tick");++cases;
    }
    MovementInput input{0,1000,65536,0, uint32_t(-100000),100000,0,0,0,0};
    FreeFlightSegment segment;MovementOutput sample{};
    check(segment.begin(input,5)&&segment.advance(sample)&&sample.position==200&&sample.velocity==1000,"constant-rate motion preserves original velocity units");
    input.velocity=300;check(segment.begin(input,5)&&segment.advance(sample)&&sample.position==60&&sample.velocity==300,"slow motion survives five-way subdivision");
    input.velocity=65536;input.scale=4096;
    check(segment.begin(input,2)&&segment.advance(sample)&&sample.velocity==16384&&sample.position==3277,"damped half tick uses fractional dynamics rather than a linear velocity blend");
    input.velocity=300;input.scale=65536;
    input.upper=300;check(!segment.begin(input,5)&&!segment.advance(sample),"boundary crossing explicitly unsupported and invalidates prior segment");
    input.upper=100000;input.flags=1U<<26;check(!segment.begin(input,5),"grid snapping requires separate policy");
    input.flags=0;input.paused=1;check(!segment.begin(input,5),"pause path requires separate policy");
    for(uint32_t speed:{0U,1U,4U,5U,6U,254U,65536U})
    for(int32_t target:{-100000,0,100000}){
        ObjectValue legacy{0,speed,0,0},fractional=legacy;FractionalApproach approach;
        for(unsigned tick=0;tick<31;++tick){
            legacy=approach_parameter(legacy,uint32_t(target),false);
            for(unsigned substep=0;substep<5;++substep)check(approach.advance(fractional,uint32_t(target),false,5),"fractional approach accepted");
            check(fractional.position==legacy.position,"approach retains integer remainder and exact whole-tick target clamp");
        }
    }
    FractionalApproach approach;ObjectValue value{0,6,0,0};
    check(approach.advance(value,100,false,5)&&value.position==1,"approach first substep");
    auto held=value;check(approach.advance(value,100,true,5)&&value.position==held.position,"pause does not advance approach or its remainder");
    check(approach.advance(value,uint32_t(-100),false,5)&&value.position==0,"fresh target reverses motion immediately");
    value.velocity=100;check(approach.advance(value,1,false,5)&&value.position==1,"substep target clamp");
    value.velocity=1;check(approach.advance(value,100,false,5)&&value.position==1,"clamp does not bank unused displacement");
    // Rate changes must not reinterpret fractions left by a previous divisor.
    FractionalApproach changing;ObjectValue tiny{0,4,0,0};
    check(changing.advance(tiny,100,false,5)&&tiny.position==0,"retain four fifths");
    check(changing.advance(tiny,100,false,7)&&tiny.position==0,"changing denominator discards incompatible remainder");
    // Exact anchoring must not introduce drift over successive legacy ticks.
    MovementInput chain{0,100000,61440,0,uint32_t(-10000000),10000000,0,0,0,0};
    for(unsigned tick=0;tick<200;++tick){
        auto endpoint=movement_step(chain);check(segment.begin(chain,5),"successive damped segment accepted");
        for(unsigned step=0;step<5;++step)check(segment.advance(sample),"successive substep accepted");
        check(sample.position==endpoint.position&&sample.velocity==endpoint.velocity,"no accumulated whole-tick drift");
        chain.position=sample.position;chain.velocity=sample.velocity;
    }
    std::printf("Fractional free-flight: %u exact endpoint cases; slow-motion, boundary, grid and pause contracts passed.\n",cases);
}
