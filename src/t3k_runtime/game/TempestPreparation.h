#pragma once
#include "TempestCommands.h"
#include "TempestProjection.h"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cmath>
namespace tempest_native {
struct PreparationInput{uint32_t module,worker;std::array<uint32_t,4> context;SimulationStep step{};};
inline bool preparation_copy(CommandMemory& m,uint32_t from,uint32_t to,uint32_t words,unsigned budget=65536){
    if(words>budget)return false;
    for(uint32_t i=0;i<words;++i){uint32_t value;if(!m.read32(from+i*4,value)||!m.write32(to+i*4,value))return false;}return true;
}
inline bool preparation_byte(CommandMemory& m,uint32_t address,uint8_t value){
    uint32_t word,shift=(3-(address&3))*8;if(!m.read32(address&~3U,word))return false;
    return m.write32(address&~3U,(word&~(255U<<shift))|(uint32_t(value)<<shift));
}
// The original emits raw hexadecimal glyph indexes, not ASCII digits. It
// consumes the most significant nibble first and leaves one separator byte.
inline bool preparation_hex(CommandMemory& m,uint32_t& address,uint32_t value,unsigned digits){
    for(unsigned i=0;i<digits;++i){uint8_t digit=uint8_t(value>>28);value<<=4;if(!preparation_byte(m,address++,digit))return false;}++address;return true;
}
// Module 7 (806A86F0): complete numeric-overlay preparation, including both
// status-word and twenty-row forms, unaligned byte fields and unchanged words.
inline bool prepare_numeric_overlay(CommandMemory& m,const PreparationInput& in){
    uint32_t destination=in.context[2],source=in.context[3];
    if(source==0x8003DE4C){
        if(!preparation_copy(m,source,0x20100A10,1))return false;
        destination+=0x40;
        if(!preparation_copy(m,destination,0x20100990,6))return false;
        uint32_t value;if(!m.read32(0x20100A10,value))return false;
        uint32_t cursor=0x20100994;if(!preparation_hex(m,cursor,value<<16,2))return false;
        cursor=0x2010099C;if(!preparation_hex(m,cursor,value<<24,2))return false;
        cursor=0x201009A4;if(!preparation_hex(m,cursor,value,4)||!preparation_copy(m,0x20100990,destination,6))return false;
    }else if(source){
        if(!preparation_copy(m,source,0x20100A10,80))return false;
        destination+=0x70;
        uint32_t data=0x20100A10;
        for(unsigned row=0;row<20;++row){
            if(!preparation_copy(m,destination,0x20100990,12))return false;
            uint32_t cursor=0x20100995;
            if(!preparation_hex(m,cursor,(row>>2)<<24,2))return false;
            cursor=0x20100999;
            for(unsigned column=0;column<4;++column){uint32_t value;if(!m.read32(data,value)||!preparation_hex(m,cursor,value,8))return false;data+=4;}
            if(!preparation_copy(m,0x20100990,destination,12))return false;destination+=0x30;
        }
    }
    return m.write32(0x20100030,0)&&m.write32(0x8003DE50+in.worker*4,0);
}
inline std::array<uint32_t,4> preparation_ycc(uint32_t pixel){
    return {((pixel>>24)&255)<<22,(((pixel>>16)&255)<<22)-0x20000000U,(((pixel>>8)&255)<<22)-0x20000000U,pixel<<24};
}
inline uint32_t preparation_ycc_pack(const std::array<uint32_t,4>& v){
    auto chroma=[](uint32_t value){int32_t signedValue=int32_t(value);if(signedValue>0x1FFFFFFF)signedValue=0x1FFFFFFF;if(signedValue< -0x20000000)signedValue=-0x20000000;return uint32_t((signedValue>>22)+128);};
    uint32_t y=v[0]>>30?255:((v[0]>>22)&255);
    return (y<<24)|(chroma(v[1])<<16)|(chroma(v[2])<<8)|(v[3]>>24);
}
// Original short-vector arithmetic truncates inputs to signed high halves at
// multiplication and discards product low halves on the following vector add.
inline uint32_t preparation_blend(uint32_t oldPixel,uint32_t target,uint32_t factor,unsigned shift){
    auto old=preparation_ycc(oldPixel),colour=preparation_ycc(target);std::array<uint32_t,4> result;
    int32_t scalar=int32_t(factor)>>16;
    for(unsigned k=0;k<4;++k){uint32_t difference=colour[k]-old[k];uint32_t product=uint32_t(scalar*(int32_t(difference)>>16))<<shift;result[k]=(old[k]&0xFFFF0000)+(product&0xFFFF0000);}
    return preparation_ycc_pack(result);
}
// Module 3 (806A4F20): 16x16 texture evolution and masked colour stamps. All
// transfers precede their consumers, including descriptor/mask-buffer aliases.
inline bool prepare_texture(CommandMemory& m,const PreparationInput& in){
    if(!in.step.legacy_boundary()){
        uint32_t flags;return m.read32(0x2010000C,flags)&&m.write32(0x2010000C,flags&~7U)&&m.write32(0x20100030,0)&&m.write32(0x8003DE50+in.worker*4,0);
    }
    uint32_t count=in.context[0],texture=in.context[2],source=in.context[3];
    uint32_t descriptors=int32_t(count)>0?count:1;
    if(descriptors>8)return false;
    if(!m.write32(0x201009C4,texture)||!m.write32(0x201009C0,count)||!m.write32(0x201009C8,source)||!preparation_copy(m,in.context[1],0x20100940,descriptors*4))return false;
    uint32_t flags;if(!m.read32(0x2010000C,flags)||!m.write32(0x2010000C,flags&~7U)||!preparation_copy(m,texture,0x201000F0,256))return false;
    if(int32_t(count)<=0){
        if(!preparation_copy(m,source,0x201004F0,256)||!preparation_copy(m,0x20100940,0x201009D0,4))return false;
        uint32_t position;if(!m.read32(0x201009D4,position))return false;
        unsigned x=position>>16,y=position&65535;
        for(unsigned row=0;row<16;++row)for(unsigned col=0;col<16;++col){
            uint32_t old,target,address=0x201000F0+(row*16+col)*4;
            if(!m.read32(address,old)||!m.read32(0x201004F0+(((y+row)&15)*16+((x+col)&15))*4,target)||!m.write32(address,preparation_blend(old,target,0x0AFF0000,0)))return false;
        }
    }else{
        for(unsigned descriptor=0;descriptor<count;++descriptor){
            if(!preparation_copy(m,0x20100940+descriptor*16,0x201009D0,4))return false;
            uint32_t mask,position,factor,colour;
            if(!m.read32(0x201009D8,mask)||!preparation_copy(m,mask,0x201009A0,8)||!m.read32(0x201009D4,position)||!m.read32(0x201009DC,factor)||!m.read32(0x201009D0,colour))return false;
            unsigned x=position>>16,y=position&65535;
            for(unsigned row=0;row<16;++row){uint8_t hi,lo;if(!m.read8(0x201009A0+row*2,hi)||!m.read8(0x201009A0+row*2+1,lo))return false;uint32_t bits=(uint32_t(hi)<<8)|lo;
                for(unsigned col=0;col<16;++col)if(bits&(1U<<col)){uint32_t address=0x201000F0+(((y+row)&15)*16+((x+col)&15))*4,old;if(!m.read32(address,old)||!m.write32(address,preparation_blend(old,colour,factor,2)))return false;}
            }
        }
    }
    return preparation_copy(m,0x201000F0,texture,256)&&m.write32(0x20100030,0)&&m.write32(0x8003DE50+in.worker*4,0);
}
inline bool load_preparation_data(CommandMemory& m,uint32_t module){
    if(module>18)return false;uint32_t source,destination,bytes;
    uint32_t table=0x800B14A0+module*32;
    if(!m.read32(table+16,source)||!m.read32(table+20,destination)||!m.read32(table+24,bytes))return false;
    // Original upload includes one extra word after dividing the byte length.
    return preparation_copy(m,source,destination,(bytes>>2)+1);
}
inline bool prepare_projected_record(CommandMemory& m,const PreparationInput& in){
    for(unsigned k=0;k<4;++k)if(!m.write32(0x20100A30+k*4,in.context[k]))return false;
    uint32_t flags,resource;if(!m.read32(0x2010000C,flags)||!m.read32(0x20100020,resource))return false;
    flags&=~4U;if(!m.write32(0x2010000C,flags))return false;
    if(!(flags&1)){if(!preparation_copy(m,resource,0x201000F0,128))return false;flags|=1;}
    if(!(flags&2)){if(!preparation_copy(m,resource+0x200,0x201002F0,256))return false;flags|=2;}
    if(!m.write32(0x2010000C,flags)||!preparation_copy(m,0x800DACC0,0x20100AC0,51)||!preparation_copy(m,in.context[0],0x20100030,16))return false;
    uint32_t entry;if(!m.read32(0x20100BA0+(in.context[1]&255)*4,entry))return false;
    bool ok=false;
    switch(entry){

    case 0x20300822:ok=prepare_projection_morph(m,in.context[2],true);break;
    case 0x2030080E:ok=prepare_projection_morph(m,in.context[2],false);break;
    case 0x20300B7E:ok=prepare_projection_curve(m,in.context[2],false);break;
    case 0x20300B40:ok=prepare_projection_curve(m,in.context[2],true);break;
    case 0x20300268:ok=prepare_projection_depth_pair(m);break;
    case 0x2030073A:ok=prepare_projection_standard(m,0x88AABD00,0x14);break;
    case 0x20300218:{PreparationLane sample;ok=preparation_lane_sample(m,sample)&&m.write32(0x20100068,0x2000-sample.angle)&&m.write32(0x20100048,0x6000-sample.angle)&&preparation_project_record(m,0,sample.atY,sample.atX,false);break;}
    case 0x203003F6:ok=prepare_projection_player_camera(m);break;
    case 0x20300608:{PreparationLane sample;uint32_t pointer,colour;ok=preparation_lane_sample(m,sample)&&m.read32(0x2010003C,pointer)&&m.read32(pointer,colour)&&m.write32(0x20100B94,colour)&&m.write32(0x20100B98,0x20)&&preparation_emit_lane(m,sample,0x20);break;}
    case 0x203004DA:{PreparationLane sample;uint32_t angle;ok=preparation_lane_sample(m,sample)&&m.read32(0x20100048,angle)&&m.write32(0x20100048,angle-sample.angle+0x4000)&&preparation_project_record(m,0,sample.atY,sample.atX,false);break;}
    case 0x20300364:ok=prepare_projection_position(m);break;
    case 0x20300506:ok=prepare_projection_flat(m);break;
    case 0x2030063A:ok=prepare_projection_standard(m,in.context[2]?0xD2921000:0x88AABD00,0x14);break;
    case 0x2030072C:{uint32_t colour;ok=m.read32(0x20100038,colour)&&prepare_projection_standard(m,colour,8);break;}
    case 0x20300572:ok=prepare_projection_particles(m);break;
    case 0x2030023E:ok=prepare_projection_unscaled(m);break;
    case 0x203004B0:ok=prepare_projection_lane_record(m,false);break;
    case 0x2030048E:ok=prepare_projection_lane_record(m,true);break;
    case 0x203000F0:ok=prepare_projection_rectangle(m);break;
    default:return false;
    }
    return ok&&preparation_copy(m,0x20100030,in.context[0],16)&&m.write32(0x20100030,0)&&m.write32(0x8003DE50+in.worker*4,0);
}
#include "TempestAnimation.h"
#include "TempestShapePreparation.h"
#include "TempestDepthPreparation.h"
#include "TempestWebPreparation.h"
inline bool execute_preparation(CommandMemory& m,const PreparationInput& in){
    if(in.worker>2||!in.step.valid())return false;
    switch(in.module){case 2:return prepare_shapes(m,in);case 3:return prepare_texture(m,in);case 6:return prepare_animation(m,in);case 7:return prepare_numeric_overlay(m,in);case 10:return prepare_web(m,in);case 12:return prepare_projected_record(m,in);case 17:return prepare_depth(m,in);default:return false;}
}
}
