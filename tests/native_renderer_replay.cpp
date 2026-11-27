#include "native_renderer_bridge.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstdlib>
static std::vector<uint8_t> load(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
static uint32_t word(const std::vector<uint8_t>& b,unsigned a){return uint32_t(b[a])<<24|uint32_t(b[a+1])<<16|uint32_t(b[a+2])<<8|b[a+3];}
int main(int argc,char** argv){
    if(argc<5||argc>7){std::fprintf(stderr,"usage: renderer_replay system.bin main.bin local.bin output.bmp [original-frame.raw [operation-count]]\n");return 2;}
    auto system=load(argv[1]),main=load(argv[2]),local=load(argv[3]);if(system.size()!=0x800000||main.size()!=0x800000||local.size()!=0x800000)return 2;
    if(const char* fixture=std::getenv("T3K_RASTER_FIXTURE")){
        auto put=[&](uint32_t address,uint32_t value){auto* bank=address>>28==8?&system:address>>28==4?&main:address>>28==2?&local:nullptr;auto at=address&0x7fffff;if(!bank||(address&3)||at>bank->size()-4)return false;for(unsigned k=0;k<4;++k)(*bank)[at+k]=uint8_t(value>>(24-k*8));return true;};
        auto root=std::filesystem::path(fixture);std::ifstream entry(root/"entry.bin",std::ios::binary);if(!entry.read(reinterpret_cast<char*>(system.data()+0x4e040),64))return 2;put(0x8004e0bc,255);
        std::ifstream patches(root/"patches.bin",std::ios::binary);std::vector<uint8_t> patch((std::istreambuf_iterator<char>(patches)),{});if(patch.size()%8)return 2;for(unsigned k=0;k<patch.size();k+=8)if(!put(word(patch,k),word(patch,k+4)))return 2;
    }
    if(!SDL_Init(SDL_INIT_VIDEO))return 2;int result=1;
    auto* window=SDL_CreateWindow("Original renderer comparison",360,240,SDL_WINDOW_HIDDEN);auto* renderer=window?t3knative::CreateRenderer(window):nullptr;
    if(renderer){
        {
            tempest_native::NativeFrameRenderer pipeline;std::string error;SDL_FRect dst{0,0,360,240};
            bool ready=pipeline.initialize(renderer,error);uint32_t previous=word(system,0x110AA4);
            if(ready&&previous>=0x40000000&&uint64_t(previous)+360*240*4<=0x40800000){
                // Seed the captured YCrCb channels directly. An RGB round trip
                // clips latent chroma and would invent feedback mismatches.
                std::vector<uint32_t> pixels(360*240);for(unsigned i=0;i<pixels.size();++i)pixels[i]=0xFF000000|(word(main,(previous&0x7fffff)+i*4)>>8);
                t3knative::FeedbackState feedback;feedback.width=360;feedback.height=240;feedback.format=SDL_PIXELFORMAT_RGBA64_FLOAT;feedback.pixels.resize(360*240*8);
                ready=SDL_ConvertPixelsAndColorspace(360,240,SDL_PIXELFORMAT_XRGB8888,SDL_COLORSPACE_SRGB,0,pixels.data(),360*4,SDL_PixelFormat(feedback.format),SDL_COLORSPACE_SRGB,0,feedback.pixels.data(),360*8)&&pipeline.restore_feedback(renderer,feedback,error);
            }
            t3knative::Picture decoded;
            ready=ready&&tempest_native::build_renderer_picture({system,main,local},1,decoded,error);
            if(ready)if(const char* path=std::getenv("T3K_VECTOR_DUMP")){
                std::ofstream out(path);out.precision(10);for(const auto& s:decoded.segs)out<<s.x0<<','<<s.y0<<','<<s.x1<<','<<s.y1<<','<<s.c0.r<<','<<s.c0.g<<','<<s.c0.b<<','<<s.c0.a<<','<<s.c1.r<<','<<s.c1.g<<','<<s.c1.b<<','<<s.c1.a<<','<<s.r<<'\n';
            }
            if(ready&&argc==7){unsigned count=unsigned(std::strtoul(argv[6],nullptr,10));if(count>decoded.ops.size()){error="operation count exceeds display list";ready=false;}else decoded.ops.resize(count);}
            if(ready&&pipeline.submit_picture(renderer,std::move(decoded),1,dst,error)){
                const auto& picture=pipeline.picture();auto* raw=SDL_RenderReadPixels(renderer,nullptr);auto* rgba=raw?SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32):nullptr;if(raw)SDL_DestroySurface(raw);
                if(rgba){result=SDL_SaveBMP(rgba,argv[4])?0:1;
                    std::printf("operations=%zu segments=%zu glyphs=%zu webs=%zu blobs=%zu planes=%zu\n",picture.ops.size(),picture.segs.size(),picture.glyphs.size(),picture.webs.size(),picture.blobs.size(),picture.planes.size());
                    for(const auto& op:picture.ops)std::printf("operation type=%08X kind=%u first=%u count=%u\n",op.type,unsigned(op.kind),op.first,op.count);
                    if(argc>=6){auto original=load(argv[5]);if(original.size()!=360*240*4){result=2;}else{
                        double absolute=0,squared=0;unsigned changed=0,originalLit=0,nativeLit=0;
                        std::vector<uint32_t> originalPixels(360*240);
                        for(unsigned y=0;y<240;++y)for(unsigned x=0;x<360;++x){auto expected=t3knative::YccToXrgb(word(original,(y*360+x)*4));auto* actual=static_cast<uint8_t*>(rgba->pixels)+y*rgba->pitch+x*4;unsigned maximum=0;bool ol=false,nl=false;
                            originalPixels[y*360+x]=0xFF000000|expected;
                            for(unsigned c=0;c<3;++c){unsigned e=(expected>>(16-8*c))&255;int d=int(actual[c])-int(e);absolute+=std::abs(d);squared+=double(d)*d;maximum=std::max(maximum,unsigned(std::abs(d)));ol|=e>8;nl|=actual[c]>8;}
                            changed+=maximum>8;originalLit+=ol;nativeLit+=nl;
                        }
                        std::printf("original comparison mean_abs=%.6f rms=%.6f pixels_over_8=%u original_lit=%u native_lit=%u\n",absolute/(360*240*3),std::sqrt(squared/(360*240*3)),changed,originalLit,nativeLit);
                        auto* surface=SDL_CreateSurfaceFrom(360,240,SDL_PIXELFORMAT_XRGB8888,originalPixels.data(),360*4);
                        auto path=std::filesystem::path(argv[4]);path.replace_filename(path.stem().string()+"-original.bmp");if(!surface||!SDL_SaveBMP(surface,path.string().c_str()))result=1;if(surface)SDL_DestroySurface(surface);
                    }}SDL_DestroySurface(rgba);
                }
            }else std::fprintf(stderr,"renderer: %s\n",error.c_str());
        }
        t3knative::ShutdownPipeline();SDL_DestroyRenderer(renderer);
    }
    if(window)SDL_DestroyWindow(window);SDL_Quit();return result;
}
