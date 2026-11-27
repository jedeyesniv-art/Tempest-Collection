#include "native_renderer_bridge.h"
#include "native_frame_memory.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <memory>
using namespace tempest_native;
static void require(bool value,const char* why){if(!value){std::fprintf(stderr,"render clocks: %s (%s)\n",why,SDL_GetError());std::exit(1);}}
static bool same(const t3knative::FeedbackState& a,const t3knative::FeedbackState& b){return a.width==b.width&&a.height==b.height&&a.format==b.format&&a.pixels==b.pixels;}
static std::vector<unsigned char> read_output(SDL_Renderer* renderer){auto* raw=SDL_RenderReadPixels(renderer,nullptr);require(raw,"cache capture");auto* rgba=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(raw);require(rgba,"cache conversion");std::vector<unsigned char> bytes(size_t(rgba->w)*rgba->h*4);for(int y=0;y<rgba->h;++y)std::copy_n(static_cast<unsigned char*>(rgba->pixels)+y*rgba->pitch,rgba->w*4,bytes.data()+size_t(y)*rgba->w*4);SDL_DestroySurface(rgba);return bytes;
}
static std::vector<unsigned char> capture_cached(SDL_Renderer* renderer,t3knative::Renderer& draw,const t3knative::Picture& picture){
    SDL_FRect dst{0,0,360,240};draw.Draw(renderer,picture,picture.segs,dst,1);auto* raw=SDL_RenderReadPixels(renderer,nullptr);require(raw,"cache capture");auto* rgba=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(raw);require(rgba,"cache conversion");std::vector<unsigned char> bytes(size_t(rgba->w)*rgba->h*4);for(int y=0;y<rgba->h;++y)std::copy_n(static_cast<unsigned char*>(rgba->pixels)+y*rgba->pitch,rgba->w*4,bytes.data()+size_t(y)*rgba->w*4);SDL_DestroySurface(rgba);return bytes;
}
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    require(SDL_Init(SDL_INIT_VIDEO),"SDL");
    auto* window=SDL_CreateWindow("Native render clock checks",360,240,SDL_WINDOW_HIDDEN);
    auto* renderer=window?t3knative::CreateRenderer(window):nullptr;require(renderer,"GPU renderer");
    if(const auto* path=std::getenv("T3K_TEMPORAL_PROBE")){
        require(t3knative::InitPipeline(renderer),"temporal probe pipeline");
        auto* out=std::fopen(path,"w");require(out,"temporal CSV");
        std::fprintf(out,"sample,phase,boundary,mean_rgb,mean_absolute_change\n");
        t3knative::Renderer draw;SDL_FRect dst{0,0,360,240};std::string error;std::vector<unsigned char> previous;
        unsigned phase=0;
        for(unsigned sample=0;sample<240;++sample){
            t3knative::Picture picture;picture.flip=sample;
            if(!sample){t3knative::Op fill{t3knative::Op::kFill};fill.w=360;fill.h=240;fill.colour={16.f/255,128.f/255,128.f/255,1};picture.ops.push_back(fill);}
            else{t3knative::Op zoom{t3knative::Op::kZoom};zoom.cx=180;zoom.cy=120;zoom.zx=zoom.zy=1.23719788f;zoom.angle=.0228179656f;zoom.fade=1.09887695f;picture.ops.push_back(zoom);}
            // Fresh, constant-speed source; no picture interpolation. The
            // feedback parameters come from the reached level-clear trace.
            t3knative::Op deposit{t3knative::Op::kBlendRect};deposit.x=165+30*std::cos(sample*.031f);deposit.y=105+30*std::sin(sample*.031f);deposit.w=30;deposit.h=30;deposit.colour={.8f,.45f,.55f,.3f};picture.ops.push_back(deposit);
            require(draw.DrawSimulationSample(renderer,picture,dst,phase,5,error,nullptr,2,std::getenv("T3K_CLOCKED_PROBE")?t3knative::FeedbackSampling::clocked:std::getenv("T3K_CONTINUOUS_PROBE")?t3knative::FeedbackSampling::continuous:t3knative::FeedbackSampling::pinned),"temporal sample");
            auto* raw=SDL_RenderReadPixels(renderer,nullptr);require(raw,"temporal read");auto* rgba=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(raw);require(rgba,"temporal conversion");
            std::vector<unsigned char> bytes(size_t(rgba->w)*rgba->h*4);for(int y=0;y<rgba->h;++y)std::copy_n(static_cast<unsigned char*>(rgba->pixels)+y*rgba->pitch,rgba->w*4,bytes.data()+size_t(y)*rgba->w*4);SDL_DestroySurface(rgba);
            double energy=0,delta=0;for(size_t i=0;i<bytes.size();++i)if(i%4!=3){energy+=bytes[i];if(!previous.empty())delta+=std::abs(int(bytes[i])-int(previous[i]));}
            std::fprintf(out,"%u,%u,%u,%.9f,%.9f\n",sample,phase,phase+2>=5,energy/(360*240*3),delta/(360*240*3));previous=std::move(bytes);phase=(phase+2)%5;
        }
        std::fclose(out);t3knative::ShutdownPipeline();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 0;
    }
    if(std::getenv("T3K_RENDER_BENCH")){
        require(t3knative::InitPipeline(renderer),"benchmark pipeline");
        SDL_SetRenderVSync(renderer,0);
        std::printf("Synthetic renderer benchmark; GPU-completed batch wall time, no game/audio/CRT; renderer=%s\n",SDL_GetRendererName(renderer));
        {
            NativeFrameMemory memory;uint32_t entry=0x8004E040,texture=0x80070000;
            for(unsigned k=0;k<128;++k)require(memory.write32(texture+k*4,0xA08080FF),"ring benchmark texture");
            require(memory.write32(entry,0x00B40078)&&memory.write32(entry+4,0x01008000)&&memory.write32(entry+20,0x00400000)&&memory.write32(entry+48,texture)&&memory.write32(entry+60,0x00500005)&&memory.write32(entry+124,0xFF),"ring benchmark entry");
            for(unsigned radius:{10u,50u,100u,200u}){
                require(memory.write32(entry+16,(radius<<16)|radius),"ring benchmark radius");
                t3knative::Picture decoded;std::string error;
                require(build_renderer_picture(memory.renderer_view(),0,decoded,error)&&decoded.glyphs.size()==1,"ring benchmark decode");
                auto start=SDL_GetTicksNS();for(unsigned i=0;i<10;++i)require(build_renderer_picture(memory.renderer_view(),i,decoded,error),"ring repeated decode");
                std::printf("CPU ring decode radius=%u image=%dx%d ms=%.4f\n",radius,decoded.glyphs[0].pw,decoded.glyphs[0].ph,double(SDL_GetTicksNS()-start)/10000000.);
                t3knative::PictureBuildClocks clocks{};clocks.gpuRings=true;
                start=SDL_GetTicksNS();for(unsigned i=0;i<100;++i)require(build_renderer_picture(memory.renderer_view(),i,decoded,error,clocks),"GPU ring parameter decode");
                std::printf("GPU ring parameter decode radius=%u ms=%.6f\n",radius,double(SDL_GetTicksNS()-start)/100000000.);
                t3knative::Renderer draw;SDL_FRect dst{0,0,360,240};
                for(unsigned i=0;i<10;++i){draw.Draw(renderer,decoded,decoded.segs,dst,1);require(SDL_RenderPresent(renderer),"ring benchmark warmup");}
                require(SDL_WaitForGPUIdle(SDL_GetGPURendererDevice(renderer)),"ring benchmark warmup idle");start=SDL_GetTicksNS();
                for(unsigned i=0;i<60;++i){decoded.flip=i;draw.Draw(renderer,decoded,decoded.segs,dst,1);require(SDL_RenderPresent(renderer),"ring benchmark submit");}
                require(SDL_WaitForGPUIdle(SDL_GetGPURendererDevice(renderer)),"ring benchmark completed");
                std::printf("GPU ring completed drawing radius=%u ms=%.6f\n",radius,double(SDL_GetTicksNS()-start)/60000000.);
            }
        }
        for(unsigned lines:{100u,1000u})for(unsigned height:{240u,480u,1080u,2160u})for(bool fine:{false,true}){
            t3knative::Renderer draw;t3knative::Picture picture;std::string error;
            t3knative::Op zoom{t3knative::Op::kZoom};zoom.cx=180;zoom.cy=120;zoom.zx=1.005f;zoom.zy=1.005f;zoom.fade=.97f;picture.ops.push_back(zoom);
            for(unsigned i=0;i<lines;++i){t3knative::Seg s{};s.x0=float(i%360);s.y0=float((i*7)%240);s.x1=s.x0+20;s.y1=s.y0+15;s.r=1;s.c0=s.c1={.7f,.5f,.5f,.6f};picture.segs.push_back(s);}
            t3knative::Op vectors{t3knative::Op::kSegs};vectors.count=lines;picture.ops.push_back(vectors);
            SDL_FRect dst{0,0,float(height)*1.5f,float(height)};
            auto sample=[&](unsigned i){picture.flip=i;if(fine)require(draw.DrawSimulationSample(renderer,picture,dst,i%5,5,error),"benchmark fine sample");else draw.Draw(renderer,picture,picture.segs,dst,1,nullptr,true);};
            for(unsigned i=0;i<10;++i){sample(i);require(SDL_RenderPresent(renderer),"benchmark warmup submit");}
            require(SDL_FlushRenderer(renderer)&&SDL_WaitForGPUIdle(SDL_GetGPURendererDevice(renderer)),"benchmark warmup completion");
            auto start=SDL_GetTicksNS();
            for(unsigned i=0;i<60;++i){sample(i);require(SDL_RenderPresent(renderer),"benchmark submit");}
            auto queued=SDL_GetTicksNS();require(SDL_WaitForGPUIdle(SDL_GetGPURendererDevice(renderer)),"benchmark GPU completion");auto end=SDL_GetTicksNS();
            std::printf("lines=%u height=%u fine=%u submit_ms=%.4f completed_ms=%.4f\n",lines,height,unsigned(fine),double(queued-start)/60000000.,double(end-start)/60000000.);
        }
        t3knative::ShutdownPipeline();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 0;
    }
    {
      t3knative::Picture p;require(!t3knative::SupportsTimeIntegratedFeedback(p),"empty picture is not feedback");
      p.ops.push_back(t3knative::Op{t3knative::Op::kZoom});p.ops.push_back(t3knative::Op{t3knative::Op::kBlobs});require(t3knative::SupportsTimeIntegratedFeedback(p),"particle feedback eligibility does not depend on scene identity");
      for(auto kind:{t3knative::Op::kFill,t3knative::Op::kZoom,t3knative::Op::kRipple}){p.ops.push_back(t3knative::Op{kind});require(!t3knative::SupportsTimeIntegratedFeedback(p),"interleaved feedback/clear retains reference composition");p.ops.pop_back();}
    }
    {
      require(t3knative::InitPipeline(renderer),"no-op fixture pipeline");
      NativeFrameMemory memory;const uint32_t entry=0x8004E040;std::string error;
      require(memory.write32(entry+124,0xFF),"no-op fixture terminator");
      for(auto type:{0x00310009u,0x0031000Eu,0x0031020Eu}){
        require(memory.write32(entry+60,type)&&memory.write32(entry+28,1),"no-op drawing selector");t3knative::Picture picture;
        require(build_renderer_picture(memory.renderer_view(),0,picture,error)&&picture.complete&&picture.ops.empty(),"original no-op primary/particle rows do not fabricate geometry from absent resources");
      }
    }
    unsigned comparisons=0;
    {
        NativeFrameRenderer reference,display;t3knative::Renderer fine;std::string error;
        require(reference.initialize(renderer,error)&&display.initialize(renderer,error),"initialize pipeline");
        auto generation=SDL_GetNumberProperty(SDL_GetRendererProperties(renderer),"tempest.t3k.pipeline_generation",0);
        require(generation>0,"native shader pipeline has renderer-owned lifetime");
        {NativeFrameRenderer additional;require(additional.initialize(renderer,error),"additional independent feedback owner");
         require(SDL_GetNumberProperty(SDL_GetRendererProperties(renderer),"tempest.t3k.pipeline_generation",0)==generation,"initializing another frame wrapper must not replace shared shaders or invalidate live temporal bindings");}
        {
            NativeFrameMemory memory;std::string why;const uint32_t entry=0x8004E040,definition=0x80071000,texture=0x80074000,root=0x80076000;
            require(memory.write32(entry,texture)&&memory.write32(entry+60,0x0011000B)&&memory.write32(entry+124,0xFF)&&memory.write32(0x800AF700,definition)&&memory.write32(definition+4,15u<<16)&&memory.write32(definition+0x28,16u<<8),"open-web definition with sixteen logical lanes and fifteen surface segments");
            for(unsigned i=0;i<256;++i)require(memory.write32(texture+i*4,0xC08080FF),"web surface texture fixture");
            auto packed=[](int x,int y){return uint32_t(uint16_t((y-120)*2))<<16|uint16_t((x-180)*2);};
            for(unsigned lane=0;lane<16;++lane){int left=lane==15?20:220,right=lane==15?80:240,top=lane==15?20:120,bottom=lane==15?80:140;
                uint32_t words[8]={packed(left,top),0,packed(right,top),8u*512<<16,packed(right,bottom),(8u*512<<16)|8u*512,packed(left,bottom),8u*512};
                for(unsigned k=0;k<8;++k)require(memory.write32(0x800BACF0+lane*32+k*4,words[k]),"poisoned trailing web record");
            }
            // A valid bound must also survive an all-zero degenerate record
            // before later nonzero quads. It must not reintroduce a sentinel.
            for(unsigned k=0;k<8;++k)require(memory.write32(0x800BACF0+k*4,0),"degenerate first surface record");
            uint32_t poison;require(memory.read32(0x800BACF0+15*32,poison),"remember stale panel data");
            for(bool gpu:{false,true}){
                t3knative::PictureBuildClocks clocks{};clocks.gpuWeb=gpu;t3knative::Picture open;
                require(build_renderer_picture(memory.renderer_view(),1,open,why,clocks)&&open.webs.size()==1&&open.webs[0].quads.size()==60,"open web decodes exactly fifteen segments and excludes the stale sixteenth panel");
                t3knative::Renderer draw;auto pixels=capture_cached(renderer,draw,open);auto at=(40*360+40)*4;
                require(pixels[at]<=1&&pixels[at+1]<=1&&pixels[at+2]<=1,"stray panel area stays clear in CPU-mesh and analytic GPU web paths");
                require(memory.write32(definition+4,16u<<16),"closed sixteen-segment fixture");t3knative::Picture closed;
                require(build_renderer_picture(memory.renderer_view(),2,closed,why,clocks)&&closed.webs[0].quads.size()==64,"a legitimate sixteenth segment must remain visible");
                auto valid=capture_cached(renderer,draw,closed);require(valid[at]>30,"count bounds do not hide a legitimate panel by colour or position");
                require(memory.write32(definition+4,15u<<16),"restore open-web count");
            }
            uint32_t after;require(memory.read32(0x800BACF0+15*32,after)&&after==poison,"decoder does not clear or rewrite original game memory to remove stale panels");
            require(memory.edit_words(NativeFrameMemory::rendererLocal,0x100020,4,[&](auto bytes){for(unsigned k=0;k<4;++k)bytes[k]=uint8_t(root>>(24-k*8));})&&memory.write32(root+0x900,definition+0x100)&&memory.write32(definition+0x104,12u<<16),"drawing-MPE definition fixture");t3knative::Picture local;
            require(build_renderer_picture(memory.renderer_view(),3,local,why)&&local.webs[0].quads.size()==48,"drawing-MPE context uses geometric segment count rather than logical lane count");
            require(memory.write32(definition+0x16C,0x80010001),"lane chroma fixture in drawing-MPE definition");
            t3knative::Picture chroma;require(build_renderer_picture(memory.renderer_view(),3,chroma,why)&&chroma.webs[0].unsignedChromaLanes==0x8001,"only upper mask bits select unsigned chroma lanes");
            // The original tests the signed dispatch mode before consulting
            // surface bounds. A disabled entry can retain a complete old web.
            for(uint32_t mode:{0xFFFFFFFFu,0x80000000u,0xFFFFFFFEu}){
                require(memory.write32(entry+4,mode),"negative web dispatch mode with nonzero stale geometry");
                t3knative::Picture disabled;
                require(build_renderer_picture(memory.renderer_view(),4,disabled,why)&&disabled.complete&&disabled.webs.empty(),"negative surface modes draw nothing despite a valid old definition and quad stream");
                t3knative::Renderer draw;auto pixels=capture_cached(renderer,draw,disabled);
                for(size_t pixel=0;pixel<pixels.size();pixel+=4)require(pixels[pixel]<=1&&pixels[pixel+1]<=1&&pixels[pixel+2]<=1,"disabled web leaves an empty picture clear");
            }
            require(memory.write32(entry+4,0)&&memory.write32(0x801095A0,127),"last ordinary web level");
            t3knative::Picture lastWeb;require(build_renderer_picture(memory.renderer_view(),5,lastWeb,why)&&lastWeb.webs.size()==1,"ordinary web remains visible at level-limit boundary");
            require(memory.write32(0x801095A0,128),"beyond ordinary surface level limit");
            t3knative::Picture beyond;require(build_renderer_picture(memory.renderer_view(),6,beyond,why)&&beyond.webs.empty(),"original ordinary-surface level gate prevents stale web drawing");
            uint32_t inlineQuad[8]={packed(10,20),0,packed(110,20),8u*512<<16,packed(10,100),8u*512,packed(110,100),(8u*512<<16)|8u*512};
            for(unsigned k=0;k<8;++k)require(memory.write32(entry+16+k*4,inlineQuad[k]),"explicit surface positions and UVs");
            require(memory.write32(entry+4,1),"positive mode is independent of ordinary-web level gate");
            uint32_t oldPoint;require(memory.read32(0x800BACF0,oldPoint),"old scratch point");memory.web_render_point(0x800BACF0,oldPoint,2000,2000);
            for(bool gpu:{false,true}){
                t3knative::PictureBuildClocks clocks{};clocks.gpuWeb=gpu;clocks.preciseWeb=true;clocks.overrideWebTextureTick=true;clocks.webTextureTick=12345;
                t3knative::Picture explicitPicture;require(build_renderer_picture(memory.renderer_view(),7,explicitPicture,why,clocks)&&explicitPicture.webs.size()==1,"positive surface mode decodes one inline quad");
                const auto& quad=explicitPicture.webs[0];require(quad.explicitQuad&&quad.quads.size()==4&&quad.quads[0].x==10&&quad.quads[0].y==20&&quad.quads[2].x==110&&quad.quads[2].y==100,"inline geometry ignores old web count, scratch and precision sidecar");
                auto op=std::find_if(explicitPicture.ops.begin(),explicitPicture.ops.end(),[](const auto& op){return op.kind==t3knative::Op::kWeb;});require(op!=explicitPicture.ops.end()&&op->fade==0,"explicit quad does not inherit the ordinary web's clock-based UV scroll");
                t3knative::Renderer draw;auto pixels=capture_cached(renderer,draw,explicitPicture);auto inside=(40*360+40)*4,outside=(130*360+230)*4;
                require(pixels[inside]>30&&pixels[outside]<=1&&pixels[outside+1]<=1&&pixels[outside+2]<=1,"CPU and GPU draw explicit surface only at its inline positions");
            }
            std::printf("Web bounds: open/closed counts, stale tail, degenerate head, both mapping paths and MPE context passed\n");
        }
        {
            auto owned=std::make_unique<NativeFrameMemory>();auto& memory=*owned;uint32_t entry=0x8004E040,source=0x40064600;
            require(memory.write32(entry,30u<<16|40)&&memory.write32(entry+4,32u<<16|24)&&memory.write32(entry+8,5u<<16|7)&&memory.write32(entry+28,0x20000000)&&memory.write32(entry+36,source)&&memory.write32(entry+44,0x40809000)&&memory.write32(entry+60,0x00100100)&&memory.write32(entry+124,255),"framebuffer copy fixture");
            for(unsigned y=47;y<71;++y)for(unsigned x=35;x<67;++x)require(memory.write32(source+(y*360+x)*4,0xA0807000),"framebuffer copy source");
            t3knative::Picture copied;require(build_renderer_picture(memory.renderer_view(),0,copied,error)&&copied.glyphs.size()==1,"copy path decodes");const auto& g=copied.glyphs[0];require(g.x==35&&g.y==47&&g.pw==32&&g.ph==24&&g.rgba[0]==0xFF708080,"copy offsets affect both source and destination; full signed 2.30 colour blend is preserved");
            require(memory.write32(entry+60,0x00300004)&&memory.write32(entry+28,0x12340001),"raw zoom selector fixture");t3knative::Picture zoom;require(build_renderer_picture(memory.renderer_view(),0,zoom,error)&&zoom.ops[0].count==1&&zoom.ops[0].fade==1&&!t3knative::SupportsTimeIntegratedFeedback(zoom),"raw nearest zoom bypasses fade and unverified fractional feedback");
            require(memory.write32(entry+28,0x12340002),"unimplemented composite zoom selector");require(!build_renderer_picture(memory.renderer_view(),0,zoom,error),"composite zoom cannot silently execute the ordinary single-source algorithm");
        }
        {
          auto owned=std::make_unique<NativeFrameMemory>();auto& memory=*owned;uint32_t entry=0x8004E040,texture=0x80070000;
          require(memory.write32(entry,20u<<16|30)&&memory.write32(entry+4,128u<<16|16)&&memory.write32(entry+16,65536)&&memory.write32(entry+36,texture)&&memory.write32(entry+60,0x00100500)&&memory.write32(entry+124,255),"nearest Z plane entry");
          t3knative::Renderer draw;
          for(unsigned z:{0u,64u,127u,255u}){
            for(unsigned k=0;k<256;++k)require(memory.write32(texture+k*4,0xA0808000|z),"signed Z plane texture mutation");t3knative::Picture p;require(build_renderer_picture(memory.renderer_view(),0,p,error)&&p.planes.size()==1&&p.planes[0].nearestZ&&p.planes[0].grid.empty(),"Z plane bypasses opaque bilinear mesh decoding");capture_cached(renderer,draw,p);t3knative::FeedbackState h;require(draw.CaptureFeedback(renderer,h,error),"Z plane readback");std::vector<float> pixels(360*240*4);
            require(SDL_ConvertPixelsAndColorspace(360,240,SDL_PixelFormat(h.format),SDL_COLORSPACE_SRGB,0,h.pixels.data(),360*SDL_BYTESPERPIXEL(SDL_PixelFormat(h.format)),SDL_PIXELFORMAT_RGBA128_FLOAT,SDL_COLORSPACE_SRGB,0,pixels.data(),360*16),"Z plane float conversion");float weight=float(int8_t(z))*(1.f/256)*(8191.f/8192);float expected=16+144*weight;
            require(std::abs(pixels[(35*360+45)*4]*255-expected)<.15f&&std::abs(pixels[(35*360+45)*4+1]*255-128)<.15f,"signed sampled Z drives the original plane blend, including same-picture alpha cache changes");require(std::abs(pixels[0]*255-16)<.15f,"Z plane coverage stays inside its rectangle");
          }
          std::printf("Z plane: nearest sampling, signed blend weights, rectangle bounds and same-tick texture changes passed\n");
        }
        {
            NativeFrameMemory memory;uint32_t entry=0x8004E040,glyphs=0x80070000,texture=0x80074000,block=0x80075000;
            for(unsigned digit=0;digit<10;++digit)for(unsigned k=0;k<256;++k){unsigned x=k%16,y=k/16;uint32_t value=((x*13+digit*7)%256)<<24|((90+x*4)%256)<<16|((100+y*3)%256)<<8|((x>1&&x<14&&y>1&&y<14)?190:0);require(memory.write32(glyphs+digit*1024+k*4,value),"number glyph fixture");}
            for(unsigned k=0;k<256;++k)require(memory.write32(texture+k*4,(40+k%150)<<24|(100+k%40)<<16|(110+k%30)<<8|255),"number colour texture");
            for(unsigned k=0;k<20;++k)require(memory.write32(block+k*4,0),"number mapping block");
            require(memory.write32(block+8,0xFFFE8000)&&memory.write32(block+12,0x00038000)&&memory.write32(block+32,0x01000000)&&memory.write32(block+36,0x00300000)&&memory.write32(block+40,0x1000)&&memory.write32(block+68,0x01000000),"number quadratic mapping");
            require(memory.write32(entry,0x00B40078)&&memory.write32(entry+16,0x1000)&&memory.write32(entry+20,0xFFFFF800)&&memory.write32(entry+24,0x4000)&&memory.write32(entry+40,glyphs)&&memory.write32(entry+44,texture)&&memory.write32(entry+48,block)&&memory.write32(entry+56,1)&&memory.write32(entry+60,0x0010000D)&&memory.write32(entry+124,0xFF),"number display entry");
            for(unsigned number:{0u,101299u,4294967295u})for(unsigned scale:{0x8000u,0x20000u,0x50000u}){
                require(memory.write32(0x800DC240,number)&&memory.write32(entry+8,scale)&&memory.write32(entry+12,scale),"number value and scale");
                t3knative::Picture cpu,gpu;t3knative::PictureBuildClocks clocks{};clocks.gpuNumbers=true;
                require(build_renderer_picture(memory.renderer_view(),0,cpu,error)&&build_renderer_picture(memory.renderer_view(),0,gpu,error,clocks)&&cpu.glyphs.size()==gpu.glyphs.size()&&!gpu.glyphs.empty()&&gpu.glyphs[0].gpuNumber&&gpu.glyphs[0].rgba.empty(),"number GPU parameter decode");
                require(std::any_of(cpu.glyphs[0].rgba.begin(),cpu.glyphs[0].rgba.end(),[](uint32_t p){return p!=0;}),"number fixture has visible content");
                auto capture=[&](const t3knative::Picture& pic){t3knative::Renderer draw;SDL_FRect dst{0,0,360,240};draw.Draw(renderer,pic,pic.segs,dst,1);auto* raw=SDL_RenderReadPixels(renderer,nullptr);require(raw,"number capture");auto* rgba=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(raw);require(rgba,"number conversion");std::vector<unsigned char> bytes(size_t(rgba->w)*rgba->h*4);for(int y=0;y<rgba->h;++y)std::copy_n(static_cast<unsigned char*>(rgba->pixels)+y*rgba->pitch,rgba->w*4,bytes.data()+size_t(y)*rgba->w*4);SDL_DestroySurface(rgba);return bytes;};
                auto a=capture(cpu),b=capture(gpu);double total=0;unsigned maximum=0;for(size_t i=0;i<a.size();++i){unsigned d=unsigned(std::abs(int(a[i])-int(b[i])));total+=d;maximum=std::max(maximum,d);}
                std::printf("Number GPU comparison number=%u scale=%u mean=%.6f max=%u\n",number,scale,total/a.size(),maximum);require(total/a.size()<.1&&maximum<12,"number shader/reference agreement");
            }
            for(unsigned k=0;k<256;++k){uint32_t value;require(memory.read32(texture+k*4,value)&&memory.renderer_word(0x100930+k*4,value),"seed retained renderer-local number texture");}
            for(bool gpu:{false,true}){t3knative::Picture direct,retained;t3knative::PictureBuildClocks clocks{};clocks.gpuNumbers=gpu;
              require(build_renderer_picture(memory.renderer_view(),0,direct,error,clocks)&&memory.write32(entry+44,0)&&build_renderer_picture(memory.renderer_view(),0,retained,error,clocks)&&memory.write32(entry+44,texture),"conditional number texture uses existing renderer scratch when upload pointer is zero");
              t3knative::Renderer a,b;require(capture_cached(renderer,a,direct)==capture_cached(renderer,b,retained),"retained texture has the same number pixels as an explicit upload in CPU and GPU paths");}
            require(memory.write32(0x800DC240,100)&&memory.write32(entry+8,0x10000)&&memory.write32(entry+12,0x10000),"number opacity boundary fixture");
            for(uint32_t opacity:{0u,0xFFFFFFFFu,0xFFFFE3FFu}){
                require(memory.write32(entry+24,opacity),"zero or negative signed number opacity");
                t3knative::Picture cpu,gpu;t3knative::PictureBuildClocks clocks{};clocks.gpuNumbers=true;
                require(build_renderer_picture(memory.renderer_view(),8,cpu,error)&&build_renderer_picture(memory.renderer_view(),8,gpu,error,clocks),"decode signed number opacity in CPU and GPU paths");
                for(const auto& glyph:cpu.glyphs)require(std::all_of(glyph.rgba.begin(),glyph.rgba.end(),[](uint32_t p){return p==0;}),"negative signed opacity must not wrap into opaque score panels");
                t3knative::Renderer draw;auto pixels=capture_cached(renderer,draw,gpu);for(size_t pixel=0;pixel<pixels.size();pixel+=4)require(pixels[pixel]<=1&&pixels[pixel+1]<=1&&pixels[pixel+2]<=1,"GPU score is invisible at zero and negative opacity");
            }
        }
        {
            NativeFrameMemory memory;uint32_t entry=0x8004E040,texture=0x80070000;
            for(unsigned k=0;k<128;++k)require(memory.write32(texture+k*4,((40+k)%220)<<24|((90+k)%220)<<16|((110+k)%220)<<8|255),"ring shader texture");
            require(memory.write32(entry,0x00B40078)&&memory.write32(entry+4,0x01008000)&&memory.write32(entry+20,0xFFFFFFFF)&&memory.write32(entry+48,texture)&&memory.write32(entry+52,0x20003000)&&memory.write32(entry+60,0x00500005)&&memory.write32(entry+124,0xFF),"ring shader entry");
            for(unsigned radius:{10u,50u,100u,200u})for(unsigned band:{0x1000u,0x8000u,0xC000u}){
                require(memory.write32(entry+16,(radius<<16)|(radius*3/4))&&memory.write32(entry+4,0x01000000|band)&&memory.write32(entry+8,0xFFF88000)&&memory.write32(entry+12,0x00038000),"ring shader radius and phase");
                t3knative::Picture cpu,gpu;t3knative::PictureBuildClocks clocks{};clocks.gpuRings=true;
                require(build_renderer_picture(memory.renderer_view(),0,cpu,error)&&build_renderer_picture(memory.renderer_view(),0,gpu,error,clocks)&&gpu.glyphs.size()==1&&gpu.glyphs[0].gpuRing&&gpu.glyphs[0].rgba.empty(),"ring shader avoids CPU raster");
                require(std::any_of(cpu.glyphs[0].rgba.begin(),cpu.glyphs[0].rgba.end(),[](uint32_t p){return p!=0;}),"ring comparison contains visible data");
                auto capture=[&](const t3knative::Picture& pic){t3knative::Renderer draw;SDL_FRect dst{0,0,360,240};draw.Draw(renderer,pic,pic.segs,dst,1);auto* raw=SDL_RenderReadPixels(renderer,nullptr);require(raw,"ring comparison capture");auto* rgba=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(raw);require(rgba,"ring comparison conversion");std::vector<unsigned char> bytes(size_t(rgba->w)*rgba->h*4);for(int y=0;y<rgba->h;++y)std::copy_n(static_cast<unsigned char*>(rgba->pixels)+y*rgba->pitch,rgba->w*4,bytes.data()+size_t(y)*rgba->w*4);SDL_DestroySurface(rgba);return bytes;};
                auto a=capture(cpu),b=capture(gpu);double total=0;unsigned maximum=0;for(size_t i=0;i<a.size();++i){unsigned d=unsigned(std::abs(int(a[i])-int(b[i])));total+=d;maximum=std::max(maximum,d);}
                std::printf("Ring GPU comparison radius=%u band=%u mean=%.6f max=%u\n",radius,band,total/a.size(),maximum);
                require(total/a.size()<.5&&maximum<16,"ring shader/reference agreement");
            }
        }
        {
            NativeFrameMemory memory;uint32_t entry=0x8004E040,stream=0x40181800;
            require(memory.write32(entry,0x00B40078)&&memory.write32(entry+16,0x00800080)&&memory.write32(entry+20,0x4FFF0002)&&memory.write32(entry+28,stream)&&memory.write32(entry+60,0x00710001)&&memory.write32(entry+64,0x80070000)&&memory.write32(entry+124,0x0011000B)&&memory.write32(entry+188,0xFF),"seed vector and textured web records");
            require(memory.write32(stream,0x00010000)&&memory.write32(stream+4,0x00010002)&&memory.write32(stream+8,0x80000001),"seed unchanged packed geometry");
            for(unsigned k=0;k<4;++k)require(memory.write32(WebRenderPrecision::base+k*8,k==0||k==3?0x00010002:0x00010000),"seed web quad positions");
            require(memory.write32(entry+8,0xA080803F)&&memory.write32(entry+12,0xA080803F),"vector colour Z fixture");
            auto bank=memory.bank(NativeFrameMemory::system);std::vector<uint8_t> before(bank.begin(),bank.end());
            t3knative::Picture legacy,first,second;t3knative::PictureBuildClocks precise{};precise.preciseWeb=true;
            require(build_renderer_picture(memory.renderer_view(),0,legacy,error)&&legacy.segs.size()==1&&legacy.webs.size()==1,"decode coarse fixture");
            t3knative::Picture stroke;
            require(build_renderer_picture(memory.renderer_view(),0,stroke,error)&&stroke.segs[0].originalGradient&&stroke.segs[0].r==2&&std::abs(stroke.segs[0].c0.a-(float(0x4FFF0002u>>2)/1073741824.f*63/32))<1e-6f,"vector decoding uses original width, Z and intensity equation rather than fitted opacity");

            auto point=[&](double delta){memory.web_render_point(stream,0x00010000,delta,1.2);memory.web_render_point(stream+4,0x00010002,2+delta,1.2);
                for(unsigned k=0;k<4;++k)memory.web_render_point(WebRenderPrecision::base+k*8,k==0||k==3?0x00010002:0x00010000,(k==0||k==3?2:0)+delta,1.2);};
            point(.1);require(build_renderer_picture(memory.renderer_view(),0,first,error,precise),"first true subpixel geometry");
            point(.2);require(build_renderer_picture(memory.renderer_view(),0,second,error,precise),"next true subpixel geometry");
            require(std::abs((second.segs[0].x0-first.segs[0].x0)-.05f)<.0001f,"fresh fractional geometry moves while packed coordinates remain unchanged");
            require(std::abs(first.segs[0].x0-first.webs[0].quads[0].x)<.0001f&&std::abs(first.segs[0].y0-first.webs[0].quads[0].y)<.0001f,"web vectors and textured surface share precise coordinates");
            require(std::equal(before.begin(),before.end(),memory.bank(NativeFrameMemory::system).begin()),"render precision does not modify native game memory");
            memory.render_transform_point(entry,0x00B40078,180.125,120.375);t3knative::Picture origin;
            require(build_renderer_picture(memory.renderer_view(),0,origin,error,precise)&&std::abs(origin.segs[0].x0-second.segs[0].x0-.125f)<.0001f&&std::abs(origin.segs[0].y0-second.segs[0].y0-.375f)<.0001f,"projected object origins retain fractions independently of stream vertex precision");
            require(memory.write32(entry,0x00B40078)&&build_renderer_picture(memory.renderer_view(),0,origin,error,precise)&&origin.segs[0].x0==second.segs[0].x0,"rewriting object origin invalidates its precision stamp");
            memory.render_transform_point(entry+16,0x00800080,128,128.5);t3knative::Picture scaleA,scaleB;
            require(build_renderer_picture(memory.renderer_view(),0,scaleA,error,precise),"first precise projected scale");memory.render_transform_point(entry+16,0x00800080,128,128.75);
            require(build_renderer_picture(memory.renderer_view(),0,scaleB,error,precise)&&std::abs((scaleB.segs[0].x0-scaleA.segs[0].x0)-2.2f*.25f/256)<.0001f,"projected scales move between identical native packed size words");
            require(memory.write32(entry+16,0x00800080)&&build_renderer_picture(memory.renderer_view(),0,scaleB,error,precise)&&scaleB.segs[0].x0==second.segs[0].x0,"rewriting projected scale invalidates its precision stamp");
            require(memory.write32(stream+4,0x00010002)&&build_renderer_picture(memory.renderer_view(),0,second,error,precise)&&second.segs[0].x0==legacy.segs[0].x0,"rewritten words invalidate stale render metadata even when packed value is unchanged");
        }
        {
            for(unsigned variant=0;variant<4;++variant){
                t3knative::Web web;web.gpuMapping=true;
                if(variant==0)web.quads={{30,30,-3,-2},{280,30,25,0},{280,210,25,28},{30,210,-3,30}};
                if(variant==1)web.quads={{25.25f,20.75f,-3,0},{300.5f,70.25f,25,0},{195.75f,190.5f,25,30},{150.25f,170.75f,-3,30}};
                if(variant==2)web.quads={{180,20,-3,0},{305,175,25,0},{190,210,25,30},{55,175,-3,30}};
                if(variant==3)web.quads={{20,30,-3,0},{160,30,25,0},{160,210,25,30},{20,210,-3,30},{200,30,2,0},{340,30,10,0},{340,210,10,30},{200,210,2,30}};
                for(unsigned k=0;k<256;++k)web.texture[k]=((30+k%180)<<24)|((100+k%40)<<16)|((110+k%30)<<8)|255;
                t3knative::Glyph image;image.w=360;image.h=240;image.pw=360;image.ph=240;image.rgba.assign(360*240,0);std::vector<bool> compare(360*240,true);
                for(unsigned y=0;y<240;++y)for(unsigned x=0;x<360;++x){double px=x+.5,py=y+.5;double hits[4][3];unsigned n=0,base=variant==3&&x>=180?4:0;
                    for(unsigned k=0;k<4;++k){auto a=web.quads[base+k],b=web.quads[base+(k+1)%4];double dx=b.x-a.x,dy=b.y-a.y,len=std::hypot(dx,dy);if(len&&std::abs(dx*(py-a.y)-dy*(px-a.x))/len<1)compare[y*360+x]=false;
                        if(a.y==b.y||py<std::min(a.y,b.y)||py>std::max(a.y,b.y))continue;double f=(py-a.y)/(b.y-a.y);hits[n][0]=a.x+dx*f;hits[n][1]=a.u+(b.u-a.u)*f;hits[n][2]=a.v+(b.v-a.v)*f;++n;}
                    if(n<2)continue;unsigned l=0,r=0;for(unsigned k=1;k<n;++k){if(hits[k][0]<hits[l][0])l=k;if(hits[k][0]>hits[r][0])r=k;}if(px<hits[l][0]||px>=hits[r][0])continue;
                    double f=(px-hits[l][0])/(hits[r][0]-hits[l][0]),u=hits[l][1]+(hits[r][1]-hits[l][1])*f,v=hits[l][2]+(hits[r][2]-hits[l][2])*f,alpha=std::clamp(1-v/32,0.,1.);v+=2.75;
                    int iu=int(std::floor(u)),iv=int(std::floor(v));double fu=u-std::floor(u),fv=v-std::floor(v);unsigned packed=unsigned(alpha*255+.5)<<24;
                    for(unsigned c=0;c<3;++c){auto channel=[&](int xx,int yy){return double((web.texture[(yy&15)*16+(xx&15)]>>(24-c*8))&255);};double top=channel(iu,iv)*(1-fu)+channel(iu+1,iv)*fu,bot=channel(iu,iv+1)*(1-fu)+channel(iu+1,iv+1)*fu;packed|=unsigned((top*(1-fv)+bot*fv)*alpha+.5)<<(16-c*8);}image.rgba[y*360+x]=packed;
                }
                t3knative::Picture expected,actual;expected.glyphs.push_back(std::move(image));t3knative::Op text{t3knative::Op::kText};text.count=1;expected.ops.push_back(text);actual.webs.push_back(web);t3knative::Op op{t3knative::Op::kWeb};op.fade=2.75;actual.ops.push_back(op);
                auto capture=[&](const t3knative::Picture& pic){t3knative::Renderer draw;SDL_FRect area{0,0,360,240};draw.Draw(renderer,pic,pic.segs,area,1);auto* raw=SDL_RenderReadPixels(renderer,nullptr);require(raw,"web capture");auto* rgba=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(raw);require(rgba,"web conversion");std::vector<unsigned char> bytes(360*240*4);for(unsigned y=0;y<240;++y)std::copy_n(static_cast<unsigned char*>(rgba->pixels)+y*rgba->pitch,360*4,bytes.data()+y*360*4);SDL_DestroySurface(rgba);return bytes;};
                auto a=capture(expected),b=capture(actual);double total=0;unsigned maximum=0,count=0;for(size_t i=0;i<a.size();++i)if(compare[i/4]){unsigned delta=unsigned(std::abs(int(a[i])-int(b[i])));total+=delta;maximum=std::max(maximum,delta);++count;}
                std::printf("Web analytic comparison variant=%u mean=%.6f max=%u\n",variant,total/count,maximum);require(total/count<.5&&maximum<10,"web shader analytic scanline agreement");
            }
        }
        {
            // The original UV load can exceed the normalized output range.
            // Clipping the source to 255 before alpha blending would produce
            // Cr=191 rather than 200 at half coverage in this fixture.
            for(bool gpu:{false,true}){
                t3knative::Picture picture;t3knative::Web web;web.gpuMapping=gpu;web.unsignedChromaLanes=1;
                web.quads={{20,20,0,16},{150,20,0,16},{150,210,0,16},{20,210,0,16},{200,20,0,16},{330,20,0,16},{330,210,0,16},{200,210,0,16}};
                std::fill(std::begin(web.texture),std::end(web.texture),0x4090A0FF);picture.webs.push_back(web);picture.ops.push_back(t3knative::Op{t3knative::Op::kWeb});
                t3knative::Renderer draw;capture_cached(renderer,draw,picture);t3knative::FeedbackState history;require(draw.CaptureFeedback(renderer,history,error),"masked web feedback");
                std::vector<float> pixels(360*240*4);require(SDL_ConvertPixelsAndColorspace(360,240,SDL_PixelFormat(history.format),SDL_COLORSPACE_SRGB,0,history.pixels.data(),360*SDL_BYTESPERPIXEL(SDL_PixelFormat(history.format)),SDL_PIXELFORMAT_RGBA128_FLOAT,SDL_COLORSPACE_SRGB,0,pixels.data(),360*16),"masked web float readback");
                for(auto sample:{std::pair{80u,200.f},std::pair{260u,136.f}}){unsigned at=(100*360+sample.first)*4;std::printf("chroma gpu=%u x=%u actual=%f expected=%f\n",unsigned(gpu),sample.first,pixels[at+1]*255,sample.second);require(std::abs(pixels[at+1]*255-sample.second)<1.5f,"per-lane UV chroma is interpreted before blending without early clipping");}
            }
            std::printf("Web chroma: CPU mesh and GPU preserve unsigned source range and lane selection\n");
        }
        {
          t3knative::Picture p;t3knative::Seg s{40,80,80,80,{.8f,128.f/255,128.f/255,1.5f},{.8f,128.f/255,128.f/255,1.5f}};s.r=4;p.segs.push_back(s);t3knative::Op op{t3knative::Op::kSegs};op.count=1;p.ops.push_back(op);
          t3knative::Renderer draw;capture_cached(renderer,draw,p);t3knative::FeedbackState history;require(draw.CaptureFeedback(renderer,history,error),"capsule stroke feedback");std::vector<float> pixels(360*240*4);
          require(SDL_ConvertPixelsAndColorspace(360,240,SDL_PixelFormat(history.format),SDL_COLORSPACE_SRGB,0,history.pixels.data(),360*SDL_BYTESPERPIXEL(SDL_PixelFormat(history.format)),SDL_PIXELFORMAT_RGBA128_FLOAT,SDL_COLORSPACE_SRGB,0,pixels.data(),360*16),"capsule stroke conversion");
          for(auto point:{std::pair{38u,78u},std::pair{60u,83u},std::pair{36u,80u},std::pair{60u,80u}}){float dx=point.first<40?40-float(point.first):point.first>80?float(point.first)-80:0,dy=float(point.second)-80;float weight=1.5f*std::max(0.f,1-std::hypot(dx,dy)/4);float expected=16+188*weight;require(std::abs(pixels[(point.second*360+point.first)*4]*255-expected)<.8f,"circular vector caps and bright stroke sides retain analytic coverage");}
          std::printf("Vector shader: circular end caps, bright off-centre coverage and zero support boundary passed\n");
        }
        {
          t3knative::Picture seed;t3knative::Op fill{t3knative::Op::kFill};fill.w=360;fill.h=240;fill.colour={.5f,128.f/255,128.f/255,1};seed.ops.push_back(fill);
          t3knative::Picture source;t3knative::Op zoom{t3knative::Op::kZoom};zoom.cx=180;zoom.cy=120;source.ops.push_back(zoom);t3knative::Seg s{40,80,80,80,{.8f,128.f/255,128.f/255,-.25f},{.8f,128.f/255,128.f/255,-.25f}};s.r=5;source.segs.push_back(s);t3knative::Op lines{t3knative::Op::kSegs};lines.count=1;source.ops.push_back(lines);
          SDL_FRect dst{0,0,360,240};t3knative::Renderer full,fine;full.Draw(renderer,seed,seed.segs,dst,1);fine.Draw(renderer,seed,seed.segs,dst,1);full.Draw(renderer,source,source.segs,dst,1);auto expected=read_output(renderer);
          for(unsigned i=0;i<5;++i)require(fine.DrawSimulationSample(renderer,source,dst,i,5,error,nullptr,1,t3knative::FeedbackSampling::timeIntegrated),"signed-paint fractional feedback");auto actual=read_output(renderer);unsigned at=(80*360+60)*4;for(unsigned c=0;c<3;++c){std::printf("signed paint channel=%u full=%u fine=%u\n",c,unsigned(expected[at+c]),unsigned(actual[at+c]));require(std::abs(int(expected[at+c])-int(actual[at+c]))<=4,"negative vector weight retains its coupled gain in fine feedback");}
          std::printf("Signed paint: five fine feedback samples match one original frozen stroke update\n");
        }
        {
          for(bool flat:{false,true}){
            t3knative::Picture p;t3knative::Blob b;b.x=90.5f;b.y=80.5f;b.r=12;b.colour={.8f,128.f/255,128.f/255,.6f};b.flat=flat;p.blobs.push_back(b);t3knative::Op op{t3knative::Op::kBlobs};op.count=1;p.ops.push_back(op);
            t3knative::Renderer draw;capture_cached(renderer,draw,p);t3knative::FeedbackState history;require(draw.CaptureFeedback(renderer,history,error),"analytic disc readback");std::vector<float> pixels(360*240*4);
            require(SDL_ConvertPixelsAndColorspace(360,240,SDL_PixelFormat(history.format),SDL_COLORSPACE_SRGB,0,history.pixels.data(),360*SDL_BYTESPERPIXEL(SDL_PixelFormat(history.format)),SDL_PIXELFORMAT_RGBA128_FLOAT,SDL_COLORSPACE_SRGB,0,pixels.data(),360*16),"analytic disc conversion");
            for(unsigned dx:{0u,3u,8u,11u,13u}){float coverage=dx>=12?0:flat?1:1-float(dx*dx)/144;float expected=16+(204-16)*.6f*coverage;require(std::abs(pixels[(80*360+90+dx)*4]*255-expected)<.6f,"radial and flat disc pixel weights follow original row equations");}
          }
          std::printf("Particle shader: analytic radial falloff and alternate flat circular coverage passed\n");
        }
        {
            t3knative::Renderer reused;
            for(unsigned sample=0;sample<12;++sample){
                t3knative::Picture picture;picture.flip=0;t3knative::Glyph glyph;glyph.x=40+sample;glyph.y=40;glyph.w=glyph.h=32;glyph.pw=glyph.ph=64;
                if(sample<3){glyph.gpuNumber=true;glyph.numberParams[4]=glyph.numberParams[13]=glyph.numberParams[8]=glyph.numberParams[17]=16777216;glyph.numberParams[20]=glyph.numberParams[21]=32;glyph.numberParams[24]=1.f/255;glyph.numberParams[25]=glyph.numberParams[26]=64;
                    for(unsigned i=0;i<512;++i)glyph.numberTexture[i]=(i%32<16?0xA08080FF:0xC09070FF)^(sample==2?0x10000000:0);
                }else if(sample==6){glyph.rgba.assign(64*64,0xAA605050);}
                else{glyph.gpuRing=true;const float params[16]={24,24,32,32,1.f/196,1.f/196,sample==5?.25f:.5f,32766,8192,12288,0,0,40,40,0,0};std::copy(params,params+16,glyph.ringParams);for(auto& v:glyph.ringTexture)v=sample==9?0xB08090FF:0xA09080FF;}
                picture.glyphs.push_back(glyph);t3knative::Op image{t3knative::Op::kText};image.count=1;picture.ops.push_back(image);
                t3knative::Web web;web.gpuMapping=true;web.quads={{200,30,0,0},{320,30,16,0},{300,180,16,30},{220,180,0,30}};for(auto& v:web.texture)v=sample==10?0x708090FF:0xA08080FF;
                if(sample==7){t3knative::Plane plane;plane.cols=plane.rows=1;plane.grid={web.quads[0],web.quads[1],web.quads[3],web.quads[2]};std::fill(std::begin(plane.texture),std::end(plane.texture),0x508070FF);picture.planes.push_back(plane);picture.ops.push_back(t3knative::Op{t3knative::Op::kPlane});}
                else{picture.webs.push_back(web);picture.ops.push_back(t3knative::Op{t3knative::Op::kWeb});}
                t3knative::Renderer fresh;auto expected=capture_cached(renderer,fresh,picture),actual=capture_cached(renderer,reused,picture);require(expected==actual,"cached render resources must match fresh rendering across same-tick content changes, type switches and shared web/plane uploads");
            }
            std::printf("Texture cache: 12 same-tick mutation/type-switch states match fresh rendering exactly\n");
        }
        SDL_FRect dst{0,0,360,240};
        for(unsigned divisions:{2u,4u,5u,8u})for(unsigned frame=0;frame<32;++frame){
            t3knative::Picture picture;picture.flip=frame;
            if(frame==0){t3knative::Op fill{t3knative::Op::kFill};fill.w=360;fill.h=240;fill.colour={.2f,.5f,.5f,1};picture.ops.push_back(fill);}
            else if(frame%4==0){t3knative::Op ripple{t3knative::Op::kRipple};ripple.x=float(frame);ripple.y=.125f;ripple.w=3;ripple.h=.25f;ripple.fade=.04f;ripple.colour={.2f,.5f,.5f,1};picture.ops.push_back(ripple);}
            else{t3knative::Op zoom{t3knative::Op::kZoom};zoom.cx=180;zoom.cy=120;zoom.zx=1.005f;zoom.zy=1.01f;zoom.angle=.01f;zoom.fade=frame%3==0?1.04f:.96f;picture.ops.push_back(zoom);}
            t3knative::Op deposit{t3knative::Op::kBlendRect};deposit.x=30+frame*6;deposit.y=40+frame;deposit.w=10;deposit.h=20;deposit.colour={.8f,.4f,.6f,.3f};picture.ops.push_back(deposit);
            require(reference.submit_picture(renderer,picture,divisions*100+frame+1,dst,error)&&display.submit_picture(renderer,picture,divisions*100+frame+1,dst,error),"submit native state");
            // Four genuinely different fresh states must not be deposited
            // into persistent history. The final sample matches reference.
            for(unsigned substep=0;substep<divisions;++substep){
                if(substep+1<divisions&&frame%divisions<=substep)continue;
                auto actual=picture;
                if(substep+1<divisions){actual.ops.back().x-=float(divisions-1-substep);actual.ops.back().colour.a=.75f;}
                require(fine.DrawSimulationSample(renderer,actual,dst,substep,divisions,error),"draw actual fine simulation state");
            }
            t3knative::FeedbackState expected,before,after;
            require(reference.capture_feedback(renderer,expected,error)&&display.capture_feedback(renderer,before,error)&&same(expected,before),"identical simulation sequence");
            t3knative::FeedbackState fineFeedback;
            require(fine.CaptureFeedback(renderer,fineFeedback,error)&&same(expected,fineFeedback),"fine geometry sampling multiplied feedback deposits");
            // 0/1/2/5/9 display refreshes per real native state, without another
            // simulation/effect update. Neither blending nor feedback advances.
            unsigned refreshes[]={0,1,2,5,9};
            for(unsigned n=0;n<refreshes[frame%5];++n){require(display.redraw(renderer,dst),"presentation redraw");require(SDL_RenderPresent(renderer),"present");}
            require(display.capture_feedback(renderer,after,error)&&same(expected,after),"display frequency changed feedback history");++comparisons;
        }
    }
    {
        for(auto mode:{t3knative::FeedbackSampling::pinned,t3knative::FeedbackSampling::continuous,t3knative::FeedbackSampling::clocked,t3knative::FeedbackSampling::timeIntegrated})for(auto ratio:{std::pair{15u,2u},std::pair{5u,2u},std::pair{10u,3u}}){
            bool continuous=mode!=t3knative::FeedbackSampling::pinned;
            NativeFrameRenderer plain,extra;std::string error;SDL_FRect dst{0,0,360,240};
            require(plain.initialize(renderer,error)&&extra.initialize(renderer,error),"rational feedback pipelines");
            unsigned phase=0;
            for(unsigned sample=0;sample<120;++sample){
                t3knative::Picture picture;picture.flip=sample;
                if(sample==0||(continuous&&sample==75)){t3knative::Op fill{t3knative::Op::kFill};fill.w=360;fill.h=240;fill.colour={.2f,.5f,.5f,1};picture.ops.push_back(fill);}
                else{t3knative::Op zoom{t3knative::Op::kZoom};zoom.cx=180;zoom.cy=120;zoom.zx=1.003f;zoom.zy=1.006f;zoom.angle=.002f;zoom.fade=continuous?(sample>=30&&sample<45?.97f:1.01f):sample%3?1.01f:.97f;picture.ops.push_back(zoom);}
                t3knative::Op point{t3knative::Op::kBlendRect};point.x=sample;point.y=60;point.w=12;point.h=10;point.colour={.5f,.4f,.3f,.25f};picture.ops.push_back(point);
                if(sample==59){plain.end_simulation_interval(phase);extra.end_simulation_interval(phase);}
                require(plain.submit_simulation_picture(renderer,picture,sample+1,dst,phase,ratio.first,error,ratio.second,mode)&&extra.submit_simulation_picture(renderer,picture,sample+1,dst,phase,ratio.first,error,ratio.second,mode),"rational fresh-state feedback across nonintegral authored boundaries");
                t3knative::FeedbackState a,b;require(plain.capture_feedback(renderer,a,error),"capture rational feedback");
                for(unsigned present=0;present<sample%6;++present)require(extra.redraw(renderer,dst),"extra rational display");
                require(extra.capture_feedback(renderer,b,error)&&same(a,b),"rational feedback independent of display frequency");
                phase=(phase+ratio.second)%ratio.first;++comparisons;
            }
            require(phase==0,"rational simulation consumes exactly a real second");
        }
    }
    // The Off path must consume original effect parameters at full strength,
    // even after a fractional feedback interval has existed in this renderer.
    for(unsigned variant=0;variant<3;++variant){
        NativeFrameRenderer off;t3knative::Renderer original;SDL_FRect dst{0,0,360,240};std::string error;
        require(off.initialize(renderer,error),"reference-rate effect pipeline");
        t3knative::Picture seed;seed.complete=true;
        t3knative::Op fill{t3knative::Op::kFill};fill.w=360;fill.h=240;fill.colour={16.f/255,128.f/255,128.f/255,1};seed.ops.push_back(fill);
        t3knative::Op paint{t3knative::Op::kBlendRect};paint.x=170;paint.y=100;paint.w=20;paint.h=40;paint.colour={.6f,.45f,.55f,.7f};seed.ops.push_back(paint);
        require(off.submit_simulation_picture(renderer,seed,1,dst,0,5,error,1,t3knative::FeedbackSampling::timeIntegrated),"warm fractional history");
        off.end_simulation_interval();t3knative::FeedbackState start;
        require(off.capture_feedback(renderer,start,error)&&original.RestoreFeedback(renderer,start,error),"share exact starting feedback");
        for(unsigned tick=0;tick<24;++tick){
            t3knative::Picture picture;picture.complete=true;picture.flip=tick;
            t3knative::Op effect{variant==2?t3knative::Op::kRipple:t3knative::Op::kZoom};
            effect.cx=180;effect.cy=120;effect.zx=1.08f;effect.zy=1.04f;effect.angle=.018f;
            effect.x=7;effect.fade=variant==0?1.05f:variant==1?.8125f:.08f;effect.colour=fill.colour;picture.ops.push_back(effect);
            paint.x=150+tick;picture.ops.push_back(paint);
            original.Draw(renderer,picture,picture.segs,dst,1);t3knative::FeedbackState expected,actual;
            require(original.CaptureFeedback(renderer,expected,error),"capture original effect tick");
            require(off.submit_picture(renderer,picture,tick+2,dst,error)&&off.capture_feedback(renderer,actual,error),"draw Off effect tick");
            require(same(expected,actual),"Off restores original per-tick blur, decay, displacement and paint strength");
        }
    }
    std::printf("Reference-rate effects: 72 full ticks match original drawing exactly after fractional history\n");
    // Stationary score-like strokes must retain the original discrete echo
    // pattern at exact authored times. Display-count invariance alone cannot
    // detect alternating .8/1.2-tick spacings or continuous-source smearing.
    for(auto ratio:{std::pair{5u,1u},std::pair{5u,2u},std::pair{10u,3u}}){
        t3knative::Renderer reference,fine,preview;SDL_FRect dst{0,0,360,240};std::string error;
        t3knative::Picture seed;t3knative::Op fill{t3knative::Op::kFill};fill.w=360;fill.h=240;fill.colour={16.f/255,128.f/255,128.f/255,1};seed.ops.push_back(fill);
        reference.Draw(renderer,seed,seed.segs,dst,1);fine.Draw(renderer,seed,seed.segs,dst,1);
        t3knative::Picture source;t3knative::Op zoom{t3knative::Op::kZoom};zoom.cx=180;zoom.cy=120;zoom.zx=zoom.zy=1.12f;zoom.angle=.006f;zoom.fade=1.02f;source.ops.push_back(zoom);
        for(unsigned x:{130u,136u,144u}){t3knative::Op stroke{t3knative::Op::kBlendRect};stroke.x=float(x);stroke.y=108;stroke.w=2;stroke.h=12;stroke.colour={.8f,128.f/255,128.f/255,1};source.ops.push_back(stroke);}
        unsigned phase=0,referenceTicks=0;double worstMean=0;unsigned worstPixel=0,checks=0;
        for(unsigned sample=0;sample<120;++sample){
            require(fine.DrawSimulationSample(renderer,source,dst,phase,ratio.first,error,nullptr,ratio.second,t3knative::FeedbackSampling::clocked),"clocked score echo sample");
            phase=(phase+ratio.second)%ratio.first;
            auto actual=read_output(renderer);unsigned ticks=(sample+1)*ratio.second/ratio.first;
            while(referenceTicks<ticks){reference.Draw(renderer,source,source.segs,dst,1);++referenceTicks;}
            if(phase){
                t3knative::FeedbackState history;
                require(reference.CaptureFeedback(renderer,history,error)&&preview.RestoreFeedback(renderer,history,error),"original discrete echo history");
                preview.Draw(renderer,source,source.segs,dst,float(phase)/ratio.first,nullptr,false);
            }else reference.Redraw(renderer,dst);
            auto expected=read_output(renderer);double difference=0;unsigned maximum=0;
            for(size_t i=0;i<actual.size();++i)if(i%4!=3){unsigned d=unsigned(std::abs(int(actual[i])-int(expected[i])));difference+=d;maximum=std::max(maximum,d);}
            double mean=difference/(360*240*3);worstMean=std::max(worstMean,mean);worstPixel=std::max(worstPixel,maximum);++checks;
            require(mean<.02&&maximum<=2,"clocked score echoes match phase-correct original-rate history at every fine sample");
        }
        std::printf("Score echo oracle %u/%u: %u checkpoints, mean<=%.6f max=%u\n",ratio.second,ratio.first,checks,worstMean,worstPixel);
    }
    // Frozen overlapping paint has a closed-form coupled equilibrium. This
    // catches independent-opacity scaling and whole-picture linear mixing.
    for(bool ripple:{false,true}){
        t3knative::Renderer draw;SDL_FRect dst{0,0,360,240};std::string error;
        double n=16.0/255,f=ripple?.8125:1.0;
        t3knative::Picture seed;t3knative::Op fill{t3knative::Op::kFill};fill.w=360;fill.h=240;fill.colour={float(n),128.f/255,128.f/255,1};seed.ops.push_back(fill);draw.Draw(renderer,seed,seed.segs,dst,1);
        t3knative::Picture source;t3knative::Op feedback{ripple?t3knative::Op::kRipple:t3knative::Op::kZoom};feedback.cx=180;feedback.cy=120;feedback.fade=float(ripple?1-f:f);feedback.colour=fill.colour;source.ops.push_back(feedback);
        for(auto paint:{std::pair{.8f,.2f},std::pair{.4f,.3f}}){t3knative::Op r{t3knative::Op::kBlendRect};r.x=160;r.y=100;r.w=r.h=40;r.colour={paint.first,128.f/255,128.f/255,paint.second};source.ops.push_back(r);}
        for(unsigned sample=0;sample<120;++sample)require(draw.DrawSimulationSample(renderer,source,dst,sample%5,5,error,nullptr,1,t3knative::FeedbackSampling::timeIntegrated),"coupled frozen paint integration");
        auto output=read_output(renderer);double a=f*.56,b=.4*.3+.8*.2*.7+.56*(1-f)*n;
        double expectedY=std::pow(a,24)*n+b*(1-std::pow(a,24))/(1-a),expectedRgb=(expectedY*255-16)*255/219;
        unsigned actual=output[(120*360+180)*4];

        std::printf("Coupled paint %s: measured=%u expected=%.4f\n",ripple?"ripple":"zoom",actual,expectedRgb);
        require(std::abs(actual-expectedRgb)<3,"overlap and feedback share the correct time-normalized fixed point");
    }
    {
        double means[2]{};unsigned support[2]{};
        for(unsigned variant=0;variant<2;++variant){
            t3knative::Renderer draw;SDL_FRect dst{0,0,360,240};std::string error;
            t3knative::Picture seed;t3knative::Op fill{t3knative::Op::kFill};fill.w=360;fill.h=240;fill.colour={16.f/255,128.f/255,128.f/255,1};seed.ops.push_back(fill);draw.Draw(renderer,seed,seed.segs,dst,1);
            for(unsigned sample=0;sample<120;++sample){
                t3knative::Picture source;t3knative::Op zoom{t3knative::Op::kZoom};zoom.cx=180;zoom.cy=120;zoom.fade=.8125f;source.ops.push_back(zoom);
                t3knative::Op stroke{t3knative::Op::kBlendRect};stroke.x=float(40+sample);stroke.y=110;stroke.w=2;stroke.h=20;stroke.colour={.8f,128.f/255,128.f/255,.2f};source.ops.push_back(stroke);
                require(draw.DrawSimulationSample(renderer,source,dst,sample%5,5,error,nullptr,1,variant?t3knative::FeedbackSampling::timeIntegrated:t3knative::FeedbackSampling::pinned),"moving trail integration");
            }
            auto pixels=read_output(renderer);for(unsigned x=40;x<161;++x)means[variant]+=pixels[(120*360+x)*4]/121.0;
            for(unsigned x=119;x<149;++x)support[variant]+=pixels[(120*360+x)*4]>1;
        }
        std::printf("Moving trail: pinned mean=%.4f support=%u/30; integrated mean=%.4f support=%u/30\n",means[0],support[0],means[1],support[1]);
        require(support[1]>=27&&support[1]>support[0]+10,"fine motion deposits fill the inter-frame trail gaps");
        require(means[1]>means[0]*.5&&means[1]<means[0]*1.5,"moving trail energy stays in the reference ballpark");
    }
    {
        t3knative::Renderer draw;SDL_FRect dst{0,0,360,240};std::string error;
        t3knative::Picture seed;t3knative::Op fill{t3knative::Op::kFill};fill.w=360;fill.h=240;fill.colour={.8f,128.f/255,128.f/255,1};seed.ops.push_back(fill);draw.Draw(renderer,seed,seed.segs,dst,1);
        auto first=read_output(renderer);double half=first[(120*360+180)*4]*.5;unsigned halfAt=0;
        t3knative::Picture decay;t3knative::Op feedback{t3knative::Op::kZoom};feedback.cx=180;feedback.cy=120;feedback.fade=.8125f;decay.ops.push_back(feedback);
        for(unsigned sample=0;sample<40;++sample){require(draw.DrawSimulationSample(renderer,decay,dst,sample%5,5,error,nullptr,1,t3knative::FeedbackSampling::timeIntegrated),"fine feedback decay");auto pixels=read_output(renderer);if(!halfAt&&pixels[(120*360+180)*4]<=half)halfAt=sample+1;}
        std::printf("Feedback half-life: %u fine samples (analytic 16.69), same real-time decay\n",halfAt);
        require(halfAt>=16&&halfAt<=18,"decay retains real-time duration at fivefold sampling");
    }
    {
        t3knative::Renderer draw;SDL_FRect dst{0,0,360,240};std::string error;
        t3knative::Picture seed;t3knative::Op fill{t3knative::Op::kFill};fill.w=360;fill.h=240;fill.colour={17.f/255,129.f/255,129.f/255,1};seed.ops.push_back(fill);draw.Draw(renderer,seed,seed.segs,dst,1);auto before=read_output(renderer);
        t3knative::Picture feedback;t3knative::Op zoom{t3knative::Op::kZoom};zoom.cx=180;zoom.cy=120;zoom.fade=1.25f;feedback.ops.push_back(zoom);
        for(unsigned sample=0;sample<300;++sample)require(draw.DrawSimulationSample(renderer,feedback,dst,sample%5,5,error,nullptr,1,t3knative::FeedbackSampling::timeIntegrated),"brightening pigment threshold");
        auto after=read_output(renderer);for(unsigned c=0;c<3;++c)require(std::abs(int(after[(120*360+180)*4+c])-int(before[(120*360+180)*4+c]))<=1,"one-byte colour deviations must not become an expanding colour wash");
        std::printf("Brightening pigment: one-byte deviations remain stable over 300 fine updates\n");
    }
    {
        t3knative::Renderer native,fine;SDL_FRect dst{0,0,360,240};std::string error;
        t3knative::Picture seed;t3knative::Op fill{t3knative::Op::kFill};fill.w=360;fill.h=240;fill.colour={16.f/255,128.f/255,128.f/255,1};seed.ops.push_back(fill);
        t3knative::Op stroke{t3knative::Op::kBlendRect};stroke.x=180;stroke.y=100;stroke.w=2;stroke.h=40;stroke.colour={.8f,128.f/255,128.f/255,1};seed.ops.push_back(stroke);
        native.Draw(renderer,seed,seed.segs,dst,1);fine.Draw(renderer,seed,seed.segs,dst,1);
        t3knative::Picture transport;t3knative::Op ripple{t3knative::Op::kRipple};ripple.x=15;ripple.fade=0;ripple.colour=fill.colour;transport.ops.push_back(ripple);
        for(unsigned tick=0;tick<8;++tick)native.Draw(renderer,transport,transport.segs,dst,1);auto referencePixels=read_output(renderer);
        for(unsigned sample=0;sample<40;++sample)require(fine.DrawSimulationSample(renderer,transport,dst,sample%5,5,error,nullptr,1,t3knative::FeedbackSampling::timeIntegrated),"pure transport without accumulation");auto finePixels=read_output(renderer);
        auto moments=[](const std::vector<unsigned char>& pixels){double sum=0,xsum=0,x2sum=0;for(unsigned y=0;y<240;++y)for(unsigned x=0;x<360;++x){double power=pixels[(y*360+x)*4];sum+=power;xsum+=power*x;x2sum+=power*x*x;}double centre=xsum/sum;return std::array<double,3>{sum,centre,x2sum/sum-centre*centre};};
        auto a=moments(referencePixels),b=moments(finePixels);
        std::printf("Pure displacement: native centre=%.6f variance=%.6f; fine centre=%.6f variance=%.6f; energy ratio=%.6f\n",a[1],a[2],b[1],b[2],b[0]/a[0]);
        require(std::abs(a[1]-b[1])<.2&&b[0]/a[0]>.9&&b[0]/a[0]<1.1,"transport keeps elapsed displacement and energy without any new paint");
        // Deliberately report rather than assert equal variance: extra spatial
        // diffusion from repeated fractional bilinear sampling remains open.
    }
    // Actual particle decoding and drawing must retain particles outside the
    // old 4:3 bounds; merely testing synthetic rectangles would miss a cutoff
    // in the point decoder. Keep the original projection and particle stream.
    for(unsigned width:{960u,1260u}){
        NativeFrameMemory memory;uint32_t entry=0x8004E040,points=0x80070000;
        require(memory.write32(entry+48,points)&&memory.write32(entry+60,0x00310309)&&memory.write32(entry+64+60,0xFF),"wide particle display records");
        for(unsigned i=0;i<2;++i){uint32_t x=i?100:uint16_t(-100);require(memory.write32(points+i*16,x<<16)&&memory.write32(points+i*16+4,0x400000)&&memory.write32(points+i*16+8,0xC0808000)&&memory.write32(points+i*16+12,4),"wide particle input");}
        require(memory.write32(points+32,0x80000000),"wide particle sentinel");
        t3knative::Picture picture;std::string error;require(build_renderer_picture(memory.renderer_view(),0,picture,error)&&picture.blobs.size()==2,"original particle decoder preserves both off-4:3 points");
        require(picture.blobs[0].x<0&&picture.blobs[1].x>360,"probe particles lie outside original horizontal bounds");
        auto* target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,width,540);require(target,"wide particle target");
        SDL_SetRenderTarget(renderer,target);SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
        SDL_FRect dst{(float(width)-720)/2,0,720,540},view{0,0,float(width),540};
        {t3knative::Renderer draw;draw.Draw(renderer,picture,picture.segs,dst,1,&view);auto pixels=read_output(renderer);
         for(auto& particle:picture.blobs){unsigned x=unsigned(dst.x+particle.x*2),y=unsigned(particle.y*2.25f);auto at=(size_t(y)*width+x)*4;
          require(pixels.at(at)+pixels.at(at+1)+pixels.at(at+2)>100,"particle outside legacy screen is visible in expanded viewport");}}
        SDL_SetRenderTarget(renderer,nullptr);SDL_DestroyTexture(target);
    }
    std::printf("Wide particle decoder/render: both old-edge crossings remain visible at 16:9 and 21:9\n");
    t3knative::ShutdownPipeline();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    std::printf("Render clock independence: %u native states, irregular 0/1/2/5/9 presentations, exact feedback match\n",comparisons);
}
