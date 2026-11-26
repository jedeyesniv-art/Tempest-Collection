// t3k_native.cpp - Tempest 3000 native renderer: the vector shapes (see t3k_native.h).
#include "t3k_native.h"
#include "t3k_runtime/native_profile.h"
namespace profiling=tempest_native::profiling;

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "shaders/ycc_fade.dxil.h"
#include "shaders/ycc_in.dxil.h"
#include "shaders/ycc_out.dxil.h"
#include "shaders/ycc_ripple.dxil.h"
#include "shaders/ycc_ring.dxil.h"
#include "shaders/ycc_number.dxil.h"
#include "shaders/ycc_web.dxil.h"
#include "shaders/ycc_temporal.dxil.h"

namespace t3knative {

// The YCrCb pipeline (InitPipeline): pictures' colours are YCrCb bytes / 255 (R = Y, G = Cr, B = Cb) and the renderer
// works in YCrCb like the hardware, converting to RGB only for the window. Off = the old RGB path.
bool g_ycc = false;
// Sharp vectors (SetLineStyle): items up to g_sharp_size game pixels across (text, claw, enemies, HUD - not the web or
// the full-screen streaks) get a thin core at g_core x the line's width plus a glow at its own width x g_glow.
bool g_sharp = false;
float g_core = 0.35f, g_glow = 0.4f, g_sharp_size = 100.f;

namespace {

// --- the module's integer maths (806A3F50), bit-exact as tools/t3k_vecmodel.py ---

int32_t S32(int64_t v) { return int32_t(uint32_t(uint64_t(v))); }
// NUON mul a, b, >>#sh: signed product, arithmetic shift, low 32 bits.
int32_t Mul(int32_t a, int32_t b, int sh) { return S32((int64_t(a) * int64_t(b)) >> sh); }
int32_t Sext(uint32_t v, int bits) {
  v &= (1u << bits) - 1;
  return (v & (1u << (bits - 1))) ? int32_t(v) - (1 << bits) : int32_t(v);
}

// Library 806A4BBE (local $20300F3E): angle (16 bits a turn) -> cos, sin in 2.30.
void SinCos(uint32_t angle, int32_t* c, int32_t* s) {
  const int32_t C1 = int32_t(0xFFB901AF), C2 = 0x051573CA, C3 = int32_t(0xD6A9B10A), C4 = 0x6487D705;
  const int32_t x = int32_t(((angle << 18) & 0xFFFFFFFFu) >> 2);
  const uint32_t q = (angle >> 14) & 3;
  int32_t cc, ss;
  if (x == 0) {
    cc = 0x40000000;
    ss = 0;
  } else {
    const int32_t one_x = S32(int64_t(0x40000000) - x);
    const int32_t u = Mul(x, x, 30), v = Mul(one_x, one_x, 30);
    int32_t pu = S32(int64_t(Mul(u, C1, 30)) + C2), pv = S32(int64_t(Mul(v, C1, 30)) + C2);
    pu = S32(int64_t(Mul(u, pu, 30)) + C3);
    pv = S32(int64_t(Mul(v, pv, 30)) + C3);
    pu = S32(int64_t(Mul(u, pu, 30)) + C4);
    pv = S32(int64_t(Mul(v, pv, 30)) + C4);
    ss = Mul(x, pu, 30);
    cc = Mul(one_x, pv, 30);
  }
  switch (q) {
    case 0: *c = cc; *s = ss; break;
    case 1: *c = S32(-int64_t(ss)); *s = cc; break;
    case 2: *c = S32(-int64_t(cc)); *s = S32(-int64_t(ss)); break;
    default: *c = ss; *s = S32(-int64_t(cc)); break;
  }
}

struct Matrix { int32_t r20, r21, r22, r23; };

Matrix ObjectMatrix(uint32_t w4, uint32_t w6) {
  const int32_t s_lo = Sext(w4, 10) * 256, s_hi = Sext(w4 >> 16, 10) * 256;
  int32_t a, b;
  SinCos(w6 & 0xFFFF, &a, &b);
  return {Mul(s_hi, a, 16), Mul(s_lo, S32(-int64_t(b)), 16), Mul(s_hi, b, 16), Mul(s_lo, a, 16)};
}

// 806A422A..806A43A8, unsorted: (x0, y0) = the current vertex, 16.16.
void Segment(const uint32_t* e, const Matrix& m, uint32_t cur, uint32_t prev, uint32_t lfsr, int32_t cf0,
             int32_t out[4]) {
  int32_t r8 = Sext(cur, 16), r9 = Sext(cur >> 16, 16), r10 = Sext(prev, 16), r11 = Sext(prev >> 16, 16);
  int32_t r28 = r8 + ((r10 - r8) >> 1), r29 = r9 + ((r11 - r9) >> 1);
  r8 -= r28; r9 -= r29; r10 -= r28; r11 -= r29;
  int32_t j16 = Sext(lfsr >> 16, 8), j17 = Sext(lfsr >> 24, 8);
  int32_t t = S32(int64_t(int32_t(e[12])) + cf0);
  t = t <= 0 ? 0 : std::min(t, 0xFFFF);
  const uint32_t w13 = e[13];
  int32_t r0 = Mul(int32_t(lfsr & 0xFFFF), Sext(w13 >> 24, 8), 4);
  j16 = Mul(int32_t((w13 >> 16) & 0xFF), j16, 5);
  j17 = Mul(int32_t((w13 >> 8) & 0xFF), j17, 5);
  j16 = Mul(t, j16, 16);
  j17 = Mul(t, j17, 16);
  r0 = Mul(t, r0, 16);
  int32_t c, s;
  SinCos(uint32_t(r0) & 0xFFFF, &c, &s);
  r28 += j16;
  r29 += j17;
  auto rot = [&](int32_t x, int32_t y, int32_t* ox, int32_t* oy) {
    *ox = S32(int64_t(Mul(x, c, 30)) + Mul(y, S32(-int64_t(s)), 30));
    *oy = S32(int64_t(Mul(x, s, 30)) + Mul(y, c, 30));
  };
  rot(r8, r9, &r8, &r9);
  rot(r10, r11, &r10, &r11);
  r8 += r28; r9 += r29; r10 += r28; r11 += r29;
  auto obj = [&](int32_t x, int32_t y, int32_t* ox, int32_t* oy) {
    *ox = S32(int64_t(Mul(m.r20, x, 14)) + Mul(m.r21, y, 14));
    *oy = S32(int64_t(Mul(m.r22, x, 14)) + Mul(m.r23, y, 14));
  };
  obj(r8, r9, &r8, &r9);
  obj(r10, r11, &r10, &r11);
  const int32_t ox = int32_t((e[0] >> 16) << 16), oy = int32_t((e[0] & 0xFFFF) << 16);
  out[0] = S32(int64_t(r8) + ox);
  out[1] = S32(int64_t(r9) + oy);
  out[2] = S32(int64_t(r10) + ox);
  out[3] = S32(int64_t(r11) + oy);
}

// --- the line look ---

// The tent's radius (game pixels from the centre line to zero coverage) is the width byte (w5 low) less ~0.06:
// width 2 -> 1.94 (all play lines), 13 -> 12.96, 14 -> 14.2 (the bonus screen's big numbers; tools/t3k_linefit.py).
constexpr float kZPeak = 0.00888f;    // centre opacity per unit of the colour's 4th byte

// A YCrCbZ colour word -> RGB as Nuance shows it (BT.601, video range), alpha = the line's centre opacity.
SDL_FColor RgbColour(uint32_t w, float strength) {
  auto cl = [](float v) { return std::clamp(v, 0.f, 1.f); };
  const float y = cl(float((w >> 24) & 0xFF) * (255.f / 219.f) / 255.f - 16.f / 219.f);
  const float cr = cl(float((w >> 16) & 0xFF) * (255.f / 224.f) / 255.f - 16.f / 224.f) - 0.5f;
  const float cb = cl(float((w >> 8) & 0xFF) * (255.f / 224.f) / 255.f - 16.f / 224.f) - 0.5f;
  return {cl(y + 1.402f * cr), cl(y - 0.34413f * cb - 0.714136f * cr), cl(y + 1.772f * cb),
          cl(kZPeak * float(w & 0xFF) * strength)};
}

// A YCrCbZ colour word as the pictures carry it: YCrCb bytes / 255 (the YCrCb pipeline), else RGB.
SDL_FColor Colour(uint32_t w, float strength) {
  if (!g_ycc) return RgbColour(w, strength);
  return {float((w >> 24) & 0xFF) / 255.f, float((w >> 16) & 0xFF) / 255.f, float((w >> 8) & 0xFF) / 255.f,
          std::clamp(kZPeak * float(w & 0xFF) * strength, 0.f, 1.f)};
}

// A picture colour (YCrCb in the pipeline) -> RGB, for drawing straight onto the window.
SDL_FColor ToRgb(const SDL_FColor& c) {
  if (!g_ycc) return c;
  auto byte = [](float v) { return uint32_t(std::clamp(v * 255.f + 0.5f, 0.f, 255.f)); };
  SDL_FColor o = RgbColour(byte(c.r) << 24 | byte(c.g) << 16 | byte(c.b) << 8, 1.f);
  o.a = c.a;
  return o;
}

// --- the YCrCb pipeline's shaders (GPU renderer only) ---
SDL_GPUDevice* g_device = nullptr;
SDL_GPUShader* g_shader[8] = {};
SDL_GPUSampler* g_temporalSampler=nullptr;
SDL_GPURenderState* g_number=nullptr;
SDL_GPURenderState* g_web=nullptr;
SDL_GPURenderState* g_ring = nullptr;
SDL_GPURenderState* g_ripple = nullptr;   // ripple feedback (uniforms: ripple, colour)
SDL_GPURenderState* g_fade = nullptr;   // zoom feedback with fade (uniform: x = fade)
SDL_GPURenderState* g_out = nullptr;    // YCrCb -> RGB
SDL_GPURenderState* g_in = nullptr;     // RGB -> YCrCb

SDL_GPURenderState* MakeState(SDL_Renderer* r, const unsigned char* code, size_t size, int uniforms, SDL_GPUShader** sh) {
  SDL_GPUShaderCreateInfo info{};
  info.code = code;
  info.code_size = size;
  info.entrypoint = "main";
  info.format = SDL_GPU_SHADERFORMAT_DXIL;
  info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  info.num_samplers = 1;
  info.num_uniform_buffers = uint32_t(uniforms);
  if (!(*sh = SDL_CreateGPUShader(g_device, &info))) return nullptr;
  SDL_GPURenderStateCreateInfo state{};
  state.fragment_shader = *sh;
  return SDL_CreateGPURenderState(r, &state);
}

// Draws tex over the whole current target through a render state (nullptr = plain copy).
void Pass(SDL_Renderer* ren, SDL_Texture* tex, SDL_GPURenderState* st, const SDL_FRect* dst) {
  SDL_BlendMode mode;
  SDL_GetTextureBlendMode(tex, &mode);
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
  if (st) SDL_SetGPURenderState(ren, st);
  SDL_RenderTexture(ren, tex, nullptr, dst);
  if (st) SDL_SetGPURenderState(ren, nullptr);
  SDL_SetTextureBlendMode(tex, mode);
}

// What the builders read: the words MPE0 read while drawing (capture), or the emulator's memory itself (live: the
// draw gate, when the renderers skip the picture). Live regions: system bus $8xxxxxxx, main bus $4xxxxxxx, one MPE's
// local memory $2xxxxxxx (the job word); big-endian words.
struct Mem {
  const std::unordered_map<uint32_t, uint32_t>* map = nullptr;
  const uint8_t* sys = nullptr;
  size_t sys_n = 0;
  const uint8_t* main = nullptr;
  size_t main_n = 0;
  const uint8_t* local = nullptr;
  size_t local_n = 0;
};

bool Read(const Mem& m, uint32_t addr, uint32_t* w) {
  if (!m.map) {
    const uint8_t* p = nullptr;
    size_t n = 0, off = 0;
    switch (addr >> 28) {
      case 8: p = m.sys; n = m.sys_n; off = addr & 0x7FFFFFu; break;
      case 4: p = m.main; n = m.main_n; off = addr - 0x40000000u; break;
      case 2: p = m.local; n = m.local_n; off = addr & 0x7FFFFFu; break;
      default: return false;
    }
    if (!p || (off & 3) || off + 4 > n) return false;
    *w = uint32_t(p[off]) << 24 | uint32_t(p[off + 1]) << 16 | uint32_t(p[off + 2]) << 8 | p[off + 3];
    return true;
  }
  const auto& mem = *m.map;
  auto it = mem.find(addr);
  if (it == mem.end()) return false;
  *w = it->second;
  return true;
}

void PreciseSegment(const uint32_t* e,const Matrix& m,const double current[2],const double previous[2],uint32_t lfsr,int32_t cf0,double out[4],const double* scales=nullptr){
  int32_t t=S32(int64_t(int32_t(e[12]))+cf0);t=t<=0?0:std::min(t,0xFFFF);
  uint32_t w13=e[13];
  int32_t angle=Mul(t,Mul(int32_t(lfsr&0xFFFF),Sext(w13>>24,8),4),16);
  int32_t jx=Mul(t,Mul(int32_t((w13>>16)&255),Sext(lfsr>>16,8),5),16);
  int32_t jy=Mul(t,Mul(int32_t((w13>>8)&255),Sext(lfsr>>24,8),5),16);
  int32_t c,s;SinCos(uint32_t(angle)&65535,&c,&s);
  double matrix[4]={double(m.r20),double(m.r21),double(m.r22),double(m.r23)};
  if(scales){int32_t c,s;SinCos(e[6]&65535,&c,&s);matrix[0]=scales[1]*c/256;matrix[1]=-scales[0]*s/256;matrix[2]=scales[1]*s/256;matrix[3]=scales[0]*c/256;}
  double mx=(current[0]+previous[0])*.5,my=(current[1]+previous[1])*.5;
  auto transform=[&](const double p[2],unsigned index){
    double dx=p[0]-mx,dy=p[1]-my;
    double x=(dx*c-dy*s)/1073741824.0+mx+jx,y=(dx*s+dy*c)/1073741824.0+my+jy;
    out[index]=(matrix[0]*x+matrix[1]*y)/1073741824.0+double(Sext(e[0]>>16,16));
    out[index+1]=(matrix[2]*x+matrix[3]*y)/1073741824.0+double(Sext(e[0],16));
    for(unsigned k=index;k<index+2;++k)if(out[k]<-32768||out[k]>=32768)out[k]-=std::floor((out[k]+32768)/65536)*65536;
  };
  transform(current,0);transform(previous,2);
}

void Shape(const uint32_t* entry, const Mem& mem, std::vector<Seg>* segs,const PictureBuildClocks& clocks,uint32_t entryAddress) {
  uint32_t e[16];
  std::copy(entry, entry + 16, e);
  uint32_t ptr = e[7], pal[16] = {}, prev = 0, w = 0;
  bool have_prev = false;
  uint32_t previousAddress=0;
  int32_t cf0 = 0;
  uint32_t lfsr = 0x31415926;
  Matrix m = ObjectMatrix(e[4], e[6]);
  double scales[2];bool preciseScale=clocks.preciseWeb&&clocks.precisionTransform&&clocks.precisionTransform(clocks.precisionData,entryAddress+16,e[4],scales[0],scales[1]);
  for (int n = 0; n < 20000; ++n) {
    uint32_t currentAddress=ptr;
    if (!Read(mem, ptr, &w)) return;
    ptr += 4;
    if ((w >> 16) == 0x8000) {
      uint32_t v = 0;
      switch (w & 0xF) {
        case 1: return;
        case 2: have_prev = false; break;
        case 0: Read(mem, ptr, &v); ptr += 4; e[5] = (e[5] & 0xFFFF0000u) | (v & 0xFFFF); break;
        case 5: Read(mem, ptr, &v); ptr += 4; e[5] = (e[5] & 0xFFFF) | ((v & 0xFFFF) << 16); break;
        case 3: Read(mem, ptr, &e[2]); ptr += 4; break;
        case 4: Read(mem, ptr, &e[3]); Read(mem, ptr + 4, &e[2]); ptr += 8; break;
        case 6: Read(mem, ptr, &v); ptr += 4; for (int i = 0; i < 16; ++i) Read(mem, v + 4 * i, &pal[i]); break;
        case 7: e[2] = pal[(w >> 8) & 0xF]; e[3] = pal[(w >> 12) & 0xF]; break;
        case 8: cf0 += Sext((w & 0xFFFF) >> 4, 12); break;
        case 9: Read(mem, ptr, &v); ptr += 4; cf0 = int32_t(v); break;
        default: break;
      }
      continue;
    }
    if (have_prev) {
      lfsr = (lfsr & 1) ? lfsr >> 1 : (lfsr >> 1) ^ 0xA3000000u;
      int32_t p[4];
      Segment(e, m, w, prev, lfsr, cf0, p);
      int32_t t = S32(int64_t(int32_t(e[12])) + cf0);
      t = t <= 0 ? 0 : std::min(t, 0xFFFF);
      // strength: w5 >> 2 relative to the measured lines (all 4FFF0002 so far), less the jitter share (1 - t)
      const float strength = float((e[5] >> 2) & 0x3FFFFFFF) / float(0x13FFC000) * (1.f - float(t) / 65536.f);
      Seg sg{float(p[0]) / 65536.f, float(p[1]) / 65536.f, float(p[2]) / 65536.f, float(p[3]) / 65536.f,
             Colour(e[2], strength), Colour(e[3], strength)};
      if(clocks.preciseWeb){
        double current[2]={double(Sext(w,16)),double(Sext(w>>16,16))},previous[2]={double(Sext(prev,16)),double(Sext(prev>>16,16))},precise[4];
        if(clocks.precisionPoint){clocks.precisionPoint(clocks.precisionData,currentAddress,w,current[0],current[1]);clocks.precisionPoint(clocks.precisionData,previousAddress,prev,previous[0],previous[1]);}
        PreciseSegment(e,m,current,previous,lfsr,cf0,precise,preciseScale?scales:nullptr);
        sg.x0=float(precise[0]);sg.y0=float(precise[1]);sg.x1=float(precise[2]);sg.y1=float(precise[3]);
      }
      if(clocks.precisionTransform){double x,y;if(clocks.precisionTransform(clocks.precisionData,entryAddress,e[0],x,y)){
        float dx=float(x-Sext(e[0]>>16,16)),dy=float(y-Sext(e[0],16));sg.x0+=dx;sg.x1+=dx;sg.y0+=dy;sg.y1+=dy;
        for(float* value:{&sg.x0,&sg.x1,&sg.y0,&sg.y1})if(*value<-32768||*value>=32768)*value-=std::floor((*value+32768)/65536)*65536;
      }}
      sg.r = std::max(float(e[5] & 0xFF) - 0.06f, 0.5f);
      segs->push_back(sg);
    }
    prev = w;previousAddress=currentAddress;
    have_prev = true;
  }
}

// --- 0010000D glowing number (806AF1E8; tools/t3k_textfit.py) ---

// Bilinear sample of a 16 x 16 YCrCbZ texture at (u, v) texels (texel i at coordinate i, wrapping); bytes as floats.
void Bilinear16(const uint32_t* tex, float u, float v, float out[4]) {
  const float fu = std::floor(u), fv = std::floor(v);
  const int iu = int(fu), iv = int(fv);
  const float a = u - fu, b = v - fv;
  const uint32_t t00 = tex[(iv & 15) * 16 + (iu & 15)], t10 = tex[(iv & 15) * 16 + ((iu + 1) & 15)];
  const uint32_t t01 = tex[((iv + 1) & 15) * 16 + (iu & 15)], t11 = tex[((iv + 1) & 15) * 16 + ((iu + 1) & 15)];
  for (int k = 0; k < 4; ++k) {
    const int sh = 24 - 8 * k;
    const float c00 = float((t00 >> sh) & 255), c10 = float((t10 >> sh) & 255);
    const float c01 = float((t01 >> sh) & 255), c11 = float((t11 >> sh) & 255);
    const float top = c00 + (c10 - c00) * a, bot = c01 + (c11 - c01) * a;
    out[k] = top + (bot - top) * b;
  }
}

// Sum of n per-pixel steps that start at a and grow by d each step, each taken >> 8 (16.16 units); n continuous.
float Steps(float n, int32_t a, int32_t d) { return (n * float(a) + 0.5f * n * (n - 1.f) * float(d)) / 256.f; }

void Text(const uint32_t* e, const Mem& mem, Picture* out, bool gpu) {
  uint32_t number = 0, blkw[20] = {}, tex2[256] = {};
  if (!Read(mem, 0x800DC240u, &number)) return;
  const int32_t w2 = int32_t(e[2]), w3 = int32_t(e[3]);
  if (w2 <= 0 || w3 <= 0) return;
  for (int k = 0; k < 20; ++k) Read(mem, e[12] + 4 * uint32_t(k), &blkw[k]);
  for (int k = 0; k < 256; ++k) Read(mem, e[11] + 4 * uint32_t(k), &tex2[k]);
  // the block lands at local $DF0: DF0 glyph u0 v0, tex2 U0 V0; E00 glyph du dv d2u d2v a pixel; E10 tex2 the same;
  // E20 glyph a row; E30 tex2 a row. E00 / E24 are replaced by 1 / w2, 1 / w3 (the module's reciprocal 806AF8D8
  // comes out a hair low: 2.0 -> 0x7FFF a pixel, not 0x8000)
  int32_t blk[20];
  for (int k = 0; k < 20; ++k) blk[k] = int32_t(blkw[k]);
  blk[4] = int32_t(((int64_t(1) << 40) / w2) - 1);
  blk[13] = int32_t(((int64_t(1) << 40) / w3) - 1);
  std::vector<int> digits;
  {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%u", number);
    for (const char* c = buf; *c; ++c) digits.push_back(*c - '0');
  }
  const int cellw = int((int64_t(16) * w2) >> 16), cellh = int((int64_t(16) * w3) >> 16);
  if (cellw <= 0 || cellh <= 0) return;
  int x = Sext(e[0] >> 16, 16), y = Sext(e[0], 16);
  if (e[14] == 1) {   // 806AF45A: centred (13 units a digit, 5 for a 1; half a cell high)
    int units = 0;
    for (int d : digits) units += d == 1 ? 5 : 13;
    x -= int((int64_t(w2) * units) >> 17);
    y -= w3 >> 13;
  }
  const float w4 = float(int32_t(e[4])), w5 = float(int32_t(e[5]));
  const float zf = float(e[6]) / float(1 << 23);   // opacity per unit of glyph Z (Z << 24, x w6 >> 16 >> 1, 2.30)
  // per digit the tex2 origin is stepped DB0 pixels along x (the advance before it); the stepped origin and steps are
  // written back, so later digits step again (806AF520) - kept
  int32_t U0 = blk[2], V0 = blk[3], e10[4] = {blk[8], blk[9], blk[10], blk[11]};
  int db0 = 0;
  // image resolution: 2 samples a game pixel for small cells (the content is smooth: a glyph texel spans 2+ pixels)
  const int S = std::max(cellw, cellh) > 64 ? 1 : 2;
  for (int d : digits) {
    for (int k = 0; k < db0; ++k) {
      U0 += e10[0] >> 8;
      V0 += e10[1] >> 8;
      e10[0] += e10[2];
      e10[1] += e10[3];
    }
    uint32_t glyph[256] = {};
    for (int k = 0; k < 256; ++k) Read(mem, e[10] + uint32_t(d) * 1024u + 4 * uint32_t(k), &glyph[k]);
    Glyph g;
    g.x = float(x);
    g.y = float(y);
    g.w = float(cellw);
    g.h = float(cellh);
    g.pw = cellw * S;
    g.ph = cellh * S;
    if(gpu&&g_number&&g_ycc){
      g.gpuNumber=true;
      for(unsigned k=0;k<20;++k)g.numberParams[k]=float(blk[k]);
      g.numberParams[2]=float(U0);g.numberParams[3]=float(V0);
      for(unsigned k=0;k<4;++k)g.numberParams[8+k]=float(e10[k]);
      g.numberParams[20]=float(cellw);g.numberParams[21]=float(cellh);g.numberParams[22]=w4;g.numberParams[23]=w5;g.numberParams[24]=zf;g.numberParams[25]=float(g.pw);g.numberParams[26]=float(g.ph);
      for(unsigned k=0;k<256;++k){unsigned y=k/16,x=k%16;g.numberTexture[y*32+x]=glyph[k];g.numberTexture[y*32+x+16]=tex2[k];}
    }else{
    g.rgba.resize(size_t(g.pw) * size_t(g.ph));
    for (int q = 0; q < g.ph; ++q) {
      // the module's pixel j uses parameter j; an image pixel's centre is (q + 1/2) / S game pixels in
      const float j = std::clamp((float(q) + 0.5f) / float(S) - 0.5f, 0.f, float(cellh - 1));
      const float gu_row = float(blk[0]) + Steps(j, blk[12], blk[14]);
      const float gv_row = float(blk[1]) + Steps(j, blk[13], blk[15]);
      const float tu_row = float(U0) + Steps(j, blk[16], blk[18]);
      const float tv_row = float(V0) + Steps(j, blk[17], blk[19]);
      for (int p = 0; p < g.pw; ++p) {
        const float i = std::clamp((float(p) + 0.5f) / float(S) - 0.5f, 0.f, float(cellw - 1));
        float gs[4], ts[4];
        Bilinear16(glyph, (gu_row + Steps(i, blk[4], blk[6])) / 65536.f, (gv_row + Steps(i, blk[5], blk[7])) / 65536.f,
                   gs);
        // the glyph's signed Cr / Cb (byte - 128, << 22) x w4 / w5 >> 20 move the texture lookup
        const float tu = tu_row + Steps(i, e10[0], e10[2]) + w4 * (gs[1] - 128.f) * 4.f;
        const float tv = tv_row + Steps(i, e10[1], e10[3]) + w5 * (gs[2] - 128.f) * 4.f;
        Bilinear16(tex2, tu / 65536.f, tv / 65536.f, ts);
        const float a = std::clamp(gs[3] * zf, 0.f, 1.f);
        auto byte = [](float v) { return uint32_t(std::clamp(v, 0.f, 255.f) + 0.5f); };
        const SDL_FColor c = Colour(byte(ts[0]) << 24 | byte(ts[1]) << 16 | byte(ts[2]) << 8 | 0xFF, 1.f);
        g.rgba[size_t(q) * size_t(g.pw) + size_t(p)] = uint32_t(a * 255.f + 0.5f) << 24 |
            uint32_t(c.r * a * 255.f + 0.5f) << 16 | uint32_t(c.g * a * 255.f + 0.5f) << 8 |
            uint32_t(c.b * a * 255.f + 0.5f);
      }
    }
    }
    out->glyphs.push_back(std::move(g));
    const int adv = int(((int64_t(d == 1 ? 0x40000 : 0xC0000) * w2) >> 16) >> 16);
    x += adv;
    db0 += adv;
  }
}

// --- 00310309 particles (806A8990 mode 3; tools/t3k_partfit.py) ---

void Particles(const uint32_t* e, const Mem& mem, Picture* out) {
  uint32_t cam[3] = {};
  for (int k = 0; k < 3; ++k) Read(mem, 0x800DAD80u + 4 * uint32_t(k), &cam[k]);
  const double camy = double(int32_t(cam[0])), camx = double(int32_t(cam[1]));
  uint32_t a = e[12];
  for (int n = 0; n < 4096; ++n, a += 16) {
    uint32_t w[4];
    if (!Read(mem, a, &w[0]) || w[0] == 0x80000000u) break;
    for (int k = 1; k < 4; ++k) Read(mem, a + 4 * uint32_t(k), &w[k]);
    const double d = double(std::max<uint32_t>(w[1] & 0x7FFFFFFFu, 0x400000u));
    const double kk = 0.75 * 17592186044416.0 / (d + 8388608.0);   // 0.75 x 2^44 / (depth + $800000)
    const double X = double(Sext(w[0] >> 16, 16)) * 65536.0 + camx * 8.0;
    const double Y = double(Sext(w[0], 16)) * 65536.0 + camy * 8.0;
    const uint32_t hi = w[3] >> 16, lo = w[3] & 0xFFFF;
    const double size = std::min(double(lo) * kk / 524288.0, 30.0);
    const double I = hi ? double(hi) * 65536.0 * kk / 524288.0 / 1073741824.0 : double(0x3FFF0000) / 1073741824.0;
    if (size <= 0.0) continue;
    Blob b;
    // the disc routine evaluates pixel column p at p (our pixel centres: +1/2) and row q at q + 1 (-1/2): measured
    // with --nativecheck + T3K_CHECK_MASK, the y optimum is exactly one pixel above the corner reading
    b.x = float(X * kk / 524288.0 / 65536.0 + 180.5 + 0.5);
    b.y = float(Y * kk / 524288.0 / 65536.0 + 120.5 - 0.5);
    b.r = float(size / 2.0);
    b.colour = Colour(w[2] | 0xFF, 1.f);
    b.colour.a = std::clamp(float(I), 0.f, 1.f);
    out->blobs.push_back(b);
  }
}

// --- 00310209 dot images (806A8990 mode 2; tools/t3k_dotfit.py) ---

int32_t KOfDepth(int64_t depth) {   // 806A9566 + $C000: 0.75 x 2^44 / (depth + $800000), as 16.16-ish fixed point
  return int32_t(int64_t(0xC000) * int64_t(17592186044416.0 / double(depth + 0x800000)) >> 16);
}

// --- 00310108 with w4 bit 0: the jump burst (806A5FB0 mode 1, 806A630A; tools/t3k_jumpburstfit.py) ---
// 96 particles from fixed LFSR seeds (the same set every picture), flying out along z as w3 grows; colour a mix of
// $D2921000 and mem[$8010B154] by the low 16 bits of z << 7; projected and drawn as the 00310309 particles.

void EmitModule8Point(const uint32_t* e,const Blob& blob,Picture* out){
  if((e[7]&127)==2){
    // Row routine 20300D02 blends a whole square by (1 - I); selectors
    // zero and one both resolve to the radial routine at 20300C64.
    Op op{Op::kBlendRect};op.x=std::floor(blob.x-.5f-blob.r);op.y=std::floor(blob.y+.5f-blob.r);
    op.w=op.h=blob.r*2;op.colour=blob.colour;op.colour.a=1-blob.colour.a;out->ops.push_back(op);
  }else out->blobs.push_back(blob);
}

void JumpBurst(const uint32_t* e, const Mem& mem, Picture* out) {
  if (!(e[4] & 1)) return;
  uint32_t cam[3]{}, target=0;
  for(int k=0;k<3;++k) Read(mem,0x800DAD80u+4u*k,&cam[k]);
  Read(mem,0x8010B154,&target);
  uint32_t sx=0x23956329,sy=0x49352850,sz=0x0946354F;
  auto step=[](uint32_t x){return (x>>1)^((x&1)?0:0xA3000000u);};
  for(int i=0;i<96;++i){
    sx=step(sx);sy=step(sy);sz=step(sz);
    const int32_t xseed=Sext(sx,24),yseed=Sext(sy,24);
    sx=step(sx);
    const int32_t z=int32_t((sz-(e[3]<<1))&0x1FFFFFFu);
    const int32_t dx=Mul(z,xseed,26),dy=Mul(z,yseed,26);
    const int32_t radius=Mul(z,int32_t(sx&15),25);
    const int32_t depth=S32(int64_t(int32_t(e[2]))+int32_t(cam[2])+z);
    // Original reciprocal refinement, including non-positive/overflowed
    // denominators. Mode 1 still emits its minimum-size point when k is zero.
    uint32_t denominator=uint32_t(depth)+0x800000,kword=0;
    if(int32_t(denominator)>0){
      unsigned bits=0;for(uint32_t value=denominator;value;value>>=1)++bits;
      uint32_t normalized=bits>=10?denominator>>(bits-10):denominator<<(10-bits),resource=0,seed=0;
      Read(mem,0x800B1740,&resource);Read(mem,resource+((normalized-512)&~3U),&seed);
      int32_t error=S32(int64_t(0x40000000)-Mul(int32_t(seed),int32_t(denominator),int(bits)));
      int32_t refined=Mul(error,int32_t(seed),29);
      error=S32(int64_t(0x40000000)-Mul(refined,int32_t(denominator),int(bits)));
      uint32_t reciprocal=uint32_t(Mul(refined,error,29));
      uint32_t scaled=bits>=15?reciprocal>>(bits-15):reciprocal<<(15-bits);
      kword=uint32_t(Mul(0xC000,int32_t(scaled),16));
    }
    const int32_t k=int32_t(kword);
    const int32_t x=S32(int64_t(int32_t(e[0]))+dx),y=S32(int64_t(int32_t(e[1]))+dy);
    const int32_t px=S32(int64_t(Mul(k,S32(int64_t(x>>1)+int64_t(int32_t(cam[1]))*8),19))+0xB48000);
    const int32_t py=S32(int64_t(Mul(k,S32(int64_t(y>>1)+int64_t(int32_t(cam[0]))*8),19))+0x788000);
    const int32_t size=std::clamp(Mul(k,radius,19),2,24);
    uint32_t colour=0xFF;
    for(int channel=0;channel<3;++channel){
      const int shift=24-channel*8;
      int32_t a=int32_t(((0xD2921000u>>shift)&255)<<22),b=int32_t(((target>>shift)&255)<<22);
      if(channel){a-=0x20000000;b-=0x20000000;}
      const int32_t delta=S32(int64_t(b)-a);
      // MUL_P ru uses the unsigned 14-bit UV fraction, not a signed scalar.
      const int32_t fraction=int32_t((uint32_t(z)<<5)&0x3FFF);
      const int32_t product=S32(int64_t(fraction)*(delta>>16)*4);
      const int32_t mixed=S32(int64_t(uint32_t(a)&0xFFFF0000u)+(uint32_t(product)&0xFFFF0000u));
      const int byte=channel?std::clamp((mixed>>22)+128,0,255):std::clamp(mixed>>22,0,255);
      colour|=uint32_t(byte)<<shift;
    }
    Blob blob;blob.x=float(double(px)/65536.0+0.5);blob.y=float(double(py)/65536.0-0.5);
    blob.r=float(size)/2;blob.colour=Colour(colour,1);blob.colour.a=float(0x3F000000)/1073741824.f;
    EmitModule8Point(e,blob,out);
  }
}

void Dots(const uint32_t* e, const Mem& mem, Picture* out) {
  const uint32_t w13 = e[13];
  uint32_t lst = (w13 & 0x80) ? e[12] : 0;
  int copies = int(w13 & 0x7F);
  if (!copies) copies = 1;
  int64_t depth = int32_t(e[4]);
  uint32_t angle = e[1];
  const uint32_t cols = e[8] >> 16, rows = e[8] & 0xFFFF;
  if (cols * rows > 4096) return;
  const float size = float(e[9]);
  for (int n = copies; n > 0; --n) {
    const int32_t k = KOfDepth(depth);
    uint32_t img = e[12];
    if (lst) {
      Read(mem, lst, &img);
      lst += 4;
    }
    const int32_t grow = int32_t(int64_t(n) * int32_t(e[11]));
    const int32_t s16 = Mul(k, -int32_t(e[2]), 19) + grow, s17 = Mul(k, -int32_t(e[3]), 19) + grow;
    const int32_t cx = Mul(k, Sext(e[0] >> 16, 16) >> 1, 19) + 0xB4, cy = Mul(k, Sext(e[0], 16) >> 1, 19) + 0x78;
    const int32_t r2 = Mul(grow, Sext(e[14] >> 16, 16), 16), r3 = Mul(grow, Sext(e[14], 16), 16);
    const int32_t r10 = -int32_t((int32_t(cols >> 1) << 16) + r2), r11 = -int32_t((int32_t(rows >> 1) << 16) + r3);
    int32_t c, s;
    SinCos(angle & 0xFFFF, &c, &s);
    uint32_t a = img;
    for (uint32_t row = rows; row > 0; --row)
      for (uint32_t col = cols; col > 0; --col, a += 4) {
        uint32_t p = 0;
        if (!Read(mem, a, &p) || (p & 0xFF) == 0) continue;
        const int32_t I = Mul(int32_t((p & 0xFF) << 22), int32_t(e[6]), 16);
        const int32_t x = Mul(s16, r10 + int32_t(col << 16), 16), y = Mul(s17, r11 + int32_t(row << 16), 16);
        const int64_t xr = int64_t(Mul(x, c, 30)) + Mul(y, S32(-int64_t(s)), 30) + (int64_t(cx) << 16);
        const int64_t yr = int64_t(Mul(x, s, 30)) + Mul(y, c, 30) + (int64_t(cy) << 16);
        Blob b;   // the call passes the top-left; the module's pixel p is its corner p (+1/2 for pixel centres)
        b.x = float(double(xr) / 65536.0) + size / 2.f + 0.5f;
        b.y = float(double(yr) / 65536.0) + size / 2.f - 0.5f;   // (rows: as Particles)
        b.r = size / 2.f;
        b.colour = Colour(p | 0xFF, 1.f);
        b.colour.a = std::clamp(float(I) / 1073741824.f, 0.f, 1.f);
        out->blobs.push_back(b);
      }
    angle += uint32_t(int32_t(angle) >> 16);
    depth -= int32_t(e[5]);
    if (depth <= 0) break;
  }
}

// --- 0031020E disc shapes (806AFA80 mode 2; tools/t3k_flowerfit.py) ---

// The disc template (806AFA80 mode 2 / 806A6E40 mode 0): grid = false: 0031020E at w0; true: one 00310010 grid point
// (px, py, pz): start angle + (w1 << 8), last rotation (px + py) >> 12, the point added before the perspective, depth
// from pz, size cap 30 px, centred at (192, 120).
void FlowerAt(const uint32_t* e, const Mem& mem, Picture* out, bool grid, int32_t px, int32_t py, int32_t pz) {
  uint32_t hdr[4] = {}, pal[16] = {};
  for (int k = 0; k < 4; ++k) Read(mem, e[10] + 4 * uint32_t(k), &hdr[k]);
  for (int k = 0; k < 16; ++k) Read(mem, hdr[0] + 4 * uint32_t(k), &pal[k]);
  const int32_t r16 = int32_t(hdr[1]);
  const uint32_t r17 = hdr[2];
  const int32_t m0 = int32_t(r17 >> 16), n0 = int32_t(r17 & 0xFFFF);
  const int32_t step_b = m0 ? (1 << 16) / m0 : 0, step_a = n0 ? (1 << 16) / n0 : 0;
  const int32_t w1 = int32_t(e[1]);
  int32_t c3, s3;
  SinCos(grid ? uint32_t((px + py) >> 12) & 0xFFFF : (e[14] + 0x2000) & 0xFFFF, &c3, &s3);
  for (int ri = 0; ri < 64; ++ri) {
    uint32_t rec = 0;
    if (!Read(mem, e[10] + 16 + 4 * uint32_t(ri), &rec)) break;
    const int32_t size_bits = int32_t((rec >> 8) & 0x7F);
    if (!size_bits) break;
    const int32_t r29 = size_bits << 16;
    int32_t a = Mul(int32_t(((rec >> 24) & 0xFF) << 8), r16, 16) + (grid ? w1 << 8 : 0);
    const int32_t r28 = Mul(Mul(Sext(rec >> 16, 8) << 10, w1, 16), int32_t(e[12]), 15);
    const int32_t inten = Mul(int32_t(((rec >> 4) & 15) << 13), S32(int64_t(0x100 - w1) << 21), 16);
    const SDL_FColor colour = Colour(pal[rec & 15] | 0xFF, 1.f);
    int32_t b = 0, n = n0, m = m0;
    for (int guard = 0; guard < 4096; ++guard) {
      int32_t c, s, c2, s2;
      SinCos(uint32_t(a) & 0xFFFF, &c, &s);
      int64_t x = int64_t(Mul(r28, c, 14)) + Mul(r28, S32(-int64_t(s)), 14);
      int64_t y = int64_t(Mul(r28, s, 14)) + Mul(r28, c, 14);
      SinCos(uint32_t(b + (w1 << 6)) & 0xFFFF, &c2, &s2);
      const int32_t depth = Mul(S32(x), s2, 30);
      x = Mul(c2, S32(x), 30);
      int64_t xr = int64_t(Mul(S32(x), c3, 30)) + Mul(S32(y), S32(-int64_t(s3)), 30);
      int64_t yr = int64_t(Mul(S32(x), s3, 30)) + Mul(S32(y), c3, 30);
      if (grid) xr += px, yr += py;
      const double d = double(std::max<int64_t>(int64_t(grid ? pz : int32_t(e[9])) + (int64_t(depth) << 2), 0xA0000));
      const int32_t k = int32_t(17592186044416.0 / d);   // 2^(28 + 16) / d
      const double ox = grid ? 192.0 : double(Sext(e[0] >> 16, 16)), oy = grid ? 120.0 : double(Sext(e[0], 16));
      const double cxp = double(Mul(k, S32(xr), 19)) / 65536.0 + ox;
      const double cyp = double(Mul(k, S32(yr), 19)) / 65536.0 + oy;
      const double size = double(std::min(Mul(r29, k, 19) + 0x10000, grid ? 0x1E0000 : 0x80000)) / 65536.0;
      Blob bl;   // (the position is the disc's centre; columns / rows as the other disc routines)
      bl.x = float(cxp + 0.5);
      bl.y = float(cyp - 0.5);
      bl.r = float(size / 2.0);
      bl.colour = colour;
      bl.colour.a = std::clamp(float(inten) / 1073741824.f, 0.f, 1.f);
      out->blobs.push_back(bl);
      a += step_a;
      if (--n > 0) continue;
      b += step_b;
      if (--m > 0) continue;
      break;
    }
  }
}

void Flower(const uint32_t* e, const Mem& mem, Picture* out) { FlowerAt(e, mem, out, false, 0, 0, 0); }

// --- 00310010 disc grid (806A6E40 mode 0; tools/t3k_gridfit.py: the emulator's calls exactly) ---

void Grid(const uint32_t* e, const Mem& mem, Picture* out) {
  uint32_t rows[32] = {};
  for (int k = 0; k < 32; ++k) Read(mem, e[11] + 4 * uint32_t(k), &rows[k]);
  const int32_t w2 = int32_t(e[2]), w3 = int32_t(e[3]), w4 = int32_t(e[4]), w5 = int32_t(e[5]);
  const int32_t d0 = w2 >> 18, d1 = Sext(e[2], 16), d2 = S32(int64_t(w4) << 3);   // point step growth
  const int32_t e0 = w3 >> 18, e1 = Sext(e[3], 16), e2 = S32(int64_t(w5) << 3);   // row step growth
  const int32_t sx0 = S32(int64_t(int32_t(e[13]) >> 16) << 8);                    // point step x at a row start
  auto row = [&](uint32_t word, int32_t x, int32_t y, int32_t z) {
    int32_t r12 = sx0, r13 = 0, r14 = 0;
    uint32_t r15 = (word >> 15) | (word << 17);
    const int32_t x0 = x, y0 = y, z0 = z;
    const uint32_t r15_0 = r15;
    for (int i = 0; i < 16; ++i) {
      if (r15 & 1) FlowerAt(e, mem, out, true, x, y, z);
      r15 = (r15 << 1) | (r15 >> 31);
      x += r12; r12 += d0;
      y += r13; r13 += d1;
      z += r14; r14 += d2;
    }
    x = x0; y = y0; z = z0; r15 = r15_0;
    r12 = -sx0; r13 = 0; r14 = 0;
    for (int i = 0; i < 16; ++i) {
      r14 -= d2; r12 -= d0; r13 -= d1;
      x += r12; y += r13; z += r14;
      r15 = (r15 >> 1) | (r15 << 31);
      if (r15 & 1) FlowerAt(e, mem, out, true, x, y, z);
    }
  };
  int32_t p[3] = {0, 0, int32_t(e[9])};
  int32_t r20 = 0, r21 = S32(int64_t(Sext(e[13], 16)) << 8), r22 = 0;
  const int32_t q[3] = {p[0], p[1], p[2]}, q20 = r20, q21 = r21, q22 = r22;
  for (int i = 0; i < 16; ++i) {
    row(rows[16 + i], p[0], p[1], p[2]);
    p[2] += r22; r22 += e2;
    p[0] += r20; r20 += e0;
    p[1] += r21; r21 += e1;
  }
  p[0] = q[0]; p[1] = q[1]; p[2] = q[2];
  r20 = -q20; r21 = -q21; r22 = -q22;
  for (int i = 0; i < 16; ++i) {
    r20 -= e0; r21 -= e1; r22 -= e2;
    p[0] += r20; p[1] += r21; p[2] += r22;
    row(rows[15 - i], p[0], p[1], p[2]);
  }
}

// --- 00500005 ring (806A7DC0; tools/t3k_ringfit.py) ---

void Ring(const uint32_t* e, const Mem& mem, Picture* out, bool gpu) {
  uint32_t tex[128] = {};
  for (int k = 0; k < 128; ++k) Read(mem, e[12] + 4 * uint32_t(k), &tex[k]);
  const int cx = Sext(e[0] >> 16, 16), cy = Sext(e[0], 16);
  const int64_t n = e[1] >> 16;
  const int64_t rx = (int64_t(int32_t(e[4]) >> 8) * n) >> 16, ry = (int64_t((e[4] & 0xFFFF) << 8) * n) >> 16;
  const double t = double(e[1] & 0xFFFF) / 65536.0;
  if (rx <= 0 || ry <= 0 || t <= 0.0 || rx > 200 || ry > 200) return;
  const double kx = double((int64_t(1) << 28) / (rx * rx)) / 268435456.0, ky = double((int64_t(1) << 28) / (ry * ry)) / 268435456.0;
  const double r7 = double(int64_t(e[5] >> 2) / int64_t(e[1] & 0xFFFF) - 1);   // the peak: (w5 >> 2) / t (w5 unsigned)
  const double uh = double(int32_t(e[13]) >> 16), vl = double(Sext(e[13], 16));
  const double u0 = double(int32_t(e[2])), v0 = double(int32_t(e[3]));
  // a texel lookup as the module's: 16 x 8, wrapping, texel i at coordinate i
  auto texel = [&](double u, double v, float o[3]) {
    const double fu = std::floor(u), fv = std::floor(v);
    const int iu = int(fu), iv = int(fv);
    const double a = u - fu, b = v - fv;
    const uint32_t t00 = tex[(iv & 7) * 16 + (iu & 15)], t10 = tex[(iv & 7) * 16 + ((iu + 1) & 15)];
    const uint32_t t01 = tex[((iv + 1) & 7) * 16 + (iu & 15)], t11 = tex[((iv + 1) & 7) * 16 + ((iu + 1) & 15)];
    for (int k = 0; k < 3; ++k) {
      const int sh = 24 - 8 * k;
      const double c00 = double((t00 >> sh) & 255), c10 = double((t10 >> sh) & 255);
      const double c01 = double((t01 >> sh) & 255), c11 = double((t11 >> sh) & 255);
      const double top = c00 + (c10 - c00) * a, bot = c01 + (c11 - c01) * a;
      o[k] = float(top + (bot - top) * b);
    }
  };
  constexpr int S = 2;
  Glyph g;
  g.x = float(cx - rx - 2);
  g.y = float(cy - ry - 2);
  g.w = float(2 * rx + 4);
  g.h = float(2 * ry + 4);
  g.pw = int(g.w) * S;
  g.ph = int(g.h) * S;
  if(gpu&&g_ring&&g_ycc){
    g.gpuRing=true;
    const float params[16]={g.x,g.y,g.w,g.h,float(kx),float(ky),float(t),float(r7),float(uh),float(vl),float(u0),float(v0),float(cx),float(cy),0,0};
    std::copy(params,params+16,g.ringParams);std::copy(tex,tex+128,g.ringTexture);
    out->glyphs.push_back(std::move(g));return;
  }
  g.rgba.assign(size_t(g.pw) * size_t(g.ph), 0);
  for (int q = 0; q < g.ph; ++q) {
    // the module's row q' (pixel row) has row = q' - cy at its centre q' + 1/2; columns mirror about cx - 1/2:
    // col = -(|X - cx| + 1/2) for the pixel centre X (the computed left half, mirrored to the right)
    const double Y = double(g.y) + (double(q) + 0.5) / S;
    const double row = Y - 0.5 - double(cy);
    for (int p = 0; p < g.pw; ++p) {
      const double X = double(g.x) + (double(p) + 0.5) / S;
      const double col = -(std::fabs(X - double(cx)) + 0.5);
      const double d = col * col * kx + row * row * ky;
      if (d <= 1.0 - 2.0 * t) continue;
      const double ev = t - std::fabs(d - (1.0 - t));
      if (ev <= 0.0) continue;
      const double k = std::clamp(ev * 65536.0 * r7 / 1073741824.0, 0.0, 1.0);
      float T[3];
      texel(std::fabs(uh * col + u0) / 65536.0, std::fabs(vl * row + v0) / 65536.0, T);
      auto byte = [](float v) { return uint32_t(std::clamp(v, 0.f, 255.f) + 0.5f); };
      const SDL_FColor c = Colour(byte(T[0]) << 24 | byte(T[1]) << 16 | byte(T[2]) << 8 | 0xFF, 1.f);
      const float a = float(k);
      g.rgba[size_t(q) * size_t(g.pw) + size_t(p)] = uint32_t(a * 255.f + 0.5f) << 24 |
          uint32_t(c.r * a * 255.f + 0.5f) << 16 | uint32_t(c.g * a * 255.f + 0.5f) << 8 | uint32_t(c.b * a * 255.f + 0.5f);
    }
  }
  out->glyphs.push_back(std::move(g));
}

// --- 00310008 point rings (806A5FB0 mode 0; tools/t3k_ringptfit.py) ---

void RingPoints(const uint32_t* e, const Mem& mem, Picture* out) {
  uint32_t cols[2] = {};
  Read(mem, e[2], &cols[0]);
  Read(mem, e[2] + 4, &cols[1]);
  const int32_t w10 = int32_t(e[10]), w11 = int32_t(e[11]), n = int32_t(e[8]);
  if (w10 <= 0 || w11 <= 0 || n <= 0 || n > 4096 || w11 > 256) return;
  int64_t z = 0x3000000 - ((int64_t(e[1] & 0xFFFF) * w10) >> 16);
  const int32_t hi1 = int32_t(e[1]) >> 16;
  int32_t p1 = int32_t(e[4] & 0xFFFF) * hi1, p2 = int32_t(e[5] & 0xFFFF) * hi1;
  const int32_t step_copy = (1 << 16) / w11;
  // colour gradient: two texels, u in 16.16 wrapping over them
  float c0[3], c1[3];
  for (int k = 0; k < 3; ++k) {
    c0[k] = float((cols[0] >> (24 - 8 * k)) & 255);
    c1[k] = float((cols[1] >> (24 - 8 * k)) & 255);
  }
  for (int guard = 0; z > 0 && guard < 4096; ++guard) {
    int32_t c, s;
    SinCos(uint32_t(p2) & 0xFFFF, &c, &s);
    const int32_t span = Sext(e[9], 16) + Mul(int32_t(e[5] >> 16), c, 30);   // w5 hi unsigned (the module; >= $8000 at the level select)
    SinCos(uint32_t(p1) & 0xFFFF, &c, &s);
    const int32_t a0 = int32_t(e[6]) + Mul(int32_t(e[4] >> 16), c, 30);
    p1 -= int32_t(e[4] & 0xFFFF);
    p2 -= int32_t(e[5] & 0xFFFF);
    const int32_t k = int32_t(17592186044416.0 / double(z));   // 2^44 / z
    const int32_t step = Mul((1 << 28) / n, span, 28), ustep = ((1 << 28) / n) >> 11;
    const int32_t I = (std::min(Mul(int32_t(e[7] >> 16), k, 20), 0x3FFF) << 16) | int32_t(e[7] & 0xFFFF);
    const int32_t radius = Mul(int32_t(e[9]), k, 20);
    int32_t size = Mul(12, k, 21);
    if (size >= 1) {
      size = std::min(size, 30);
      for (int32_t copy = 0; copy < w11; ++copy) {
        int32_t a = a0 + copy * step_copy - (span >> 1);
        uint32_t u = 0;
        for (int32_t i = 0; i < n; ++i, u += uint32_t(ustep), a += step) {
          SinCos(uint32_t(a) & 0xFFFF, &c, &s);
          const double x = double(Mul(radius, c, 30)) / 65536.0 + 180.5;
          const double y = double(Mul(radius, s, 30)) / 65536.0 + 120.5;
          const float fu = float(u & 0xFFFF) / 65536.f;
          const float* ca = ((u >> 16) & 1) ? c1 : c0;
          const float* cb = ((u >> 16) & 1) ? c0 : c1;
          auto byte = [](float v) { return uint32_t(std::clamp(v, 0.f, 255.f) + 0.5f); };
          const uint32_t ycc = byte(ca[0] + (cb[0] - ca[0]) * fu) << 24 | byte(ca[1] + (cb[1] - ca[1]) * fu) << 16 |
                               byte(ca[2] + (cb[2] - ca[2]) * fu) << 8 | 0xFF;
          Blob b;   // the centre (the call passes centre - size / 2); disc rows / columns as the other disc routines
          b.x = float(x + 0.5);
          b.y = float(y - 0.5);
          b.r = float(size) / 2.f;
          b.colour = Colour(ycc, 1.f);
          b.colour.a = std::clamp(float(I) / 1073741824.f, 0.f, 1.f);
          EmitModule8Point(e,b,out);
        }
      }
    }
    z -= w10;
  }
}

// --- 00310208 powerup burst (806A5FB0 mode 2, handler 806A6182; tools/t3k_burstfit.py: the calls exactly) ---
// Five rings of 16 discs around a 3D point (w0 / w1 projected by the pre-pass, w2 depth): ring n = size 16 - 3n,
// radius $4000 + $3000 n, intensity 1, 4, 7, 10, 63 / 64, colour word n of the list at $8010B150; disc angles
// A = w4 + w6 (+ $4000 a disc), B = w5 (+ $800 a disc). Row routine 0 (w7 & $7F) = the plain disc blend.

bool BurstDrawable(const uint32_t* e) { return (e[7] & 0x7F) <= 2; }

void Burst(const uint32_t* e, const Mem& mem, Picture* out) {
  uint32_t cam[3] = {}, cols[5] = {};
  for (int k = 0; k < 3; ++k) Read(mem, 0x800DAD80u + 4 * uint32_t(k), &cam[k]);
  for (int k = 0; k < 5; ++k) Read(mem, 0x8010B150u + 4 * uint32_t(k), &cols[k]);
  const int32_t camy = int32_t(cam[0]), camx = int32_t(cam[1]);
  const int32_t z0 = S32(int64_t(int32_t(e[2])) + int32_t(cam[2]));
  if (z0 <= 0) return;
  int32_t I = 0x01000000, n = 16, rad = 0x4000;
  for (int ring = 0; n > 1; ++ring) {
    int32_t A = S32(int64_t(int32_t(e[4])) + int32_t(e[6])), B = int32_t(e[5]);
    for (int i = 0; i < 16; ++i, A = S32(int64_t(A) + 0x4000), B = S32(int64_t(B) + 0x800)) {
      int32_t cA, sA, cB, sB;
      SinCos(uint32_t(A) & 0xFFFF, &cA, &sA);
      SinCos(uint32_t(B) & 0xFFFF, &cB, &sB);
      const int32_t a = cA >> 14;
      const int32_t dx = Mul(rad, Mul(cB, a, 30), 16), dy = Mul(rad, sA >> 14, 16), dz = Mul(rad, Mul(a, sB, 30), 16);
      const int32_t x = S32(int64_t(int32_t(e[0])) + (int64_t(dx) << 5));
      const int32_t y = S32(int64_t(int32_t(e[1])) + (int64_t(dy) << 5));
      const int32_t d = S32(int64_t(S32(int64_t(z0) + (int64_t(dz) << 6))) + 0x800000);
      if (d <= 0) continue;
      const int32_t k = KOfDepth(int64_t(d) - 0x800000);
      const int32_t X = Mul(k, S32(int64_t(x >> 1) + int64_t(camx) * 8), 19) + 0xB48000;
      const int32_t Y = Mul(k, S32(int64_t(y >> 1) + int64_t(camy) * 8), 19) + 0x788000;
      const int32_t size = std::clamp(Mul(k, n, 19), 2, 24);
      Blob b;   // the call passes centre - size / 2; disc rows / columns as the other disc routines
      b.x = float(double(X) / 65536.0 + 0.5);
      b.y = float(double(Y) / 65536.0 - 0.5);
      b.r = float(size) / 2.f;
      b.colour = Colour(cols[ring] | 0xFF, 1.f);
      b.colour.a = std::clamp(float(I) / 1073741824.f, 0.f, 1.f);
      EmitModule8Point(e,b,out);
    }
    I += 0x03000000;
    n -= 3;
    rad += 0x3000;
    if (n == 4) I = 0x3F000000;
  }
}

// --- 0031000E title dot field (806AFA80 mode 0, handler 806AFECA; tools/t3k_fieldfit.py) ---

void TitleField(const uint32_t* e, const Mem& mem, Picture* out) {
  uint32_t cam_y = 0, cam_x = 0;
  Read(mem, 0x800DC108, &cam_y);
  Read(mem, 0x800DC10C, &cam_x);
  const int32_t cx0 = -2 * int32_t(cam_x), cy0 = -2 * int32_t(cam_y);
  const int32_t hi1 = int32_t(e[1]) >> 16;
  int32_t p1 = int32_t(e[4] & 0xFFFF) * hi1, p2 = int32_t(e[5] & 0xFFFF) * hi1;
  const int32_t w9 = int32_t(e[9]), w10 = int32_t(e[10]), w11 = int32_t(e[11]);
  const int32_t w12 = int32_t(e[12]), w13 = int32_t(e[13]), w14 = int32_t(e[14]), w3 = int32_t(e[3]);
  if (w10 <= 0 || w11 <= 0 || w11 > 4096 || w13 <= 0 || w13 > 256) return;
  int64_t z = 0x3000000 - ((int64_t(e[1] & 0xFFFF) * w10) >> 16);
  const int32_t step_copy = (1 << 16) / w11;
  for (int guard = 0; z > 0 && guard < 4096; ++guard) {
    int32_t c, s;
    SinCos(uint32_t(p2) & 0xFFFF, &c, &s);
    const int32_t b = Mul(c, int32_t(e[5] >> 16), 30);   // w5 / w4 hi unsigned
    SinCos(uint32_t(p1) & 0xFFFF, &c, &s);
    const int32_t a = Mul(c, int32_t(e[4] >> 16), 30);
    p1 -= int32_t(e[4] & 0xFFFF);
    p2 -= int32_t(e[5] & 0xFFFF);
    if (((int64_t(a) * a) >> 16) + ((int64_t(b) * b) >> 16) >= 0x2000) {
      // the colour pixel: Y $1FFF0000, Cr a << 12, Cb b << 12 (2.30) -> bytes
      const uint32_t cr = uint32_t(((S32(int64_t(a) << 14) >> 2) >> 22) + 128) & 0xFF;
      const uint32_t cb = uint32_t(((S32(int64_t(b) << 14) >> 2) >> 22) + 128) & 0xFF;
      const uint32_t ycc = (0x1FFF0000u >> 22) << 24 | cr << 16 | cb << 8 | 0xFF;
      const int32_t rx = Mul(w12, S32(int64_t(b) << 7), 16), ry = Mul(w12, S32(int64_t(a) << 7), 16);
      int32_t sc = 0x20000;
      int64_t zz = z;
      for (int32_t ring = 0; ring < w13; ++ring) {
        if (zz > 0) {
          const int32_t k = int32_t(17592186044416.0 / double(zz));   // 2^44 / z
          const int32_t I = (std::min(Mul(Mul(sc, int32_t(e[7] >> 16), 16), k, 20), 0x3FFF) << 16) |
                            int32_t(e[7] & 0xFFFF);
          int32_t size = Mul(k, 0xC0000, 21);
          if (size >= 0x10000) {
            size = std::min(size, 0x1E0000);
            const int32_t RX = Mul(k, rx, 20), RY = Mul(k, ry, 20);
            const int32_t ccx = Mul(k, cx0, 20), ccy = Mul(k, cy0, 20);
            const int32_t step = Mul(w9, step_copy, 16);
            int32_t ang = int32_t(e[6]);
            for (int32_t i = 0; i < w11; ++i, ang += step) {
              SinCos(uint32_t(ang) & 0xFFFF, &c, &s);
              const int32_t x = S32(int64_t(Mul(RX, c, 30)) - Mul(RY, s, 30));
              const int32_t y = S32(int64_t(Mul(RX, s, 30)) + Mul(RY, c, 30));
              for (int32_t xx : {x, -x}) {
                Blob bl;   // centre (the call passes centre - size / 2); disc rows / columns as the other disc routines
                bl.x = float(double(int64_t(xx) + ccx + 0xB48000) / 65536.0 + 0.5);
                bl.y = float(double(int64_t(y) + ccy + 0x788000) / 65536.0 - 0.5);
                bl.r = float(size >> 16) / 2.f;
                bl.colour = Colour(ycc, 1.f);
                bl.colour.a = std::clamp(float(I) / 1073741824.f, 0.f, 1.f);
                out->blobs.push_back(bl);
              }
            }
          }
        }
        sc = Mul(sc, w3, 16);
        zz = (zz - w14) & 0x7FFFFFF;
      }
    }
    z -= w10;
  }
}

// --- 00310409 title wave (806A8990 mode 4, handler 806A8C48; tools/t3k_wavefit.py): a fixed shape at w0 ---

void TitleWave(const uint32_t* e, const Mem& mem, Picture* out) {
  uint32_t cols[2] = {};
  Read(mem, 0x8010DB60, &cols[0]);
  Read(mem, 0x8010DB64, &cols[1]);
  const int64_t x0 = int64_t(e[0] >> 16) << 16, y0 = int64_t(e[0] & 0xFFFF) << 16;
  const int32_t w8 = int32_t(e[8]);
  const int32_t I = S32(int64_t(0x10000 - w8) << 12);
  auto step = [](uint32_t v) { return (v & 1) ? v >> 1 : (v >> 1) ^ 0xA3000000u; };
  auto add = [&](int64_t tlx, int64_t tly, uint32_t colour) {
    Blob b;   // top-left + size / 2 = centre; disc rows / columns as the other disc routines
    b.x = float(double(tlx) / 65536.0 + 4.0 + 0.5);
    b.y = float(double(tly) / 65536.0 + 4.0 - 0.5);
    b.r = 4.f;
    b.colour = Colour(colour | 0xFF, 1.f);
    b.colour.a = std::clamp(float(I) / 1073741824.f, 0.f, 1.f);
    out->blobs.push_back(b);
  };
  uint32_t r8 = 0x23956329u, r9 = 0x49352850u;
  int64_t xs = 0;
  int32_t a = 0x4000;
  for (int i = 0; i < 96; ++i, a += 0x400) {
    int32_t c, s;
    SinCos(uint32_t(a) & 0xFFFF, &c, &s);
    int64_t x, y;
    if (a >= 0x18000) {        // the closing arc
      x = xs - Mul(0x120000, s, 30);
      y = 0x90000 + Mul(0x120000, c, 30);
    } else if (a >= 0x8000) {  // the wave
      x = xs;
      y = Mul(0x90000, c, 30);
      xs += 0x400 << 6;
    } else {                   // the opening arc
      x = -int64_t(Mul(0x120000, s, 30));
      y = 0x90000 + Mul(0x120000, c, 30);
    }
    r8 = step(r8);
    r9 = step(r9);
    const int32_t jx = Mul(int32_t(r8) >> 10, w8, 16), jy = Mul(int32_t(r9) >> 10, w8, 16);
    add(S32(x0 + x + jx), S32(y0 + y + jy), cols[0]);
  }
  a = w8 >> 2;
  const int32_t R = 0x80000 + (w8 << 6);
  for (int i = 0; i < 32; ++i, a += 0x800) {   // the circle
    int32_t c, s;
    SinCos(uint32_t(a) & 0xFFFF, &c, &s);
    add(S32(x0 + 0x350000 + Mul(R, s, 30)), S32(y0 - 0x40000 + Mul(R, c, 30)), cols[1]);
  }
}

// --- 00100200 textured plane (806A3108 mode 2; tools/t3k_planefit.py) ---

void PlaneOp(const uint32_t* e, const Mem& mem, Picture* out) {
  Plane pl;
  for (int k = 0; k < 256; ++k) Read(mem, e[9] + 4 * uint32_t(k), &pl.texture[k]);
  const double x0 = Sext(e[0] >> 16, 16), y0 = Sext(e[0], 16), w = double(e[1] >> 16), h = double(e[1] & 0xFFFF);
  if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return;
  auto hi = [](uint32_t v) { return double(Sext(v >> 16, 16)); };
  auto lo = [](uint32_t v) { return double(Sext(v, 16)); };
  const double U0 = double(int32_t(e[2])), V0 = double(int32_t(e[3])), DU = double(int32_t(e[4])), DV = double(int32_t(e[5]));
  const double RU = double(int32_t(e[7])), RV = double(int32_t(e[10]));
  // (u, v) for the pixel at column i, row j (continuous): rows plain sums, columns in 64-pixel chunks, each starting one
  // step further on (the module's pipelined loop)
  auto uv = [&](double i, double j, float* u, float* v) {
    double uu = U0 + j * RU + hi(e[11]) * j * (j - 1) / 2, vv = V0 + j * RV + lo(e[11]) * j * (j - 1) / 2;
    double du = DU + j * hi(e[12]), dv = DV + j * lo(e[12]);
    const double a = hi(e[6]), b = lo(e[6]);
    const double k = std::floor(std::max(i, 0.0) / 64.0);
    // k whole chunks: each adds 65 steps (65 du + a 64 65 / 2) and grows the step by 64 a
    uu += 65.0 * (k * du + a * 64.0 * k * (k - 1) / 2) + k * a * 64.0 * 65.0 / 2;
    vv += 65.0 * (k * dv + b * 64.0 * k * (k - 1) / 2) + k * b * 64.0 * 65.0 / 2;
    du += 64.0 * k * a;
    dv += 64.0 * k * b;
    const double p = i - 64.0 * k;
    *u = float((uu + p * du + a * p * (p - 1) / 2) / 65536.0);
    *v = float((vv + p * dv + b * p * (p - 1) / 2) / 65536.0);
  };
  constexpr double kCell = 8.0;
  // a plane across the whole width carries on past the 4:3 edges (wide windows), one screen width each side
  const double ext = (x0 <= 0 && x0 + w >= 360) ? 360.0 : 0.0;
  const double xa = -ext, xb = w + ext;
  pl.cols = std::max(1, int(std::ceil((xb - xa) / kCell)));
  pl.rows = std::max(1, int(std::ceil(h / kCell)));
  pl.grid.resize(size_t(pl.cols + 1) * size_t(pl.rows + 1));
  for (int r = 0; r <= pl.rows; ++r)
    for (int c = 0; c <= pl.cols; ++c) {
      const double X = std::min(xa + c * kCell, xb), Y = std::min(r * kCell, h);
      Web::Vertex& vx = pl.grid[size_t(r) * size_t(pl.cols + 1) + size_t(c)];
      vx.x = float(x0 + X);
      vx.y = float(y0 + Y);
      uv(X - 0.5, Y - 0.5, &vx.u, &vx.v);   // the module's pixel i is sampled at its centre, i + 1/2 on screen
    }
  out->planes.push_back(std::move(pl));
}

}  // namespace

uint32_t YccToXrgb(uint32_t ycc) {
  const SDL_FColor c = RgbColour(ycc | 0xFF, 1.f);
  return uint32_t(c.r * 255.f + 0.5f) << 16 | uint32_t(c.g * 255.f + 0.5f) << 8 | uint32_t(c.b * 255.f + 0.5f);
}

void BuildFrom(const std::vector<uint32_t>& entries, const Mem& mem, uint32_t flip, Picture* out, const PictureBuildClocks& clocks) {
  struct { uint32_t flip; } cap{flip};
  out->flip = flip;
  out->segs.clear();
  out->items.clear();
  out->ops.clear();
  out->webs.clear();
  out->glyphs.clear();
  out->blobs.clear();
  out->planes.clear();
  out->complete = true;
  out->first_unknown = 0;
  // tests (T3K_NATIVE_FORCE=1 with NUANCE_ONLYTYPES=<types>): draw only the types the emulator draws too
  static const char* const only = std::getenv("T3K_NATIVE_FORCE") ? std::getenv("NUANCE_ONLYTYPES") : nullptr;
  for (size_t i = 0; i + 17 <= entries.size(); i += 17) {
    const uint32_t* e = &entries[i + 1];
    if((e[15]&255)==255)break;
    if((e[15]&255)==254)continue;
    if (only) {
      char t[12];
      std::snprintf(t, sizeof t, "%08X", e[15]);
      if (!std::strstr(only, t)) continue;
    }
    // T3K_NATIVE_UNPORT=<type>[,..] (tests): treat these types as not ported (the picture goes to the emulator)
    static const char* unport = std::getenv("T3K_NATIVE_UNPORT");
    if (unport) {
      char t[12];
      std::snprintf(t, sizeof t, "%08X", e[15]);
      if (std::strstr(unport, t)) {
        if (out->complete) {
          out->first_unknown = e[15];
          std::memcpy(out->unknown_entry, e, sizeof out->unknown_entry);
        }
        out->complete = false;
        continue;
      }
    }
    const size_t ops_before = out->ops.size();
    switch (e[15]) {
      case 0x00710001: {   // vector shape
        const uint32_t first = uint32_t(out->segs.size());
        Shape(e, mem, &out->segs,clocks,entries[i]);
        const uint32_t n = uint32_t(out->segs.size()) - first;
        out->items.push_back({e[7], float(Sext(e[0] >> 16, 16)), float(Sext(e[0], 16)), first, n});
        Op op{Op::kSegs};
        op.first = first;
        op.count = n;
        if (n) out->ops.push_back(op);
        break;
      }
      case 0x00000300: {   // fill
        Op op{Op::kFill};
        op.x = float(e[0] >> 16);
        op.y = float(e[0] & 0xFFFF);
        op.w = float(e[1] >> 16);
        op.h = float(e[1] & 0xFFFF);
        op.colour = Colour(e[2] | 0xFF, 1.f);
        op.colour.a = 1.f;
        out->ops.push_back(op);
        break;
      }
      case 0x00300004: {   // zoom feedback
        Op op{Op::kZoom};
        op.cx = float(e[0] >> 16);
        op.cy = float(e[0] & 0xFFFF);
        op.zx = float(e[4]) / 65536.f;
        op.zy = float(e[5]) / 65536.f;
        // signed: $FF70 is -0.79 deg, not 359.21 - the same for a whole picture's step, but the window splits a step
        // across presents (angle x step), and 359.21 x 1/4 turned the feedback ~90 deg a present: the kaleidoscope
        // on "Crosseyed and Painless" (owner 2026-11-17)
        op.angle = float(int16_t(e[6] & 0xFFFF)) * 6.2831853f / 65536.f;
        op.fade = float(e[7] >> 16) / 16384.f;
        if (std::getenv("T3K_ZOOM_LOG"))
          std::fprintf(stderr, "zoom flip %u: centre %.0f,%.0f zoom %.4f %.4f angle %.2f deg fade %.3f w6 %08X w1 %08X w2 %08X "
                       "w3 %08X w8 %08X w9 %08X w10 %08X w11 %08X\n", cap.flip, op.cx, op.cy, op.zx, op.zy,
                       op.angle * 57.29578f, op.fade, e[6], e[1], e[2], e[3], e[8], e[9], e[10], e[11]);
        out->ops.push_back(op);
        break;
      }
      case 0x0011000B: {   // web surface
        Web web;
        for (int k = 0; k < 256; ++k) Read(mem, e[0] + 4 * uint32_t(k), &web.texture[k]);
        uint32_t job = 0;
        Read(mem, 0x20100000u, &job);
        if (clocks.overrideWebTextureTick) job = clocks.webTextureTick;
        // Preparation emits one surface quad per geometric segment. The
        // logical lane count can be larger on open webs. Unwritten records
        // retain old geometry, so a zero sentinel is not a valid live bound.
        uint32_t segments=32,p=0,definition=0;bool bounded=false;
        auto shape_count=[&](uint32_t at){
          uint32_t word;if(!at||!Read(mem,at+4,&word))return false;
          uint32_t count=word>>16;if(!count||count>16)return false;
          segments=count;return true;
        };
        if(Read(mem,0x20100020u,&p)&&p&&Read(mem,p+0x900u,&definition))bounded=shape_count(definition);
        // The native host has no drawing-MPE environment in this memory view.
        // This is the canonical active definition also used by level select
        // and sessions; full-memory emulator views can use the same fallback.
        if(!bounded&&Read(mem,0x800AF700u,&definition))bounded=shape_count(definition);
        for (uint32_t a = 0x800BACF0u; web.quads.size() < 4 * size_t(segments); a += 32) {
          uint32_t w[8];
          bool any = false;
          for (int k = 0; k < 8; ++k) {
            w[k] = 0;
            Read(mem, a + 4 * uint32_t(k), &w[k]);
            any |= w[k] != 0;
          }
          if (!any&&!bounded) break;
          for (int k = 0; k < 4; ++k)
            // position: y hi / x lo, signed half pixels about the centre (806AD7BE); u hi / v lo in 1/512 texel
            web.quads.push_back({180.f + float(Sext(w[2 * k], 16)) / 2.f, 120.f + float(Sext(w[2 * k] >> 16, 16)) / 2.f,
                                 float(w[2 * k + 1] >> 16) / 512.f, float(w[2 * k + 1] & 0xFFFF) / 512.f});
        }
        if (!web.quads.empty()) {
          Op op{Op::kWeb};
          op.first = uint32_t(out->webs.size());
          op.fade = float(job) / 2.f;   // (kWeb: the v scroll in texels)
          if (std::getenv("T3K_NATIVE_DEBUG"))
            std::fprintf(stderr, "web flip %u job %X quads %zu  e %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X "
                         "%08X %08X %08X %08X\n", cap.flip, job, web.quads.size() / 4, e[0], e[1], e[2], e[3], e[4], e[5],
                         e[6], e[7], e[8], e[9], e[10], e[11], e[12], e[13], e[14]);
          web.gpuMapping=clocks.gpuWeb;out->webs.push_back(std::move(web));
          out->ops.push_back(op);
        }
        break;
      }
      case 0x00500005: {   // ring (drawn as an image, as the glowing number's digits)
        Op op{Op::kText};
        op.first = uint32_t(out->glyphs.size());
        Ring(e, mem, out, clocks.gpuRings);
        op.count = uint32_t(out->glyphs.size()) - op.first;
        if (op.count) out->ops.push_back(op);
        break;
      }
      case 0x00100200: {   // textured plane
        Op op{Op::kPlane};
        op.first = uint32_t(out->planes.size());
        PlaneOp(e, mem, out);
        if (out->planes.size() > op.first) out->ops.push_back(op);
        break;
      }
      case 0x0000000F: {   // ripple feedback (boot / title logo screens; tools/t3k_ripplefit.py)
        Op op{Op::kRipple};
        auto s32 = [](uint32_t v) { return int32_t(v); };
        // the module's A = (w10 << 17) + x w4, B = w11 + y w5 + A: only bits 16-21 matter, so keep them mod 2^22
        op.x = float(((e[10] << 17) & 0x3FFFFF) / 65536.0);
        op.y = float(s32(e[4]) / 65536.0);
        op.w = float((e[11] & 0x3FFFFF) / 65536.0);
        op.h = float(s32(e[5]) / 65536.0);
        op.colour = Colour(e[2] | 0xFF, 1.f);
        op.fade = float(s32(e[7]) / 1073741824.0);
        out->ops.push_back(op);
        break;
      }
      case 0x00310409: {   // title wave
        Op op{Op::kBlobs};
        op.first = uint32_t(out->blobs.size());
        TitleWave(e, mem, out);
        op.count = uint32_t(out->blobs.size()) - op.first;
        if (op.count) out->ops.push_back(op);
        break;
      }
      case 0x0031000E: {   // title dot field
        Op op{Op::kBlobs};
        op.first = uint32_t(out->blobs.size());
        TitleField(e, mem, out);
        op.count = uint32_t(out->blobs.size()) - op.first;
        if (op.count) out->ops.push_back(op);
        break;
      }
      case 0x00310008: {   // point rings
        Op op{Op::kBlobs};
        op.first = uint32_t(out->blobs.size());
        RingPoints(e, mem, out);
        op.count = uint32_t(out->blobs.size()) - op.first;
        if (op.count) out->ops.push_back(op);
        break;
      }
      case 0x00310010: {   // disc grid
        Op op{Op::kBlobs};
        op.first = uint32_t(out->blobs.size());
        Grid(e, mem, out);
        op.count = uint32_t(out->blobs.size()) - op.first;
        if (op.count) out->ops.push_back(op);
        break;
      }
      case 0x0031020E: {   // disc shapes
        Op op{Op::kBlobs};
        op.first = uint32_t(out->blobs.size());
        Flower(e, mem, out);
        op.count = uint32_t(out->blobs.size()) - op.first;
        if (op.count) out->ops.push_back(op);
        break;
      }
      case 0x00310208:   // powerup burst
        if (BurstDrawable(e)) {
          Op op{Op::kBlobs};
          op.first = uint32_t(out->blobs.size());
          Burst(e, mem, out);
          op.count = uint32_t(out->blobs.size()) - op.first;
          if (op.count) out->ops.push_back(op);
        } else if (out->complete) {   // another row routine (not seen with this mode)
          out->first_unknown = e[15];
          std::memcpy(out->unknown_entry, e, sizeof out->unknown_entry);
          out->complete = false;
        }
        break;
      case 0x00310209: {   // dot images
        Op op{Op::kBlobs};
        op.first = uint32_t(out->blobs.size());
        Dots(e, mem, out);
        op.count = uint32_t(out->blobs.size()) - op.first;
        if (op.count) out->ops.push_back(op);
        break;
      }
      case 0x00310309: {   // particles
        Op op{Op::kBlobs};
        op.first = uint32_t(out->blobs.size());
        Particles(e, mem, out);
        op.count = uint32_t(out->blobs.size()) - op.first;
        if (op.count) out->ops.push_back(op);
        break;
      }
      case 0x0010000D: {   // glowing number
        Op op{Op::kText};
        op.first = uint32_t(out->glyphs.size());
        Text(e, mem, out, clocks.gpuNumbers);
        op.count = uint32_t(out->glyphs.size()) - op.first;
        if (op.count) out->ops.push_back(op);
        break;
      }
      case 0x000000FF: case 0x000000FE: case 0x003102FE:   // end of list; entries that draw nothing
        break;
      case 0x007100FE:   // (a disabled vector entry: the low byte FE makes the renderers pass over it)
        break;
      case 0x00310108:   // 806A5FB0 mode 1 (806A630A): the jump burst, drawn only when w4 bit 0 is set
        if (e[4] & 1) {
          if(!BurstDrawable(e)){out->first_unknown=e[15];out->complete=false;break;}
          Op op{Op::kBlobs};
          op.first = uint32_t(out->blobs.size());
          JumpBurst(e, mem, out);
          op.count = uint32_t(out->blobs.size()) - op.first;
          if (op.count) out->ops.push_back(op);
        }
        break;
      default: {
        // T3K_NATIVE_FORCE=1 (tests): types not done yet are left out instead of making the picture emulated
        static const bool force = std::getenv("T3K_NATIVE_FORCE") != nullptr;
        if (force) break;
        if (out->complete) {
          out->first_unknown = e[15];
          std::memcpy(out->unknown_entry, e, sizeof out->unknown_entry);
        }
        out->complete = false;
        break;
      }
    }
    for (size_t k = ops_before; k < out->ops.size(); ++k) out->ops[k].type = e[15];
  }
}

void BuildPicture(const nuon::Capture& cap, Picture* out, const PictureBuildClocks& clocks) {
  Mem mem;
  mem.map = &cap.mem;
  BuildFrom(cap.entries, mem, cap.flip, out, clocks);
}

bool BuildPictureLive(int mpe, uint32_t flip, Picture* out, const PictureBuildClocks& clocks) {
  Mem mem;
  mem.sys = nuon::Memory(0, &mem.sys_n);
  mem.main = nuon::Memory(1, &mem.main_n);
  mem.local = nuon::Memory(2 + std::clamp(mpe, 0, 3), &mem.local_n);
  if (!mem.sys || !mem.main) return false;
  // the display list: 64-byte entries from $8004E040 to the end-of-list entry (as the renderers walk it)
  std::vector<uint32_t> entries;
  for (uint32_t a = 0x8004E040u; entries.size() < 17 * 1024; a += 64) {
    uint32_t e[16];
    for (int k = 0; k < 16; ++k)
      if (!Read(mem, a + 4 * uint32_t(k), &e[k])) return false;
    entries.push_back(a);
    entries.insert(entries.end(), e, e + 16);
    if ((e[15]&255) == 0xFFu) break;
  }
  BuildFrom(entries, mem, flip, out, clocks);
  return true;
}

Renderer::~Renderer() {
  if(temporalState_)SDL_DestroyGPURenderState(temporalState_);
  if(paintLayer_)SDL_DestroyTexture(paintLayer_);
  if(simulationBase_)SDL_DestroyTexture(simulationBase_);
  if(ring_tex_)SDL_DestroyTexture(ring_tex_);
  for(auto& atlas:numberAtlases_)if(atlas.texture)SDL_DestroyTexture(atlas.texture);
  for (SDL_Texture* t : tex_)
    if (t) SDL_DestroyTexture(t);
  if (web_tex_) SDL_DestroyTexture(web_tex_);
  for (SDL_Texture* t : glyph_tex_)
    if (t) SDL_DestroyTexture(t);
  if (disc_tex_) SDL_DestroyTexture(disc_tex_);
}

void Renderer::Redraw(SDL_Renderer* ren, const SDL_FRect& dst, const SDL_FRect* view) {
  if (tex_[cur_]) Pass(ren, tex_[cur_], g_ycc ? g_out : nullptr, view ? view : &dst);
}

void Renderer::Seed(SDL_Renderer* ren, SDL_Texture* picture, const SDL_FRect& dst, const SDL_FRect* view) {
  if (!picture) return;
  const SDL_FRect vw = view ? *view : dst;
  Resize(ren, vw);
  SDL_Texture* keep = SDL_GetRenderTarget(ren);
  SDL_SetRenderTarget(ren, tex_[cur_]);
  ClearBlack(ren);
  const SDL_FRect game{dst.x - vw.x, dst.y - vw.y, dst.w, dst.h};   // (the emulated picture: the 4:3 part)
  Pass(ren, picture, g_ycc ? g_in : nullptr, &game);
  SDL_SetRenderTarget(ren, keep);
}

bool Renderer::CaptureFeedback(SDL_Renderer* ren,FeedbackState& out,std::string& error) {
  FeedbackState next;
  if(!tex_[cur_]){out=std::move(next);error.clear();return true;}
  auto* keep=SDL_GetRenderTarget(ren);
  if(!SDL_SetRenderTarget(ren,tex_[cur_])){error=SDL_GetError();return false;}
  auto* surface=SDL_RenderReadPixels(ren,nullptr);
  SDL_SetRenderTarget(ren,keep);
  if(!surface){error=SDL_GetError();return false;}
  next.width=surface->w;next.height=surface->h;next.format=surface->format;
  size_t row=size_t(next.width)*SDL_BYTESPERPIXEL(surface->format);
  next.pixels.resize(row*next.height);
  for(int y=0;y<next.height;++y)std::memcpy(next.pixels.data()+y*row,static_cast<uint8_t*>(surface->pixels)+y*surface->pitch,row);
  SDL_DestroySurface(surface);out=std::move(next);error.clear();return true;
}
bool Renderer::RestoreFeedback(SDL_Renderer* ren,const FeedbackState& in,std::string& error) {
  if(in.width==0&&in.height==0&&in.pixels.empty()){
    for(auto*& t:tex_){if(t)SDL_DestroyTexture(t);t=nullptr;}w_=h_=0;cur_=0;glyph_flip_=0xFFFFFFFFu;simulationPhase_=simulationDivisions_=simulationBasePhase_=0;continuousSimulation_=echoSimulation_=echoNeedsPin_=integratedSimulation_=false;error.clear();return true;
  }
  if(in.width<=0||in.height<=0||in.width>8192||in.height>8192||
     (in.format!=SDL_PIXELFORMAT_RGBA64_FLOAT&&in.format!=SDL_PIXELFORMAT_XRGB8888)||
     uint64_t(in.width)*in.height*SDL_BYTESPERPIXEL(in.format)!=in.pixels.size()){
    error="Invalid native feedback image";return false;
  }
  Resize(ren,{0,0,float(in.width),float(in.height)});
  float width=0,height=0;SDL_GetTextureSize(tex_[0],&width,&height);
  auto actual=SDL_GetNumberProperty(SDL_GetTextureProperties(tex_[0]),SDL_PROP_TEXTURE_FORMAT_NUMBER,0);
  if(actual!=in.format){error="Native feedback texture precision changed";return false;}
  if(!SDL_UpdateTexture(tex_[0],nullptr,in.pixels.data(),in.width*SDL_BYTESPERPIXEL(in.format))){error=SDL_GetError();return false;}
  cur_=0;glyph_flip_=0xFFFFFFFFu;simulationPhase_=simulationDivisions_=simulationBasePhase_=0;continuousSimulation_=echoSimulation_=echoNeedsPin_=integratedSimulation_=false;error.clear();return true;
}

bool Renderer::DrawSimulationSample(SDL_Renderer* ren,const Picture& picture,const SDL_FRect& dst,
                                    unsigned substep,unsigned divisions,std::string& error,const SDL_FRect* view,unsigned numerator,FeedbackSampling feedback){
  bool active=simulationDivisions_!=0&&!continuousSimulation_&&!echoSimulation_&&!integratedSimulation_;
  if(!divisions||divisions>65536||!numerator||numerator>divisions||substep>=divisions||
     (active&&(substep<simulationPhase_||divisions!=simulationDivisions_))||
     ((continuousSimulation_||echoSimulation_||integratedSimulation_)&&(substep!=simulationPhase_||divisions!=simulationDivisions_))){error="Invalid simulation effect interval order";return false;}
  const SDL_FRect area=view?*view:dst;
  int width=std::max(1,int(area.w+.5f)),height=std::max(1,int(area.h+.5f));
  if(active&&(width!=w_||height!=h_)){error="Resize inside simulation effect interval";return false;}
  Resize(ren,area);
  if(!simulationBase_){
    SDL_PropertiesID props=SDL_CreateProperties();
    auto format=SDL_GetNumberProperty(SDL_GetTextureProperties(tex_[cur_]),SDL_PROP_TEXTURE_FORMAT_NUMBER,0);
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,format);
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,SDL_TEXTUREACCESS_TARGET);
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,w_);
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,h_);
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER,SDL_COLORSPACE_SRGB);
    simulationBase_=SDL_CreateTextureWithProperties(ren,props);SDL_DestroyProperties(props);
    if(!simulationBase_){error=SDL_GetError();return false;}
    SDL_SetTextureScaleMode(simulationBase_,SDL_SCALEMODE_LINEAR);
  }
  bool integrated=feedback==FeedbackSampling::timeIntegrated&&divisions>1&&!picture.ops.empty()&&
      (picture.ops.front().kind==Op::kZoom||picture.ops.front().kind==Op::kRipple);
  for(size_t i=1;integrated&&i<picture.ops.size();++i)
    if(picture.ops[i].kind==Op::kZoom||picture.ops[i].kind==Op::kRipple||picture.ops[i].kind==Op::kFill)integrated=false;
  if(integrated){
    if(!g_ycc||!g_shader[7]||!g_temporalSampler){error="Elapsed-time feedback GPU pipeline unavailable";return false;}
    if(!paintLayer_){
      auto props=SDL_CreateProperties();SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,float_tex_?SDL_PIXELFORMAT_RGBA64_FLOAT:SDL_PIXELFORMAT_RGBA32);
      SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,SDL_TEXTUREACCESS_TARGET);
      SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,w_);SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,h_);
      SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER,SDL_COLORSPACE_SRGB);
      paintLayer_=SDL_CreateTextureWithProperties(ren,props);SDL_DestroyProperties(props);
      if(!paintLayer_){error=SDL_GetError();return false;}
      SDL_GPUTextureSamplerBinding binding{static_cast<SDL_GPUTexture*>(SDL_GetPointerProperty(SDL_GetTextureProperties(paintLayer_),SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER,nullptr)),g_temporalSampler};
      if(!binding.texture||!binding.sampler){error="Temporal paint texture has no native GPU binding";return false;}
      SDL_GPURenderStateCreateInfo info{};info.fragment_shader=g_shader[7];info.num_sampler_bindings=1;info.sampler_bindings=&binding;
      temporalState_=SDL_CreateGPURenderState(ren,&info);if(!temporalState_){error=SDL_GetError();return false;}
    }
    // Paint into transparent premultiplied coverage. Overlaps are composed
    // before temporal scaling, rather than scaling finished-image differences
    // or multiplying every individual primitive's opacity independently.
    float elapsed=float(numerator)/divisions;auto feedbackOp=picture.ops.front();
    glyph_flip_=~picture.flip;drawingPaintLayer_=true;drawingSimulation_=false;
    Draw(ren,picture,picture.segs,dst,1,view,false);drawingPaintLayer_=false;
    Picture warp;warp.flip=picture.flip;auto transport=feedbackOp;
    transport.fade=transport.kind==Op::kZoom?1.f:0.f;warp.ops.push_back(transport);
    suppressPresentation_=true;Draw(ren,warp,warp.segs,dst,elapsed,view,false);suppressPresentation_=false;
    SDL_Texture* warped=tex_[cur_];auto* keep=SDL_GetRenderTarget(ren);SDL_SetRenderTarget(ren,tex_[cur_^1]);
    float retention=feedbackOp.kind==Op::kZoom?feedbackOp.fade:1.f-feedbackOp.fade;
    auto neutral=feedbackOp.kind==Op::kZoom?SDL_FColor{16.f/255,128.f/255,128.f/255,1}:feedbackOp.colour;
    const float u[8]={elapsed,retention,feedbackOp.kind==Op::kZoom&&retention>1.f?1.f:0.f,0,neutral.r,neutral.g,neutral.b,1};
    SDL_SetGPURenderStateFragmentUniforms(temporalState_,0,u,sizeof(u));Pass(ren,warped,temporalState_,nullptr);
    SDL_SetRenderTarget(ren,keep);cur_^=1;Redraw(ren,dst,view);
    continuousSimulation_=echoSimulation_=echoNeedsPin_=false;integratedSimulation_=true;
    simulationDivisions_=divisions;simulationPhase_=(substep+numerator)%divisions;simulationBasePhase_=0;
    error.clear();return true;
  }
  if(integratedSimulation_){active=false;integratedSimulation_=false;simulationDivisions_=simulationPhase_=simulationBasePhase_=0;}
  // Special feedback sampling is currently restricted to a single leading
  // brightening zoom followed by drawing. Preserve the reference path for
  // full fills, interleaved feedback and all unsupported operation orders.
  bool eligible=feedback!=FeedbackSampling::pinned&&divisions>1&&!picture.ops.empty()&&picture.ops.front().kind==Op::kZoom&&picture.ops.front().fade>1.f;
  for(size_t i=1;eligible&&i<picture.ops.size();++i)
    if(picture.ops[i].kind==Op::kZoom||picture.ops[i].kind==Op::kRipple||picture.ops[i].kind==Op::kFill)eligible=false;
  if(eligible&&feedback==FeedbackSampling::clocked){
    if(!echoSimulation_){
      std::swap(simulationBase_,tex_[cur_]);echoUv_={1,0,0,1,0,0};echoFade_=1;echoNeedsPin_=false;
    }else if(echoNeedsPin_){std::swap(simulationBase_,tex_[cur_]);echoNeedsPin_=false;}
    const auto& zoom=picture.ops.front();
    // Compose output-to-history maps without resampling the history at fine
    // cadence. This also handles moving centres and anisotropic zooms.
    auto advance=[&](double elapsed){
      double angle=-double(zoom.angle)*elapsed,ca=std::cos(angle),sa=std::sin(angle);
      double zx=std::pow(double(zoom.zx),elapsed),zy=std::pow(double(zoom.zy),elapsed);
      std::array<double,6> f{ca/zx,-sa/zx,sa/zy,ca/zy,0,0};
      f[4]=zoom.cx-f[0]*zoom.cx-f[1]*zoom.cy;f[5]=zoom.cy-f[2]*zoom.cx-f[3]*zoom.cy;
      auto a=echoUv_;
      echoUv_={a[0]*f[0]+a[1]*f[2],a[0]*f[1]+a[1]*f[3],
               a[2]*f[0]+a[3]*f[2],a[2]*f[1]+a[3]*f[3],
               a[0]*f[4]+a[1]*f[5]+a[4],a[2]*f[4]+a[3]*f[5]+a[5]};
      echoFade_*=std::pow(double(zoom.fade),elapsed);
    };
    auto draw=[&](bool commit){
      glyph_flip_=~picture.flip;drawingSimulation_=drawingEchoTransform_=true;
      Draw(ren,picture,picture.segs,dst,1.f,view,commit);
      drawingSimulation_=drawingEchoTransform_=false;
    };
    unsigned end=substep+numerator;
    if(end>=divisions){
      // Split the fine interval at the exact authored boundary. Alternating
      // 2/3-sample groups must not become alternating .8/1.2-tick echo gaps.
      advance(double(divisions-substep)/divisions);draw(true);
      echoUv_={1,0,0,1,0,0};echoFade_=1;
      if(end>divisions){
        std::swap(simulationBase_,tex_[cur_]);advance(double(end-divisions)/divisions);draw(false);
      }else echoNeedsPin_=true;
    }else{advance(double(numerator)/divisions);draw(false);}
    echoSimulation_=true;continuousSimulation_=false;
    simulationDivisions_=divisions;simulationPhase_=end%divisions;simulationBasePhase_=0;
    error.clear();return true;
  }
  if(eligible&&feedback==FeedbackSampling::continuous){
    echoSimulation_=echoNeedsPin_=false;
    if(!continuousSimulation_)std::swap(simulationBase_,tex_[cur_]);
    const float elapsed=float(numerator)/divisions;
    glyph_flip_=~picture.flip;drawingSimulation_=true;
    // Display the full-strength current geometry, but add only an elapsed-time
    // share to persistent history. This is source accumulation, not motion
    // interpolation between pictures. Two scratch targets avoid read/write
    // aliasing and keep float YCrCb history through both passes.
    Draw(ren,picture,picture.segs,dst,elapsed,view,false);
    SDL_Texture* visible=tex_[cur_];
    Picture background; background.flip=picture.flip;background.ops.push_back(picture.ops.front());
    Draw(ren,background,background.segs,dst,elapsed,view,false);
    auto* keep=SDL_GetRenderTarget(ren);SDL_SetRenderTarget(ren,tex_[cur_]);
    SDL_SetTextureBlendMode(visible,SDL_BLENDMODE_BLEND);SDL_SetTextureAlphaModFloat(visible,elapsed);
    SDL_RenderTexture(ren,visible,nullptr,nullptr);SDL_SetTextureAlphaModFloat(visible,1.f);
    SDL_SetRenderTarget(ren,keep);
    std::swap(simulationBase_,tex_[cur_]);cur_^=1;
    drawingSimulation_=false;continuousSimulation_=true;
    simulationDivisions_=divisions;simulationPhase_=(substep+numerator)%divisions;simulationBasePhase_=0;
    Redraw(ren,dst,view);error.clear();return true;
  }
  if(continuousSimulation_||echoSimulation_){active=false;continuousSimulation_=echoSimulation_=echoNeedsPin_=integratedSimulation_=false;simulationDivisions_=simulationPhase_=simulationBasePhase_=0;}
  // Pin the committed history by exchanging ownership, not copying pixels.
  // The other two targets remain scratch outputs throughout this interval.
  if(!active)std::swap(simulationBase_,tex_[cur_]);
  bool ends=substep+numerator>=divisions;
  // Fine states may share the legacy picture tick but contain new text data.
  glyph_flip_=~picture.flip;
  drawingSimulation_=true;
  Draw(ren,picture,picture.segs,dst,float(substep+numerator-simulationBasePhase_)/divisions,view,ends);
  drawingSimulation_=false;
  simulationDivisions_=ends?0:divisions;simulationPhase_=ends?0:substep+numerator;
  if(ends)simulationBasePhase_=(substep+numerator)%divisions;
  error.clear();return true;
}

void Renderer::Resize(SDL_Renderer* ren, const SDL_FRect& dst) {
  const int w = std::max(1, int(dst.w + 0.5f)), h = std::max(1, int(dst.h + 0.5f));
  if (w == w_ && h == h_ && tex_[0]) return;
  if(temporalState_){SDL_DestroyGPURenderState(temporalState_);temporalState_=nullptr;}
  if(paintLayer_){SDL_DestroyTexture(paintLayer_);paintLayer_=nullptr;}
  if(simulationBase_){SDL_DestroyTexture(simulationBase_);simulationBase_=nullptr;}
  simulationPhase_=simulationDivisions_=simulationBasePhase_=0;continuousSimulation_=echoSimulation_=echoNeedsPin_=integratedSimulation_=false;
  bool all_float = true;
  for (SDL_Texture*& t : tex_) {
    if (t) SDL_DestroyTexture(t);
    // float, so partial feedback steps between presents don't round (see ycc_fade.frag.hlsl); 8-bit if unsupported
    // (colourspace sRGB: SDL treats float textures as linear by default and would convert our YCrCb values)
    t = nullptr;
    if (!std::getenv("T3K_FEEDBACK_8BIT")) {
      const SDL_PropertiesID p = SDL_CreateProperties();
      SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA64_FLOAT);
      SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET);
      SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
      SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
      SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB);
      t = SDL_CreateTextureWithProperties(ren, p);
      SDL_DestroyProperties(p);
    }
    all_float = all_float && t != nullptr;
    if (!t) t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
  }
  float_tex_ = all_float;
  if (std::getenv("T3K_FEEDBACK_LOG")) std::fprintf(stderr, "feedback textures %dx%d %s\n", w, h, all_float ? "float" : "8-bit");
  w_ = w;
  h_ = h;
  SDL_Texture* keep = SDL_GetRenderTarget(ren);
  for (SDL_Texture* t : tex_) {
    SDL_SetRenderTarget(ren, t);
    ClearBlack(ren);
  }
  SDL_SetRenderTarget(ren, keep);
}

void Renderer::Draw(SDL_Renderer* ren, const Picture& pic, const std::vector<Seg>& segs, const SDL_FRect& dst,
                    float step, const SDL_FRect* view, bool step_ends) {
  const SDL_FRect vw = view ? *view : dst;
  Resize(ren, vw);
  const int w = w_, h = h_;
  SDL_Texture* prev = drawingSimulation_ ? simulationBase_ : tex_[cur_];
  SDL_Texture* next = tex_[cur_ ^ 1];
  SDL_Texture* keep = SDL_GetRenderTarget(ren);
  SDL_SetRenderTarget(ren, drawingPaintLayer_?paintLayer_:next);
  if(drawingPaintLayer_){SDL_SetRenderDrawColorFloat(ren,0,0,0,0);SDL_RenderClear(ren);}else ClearBlack(ren);
  // Game pixel (x, y) -> texture (x sx + ox, y sy + oy): the 360 x 240 picture fills dst, the texture covers the view
  // (wider: the game's world past the 4:3 edges). gx0..gx1 / gy0..gy1: the texture's extent in game pixels.
  const float sx = dst.w / 360.f, sy = dst.h / 240.f;
  const float ox = dst.x - vw.x, oy = dst.y - vw.y;
  const SDL_FRect full{ox, oy, dst.w, dst.h};   // (DrawVectors: where the 360 x 240 picture goes)
  const float gx0 = -ox / sx, gx1 = (float(w) - ox) / sx, gy0 = -oy / sy, gy1 = (float(h) - oy) / sy;
  std::vector<Seg> part;
  // A brightening zoom feedback (the warps: fade > 1) amplifies whatever the vectors leave in it every picture: the
  // original's soft, wide strokes grow into round glows, sharp thin ones into long spikes (level 3's warp became a
  // four-pointed star instead of a soft square; owner 2026-11-17, tools/t3k_zoomfit2.py: the zoom step itself is
  // exact). Such pictures keep the original line look; play (darkening trails) stays sharp.
  bool brightening = false;
  for (const Op& op : pic.ops) brightening |= op.kind == Op::kZoom && op.fade > 1.f;
  size_t paintIndex=0;
  for (const Op& op : pic.ops) {
    if(drawingPaintLayer_&&paintIndex++==0)continue;
    switch (op.kind) {
      case Op::kFill: case Op::kBlendRect: {
        const bool blend=op.kind==Op::kBlendRect;
        SDL_SetRenderDrawBlendMode(ren, blend?SDL_BLENDMODE_BLEND:SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColorFloat(ren, op.colour.r, op.colour.g, op.colour.b, blend?op.colour.a:1.f);
        float x0 = op.x, x1 = op.x + op.w, y0 = op.y, y1 = op.y + op.h;
        if (x0 <= 0.f && x1 >= 360.f) x0 = std::min(x0, gx0), x1 = std::max(x1, gx1);   // full width: to the edges
        if (y0 <= 0.f && y1 >= 240.f) y0 = std::min(y0, gy0), y1 = std::max(y1, gy1);
        const SDL_FRect r{x0 * sx + ox, y0 * sy + oy, (x1 - x0) * sx, (y1 - y0) * sy};
        SDL_RenderFillRect(ren, &r);
        break;
      }
      case Op::kZoom: {profiling::Scope profile(profiling::feedback);
        // Each output point p samples the previous picture at c + R(-a) (p - c) / z (game pixels); for an in-between
        // frame the step's share: z^step, a x step, fade^step.
        const float zx = std::pow(op.zx, step), zy = std::pow(op.zy, step), a = -op.angle * step;
        const float fade = drawingEchoTransform_?float(echoFade_):std::pow(std::max(op.fade, 0.f), step);
        const float ca = std::cos(a), sa = std::sin(a);
        auto uv = [&](float px, float py) {
          if(drawingEchoTransform_)return SDL_FPoint{float(((echoUv_[0]*px+echoUv_[1]*py+echoUv_[4])*sx+ox)/w),float(((echoUv_[2]*px+echoUv_[3]*py+echoUv_[5])*sy+oy)/h)};
          const float dx = px - op.cx, dy = py - op.cy;
          const float rx = (dx * ca - dy * sa) / zx, ry = (dx * sa + dy * ca) / zy;
          // (the module samples bilinear at the mapped point, as the texture does)
          return SDL_FPoint{((op.cx + rx) * sx + ox) / float(w), ((op.cy + ry) * sy + oy) / float(h)};
        };
        const float f1 = std::min(fade, 1.f), f2 = std::max(fade - 1.f, 0.f);
        auto quad = [&](float k, SDL_BlendMode mode) {
          const SDL_FColor c{k, k, k, 1.f};
          const SDL_Vertex v[4] = {{{0, 0}, c, uv(gx0, gy0)}, {{float(w), 0}, c, uv(gx1, gy0)},
                                   {{0, float(h)}, c, uv(gx0, gy1)}, {{float(w), float(h)}, c, uv(gx1, gy1)}};
          const int idx[6] = {0, 1, 2, 1, 3, 2};
          SDL_SetTextureBlendMode(prev, mode);
          SDL_RenderGeometry(ren, prev, v, 4, idx, 6);
        };
        if (g_ycc) {   // the module's fade: every component about its neutral value, clipped to a byte
          static const bool old_floor = std::getenv("T3K_FADE_FLOOR") != nullptr;
          const float mode = old_floor ? 3.f : step_ends ? 1.f : (float_tex_ ? 0.f : 2.f);
          const float u[4] = {fade, mode, 0, 0};
          SDL_SetGPURenderStateFragmentUniforms(g_fade, 0, u, sizeof(u));
          SDL_SetGPURenderState(ren, g_fade);
          quad(1.f, SDL_BLENDMODE_NONE);
          SDL_SetGPURenderState(ren, nullptr);
          break;
        }
        quad(f1, SDL_BLENDMODE_NONE);
        if (f2 > 0.f) quad(std::min(f2, 1.f), SDL_BLENDMODE_ADD);   // brightening (level start): add the excess
        break;
      }
      case Op::kRipple: {profiling::Scope profile(profiling::feedback);
        // Approximate the ripple's displacement generator over elapsed authored
        // time. History remains pinned; fine previews do not compound deposits.
        const SDL_FColor c{1, 1, 1, 1};
        const SDL_Vertex v[4] = {{{0, 0}, c, {0, 0}}, {{float(w), 0}, c, {1, 0}}, {{0, float(h)}, c, {0, 1}},
                                 {{float(w), float(h)}, c, {1, 1}}};
        const int idx[6] = {0, 1, 2, 1, 3, 2};
        SDL_SetTextureBlendMode(prev, SDL_BLENDMODE_NONE);
        if (g_ycc) {
          const float u[16] = {op.x, op.y, op.w, op.h, op.colour.r, op.colour.g, op.colour.b, op.fade,
                               step, 0, 0, 0, gx0, gy0, gx1 - gx0, gy1 - gy0};
          SDL_SetGPURenderStateFragmentUniforms(g_ripple, 0, u, sizeof(u));
          SDL_SetGPURenderState(ren, g_ripple);
          SDL_RenderGeometry(ren, prev, v, 4, idx, 6);
          SDL_SetGPURenderState(ren, nullptr);
        } else {   // no shaders: the previous picture faded, without the ripple
          const float k = step==1.f?1.f-op.fade:std::pow(std::max(0.f,1.f-op.fade),step);
          SDL_Vertex vv[4];
          for (int i = 0; i < 4; ++i) { vv[i] = v[i]; vv[i].color = {k, k, k, 1}; }
          SDL_RenderGeometry(ren, prev, vv, 4, idx, 6);
        }
        break;
      }
      case Op::kSegs: {
        part.assign(segs.begin() + op.first, segs.begin() + op.first + op.count);
        bool sharpen = g_sharp && !part.empty() && !brightening;
        if (sharpen) {   // only the small items: the web and the screen-wide streaks keep the original look
          // small (claw, enemies, HUD) or made of short strokes (lines of text); the web and the streaks have long ones
          float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, len = 0.f;
          for (const Seg& sg : part) {
            x0 = std::min({x0, sg.x0, sg.x1}); x1 = std::max({x1, sg.x0, sg.x1});
            y0 = std::min({y0, sg.y0, sg.y1}); y1 = std::max({y1, sg.y0, sg.y1});
            len += std::hypot(sg.x1 - sg.x0, sg.y1 - sg.y0);
          }
          sharpen = std::max(x1 - x0, y1 - y0) <= g_sharp_size || len / float(part.size()) <= 12.f;
        }
        if (!sharpen) {
          DrawVectors(ren, part, full, false);
          break;
        }
        if (g_glow > 0.f) {   // the glow: the original line, fainter
          std::vector<Seg> glow = part;
          for (Seg& sg : glow) { sg.c0.a *= g_glow; sg.c1.a *= g_glow; }
          DrawVectors(ren, glow, full, false);
        }
        // the core: thin, full strength (never under ~1 window pixel)
        const float min_r = 0.75f * 360.f / std::max(1.f, dst.w);
        for (Seg& sg : part) sg.r = std::max(sg.r * g_core, min_r);
        DrawVectors(ren, part, full, false);
        break;
      }
      case Op::kWeb: {profiling::Scope profile(profiling::web);
        const Web& web = pic.webs[op.first];
        if (!web_tex_) {
          web_tex_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 16, 16);
          SDL_SetTextureScaleMode(web_tex_, SDL_SCALEMODE_LINEAR);   // the module filters bilinearly
        }
        uint32_t px[256];
        for (int k = 0; k < 256; ++k) {
          const SDL_FColor c = Colour(web.texture[k] | 0xFF, 1.f);
          px[k] = uint32_t(c.r * 255.f + 0.5f) << 16 | uint32_t(c.g * 255.f + 0.5f) << 8 | uint32_t(c.b * 255.f + 0.5f);
        }
        if(!webPixelsValid_||std::memcmp(webPixels_.data(),px,sizeof(px))){
          profiling::upload(px,sizeof(px),profileWebHash_);SDL_UpdateTexture(web_tex_,nullptr,px,16*4);std::copy(px,px+256,webPixels_.begin());webPixelsValid_=true;
        }else if(profiling::enabled()&&profiling::collecting)++profiling::data.cachedUploads;
        bool direct=web.gpuMapping&&g_web&&g_ycc&&web.quads.size()<=64;
        // Two triangles cover convex lanes exactly. Keep the reference mesh for
        // folded/concave inputs, whose coverage needs separate treatment.
        for(size_t k=0;direct&&k+3<web.quads.size();k+=4){float sign=0;
          for(unsigned j=0;j<4;++j){auto a=web.quads[k+j],b=web.quads[k+(j+1)%4],c=web.quads[k+(j+2)%4];float cross=(b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x);if(std::abs(cross)<1e-5f)continue;if(sign&&cross*sign<0){direct=false;break;}sign=cross;}}
        if(direct){
          SDL_SetTextureBlendMode(web_tex_,SDL_BLENDMODE_BLEND);SDL_SetRenderTextureAddressMode(ren,SDL_TEXTURE_ADDRESS_CLAMP,SDL_TEXTURE_ADDRESS_CLAMP);SDL_SetGPURenderState(ren,g_web);
          static const float uoff=std::getenv("T3K_WEB_UOFF")?float(std::atof(std::getenv("T3K_WEB_UOFF"))):0;
          static const float voff=std::getenv("T3K_WEB_VOFF")?float(std::atof(std::getenv("T3K_WEB_VOFF"))):0;
          float params[260]{};SDL_Vertex vertices[64];int indices[96];unsigned count=0;
          for(size_t k=0;k+3<web.quads.size();k+=4){
            for(unsigned j=0;j<4;++j){auto q=web.quads[k+j];params[(k+j)*4]=q.x;params[(k+j)*4+1]=q.y;params[(k+j)*4+2]=q.u;params[(k+j)*4+3]=q.v;vertices[k+j]={{q.x*sx+ox,q.y*sy+oy},{float(k/4)/255.f,1,1,1},{q.x/360.f,q.y/240.f}};}
            for(unsigned j:{0u,1u,2u,0u,2u,3u})indices[count++]=int(k+j);
          }
          params[256]=op.fade;params[257]=uoff;params[258]=voff;
          SDL_SetGPURenderStateFragmentUniforms(g_web,0,params,sizeof(params));if(count)SDL_RenderGeometry(ren,web_tex_,vertices,int(web.quads.size()),indices,int(count));
          SDL_SetGPURenderState(ren,nullptr);SDL_SetRenderTextureAddressMode(ren,SDL_TEXTURE_ADDRESS_AUTO,SDL_TEXTURE_ADDRESS_AUTO);break;
        }
        // out = T + (dest - T) w, w = v / 32: the texture at alpha 1 - v / 32 over what is there. (The module puts
        // texel i at coordinate i; the GPU puts texel centres at i + 1/2.)
        std::vector<SDL_Vertex> v;
        v.reserve(web.quads.size() / 4 * 6);
        auto V = [&](const Web::Vertex& q) {
          const float a = std::clamp(1.f - q.v / 32.f, 0.f, 1.f);
          static const float uoff = std::getenv("T3K_WEB_UOFF") ? float(std::atof(std::getenv("T3K_WEB_UOFF"))) : 0.f;
          static const float voff = std::getenv("T3K_WEB_VOFF") ? float(std::atof(std::getenv("T3K_WEB_VOFF"))) : 0.f;
          return SDL_Vertex{{q.x * sx + ox, q.y * sy + oy}, {1.f, 1.f, 1.f, a},
                            {(q.u + uoff + 0.5f) / 16.f, (q.v + op.fade + voff + 0.5f) / 16.f}};
        };
        // The module edge-walks each quad: on a scanline, (u, v) where it crosses the quad's left and right edges
        // (interpolated along those edges by y), then linear across. These lane quads taper strongly, so two GPU
        // triangles would place the texture differently: an N x N grid takes the module's (u, v) at each point.
        constexpr int N = 8;
        auto walk = [](const Web::Vertex* q, float px, float py, float* u, float* vv) {
          float xs[4][3];
          int n = 0;
          for (int k = 0; k < 4; ++k) {
            const Web::Vertex &a = q[k], &b = q[(k + 1) & 3];
            if (a.y == b.y || py < std::min(a.y, b.y) || py > std::max(a.y, b.y)) continue;
            const float f = (py - a.y) / (b.y - a.y);
            xs[n][0] = a.x + (b.x - a.x) * f;
            xs[n][1] = a.u + (b.u - a.u) * f;
            xs[n][2] = a.v + (b.v - a.v) * f;
            ++n;
          }
          if (n < 2) return false;
          int l = 0, r = 0;
          for (int k = 1; k < n; ++k) {
            if (xs[k][0] < xs[l][0]) l = k;
            if (xs[k][0] > xs[r][0]) r = k;
          }
          const float w = xs[r][0] - xs[l][0];
          const float f = w > 1e-4f ? std::clamp((px - xs[l][0]) / w, 0.f, 1.f) : 0.f;
          *u = xs[l][1] + (xs[r][1] - xs[l][1]) * f;
          *vv = xs[l][2] + (xs[r][2] - xs[l][2]) * f;
          return true;
        };
        for (size_t k = 0; k + 3 < web.quads.size(); k += 4) {
          const Web::Vertex* q = &web.quads[k];
          Web::Vertex g[N + 1][N + 1];
          for (int j = 0; j <= N; ++j)
            for (int i = 0; i <= N; ++i) {
              const float s = float(i) / N, t = float(j) / N;
              Web::Vertex p;
              p.x = (1 - t) * ((1 - s) * q[0].x + s * q[1].x) + t * ((1 - s) * q[3].x + s * q[2].x);
              p.y = (1 - t) * ((1 - s) * q[0].y + s * q[1].y) + t * ((1 - s) * q[3].y + s * q[2].y);
              if (!walk(q, p.x, p.y, &p.u, &p.v)) {
                p.u = (1 - t) * ((1 - s) * q[0].u + s * q[1].u) + t * ((1 - s) * q[3].u + s * q[2].u);
                p.v = (1 - t) * ((1 - s) * q[0].v + s * q[1].v) + t * ((1 - s) * q[3].v + s * q[2].v);
              }
              g[j][i] = p;
            }
          for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i) {
              const SDL_Vertex a = V(g[j][i]), b = V(g[j][i + 1]), c = V(g[j + 1][i + 1]), d = V(g[j + 1][i]);
              v.insert(v.end(), {a, b, c, a, c, d});
            }
        }
        SDL_SetTextureBlendMode(web_tex_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTextureAddressMode(ren, SDL_TEXTURE_ADDRESS_WRAP, SDL_TEXTURE_ADDRESS_WRAP);
        SDL_RenderGeometry(ren, web_tex_, v.data(), int(v.size()), nullptr, 0);
        SDL_SetRenderTextureAddressMode(ren, SDL_TEXTURE_ADDRESS_AUTO, SDL_TEXTURE_ADDRESS_AUTO);
        break;
      }
      case Op::kPlane: {
        const Plane& pl = pic.planes[op.first];
        if (!web_tex_) {
          web_tex_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 16, 16);
          SDL_SetTextureScaleMode(web_tex_, SDL_SCALEMODE_LINEAR);
        }
        uint32_t px[256];
        for (int k = 0; k < 256; ++k) {
          const SDL_FColor c = Colour(pl.texture[k] | 0xFF, 1.f);
          px[k] = uint32_t(c.r * 255.f + 0.5f) << 16 | uint32_t(c.g * 255.f + 0.5f) << 8 | uint32_t(c.b * 255.f + 0.5f);
        }
        if(!webPixelsValid_||std::memcmp(webPixels_.data(),px,sizeof(px))){
          profiling::upload(px,sizeof(px),profileWebHash_);SDL_UpdateTexture(web_tex_,nullptr,px,16*4);std::copy(px,px+256,webPixels_.begin());webPixelsValid_=true;
        }else if(profiling::enabled()&&profiling::collecting)++profiling::data.cachedUploads;
        std::vector<SDL_Vertex> v;
        v.reserve(size_t(pl.cols) * size_t(pl.rows) * 6);
        const SDL_FColor white{1.f, 1.f, 1.f, 1.f};
        auto V = [&](const Web::Vertex& q) {   // (texel i at coordinate i; the GPU's texel centres at i + 1/2)
          return SDL_Vertex{{q.x * sx + ox, q.y * sy + oy}, white, {(q.u + 0.5f) / 16.f, (q.v + 0.5f) / 16.f}};
        };
        for (int r = 0; r < pl.rows; ++r)
          for (int c = 0; c < pl.cols; ++c) {
            const size_t i0 = size_t(r) * size_t(pl.cols + 1) + size_t(c), i1 = i0 + size_t(pl.cols + 1);
            const SDL_Vertex a = V(pl.grid[i0]), b = V(pl.grid[i0 + 1]), cc = V(pl.grid[i1 + 1]), d = V(pl.grid[i1]);
            v.insert(v.end(), {a, b, cc, a, cc, d});
          }
        SDL_SetTextureBlendMode(web_tex_, SDL_BLENDMODE_NONE);
        SDL_SetRenderTextureAddressMode(ren, SDL_TEXTURE_ADDRESS_WRAP, SDL_TEXTURE_ADDRESS_WRAP);
        SDL_RenderGeometry(ren, web_tex_, v.data(), int(v.size()), nullptr, 0);
        SDL_SetRenderTextureAddressMode(ren, SDL_TEXTURE_ADDRESS_AUTO, SDL_TEXTURE_ADDRESS_AUTO);
        break;
      }
      case Op::kBlobs: {profiling::Scope profile(profiling::discs);
        // each particle a quad over its disc, textured alpha = 1 - r^2 (white), vertex colour = the particle colour
        // with alpha = its strength: BLEND gives dest + (colour - dest) x I x (1 - d^2 / R^2)
        if (!disc_tex_) {
          constexpr int N = 64;
          std::vector<uint32_t> px(N * N);
          for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
              const float u = (float(x) + 0.5f) / N * 2.f - 1.f, v = (float(y) + 0.5f) / N * 2.f - 1.f;
              const float k = std::max(0.f, 1.f - (u * u + v * v));
              px[size_t(y) * N + size_t(x)] = uint32_t(k * 255.f + 0.5f) << 24 | 0xFFFFFF;
            }
          disc_tex_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, N, N);
          SDL_UpdateTexture(disc_tex_, nullptr, px.data(), N * 4);
          SDL_SetTextureScaleMode(disc_tex_, SDL_SCALEMODE_LINEAR);
          SDL_SetTextureBlendMode(disc_tex_, SDL_BLENDMODE_BLEND);
        }
        std::vector<SDL_Vertex> v;
        v.reserve(size_t(op.count) * 6);
        for (uint32_t k = 0; k < op.count; ++k) {
          const Blob& b = pic.blobs[op.first + k];
          const float x0 = (b.x - b.r) * sx + ox, x1 = (b.x + b.r) * sx + ox;
          const float y0 = (b.y - b.r) * sy + oy, y1 = (b.y + b.r) * sy + oy;
          const SDL_Vertex a{{x0, y0}, b.colour, {0, 0}}, bb{{x1, y0}, b.colour, {1, 0}};
          const SDL_Vertex c{{x1, y1}, b.colour, {1, 1}}, d{{x0, y1}, b.colour, {0, 1}};
          v.insert(v.end(), {a, bb, c, a, c, d});
        }
        SDL_RenderGeometry(ren, disc_tex_, v.data(), int(v.size()), nullptr, 0);
        break;
      }
      case Op::kText: {profiling::Scope profile(profiling::images);
        // one texture per digit, filled when a new picture comes (in-between frames redraw the same picture)
        const bool fresh = glyph_flip_ != pic.flip;
        for (uint32_t k = 0; k < op.count; ++k) {
          const uint32_t gi = op.first + k;
          const Glyph& g = pic.glyphs[gi];
          if (glyph_tex_.size() <= gi) glyph_tex_.resize(gi + 1, nullptr);
          if(g.gpuNumber){
            if(numberAtlases_.size()<=gi)numberAtlases_.resize(gi+1);auto& atlas=numberAtlases_[gi];
            if(!atlas.texture){atlas.texture=SDL_CreateTexture(ren,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,32,16);SDL_SetTextureBlendMode(atlas.texture,SDL_BLENDMODE_BLEND_PREMULTIPLIED);atlas.valid=false;}
            if(!atlas.valid||std::memcmp(atlas.source.data(),g.numberTexture,sizeof(g.numberTexture))){
              uint32_t pixels[512];for(unsigned i=0;i<512;++i)pixels[i]=(g.numberTexture[i]<<24)|(g.numberTexture[i]>>8);
              if(profiling::enabled()){if(profileImageHashes_.size()<=gi)profileImageHashes_.resize(gi+1);profiling::upload(pixels,sizeof(pixels),profileImageHashes_[gi]);}
              SDL_UpdateTexture(atlas.texture,nullptr,pixels,128);std::copy(g.numberTexture,g.numberTexture+512,atlas.source.begin());atlas.valid=true;
            }else if(profiling::enabled()&&profiling::collecting)++profiling::data.cachedUploads;
            SDL_SetGPURenderStateFragmentUniforms(g_number,0,g.numberParams,sizeof(g.numberParams));SDL_SetGPURenderState(ren,g_number);
            const SDL_FRect rect{g.x*sx+ox,g.y*sy+oy,g.w*sx,g.h*sy};SDL_RenderTexture(ren,atlas.texture,nullptr,&rect);SDL_SetGPURenderState(ren,nullptr);continue;
          }
          if(ringImages_.size()<=gi)ringImages_.resize(gi+1);
          SDL_Texture*& t = glyph_tex_[gi];
          float tw = 0, th = 0;
          if (t) SDL_GetTextureSize(t, &tw, &th);
          if (!t || int(tw) != g.pw || int(th) != g.ph || (SDL_GetNumberProperty(SDL_GetTextureProperties(t),SDL_PROP_TEXTURE_ACCESS_NUMBER,0)==SDL_TEXTUREACCESS_TARGET)!=(g.gpuRing||g.gpuNumber)) {
            if (t) SDL_DestroyTexture(t);ringImages_[gi].valid=false;
            t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, (g.gpuRing||g.gpuNumber)?SDL_TEXTUREACCESS_TARGET:SDL_TEXTUREACCESS_STREAMING, g.pw, g.ph);
            if (!t) continue;
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
            if(!g.gpuRing&&!g.gpuNumber)SDL_UpdateTexture(t, nullptr, g.rgba.data(), g.pw * 4);
          } else if (fresh) {
            if(!g.gpuRing&&!g.gpuNumber)SDL_UpdateTexture(t, nullptr, g.rgba.data(), g.pw * 4);
          }
          if(g.gpuRing&&(!ringImages_[gi].valid||std::memcmp(ringImages_[gi].params.data(),g.ringParams,sizeof(g.ringParams))||std::memcmp(ringImages_[gi].source.data(),g.ringTexture,sizeof(g.ringTexture)))){
            if(!ring_tex_)ring_tex_=SDL_CreateTexture(ren,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,16,8);
            uint32_t pixels[128];for(unsigned i=0;i<128;++i)pixels[i]=0xFF000000|(g.ringTexture[i]>>8);
            if(!ringSourceValid_||std::memcmp(ringSource_.data(),g.ringTexture,sizeof(g.ringTexture))){
              if(profiling::enabled()){if(profileImageHashes_.size()<=gi)profileImageHashes_.resize(gi+1);profiling::upload(pixels,sizeof(pixels),profileImageHashes_[gi]);}
              SDL_UpdateTexture(ring_tex_,nullptr,pixels,64);std::copy(g.ringTexture,g.ringTexture+128,ringSource_.begin());ringSourceValid_=true;
            }else if(profiling::enabled()&&profiling::collecting)++profiling::data.cachedUploads;
            SDL_SetTextureBlendMode(ring_tex_,SDL_BLENDMODE_NONE);
            auto* target=SDL_GetRenderTarget(ren);SDL_SetRenderTarget(ren,t);
            SDL_SetGPURenderStateFragmentUniforms(g_ring,0,g.ringParams,sizeof(g.ringParams));SDL_SetGPURenderState(ren,g_ring);
            SDL_RenderTexture(ren,ring_tex_,nullptr,nullptr);SDL_SetGPURenderState(ren,nullptr);SDL_SetRenderTarget(ren,target);
            std::copy(g.ringParams,g.ringParams+16,ringImages_[gi].params.begin());std::copy(g.ringTexture,g.ringTexture+128,ringImages_[gi].source.begin());ringImages_[gi].valid=true;
          }
          const SDL_FRect r{g.x * sx + ox, g.y * sy + oy, g.w * sx, g.h * sy};
          SDL_RenderTexture(ren, t, nullptr, &r);
        }
        break;
      }
    }
  }
  glyph_flip_ = pic.flip;
  SDL_SetRenderTarget(ren, keep);
  if(drawingPaintLayer_)return;
  if(!suppressPresentation_)Pass(ren, next, g_ycc ? g_out : nullptr, &vw);
  cur_ ^= 1;
}

void Interpolate(const Picture& prev, const Picture& cur, float t, std::vector<Seg>* out, float max_move) {
  out->assign(cur.segs.begin(), cur.segs.end());
  if (t >= 1.f) return;
  t = std::max(t, 0.f);
  // Candidate pairs (same shape and segment count), nearest first; each item used once.
  struct Pair { float d; uint32_t c, p; };
  std::vector<Pair> pairs;
  for (uint32_t c = 0; c < cur.items.size(); ++c)
    for (uint32_t p = 0; p < prev.items.size(); ++p) {
      const Item &ic = cur.items[c], &ip = prev.items[p];
      if (ic.shape != ip.shape || ic.count != ip.count || !ic.count) continue;
      // distance: the first segment's start (covers shapes placed by their vertices, e.g. the web)
      const Seg &sc = cur.segs[ic.first], &sp = prev.segs[ip.first];
      pairs.push_back({std::hypot(sc.x0 - sp.x0, sc.y0 - sp.y0) + std::hypot(ic.x - ip.x, ic.y - ip.y), c, p});
    }
  std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.d < b.d; });
  std::vector<char> used_c(cur.items.size(), 0), used_p(prev.items.size(), 0);
  auto lerp = [t](float a, float b) { return a + (b - a) * t; };
  auto lerpc = [&](const SDL_FColor& a, const SDL_FColor& b) {
    return SDL_FColor{lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a)};
  };
  for (const Pair& pr : pairs) {
    if (used_c[pr.c] || used_p[pr.p]) continue;
    used_c[pr.c] = used_p[pr.p] = 1;
    const Item &ic = cur.items[pr.c], &ip = prev.items[pr.p];
    bool ok = true;
    for (uint32_t k = 0; k < ic.count && ok; ++k) {
      const Seg &a = prev.segs[ip.first + k], &b = cur.segs[ic.first + k];
      ok = std::fabs(a.x0 - b.x0) + std::fabs(a.y0 - b.y0) <= max_move &&
           std::fabs(a.x1 - b.x1) + std::fabs(a.y1 - b.y1) <= max_move;
    }
    if (!ok) continue;
    for (uint32_t k = 0; k < ic.count; ++k) {
      const Seg &a = prev.segs[ip.first + k], &b = cur.segs[ic.first + k];
      (*out)[ic.first + k] = {lerp(a.x0, b.x0), lerp(a.y0, b.y0), lerp(a.x1, b.x1), lerp(a.y1, b.y1),
                              lerpc(a.c0, b.c0), lerpc(a.c1, b.c1), lerp(a.r, b.r)};
    }
  }
}

void InterpolatePicture(const Picture& prev, const Picture& cur, float t, Picture* out) {
  *out = cur;
  if (t >= 1.f) return;
  t = std::max(t, 0.f);
  Interpolate(prev, cur, t, &out->segs);
  auto lerp = [t](float a, float b) { return a + (b - a) * t; };
  // The web surface: the same lanes from picture to picture (4 vertices each), moved by the camera. Blended vertex by
  // vertex when both pictures have it with the same lane count and nothing jumped (a new level, a camera cut); the
  // texture coordinates too, unless they wrapped (the surface pattern scrolls).
  if (prev.webs.size() == cur.webs.size())
    for (size_t w = 0; w < cur.webs.size(); ++w) {
      const auto &a = prev.webs[w].quads, &b = cur.webs[w].quads;
      if (a.size() != b.size() || b.empty()) continue;
      bool ok = true;
      for (size_t i = 0; i < b.size() && ok; ++i)
        ok = std::fabs(a[i].x - b[i].x) + std::fabs(a[i].y - b[i].y) <= 60.f;
      if (!ok) continue;
      auto& q = out->webs[w].quads;
      for (size_t i = 0; i < b.size(); ++i) {
        q[i].x = lerp(a[i].x, b[i].x);
        q[i].y = lerp(a[i].y, b[i].y);
        if (std::fabs(a[i].u - b[i].u) < 4.f && std::fabs(a[i].v - b[i].v) < 4.f) {
          q[i].u = lerp(a[i].u, b[i].u);
          q[i].v = lerp(a[i].v, b[i].v);
        }
      }
    }
  // Discs, numbers, planes: an op pairs with prev's op of the same kind from the same entry type, by order (the k-th
  // of that type in each picture); items blend index by index when the counts agree (lists that grew or shrank:
  // drawn as in cur), each only if it moved little (a particle that wrapped or a reused slot: drawn as in cur).
  auto pairs = [&](Op::Kind kind) {
    std::vector<std::pair<const Op*, const Op*>> v;
    std::vector<char> used(prev.ops.size(), 0);
    for (const Op& ob : cur.ops) {
      if (ob.kind != kind) continue;
      for (size_t j = 0; j < prev.ops.size(); ++j)
        if (!used[j] && prev.ops[j].kind == kind && prev.ops[j].type == ob.type) {
          used[j] = 1;
          v.push_back({&prev.ops[j], &ob});
          break;
        }
    }
    return v;
  };
  // Measured 2026-11-22 (T3K_WEBDUMP: a true pair's motion continues from the last pair): the grid 00310010, shapes
  // 0031020E, bursts, particles 00310309, rings / wave pair by index; the title dot field 0031000E does NOT (median
  // velocity change 11.7 px against a speed of 2: its index i is another dot each picture) - drawn as in cur.
  for (const auto& [pa, pb] : pairs(Op::kBlobs)) {
    if (pa->count != pb->count || pb->type == 0x0031000Eu) continue;
    for (uint32_t i = 0; i < pb->count; ++i) {
      const Blob &a = prev.blobs[pa->first + i], &b = cur.blobs[pb->first + i];
      if (std::fabs(a.x - b.x) + std::fabs(a.y - b.y) > 30.f) continue;
      Blob& o = out->blobs[pb->first + i];
      o.x = lerp(a.x, b.x);
      o.y = lerp(a.y, b.y);
      o.r = lerp(a.r, b.r);
    }
  }
  for (const auto& [pa, pb] : pairs(Op::kText)) {
    {
      const Op &oa = *pa, &ob = *pb;
      if (oa.count != ob.count) continue;
      for (uint32_t i = 0; i < ob.count; ++i) {
        const Glyph &a = prev.glyphs[oa.first + i], &b = cur.glyphs[ob.first + i];
        if (std::fabs(a.x - b.x) + std::fabs(a.y - b.y) > 30.f) continue;
        Glyph& o = out->glyphs[ob.first + i];
        o.x = lerp(a.x, b.x);
        o.y = lerp(a.y, b.y);
        o.w = lerp(a.w, b.w);
        o.h = lerp(a.h, b.h);
      }
    }
  }
  for (const auto& [pa, pb] : pairs(Op::kPlane)) {
    {
      const Plane &a = prev.planes[pa->first], &b = cur.planes[pb->first];
      if (a.cols != b.cols || a.rows != b.rows || a.grid.size() != b.grid.size()) continue;
      bool ok = true;
      for (size_t i = 0; i < b.grid.size() && ok; ++i)
        ok = std::fabs(a.grid[i].x - b.grid[i].x) + std::fabs(a.grid[i].y - b.grid[i].y) <= 60.f;
      if (!ok) continue;
      auto& g = out->planes[pb->first].grid;
      for (size_t i = 0; i < b.grid.size(); ++i) {
        g[i].x = lerp(a.grid[i].x, b.grid[i].x);
        g[i].y = lerp(a.grid[i].y, b.grid[i].y);
      }
    }
  }
}

void DrawVectors(SDL_Renderer* ren, const std::vector<Seg>& segs, const SDL_FRect& dst, bool rgb) {
  profiling::Scope profile(profiling::vectors);
  if (segs.empty()) return;
  const float sx = dst.w / 360.f, sy = dst.h / 240.f;
  // The module samples coverage at integer pixel coordinates (a pixel's corner): shift half a pixel to centres.
  auto X = [&](float x) { return dst.x + (x + 0.5f) * sx; };
  auto Y = [&](float y) { return dst.y + (y + 0.5f) * sy; };
  std::vector<SDL_Vertex> v;
  v.reserve(segs.size() * 30);
  for (const Seg& s : segs) {
    float dx = s.x1 - s.x0, dy = s.y1 - s.y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1e-4f) { dx /= len; dy /= len; } else { dx = 1.f; dy = 0.f; }
    const float nx = -dy * s.r, ny = dx * s.r;      // perpendicular, in game pixels
    const float ex = dx * s.r, ey = dy * s.r;       // along, for the end caps
    const SDL_FColor k0 = rgb ? ToRgb(s.c0) : s.c0, k1 = rgb ? ToRgb(s.c1) : s.c1;
    SDL_FColor e0 = k0, e1 = k1;
    e0.a = 0.f;
    e1.a = 0.f;
    auto V = [&](float x, float y, SDL_FColor c) { return SDL_Vertex{{X(x), Y(y)}, c, {0, 0}}; };
    const SDL_Vertex c0 = V(s.x0, s.y0, k0), c1 = V(s.x1, s.y1, k1);
    const SDL_Vertex l0 = V(s.x0 + nx, s.y0 + ny, e0), l1 = V(s.x1 + nx, s.y1 + ny, e1);
    const SDL_Vertex r0 = V(s.x0 - nx, s.y0 - ny, e0), r1 = V(s.x1 - nx, s.y1 - ny, e1);
    const SDL_Vertex t0 = V(s.x0 - ex, s.y0 - ey, e0), t1 = V(s.x1 + ex, s.y1 + ey, e1);
    // the tent: centre line opaque, both sides fading to nothing; caps: a fan to a point R beyond each end
    v.insert(v.end(), {c0, l0, c1, l0, l1, c1, c0, c1, r0, r0, c1, r1, c0, r0, t0, c0, t0, l0, c1, l1, t1, c1, t1, r1});
  }
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_RenderGeometry(ren, nullptr, v.data(), int(v.size()), nullptr, 0);
}

void ClearBlack(SDL_Renderer* ren) {
  if (g_ycc) SDL_SetRenderDrawColor(ren, 16, 128, 128, 255);   // YCrCb black
  else SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
  SDL_RenderClear(ren);
}

SDL_Renderer* CreateRenderer(SDL_Window* window) {
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
  const char* name = SDL_getenv("T3K_RENDERER");   // tests: another renderer (direct3d11, ...)
  SDL_SetStringProperty(props, SDL_PROP_RENDERER_CREATE_NAME_STRING, name ? name : "gpu");
  SDL_SetBooleanProperty(props, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_DXIL_BOOLEAN, true);
  SDL_Renderer* r = SDL_CreateRendererWithProperties(props);
  SDL_DestroyProperties(props);
  if (!r) r = SDL_CreateRenderer(window, nullptr);
  return r;
}

void SetLineStyle(bool sharp, float core, float glow, float max_size) {
  g_sharp = sharp;
  g_core = std::clamp(core, 0.05f, 1.f);
  g_glow = std::clamp(glow, 0.f, 1.f);
  g_sharp_size = max_size;
}

void ShutdownPipeline() {
  for(auto* state:{g_fade,g_out,g_in,g_ripple,g_ring,g_number,g_web})if(state)SDL_DestroyGPURenderState(state);
  g_fade=g_out=g_in=g_ripple=g_ring=g_number=g_web=nullptr;
  for(auto*& shader:g_shader){if(shader&&g_device)SDL_ReleaseGPUShader(g_device,shader);shader=nullptr;}
  if(g_temporalSampler&&g_device)SDL_ReleaseGPUSampler(g_device,g_temporalSampler);g_temporalSampler=nullptr;
  g_device=nullptr;g_ycc=false;
}

bool InitPipeline(SDL_Renderer* ren) {
  ShutdownPipeline();
  g_ycc = false;
  if (SDL_getenv("T3K_NATIVE_RGB")) return false;   // tests: the old RGB path
  g_device = SDL_GetGPURendererDevice(ren);
  if (!g_device || !(SDL_GetGPUShaderFormats(g_device) & SDL_GPU_SHADERFORMAT_DXIL)) return false;
  g_fade = MakeState(ren, k_ycc_fade_dxil, sizeof(k_ycc_fade_dxil), 1, &g_shader[0]);
  g_out = MakeState(ren, k_ycc_out_dxil, sizeof(k_ycc_out_dxil), 0, &g_shader[1]);
  g_in = MakeState(ren, k_ycc_in_dxil, sizeof(k_ycc_in_dxil), 0, &g_shader[2]);
  g_ripple = MakeState(ren, k_ycc_ripple_dxil, sizeof(k_ycc_ripple_dxil), 1, &g_shader[3]);
  g_ring = MakeState(ren, k_ycc_ring_dxil, sizeof(k_ycc_ring_dxil), 1, &g_shader[4]);
  g_number=MakeState(ren,k_ycc_number_dxil,sizeof(k_ycc_number_dxil),1,&g_shader[5]);
  g_web=MakeState(ren,k_ycc_web_dxil,sizeof(k_ycc_web_dxil),1,&g_shader[6]);
  SDL_GPUShaderCreateInfo temporal{};temporal.code=k_ycc_temporal_dxil;temporal.code_size=sizeof(k_ycc_temporal_dxil);
  temporal.entrypoint="main";temporal.format=SDL_GPU_SHADERFORMAT_DXIL;temporal.stage=SDL_GPU_SHADERSTAGE_FRAGMENT;temporal.num_samplers=2;temporal.num_uniform_buffers=1;
  g_shader[7]=SDL_CreateGPUShader(g_device,&temporal);
  SDL_GPUSamplerCreateInfo sampler{};sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_NEAREST;
  sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  g_temporalSampler=SDL_CreateGPUSampler(g_device,&sampler);
  g_ycc = g_fade && g_out && g_in && g_ripple;
  if (!g_ycc) std::fprintf(stderr, "t3k native: no YCrCb pipeline (%s), RGB\n", SDL_GetError());
  return g_ycc;
}

}  // namespace t3knative
