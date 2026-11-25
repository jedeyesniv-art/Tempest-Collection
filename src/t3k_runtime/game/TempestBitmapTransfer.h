#pragma once
#include "TempestBitmapBlit.h"
#include "TempestBitmapTilePlan.h"
#include "TempestGraphicsDispatch.h"
namespace tempest_native {
struct BitmapRectangle {
    uint32_t address=0;bool provided=false;std::array<uint32_t,2> words{};
    bool read(CommandMemory& m,unsigned index,uint32_t& value)const{if(!provided||index>1)return false;if(address)return m.read32(address+index*4,value);value=words[index];return true;}
};
struct BitmapTransferInput {
    uint32_t context,source,destination,stack;
    BitmapRectangle sourceRectangle,destinationRectangle;
    uint32_t numeratorX,denominatorX,numeratorY,denominatorY;
};
// Complete SDK parameter builder 8002434E..800249CC, for its validated scale
// operands, supported source formats and live nonoverlapping allocations.
// Rectangles may be caller-owned data or native private values. No guest stack
// is fabricated for private rectangles or the scratch-size output.
inline bool build_bitmap_transfer(CommandMemory& m,GraphicsDispatchServices& services,const BitmapTransferInput& input,uint32_t& result){
    auto sourceRect=input.sourceRectangle,destinationRect=input.destinationRectangle;
    if(!input.source||!input.destination)return false;
    uint32_t sourceFlags;if(!m.read32(input.source+12,sourceFlags))return false;uint32_t format=(sourceFlags>>4)&15;
    if(format>=10||format==2||format==7)return false;
    if(format==1||format==3){uint32_t palette;if(!m.read32(input.source+16,palette)||!palette)return false;}
    uint32_t parameters;if(!allocate_c_heap(m,0x7C,input.stack-0xA0,parameters)||!parameters)return false;
    // A freshly allocated parameter block cannot overlap another live object.
    // Refuse malformed aliases rather than caching changed caller data.
    auto overlaps=[&](uint32_t address,uint32_t bytes){return uint64_t(parameters)<uint64_t(address)+bytes&&uint64_t(address)<uint64_t(parameters)+0x7C;};
    if(overlaps(input.context,0xD8)||overlaps(input.source,20)||overlaps(input.destination,20)||
       (sourceRect.address&&overlaps(sourceRect.address,8))||(destinationRect.address&&overlaps(destinationRect.address,8)))return false;
    const uint32_t nx=input.numeratorX,dx=input.denominatorX,ny=input.numeratorY,dy=input.denominatorY;
    if(!nx||nx>10||!dx||dx>10||!ny||ny>10||!dy||dy>10)return false;
    if(!sourceRect.provided){uint32_t dimensions;if(!m.read32(input.source+8,dimensions))return false;sourceRect.provided=true;sourceRect.words={0,((((dimensions>>16)-1)&65535)<<16)|((dimensions-1)&65535)};}
    uint32_t first,last;if(!sourceRect.read(m,0,first)||!sourceRect.read(m,1,last))return false;
    if((last>>16)<(first>>16)||(last&65535)<(first&65535)){result=0xB;return true;}
    uint32_t width=(last>>16)-(first>>16)+1,height=(last&65535)-(first&65535)+1;
    uint32_t shift,rowFlags,pixels;if(!m.read32(0x806A2774+format*4,shift)||!m.read32(input.source,rowFlags)||!m.read32(input.source+4,pixels))return false;
    uint32_t stride=rowFlags>>16;if(shift==0xFFFFFFFF)stride>>=1;else if(shift<=4)stride<<=shift;else return false;
    if(!m.write32(parameters+0x0C,pixels)||!m.write32(parameters+0x10,stride))return false;
    uint32_t transparent;if(!m.read32(input.context+0xC4,transparent)||!m.write32(parameters+0x14,format))return false;
    if(transparent){if(!m.write32(parameters+0x14,format|0x200))return false;
        if(format!=3){uint32_t color;if(!m.read32(input.context+0xC8,color)||!m.write32(parameters+0x78,color))return false;}
    }else{uint32_t keyed;if(!m.read32(input.context+0x5C,keyed))return false;if(keyed&&!m.write32(parameters+0x14,format|0x100))return false;}
    if(!m.write32(parameters+0x18,first&65535)||!m.write32(parameters+0x1C,first>>16)||
       !m.write32(parameters+0x20,height)||!m.write32(parameters+0x24,width))return false;
    uint32_t destinationFlags,destinationPixels;if(!m.read32(input.destination,destinationFlags)||!m.read32(input.destination+4,destinationPixels))return false;
    uint32_t destinationType=destinationFlags&0xF0;if(destinationType==0x20)destinationType=0x80;
    if(!m.write32(parameters+0x28,destinationPixels)||!m.write32(parameters+0x2C,(destinationFlags>>13)&~7U)||
       !m.write32(parameters+0x30,destinationType|(destinationFlags&0x800)))return false;
    if(!destinationRect.provided){uint32_t dimensions;if(!m.read32(input.destination+8,dimensions))return false;destinationRect.provided=true;destinationRect.words={0,((((dimensions>>16)-1)&65535)<<16)|((dimensions-1)&65535)};}
    uint32_t destinationFirst,destinationLast;if(!destinationRect.read(m,0,destinationFirst)||!destinationRect.read(m,1,destinationLast))return false;
    if((destinationLast>>16)<(destinationFirst>>16)||(destinationLast&65535)<(destinationFirst&65535)){result=0xB;return true;}
    if(!m.write32(parameters+0x34,destinationFirst&65535)||!m.write32(parameters+0x38,destinationFirst>>16))return false;
    uint32_t targetWidth=width*nx/dx+1,targetHeight=height*ny/dy+1;
    uint32_t clippedWidth=(destinationLast>>16)-(destinationFirst>>16)+1,clippedHeight=(destinationLast&65535)-(destinationFirst&65535)+1;
    if(targetWidth>clippedWidth)targetWidth=clippedWidth;
    if(!m.write32(parameters+0x40,targetWidth))return false;
    uint32_t limited=targetWidth*dx/nx+1;if(limited<width){width=limited;if(!m.write32(parameters+0x24,width))return false;}
    if(targetHeight>clippedHeight)targetHeight=clippedHeight;
    if(!m.write32(parameters+0x3C,targetHeight))return false;
    limited=targetHeight*dy/ny+1;if(limited<height){height=limited;if(!m.write32(parameters+0x20,height))return false;}
    if(!m.write32(parameters+0x44,nx)||!m.write32(parameters+0x48,dx)||!m.write32(parameters+0x4C,ny)||!m.write32(parameters+0x50,dy))return false;
    uint32_t environment,mode,scratchBytes,scratch;
    if(!m.read32(input.context,environment)||!m.read32(environment+0x1C,mode)||(mode!=1&&mode!=2))return false;
    uint32_t field=mode==1?0x28:0x20;
    if(!m.read32(environment+field,scratchBytes)||!m.read32(environment+field+4,scratch))return false;
    // The selected pointer is consumed by the original hardware scratch setup;
    // the encoded CPU-call descriptor comes from the dispatcher globals.
    (void)scratch;
    if(format==1||format==3){uint32_t palette;if(!m.read32(input.source+16,palette)||!m.write32(parameters+0x78,palette))return false;}
    if(!plan_bitmap_tiles(m,parameters,scratchBytes-0xAC,result))return false;
    if(result!=1)return true;
    return dispatch_graphics_operation(m,services,input.context,0x13,parameters,0x7C,0,0,result);
}
class NativeBitmapTransfer final:public BitmapBlitServices {
public:
    NativeBitmapTransfer(CommandMemory& m,GraphicsDispatchServices& services,uint32_t stack):memory_(m),services_(services),stack_(stack){}
    bool blit_rectangle(const BitmapBlitRequest& request)override{
        BitmapTransferInput input{request.context,request.source,request.destination,stack_,
            {request.externalSourceRectangle,true,request.sourceRectangle},{0,true,request.destinationRectangle},
            request.scaleNumerator,request.scaleDenominator,request.operation,request.flags};
        return build_bitmap_transfer(memory_,services_,input,result_);
    }
    uint32_t status()const{return result_;}
private:
    CommandMemory& memory_;GraphicsDispatchServices& services_;uint32_t stack_,result_=0;
};
}
