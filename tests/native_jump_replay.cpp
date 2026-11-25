#include "native_renderer_bridge.h"
#include <fstream>
#include <cstdio>
#include <cmath>
#include <array>
namespace t3knative {extern bool g_ycc;}
int main(int argc,char** argv){
    if(argc!=2&&argc!=3)return 2;std::ifstream input(argv[1],std::ios::binary);
    auto read=[&](auto& v){return bool(input.read(reinterpret_cast<char*>(&v),sizeof(v)));};
    uint32_t header[2],seeds[128];if(!read(header)||header[0]!=0x4A4D5032||!read(seeds))return 2;
    unsigned failures=0,points=0;double maximum=0;t3knative::g_ycc=true;
    for(unsigned i=0;i<header[1];++i){
        uint32_t e[16],cam[3],target,n;if(!read(e)||!read(cam)||!read(target)||!read(n))return 2;
        std::vector<std::array<uint32_t,4>> expected(n);for(auto& point:expected)if(!read(point))return 2;
        nuon::Capture capture;capture.entries.push_back(0x8004E040);capture.entries.insert(capture.entries.end(),e,e+16);
        for(unsigned k=0;k<3;++k)capture.mem[0x800DAD80+k*4]=cam[k];capture.mem[0x8010B154]=target;
        capture.mem[0x800B1740]=0x807F0000;for(unsigned k=0;k<128;++k)capture.mem[0x807F0000+k*4]=seeds[k];
        t3knative::Picture picture;t3knative::BuildPicture(capture,&picture);
        bool ok=picture.complete&&picture.blobs.size()==n;
        if(ok)for(unsigned j=0;j<n;++j){const auto& b=picture.blobs[j];const auto& p=expected[j];
            double error=(std::max)(std::abs(double(b.x)-(double(int32_t(p[0]))/65536+.5)),std::abs(double(b.y)-(double(int32_t(p[1]))/65536-.5)));
            maximum=(std::max)(maximum,error);ok&=error<.05&&b.r==float(p[2])/2;
            const float channels[]{b.colour.r,b.colour.g,b.colour.b};
            for(unsigned c=0;c<3;++c)ok&=std::abs(channels[c]-float((p[3]>>(24-c*8))&255)/255)<.00001f;
            if(!ok&&failures<3){std::fprintf(stderr,"jump case=%u point=%u error=%g size=%g/%u colour=%g,%g,%g/%08X\n",i,j,error,b.r*2,p[2],channels[0],channels[1],channels[2],p[3]);break;}
        }
        points+=n;if(!ok)++failures;
        // Both valid radial table entries alias the original row routine;
        // the third is the constant-strength square blend, preserving order.
        capture.entries[8]=1;t3knative::Picture alias;t3knative::BuildPicture(capture,&alias);
        if(!alias.complete||alias.blobs.size()!=n)++failures;
        capture.entries[8]=2;t3knative::Picture squares;t3knative::BuildPicture(capture,&squares);
        if(!squares.complete||squares.ops.size()!=n||!squares.blobs.empty())++failures;
        for(unsigned j=0;j<n&&j<squares.ops.size()&&j<picture.blobs.size();++j){const auto& op=squares.ops[j];const auto& b=picture.blobs[j];
            if(op.kind!=t3knative::Op::kBlendRect||op.x!=std::floor(b.x-.5f-b.r)||op.y!=std::floor(b.y+.5f-b.r)||op.w!=b.r*2||op.h!=b.r*2||op.colour.a!=1-b.colour.a)++failures;
        }
        // Disabled and terminal opcodes are tested by low byte on hardware,
        // even when the rest of their descriptor still names another module.
        capture.entries[16]=0x765432FE;t3knative::Picture disabled;t3knative::BuildPicture(capture,&disabled);
        if(!disabled.complete||!disabled.ops.empty())++failures;
        capture.entries[16]=0x765432FF;capture.entries.insert(capture.entries.end(),17,0xDEADBEEF);t3knative::BuildPicture(capture,&disabled);
        if(!disabled.complete||!disabled.ops.empty())++failures;
    }
    if(argc==3){
        if(!SDL_Init(SDL_INIT_VIDEO))return 2;
        auto* window=SDL_CreateWindow("Particle row blend checks",360,240,SDL_WINDOW_HIDDEN);
        auto* renderer=window?t3knative::CreateRenderer(window):nullptr;if(!renderer)return 2;
        {tempest_native::NativeFrameRenderer gpu;std::string error;t3knative::Picture picture;picture.complete=true;
        t3knative::Op fill{t3knative::Op::kFill};fill.w=360;fill.h=240;fill.colour={128.f/255,128.f/255,128.f/255,1};picture.ops.push_back(fill);
        t3knative::Op square{t3knative::Op::kBlendRect};square.x=100;square.y=100;square.w=square.h=40;square.colour={192.f/255,128.f/255,128.f/255,.25f};picture.ops.push_back(square);
        if(!gpu.initialize(renderer,error)||!gpu.submit_picture(renderer,std::move(picture),1,{0,0,360,240},error)){std::fprintf(stderr,"GPU rows: %s\n",error.c_str());++failures;}
        else {auto* pixels=SDL_RenderReadPixels(renderer,nullptr);if(!pixels)++failures;else{
            auto* converted=SDL_ConvertSurface(pixels,SDL_PIXELFORMAT_XRGB8888);if(!converted)++failures;else{
                auto check=[&](int x,int y,uint32_t ycc){uint32_t actual=*reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(converted->pixels)+y*converted->pitch+x*4),expected=t3knative::YccToXrgb(ycc);for(int shift:{0,8,16})if(std::abs(int((actual>>shift)&255)-int((expected>>shift)&255))>3)++failures;};
                check(120,120,0x908080FF);check(80,80,0x808080FF);SDL_DestroySurface(converted);
            }SDL_SaveBMP(pixels,argv[2]);SDL_DestroySurface(pixels);
        }} }
        SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    }
    std::printf("Jump geometry: %u cases, %u original points, %u failures; max position error %.6f game pixels\n",header[1],points,failures,maximum);
    return failures?1:0;
}
