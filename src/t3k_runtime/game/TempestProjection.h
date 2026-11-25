#pragma once
#include "TempestCommands.h"
#include <array>
#include <cstdlib>
namespace tempest_native {
inline uint32_t preparation_shift(uint32_t value,int amount,bool arithmetic=false){
    if(amount<0)return -amount>=32?0:value<<unsigned(-amount);
    if(amount>=32)return arithmetic&&int32_t(value)<0?0xFFFFFFFF:0;
    return arithmetic?uint32_t(int32_t(value)>>amount):value>>amount;
}
// Same recovered polynomial used by John's shape renderer, kept independent
// of SDL. Angle is one turn / 65536; outputs are signed 2.30 cosine and sine.
inline std::array<uint32_t,2> preparation_sincos(uint32_t angle){
    uint32_t x=(angle<<18)>>2,c=0x40000000,s=0;
    if(x){uint32_t inverse=0x40000000-x,u=command_mul(x,x,30),v=command_mul(inverse,inverse,30);
        auto polynomial=[](uint32_t t){uint32_t p=command_mul(t,0xFFB901AF,30)+0x051573CA;p=command_mul(t,p,30)+0xD6A9B10A;return command_mul(t,p,30)+0x6487D705;};
        s=command_mul(x,polynomial(u),30);c=command_mul(inverse,polynomial(v),30);
    }
    switch((angle>>14)&3){case 0:return {c,s};case 1:return {0U-s,c};case 2:return {0U-c,0U-s};default:return {s,0U-c};}
}
struct PreparationLane{uint32_t angle,atY,atX,middleY,middleX,halfY,halfX;};
inline bool preparation_lane(CommandMemory& m,uint32_t position,PreparationLane& out){
    uint32_t index=((position>>16)&15)*4,a,b;
    if(!m.read32(0x20100B00+index,a)||!m.read32(0x20100B04+index,b)||!m.read32(0x20100AC0+index,out.angle))return false;
    uint32_t y0=uint32_t(int32_t(a)>>16)<<16,x0=a<<16,y1=uint32_t(int32_t(b)>>16)<<16,x1=b<<16;
    uint32_t dy=y1-y0,dx=x1-x0,fraction=position&65535;
    out.atY=y0+command_mul(dy,fraction,16);out.atX=x0+command_mul(dx,fraction,16);
    out.halfY=uint32_t(int32_t(dy)>>1);out.halfX=uint32_t(int32_t(dx)>>1);out.middleY=y0+out.halfY;out.middleX=x0+out.halfX;
    return m.write32(0x20100B90,out.atY)&&m.write32(0x20100B8C,out.atX);
}
inline bool preparation_projection_scale(CommandMemory& m,uint32_t depth,uint32_t& scale){
    uint32_t denominator=depth+0x800000;
    if(int32_t(denominator)<=0){scale=0;return true;}
    unsigned bits=0;for(uint32_t v=denominator;v;v>>=1)++bits;
    uint32_t normalized=preparation_shift(denominator,int(bits)-10,true),seed;
    if(!m.read32(0x200FFEF0+normalized,seed))return false;
    uint32_t error=0x40000000-command_mul(seed,denominator,bits);
    uint32_t refined=command_mul(error,seed,29);
    error=0x40000000-command_mul(refined,denominator,bits);
    uint32_t reciprocal=command_mul(refined,error,29);
    scale=command_mul(0xC000,preparation_shift(reciprocal,int(bits)-15),16);return true;
}
inline uint32_t preparation_screen_position(uint32_t y,uint32_t x,uint32_t scale){
    y=command_mul(scale,y,19)+0x780000;x=command_mul(scale,x,19)+0xB40000;
    return (x&0xFFFF0000)|(y>>16);
}
inline bool preparation_reciprocal(CommandMemory& m,uint32_t denominator,int precision,int shift,uint32_t& out){
    if(int32_t(denominator)<=0){out=0;return true;}
    unsigned bits=0;for(uint32_t v=denominator;v;v>>=1)++bits;uint32_t seed;
    if(!m.read32(0x200FFEF0+preparation_shift(denominator,int(bits)-10,true),seed))return false;
    uint32_t refined=command_mul(0x40000000-command_mul(seed,denominator,bits),seed,29);
    uint32_t reciprocal=command_mul(refined,0x40000000-command_mul(refined,denominator,bits),29);
    out=preparation_shift(reciprocal,int(bits)+29-precision-shift);return true;
}
inline bool prepare_projection_position(CommandMemory& m){
    uint32_t lane,depth,cameraA,cameraB,cameraDepth,extraA,extraB;
    if(!m.read32(0x20100050,lane))return false;PreparationLane sample;if(!preparation_lane(m,lane,sample))return false;
    if(!m.read32(0x20100B80,cameraA)||!m.read32(0x20100B84,cameraB)||!m.read32(0x20100B88,cameraDepth)||
       !m.read32(0x20100054,depth)||!m.read32(0x20100058,extraA)||!m.read32(0x2010005C,extraB))return false;
    uint32_t y=uint32_t(int32_t(sample.atY)>>1)+(cameraA<<3)+extraA;
    uint32_t x=uint32_t(int32_t(sample.atX)>>1)+(cameraB<<3)+extraB,scale;
    if(!preparation_projection_scale(m,depth+cameraDepth,scale))return false;
    return m.write32(0x20100030,preparation_screen_position(y,x,scale));
}
inline bool prepare_projection_unscaled(CommandMemory& m){
    uint32_t position,a,b;if(!m.read32(0x20100030,position)||!m.read32(0x20100B80,a)||!m.read32(0x20100B84,b))return false;
    // The original computes coordinates, then executes RTS,NOP (no delay).
    // Its following OR/store are unreachable. The caller discards the scalar
    // results and writes back the unchanged record. Do not "repair" this path.
    return true;
}
inline bool prepare_projection_lane_record(CommandMemory& m,bool angle){
    uint32_t position,depth;if(!m.read32(0x20100050,position)||!m.read32(0x20100054,depth))return false;
    PreparationLane sample;if(!preparation_lane(m,position,sample))return false;
    return (!angle||m.write32(0x20100048,sample.angle+0x4000))&&m.write32(0x20100030,sample.atX)&&m.write32(0x20100034,sample.atY)&&m.write32(0x20100038,depth);
}
inline bool prepare_projection_rectangle(CommandMemory& m){
    uint32_t context,size,angle;if(!m.read32(0x20100A34,context)||!m.read32(0x20100038,size)||!m.read32(0x2010003C,angle))return false;
    uint32_t centre=0x180+uint32_t(int32_t(context)>>16);
    std::array<uint32_t,4> vertical{command_mul(0xCCCCCC,centre,24),command_mul(0xCCCCCC,centre-0x300,24),command_mul(0x30C30C,centre,24),command_mul(0x30C30C,centre-0x300,24)};
    uint32_t widthA=command_mul(0xCCCCCC,size,24),widthB=command_mul(0x30C30C,size,24);auto cs=preparation_sincos(angle);
    for(unsigned i=0;i<4;++i){uint32_t a=vertical[i],b=i<2?widthA:widthB;
        uint32_t lo=command_mul(cs[0],a,30)+command_mul(cs[1],b,30),hi=command_mul(0U-cs[1],a,30)+command_mul(cs[0],b,30);
        if(!m.write32(0x20100040+i*8,(lo&65535)|(hi<<16)))return false;
    }
    return true;
}
inline bool preparation_camera_scratch(CommandMemory& m,uint32_t x,uint32_t y){
    x=uint32_t(int32_t(x)>>1);y=uint32_t(int32_t(y)>>1);
    return m.write32(0x20100A40,x)&&m.write32(0x20100A44,y)&&m.write32(0x20100A48,uint32_t(int32_t(x)>>4))&&m.write32(0x20100A4C,uint32_t(int32_t(y)>>4))&&m.write32(0x20100A50,0);
}
inline bool preparation_project_record(CommandMemory& m,uint32_t angle,uint32_t y,uint32_t x,bool setAngle=true){
    if(setAngle&&!m.write32(0x20100048,0x4000-angle))return false;
    uint32_t a,b,depth,cameraDepth;if(!m.read32(0x20100B80,a)||!m.read32(0x20100B84,b)||!m.read32(0x20100B88,cameraDepth)||!m.read32(0x20100054,depth))return false;
    y=uint32_t(int32_t(y)>>1)+(a<<3);x=uint32_t(int32_t(x)>>1)+(b<<3);depth+=cameraDepth;
    if(int32_t(depth)<=0xA0000){uint32_t type;depth=0xA0000;if(!m.read32(0x2010006C,type)||!m.write32(0x2010006C,(type&0xFFFFFF00)|0xFE))return false;}
    uint32_t scale,atX,atY,size,detail;
    if(!preparation_projection_scale(m,depth,scale)||!m.read32(0x20100B8C,atX)||!m.read32(0x20100B90,atY)||!preparation_camera_scratch(m,atX,atY)||!m.read32(0x20100040,size)||!m.read32(0x20100044,detail))return false;
    uint32_t lo=command_mul(scale,uint32_t(int32_t(size)>>16),19)&65535,hi=command_mul(scale,size&65535,19);
    uint32_t lod=command_mul(scale,detail>>16,18);if(int32_t(lod)>=0xEFFF)lod=0xEFFF;
    return m.write32(0x20100040,(hi<<16)|lo)&&m.write32(0x20100044,(detail&65535)|(lod<<16))&&m.write32(0x20100030,preparation_screen_position(y,x,scale));
}
inline bool preparation_emit_point(CommandMemory& m,uint32_t packed,uint32_t depth){
    if(int32_t(depth)<=0xA0000)return true;
    uint32_t colour,size,cursor;if(!m.read32(0x20100B94,colour)||!m.read32(0x20100B98,size))return false;
    std::array<uint32_t,8> words{packed,depth,colour,size,0x80000000,depth,colour,size};
    for(unsigned i=0;i<8;++i)if(!m.write32(0x20100A40+i*4,words[i]))return false;
    if(!m.read32(0x800DC3A0,cursor)||!m.write32(0x20100A60,cursor))return false;
    for(unsigned i=0;i<8;++i)if(!m.write32(cursor+i*4,words[i]))return false;
    return m.write32(0x20100A60,cursor+16)&&m.write32(0x800DC3A0,cursor+16);
}
inline bool preparation_lane_sample(CommandMemory& m,PreparationLane& sample){uint32_t position;return m.read32(0x20100050,position)&&preparation_lane(m,position,sample);}
inline bool preparation_emit_lane(CommandMemory& m,const PreparationLane& sample,uint32_t angle){
    uint32_t depth,cameraDepth;if(!m.read32(0x20100054,depth)||!m.read32(0x20100B88,cameraDepth))return false;
    uint32_t packed=(uint32_t(int32_t(sample.atX)>>17)<<16)|(uint32_t(int32_t(sample.atY)>>17)&65535);
    return preparation_emit_point(m,packed,depth+cameraDepth)&&preparation_project_record(m,angle,sample.atY,sample.atX);
}
inline bool prepare_projection_standard(CommandMemory& m,uint32_t colour,uint32_t kind){
    if(!m.write32(0x20100B94,colour))return false;PreparationLane sample;if(!preparation_lane_sample(m,sample))return false;
    uint32_t camera,angle,size;if(!m.read32(0x20100B84,camera)||!m.read32(0x20100048,angle)||!m.read32(0x20100068,size))return false;
    angle+=sample.angle+uint32_t(int32_t(camera)>>5);
    if(!m.write32(0x20100B98,(size<<16)|kind))return false;
    return preparation_emit_lane(m,sample,angle);
}
inline bool prepare_projection_player_camera(CommandMemory& m){
    uint32_t position,depth;if(!m.read32(0x20100050,position)||!m.read32(0x20100054,depth)||!m.write32(0x800DC100,position)||!m.write32(0x800DC104,depth))return false;
    uint32_t index=((position>>16)&15)*4,point,angle;
    if(!m.read32(0x20100B00+index,point)||!m.read32(0x20100AC0+index,angle))return false;
    uint32_t x=point<<16,y=uint32_t(int32_t(point)>>16)<<16;
    if(!preparation_camera_scratch(m,x,y))return false;
    for(unsigned k=0;k<5;++k){uint32_t word;if(!m.read32(0x20100A40+k*4,word)||!m.write32(0x800DC114+k*4,word))return false;}
    return preparation_project_record(m,angle,y,x);
}
inline bool prepare_projection_flat(CommandMemory& m){
    uint32_t depth,camera;if(!m.read32(0x20100040,depth)||!m.read32(0x20100B88,camera)||!m.write32(0x20100040,depth+camera))return false;depth+=camera;
    if(int32_t(depth)<=0xA0000){uint32_t type;return m.read32(0x2010006C,type)&&m.write32(0x2010006C,(type&0xFFFFFF00)|0xFE);}
    uint32_t position;if(!m.read32(0x20100030,position))return false;PreparationLane sample;if(!preparation_lane(m,position,sample))return false;uint32_t a,b;if(!m.read32(0x20100B80,a)||!m.read32(0x20100B84,b))return false;
    uint32_t y=sample.atY+(a<<4),x=sample.atX+(b<<4);return m.write32(0x20100030,(x&0xFFFF0000)|(y>>16));
}
inline bool prepare_projection_particles(CommandMemory& m,unsigned budget=4096){
    uint32_t count,base,amplitude;if(!m.read32(0x20100064,count)||!m.read32(0x20100050,base)||!m.read32(0x20100058,amplitude))return false;
    if(!count)return true;uint32_t seed=0x390FD92C,step=0x10000;
    for(unsigned i=0;i<budget;++i){
        uint32_t fraction=(command_mul(base,step,16)+seed)&65535;
        if(!m.write32(0x20100054,(((seed>>14)&4095)+0x800)<<16)||!m.write32(0x20100050,command_mul(fraction,amplitude,16)))return false;
        PreparationLane sample;if(!preparation_lane_sample(m,sample)||!m.write32(0x20100B94,0x51F05A00)||!m.write32(0x20100B98,0x10)||!preparation_emit_lane(m,sample,0x10))return false;
        step+=0x800;seed=(seed>>1)^((seed&1)?0xA3000000:0);
        bool more=int64_t(int32_t(count))-1>0;--count;if(!more)return true;
    }
    return false;
}
inline bool preparation_morph(CommandMemory& m,uint32_t phase,unsigned budget=192){
    uint32_t descriptor,count,cursor,source;
    if(!m.read32(0x2010004C,descriptor)||!m.read32(0x20100068,source))return false;
    count=descriptor&65535;if(!count||count*2>budget)return false;
    if(!m.read32(0x800DACB0,cursor)||!m.write32(0x2010004C,cursor))return false;
    for(unsigned i=0;i<count*2;++i){uint32_t v;if(!m.read32(source+i*4,v)||!m.write32(0x201006F0+count*4+i*4,v))return false;}
    uint32_t fraction=phase&65535;
    fraction=descriptor&0x80000000?fraction>>1:uint32_t(std::abs(int32_t(fraction)-0x7FFF));
    uint32_t from=0x201006F0+count*4,to=0x201006F0,other=0x201006F0+count*8,remaining=count;
    for(unsigned iterations=0;iterations<budget;++iterations){
        uint32_t a;if(!m.read32(from,a))return false;--remaining;
        if((a>>16)==0x8000){
            if(!m.write32(to,a))return false;to+=4;if(!remaining)break;from+=4;other+=4;
            if((a&255)==6){uint32_t extra;if(!m.read32(from,extra)||!m.write32(to,extra))return false;--remaining;from+=4;other+=4;to+=4;}
        }else{
            if(!remaining)break;uint32_t b;if(!m.read32(other,b))return false;from+=4;other+=4;
            uint32_t x=a<<16,y=uint32_t(int32_t(a)>>16)<<16;
            uint32_t outX=x+command_mul(fraction,(b<<16)-x,15),outY=y+command_mul(fraction,(uint32_t(int32_t(b)>>16)<<16)-y,15);
            if(!(descriptor&0x80000000)&&(phase&0x8000))outX=0U-outX;
            if(!m.write32(to,((outY>>16)<<16)|(outX>>16)|1))return false;to+=4;
        }
        if(!remaining)return false;
        if(iterations+1==budget)return false;
    }
    for(unsigned i=0;i<count;++i){uint32_t v;if(!m.read32(0x201006F0+i*4,v)||!m.write32(cursor+i*4,v))return false;}
    return m.write32(0x201006F0,cursor+count*4)&&m.write32(0x800DACB0,cursor+count*4);
}
inline bool prepare_projection_morph(CommandMemory& m,uint32_t context,bool camera){
    uint32_t phase;if(!m.read32(0x20100050,phase)||!preparation_morph(m,camera&&(context&1)?0x8000:phase))return false;
    PreparationLane sample;if(!preparation_lane(m,phase,sample))return false;
    uint32_t y=camera&&(context&1)?sample.atY:sample.middleY,x=camera&&(context&1)?sample.atX:sample.middleX;
    if(!preparation_project_record(m,sample.angle,y,x))return false;
    if(camera)for(unsigned k=0;k<5;++k){uint32_t v;if(!m.read32(0x20100A40+k*4,v)||!m.write32(0x800DC114+k*4,v))return false;}
    return true;
}
inline bool preparation_curve_angle(CommandMemory& m,uint32_t position,uint32_t context,bool angleOnly,uint32_t& angle,uint32_t& vertex,uint32_t& turn){
    uint32_t index=(position>>16)&15,first,second,next=index+1;
    if(next>=((context>>8)&31))next=0;
    if(!m.read32(0x20100AC0+index*4,first)||!m.read32(0x20100AC0+next*4,second))return false;
    first=uint32_t(int32_t(int16_t(first)));second=uint32_t(int32_t(int16_t(second)));
    uint32_t delta=uint32_t(int32_t(int16_t(second-first-(angleOnly?0x10000:0x8000))));
    if(int32_t(delta)>=0)delta-=0xFFFF;
    delta=command_mul(position&65535,delta,16);angle=first+delta;turn=first+0x8000+delta;vertex=0x20100B04+index*4;return true;
}
inline bool prepare_projection_curve(CommandMemory& m,uint32_t context,bool angleOnly){
    uint32_t position,angle,vertex,turn;if(!m.read32(0x20100050,position)||!preparation_curve_angle(m,position,context,angleOnly,angle,vertex,turn))return false;
    if(angleOnly){
        if(!m.write32(0x20100048,0x4000-angle))return false;PreparationLane sample;
        return preparation_lane(m,position+0x8000,sample)&&preparation_project_record(m,0,sample.atY,sample.atX,false);
    }
    uint32_t point,a,b,depth,cameraDepth,size,detail;
    auto cs=preparation_sincos(turn);
    if(!m.read32(vertex,point)||!m.read32(0x20100B80,a)||!m.read32(0x20100B84,b)||!m.read32(0x20100B88,cameraDepth)||!m.read32(0x20100054,depth)||!m.read32(0x20100040,size)||!m.read32(0x20100044,detail))return false;
    uint32_t y=(uint32_t(int32_t(point)>>16)<<16)+uint32_t(int32_t(cs[0])>>9),x=(point<<16)+uint32_t(int32_t(cs[1])>>9);depth+=cameraDepth;
    if(int32_t(depth)<=0xA0000){uint32_t type;depth=0xA0000;if(!m.read32(0x2010006C,type)||!m.write32(0x2010006C,(type&0xFFFFFF00)|0xFE))return false;}
    uint32_t scale;if(!preparation_projection_scale(m,depth,scale))return false;
    y=command_mul(scale,uint32_t(int32_t(y)>>1),19)+command_mul(scale,a<<3,19);
    x=command_mul(scale,uint32_t(int32_t(x)>>1),19)+command_mul(scale,b<<3,19);
    uint32_t lod=command_mul(scale,detail>>16,18);if(int32_t(lod)>=0xCFFF)lod=0xCFFF;
    uint32_t packedSize=(command_mul(scale,size&65535,19)<<16)|(command_mul(scale,uint32_t(int32_t(size)>>16),19)&65535);
    return m.write32(0x20100044,(lod<<16)|(detail&65535))&&m.write32(0x20100040,packedSize)&&m.write32(0x20100048,0x4000-angle)&&m.write32(0x20100030,((x+0xB40000)&0xFFFF0000)|((y+0x780000)>>16));
}
inline bool preparation_depth_point(CommandMemory& m,uint32_t& packed){
    uint32_t position,depth,a,b,camera,scale;if(!m.read32(0x20100050,position))return false;PreparationLane sample;
    if(!preparation_lane(m,position,sample)||!m.read32(0x20100054,depth)||!m.read32(0x20100B80,a)||!m.read32(0x20100B84,b)||!m.read32(0x20100B88,camera)||!preparation_projection_scale(m,depth+camera,scale))return false;
    uint32_t y=command_mul(scale,sample.atY+(a<<4),19),x=command_mul(scale,sample.atX+(b<<4),19);
    packed=(y&0xFFFF0000)|(x>>16);return true;
}
inline bool prepare_projection_depth_pair(CommandMemory& m){
    uint32_t depth,first,second,cursor;if(!m.read32(0x20100054,depth)||!m.write32(0x20100054,0x800000)||!preparation_depth_point(m,first)||!m.write32(0x20100054,depth)||!m.write32(0x20100038,first)||!preparation_depth_point(m,second)||!m.write32(0x20100034,second)||!m.read32(0x80110B10,cursor)||!m.write32(0x20100030,cursor)||!m.write32(0x2010003C,0x0F10551E))return false;
    for(unsigned k=0;k<3;++k){uint32_t v;if(!m.read32(0x20100034+k*4,v)||!m.write32(cursor+k*4,v))return false;}
    return m.write32(0x20100030,cursor+8)&&m.write32(0x80110B10,cursor+8);
}
}
