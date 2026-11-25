#pragma once
#include "TempestCommands.h"
#include <array>
namespace tempest_native {
struct BitmapBlitRequest {
    uint32_t context,source,destination;
    // Preserve caller-owned rectangle identity for the eventual native callee.
    // Zero identifies the caller's private default rectangle held by value.
    uint32_t externalSourceRectangle;
    std::array<uint32_t,2> sourceRectangle,destinationRectangle;
    uint32_t scaleNumerator,scaleDenominator,operation,flags;
};
static_assert(sizeof(BitmapBlitRequest)==48);
struct BitmapBlitServices {
    virtual bool blit_rectangle(const BitmapBlitRequest&)=0;
    virtual ~BitmapBlitServices()=default;
};
// Complete SDK caller 80023A74..80023C86. Source/destination rectangle endpoints
// are inclusive packed unsigned halfwords. Preserve intermediate 16-bit wrap
// before division and destination clipping. Drawing remains an explicit native
// dependency: this caller must never replace the SDK worker with a guessed copy.
inline bool prepare_bitmap_blit(CommandMemory& m,BitmapBlitServices& services,uint32_t context,
                                uint32_t source,uint32_t destination,uint32_t rectangle,uint32_t origin){
    BitmapBlitRequest request{};request.context=context;request.source=source;request.destination=destination;request.externalSourceRectangle=rectangle;
    if(rectangle){if(!m.read32(rectangle,request.sourceRectangle[0])||!m.read32(rectangle+4,request.sourceRectangle[1]))return false;}
    else{uint32_t dimensions;if(!m.read32(source+8,dimensions))return false;
        request.sourceRectangle={0,((((dimensions>>16)-1)&65535)<<16)|((dimensions-1)&65535)};
    }
    uint32_t aspect,dimensions;if(!m.read32(context+0x60,aspect)||!m.read32(destination+8,dimensions))return false;
    request.scaleNumerator=aspect?9:1;request.scaleDenominator=aspect?8:1;request.operation=1;request.flags=1;
    uint32_t first=request.sourceRectangle[0],last=request.sourceRectangle[1];
    uint32_t x=(((origin>>16)+(last>>16)-(first>>16))*request.scaleNumerator)&65535;
    x/=request.scaleDenominator;
    uint32_t y=(origin+(last&65535)-(first&65535))&65535;
    uint32_t width=dimensions>>16,height=dimensions&65535;
    if(x>=width)x=(width-1)&65535;if(y>=height)y=(height-1)&65535;
    request.destinationRectangle={origin,(x<<16)|y};
    return services.blit_rectangle(request);
}
}
