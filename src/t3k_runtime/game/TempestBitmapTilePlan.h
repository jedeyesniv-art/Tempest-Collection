#pragma once
#include "TempestCommands.h"
#include <bit>
namespace tempest_native {
// 80023F16..800242EC: scratch-bounded bitmap tile planning, on the caller's
// validated positive 1..10 scale operands and inclusive 16-bit rectangle sizes.
// Unsupported/assertion inputs fail rather than manufacturing a usable plan.
// The original capacity failure is a successful function return of status 7.
inline bool plan_bitmap_tiles(CommandMemory& m,uint32_t parameters,uint32_t available,uint32_t& result){
    uint32_t nx,dx,ny,dy,height,width;
    if(!m.read32(parameters+0x4C,ny)||!m.read32(parameters+0x50,dy)||!m.read32(parameters+0x48,dx)||!m.read32(parameters+0x44,nx))return false;
    if(!nx||nx>10||!dx||dx>10||!ny||ny>10||!dy||dy>10)return false;
    uint32_t scaleY=ny>dy?ny:dy,scaleX=nx>dx?nx:dx;
    uint32_t tileWidth=scaleX,tileHeight=scaleY;
    auto fits=[&](uint32_t w,uint32_t h){return int32_t(std::bit_ceil(w)*h*4)<=int32_t(available);};
    if(!fits(tileWidth,tileHeight)){result=7;return true;}
    if(!m.read32(parameters+0x24,width)||!width||width>65536||!m.read32(parameters+0x20,height)||!height||height>65536)return false;
    uint32_t target=width>64?64:width,covered=dx;bool widthCapacity=false;
    while(covered<target){tileWidth+=scaleX;if(!fits(tileWidth,scaleY)){tileWidth-=scaleX;widthCapacity=true;break;}covered+=dx;}
    uint32_t rounded=std::bit_ceil(tileWidth);covered=dy;
    while(!widthCapacity&&covered<height){tileHeight+=scaleY;if(int32_t(tileHeight*rounded*4)>int32_t(available)){tileHeight-=scaleY;break;}covered+=dy;}
    if(tileWidth>64)tileWidth=64;rounded=std::bit_ceil(tileWidth);
    if(!m.write32(parameters,0)||!m.write32(parameters+4,0)||!m.write32(parameters+0x64,rounded)||!m.write32(parameters+0x68,tileHeight)||
       !m.write32(parameters+0x70,tileHeight/scaleY)||!m.write32(parameters+0x6C,tileWidth/scaleX))return false;
    uint32_t sourceRows=dy*tileHeight/scaleY,tileRows=(height+sourceRows-1)/sourceRows;
    if(!m.write32(parameters+0x60,tileRows))return false;
    uint32_t sourceColumns=dx*tileWidth/scaleX;
    uint32_t old,extent=(nx*width+(dx>>1))/dx;
    if(!m.read32(parameters+0x40,old))return false;if(extent<old&&!m.write32(parameters+0x40,extent))return false;
    extent=(ny*height+(dy>>1))/dy;
    if(!m.read32(parameters+0x3C,old))return false;if(extent<old&&!m.write32(parameters+0x3C,extent))return false;
    uint32_t tileColumns=(width+sourceColumns-1)/sourceColumns;
    if(!m.write32(parameters+0x5C,tileColumns)||!m.write32(parameters+8,tileRows*tileColumns)||
       !m.write32(parameters+0x54,0x40000000U/dy)||!m.write32(parameters+0x58,0x40000000U/dx))return false;
    uint32_t format,pixelShift;if(!m.read32(parameters+0x14,format)||(format&15)>=14||!m.read32(0x806A273C+(format&15)*4,pixelShift)||!m.write32(parameters+0x74,pixelShift))return false;
    result=1;return true;
}
}
