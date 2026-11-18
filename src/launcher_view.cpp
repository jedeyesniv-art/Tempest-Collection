// launcher_view.cpp - the launcher's look; see launcher_view.h. A port of tools/launcher_mockup/index.html to SDL's 2D
// renderer: glow = additive soft sprites (a gaussian dot, a beam profile for lines), blurred backdrops made once on the
// CPU from the covers, feedback backgrounds (T3K, Space Giraffe) in two render-target textures.
#include "launcher_view.h"

#include "pixfont.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace fs = std::filesystem;

namespace lview {
namespace {

constexpr float kPi = 3.14159265f;

struct Col { float r, g, b; };

struct Style {          // per game: accent colour, motion (0 calm .. 1 wild)
  const char* id;
  Col accent;
  float motion;
};
const Style kStyles[] = {
    {"t81", {90 / 255.f, 200 / 255.f, 1.f}, 0.15f}, {"t2k", {1.f, 200 / 255.f, 40 / 255.f}, 0.55f},
    {"t3k", {1.f, 80 / 255.f, 1.f}, 0.8f},          {"sg", {120 / 255.f, 1.f, 120 / 255.f}, 1.0f},
    {"txk", {80 / 255.f, 200 / 255.f, 1.f}, 0.3f},  {"t4k", {80 / 255.f, 120 / 255.f, 1.f}, 0.45f},
    {"more", {1.f, 190 / 255.f, 60 / 255.f}, 0.35f},   // MORE LLAMASOFT: llama gold
};
const Style& StyleOf(const std::string& id) {
  for (const Style& s : kStyles)
    if (id == s.id) return s;
  return kStyles[0];
}

Col Hsl(float h, float s, float l) {   // h in degrees, s / l 0..1
  h = std::fmod(std::fmod(h, 360.f) + 360.f, 360.f) / 60.f;
  const float c = (1 - std::fabs(2 * l - 1)) * s, x = c * (1 - std::fabs(std::fmod(h, 2.f) - 1)), m = l - c / 2;
  Col o{};
  switch (int(h)) {
    case 0: o = {c, x, 0}; break;
    case 1: o = {x, c, 0}; break;
    case 2: o = {0, c, x}; break;
    case 3: o = {0, x, c}; break;
    case 4: o = {x, 0, c}; break;
    default: o = {c, 0, x}; break;
  }
  return {o.r + m, o.g + m, o.b + m};
}

// The 5x7 pixel font (system text, T3K style) is pixfont.cpp's (shared with the settings menus).

// ---- textures made at start ----
SDL_Texture* g_dot = nullptr;        // gaussian dot (white, alpha)
SDL_Texture* g_beam = nullptr;       // a line's cross profile (white, alpha by v)
SDL_Texture* g_vignette = nullptr;   // black, alpha rising to the edges
SDL_Texture* g_radial = nullptr;     // white, alpha 1 in the middle -> 0

struct Art {
  SDL_Texture* cover = nullptr;
  SDL_Texture* blur = nullptr;   // the cover, tiny and blurred: the backdrop
  SDL_Texture* logo = nullptr;
  SDL_Texture* logo_glow = nullptr;   // the logo's alpha, blurred, white
  float logo_w = 0, logo_h = 0;
  SDL_Texture* fb[2] = {nullptr, nullptr};   // feedback backgrounds
  int fb_cur = 0;
  bool fb_live = false;
};

SDL_Renderer* g_ren = nullptr;
std::vector<Entry> g_entries;
std::vector<Art> g_art;
fs::path g_dir;
std::vector<float> g_fades;   // background crossfade per entry
float g_pos = 0.f;            // eased scroll position
double g_time = 0.0;
int g_warp = -1;
std::string g_extra_hint;
float g_motion = 1.f;   // SetMotion
// the MORE LLAMASOFT list
std::vector<MoreItem> g_more;
std::vector<SDL_Texture*> g_more_art;
int g_more_ours = -1;
float g_more_scroll = 0.f;
bool g_more_snap = true;   // the first frame jumps to the selection
double g_warp_t = 0.0;
int g_w = 0, g_h = 0;

SDL_Texture* MakeTexture(SDL_Surface* s, SDL_BlendMode mode) {
  SDL_Texture* t = SDL_CreateTextureFromSurface(g_ren, s);
  if (t) {
    SDL_SetTextureBlendMode(t, mode);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
  }
  return t;
}

// A white RGBA surface whose alpha is f(x, y) in 0..1 (x, y in 0..1).
template <typename F>
SDL_Texture* AlphaTexture(int w, int h, F f, Col c = {1, 1, 1}, SDL_BlendMode mode = SDL_BLENDMODE_ADD) {
  SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
  if (!s) return nullptr;
  for (int y = 0; y < h; ++y) {
    uint8_t* row = static_cast<uint8_t*>(s->pixels) + y * s->pitch;
    for (int x = 0; x < w; ++x) {
      const float a = std::clamp(f((x + 0.5f) / w, (y + 0.5f) / h), 0.f, 1.f);
      row[x * 4 + 0] = uint8_t(c.r * 255);
      row[x * 4 + 1] = uint8_t(c.g * 255);
      row[x * 4 + 2] = uint8_t(c.b * 255);
      row[x * 4 + 3] = uint8_t(a * 255 + 0.5f);
    }
  }
  SDL_Texture* t = MakeTexture(s, mode);
  SDL_DestroySurface(s);
  return t;
}

// Box blur of an RGBA32 surface, in place (n passes of radius r: close to a gaussian).
void Blur(SDL_Surface* s, int r, int n) {
  const int w = s->w, h = s->h;
  std::vector<float> a(size_t(w) * h * 4), b(a.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w * 4; ++x) a[size_t(y) * w * 4 + x] = static_cast<uint8_t*>(s->pixels)[y * s->pitch + x];
  for (int pass = 0; pass < n; ++pass) {
    for (int dir = 0; dir < 2; ++dir) {
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          for (int c = 0; c < 4; ++c) {
            float sum = 0;
            int cnt = 0;
            for (int k = -r; k <= r; ++k) {
              const int xx = dir == 0 ? std::clamp(x + k, 0, w - 1) : x, yy = dir == 1 ? std::clamp(y + k, 0, h - 1) : y;
              sum += a[(size_t(yy) * w + xx) * 4 + c];
              ++cnt;
            }
            b[(size_t(y) * w + x) * 4 + c] = sum / cnt;
          }
      std::swap(a, b);
    }
  }
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w * 4; ++x)
      static_cast<uint8_t*>(s->pixels)[y * s->pitch + x] = uint8_t(std::clamp(a[size_t(y) * w * 4 + x], 0.f, 255.f));
}

SDL_Surface* LoadRGBA(const fs::path& p) {
  SDL_Surface* s = SDL_LoadPNG(p.string().c_str());
  if (!s) return nullptr;
  SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(s);
  return c;
}

void LoadArt(size_t i) {
  Art& a = g_art[i];
  const std::string& id = g_entries[i].id;
  if (SDL_Surface* s = LoadRGBA(g_dir / (id + ".png"))) {
    a.cover = MakeTexture(s, SDL_BLENDMODE_BLEND);
    if (SDL_Surface* small = SDL_ScaleSurface(s, 45, 60, SDL_SCALEMODE_LINEAR)) {
      Blur(small, 3, 3);
      a.blur = MakeTexture(small, SDL_BLENDMODE_BLEND);
      SDL_DestroySurface(small);
    }
    SDL_DestroySurface(s);
  }
  if (SDL_Surface* s = LoadRGBA(g_dir / (id + "_logo.png"))) {
    a.logo = MakeTexture(s, SDL_BLENDMODE_BLEND);
    a.logo_w = float(s->w);
    a.logo_h = float(s->h);
    // the glow: the logo's alpha, padded, small, blurred, in white
    const int gw = 160, gh = std::max(8, int(160 * a.logo_h / a.logo_w)), pad = 24;
    if (SDL_Surface* small = SDL_ScaleSurface(s, gw, gh, SDL_SCALEMODE_LINEAR)) {
      SDL_Surface* g = SDL_CreateSurface(gw + 2 * pad, gh + 2 * pad, SDL_PIXELFORMAT_RGBA32);
      if (g) {
        SDL_FillSurfaceRect(g, nullptr, 0);
        SDL_Rect dst{pad, pad, gw, gh};
        SDL_SetSurfaceBlendMode(small, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(small, nullptr, g, &dst);
        for (int y = 0; y < g->h; ++y) {
          uint8_t* row = static_cast<uint8_t*>(g->pixels) + y * g->pitch;
          for (int x = 0; x < g->w; ++x) row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
        }
        Blur(g, 4, 3);
        a.logo_glow = MakeTexture(g, SDL_BLENDMODE_ADD);
        SDL_DestroySurface(g);
      }
      SDL_DestroySurface(small);
    }
    SDL_DestroySurface(s);
  }
}

// ---- drawing helpers ----
void SetDraw(Col c, float a, SDL_BlendMode mode = SDL_BLENDMODE_BLEND) {
  SDL_SetRenderDrawBlendMode(g_ren, mode);
  SDL_SetRenderDrawColorFloat(g_ren, c.r, c.g, c.b, a);
}
void Fill(float x, float y, float w, float h, Col c, float a, SDL_BlendMode mode = SDL_BLENDMODE_BLEND) {
  SetDraw(c, a, mode);
  const SDL_FRect r{x, y, w, h};
  SDL_RenderFillRect(g_ren, &r);
}
void Sprite(SDL_Texture* t, float x, float y, float w, float h, Col c, float a, double angle = 0.0,
            SDL_FlipMode flip = SDL_FLIP_NONE) {
  if (!t || a <= 0.f) return;
  SDL_SetTextureColorModFloat(t, c.r, c.g, c.b);
  SDL_SetTextureAlphaModFloat(t, std::min(a, 1.f));
  const SDL_FRect r{x, y, w, h};
  if (angle != 0.0 || flip != SDL_FLIP_NONE) SDL_RenderTextureRotated(g_ren, t, nullptr, &r, angle, nullptr, flip);
  else SDL_RenderTexture(g_ren, t, nullptr, &r);
}
void Dot(float x, float y, float size, Col c, float a) { Sprite(g_dot, x - size / 2, y - size / 2, size, size, c, a); }
// A soft line (additive): the beam profile across, `width` wide.
void Beam(float x0, float y0, float x1, float y1, float width, Col c, float a) {
  if (a <= 0.f) return;
  float dx = x1 - x0, dy = y1 - y0;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1e-3f) return;
  const float nx = -dy / len * width / 2, ny = dx / len * width / 2;
  const SDL_FColor fc{c.r, c.g, c.b, std::min(a, 1.f)};
  const SDL_Vertex v[4] = {{{x0 + nx, y0 + ny}, fc, {0, 0}}, {{x1 + nx, y1 + ny}, fc, {1, 0}},
                           {{x0 - nx, y0 - ny}, fc, {0, 1}}, {{x1 - nx, y1 - ny}, fc, {1, 1}}};
  const int idx[6] = {0, 1, 2, 1, 3, 2};
  SDL_RenderGeometry(g_ren, g_beam, v, 4, idx, 6);
}
// A glowing line: a wide faint beam and a thin bright core.
void Glow(float x0, float y0, float x1, float y1, float width, Col c, float a, float glow = 4.f) {
  Beam(x0, y0, x1, y1, width * glow, c, a * 0.35f);
  Beam(x0, y0, x1, y1, width * 1.6f, {std::min(1.f, c.r * 0.5f + 0.5f), std::min(1.f, c.g * 0.5f + 0.5f),
                                      std::min(1.f, c.b * 0.5f + 0.5f)}, a);
}

// Pixel text (pixfont); unit = one font pixel.
float TextWidth(const std::string& s, float unit) { return pixfont::TextWidth(s, unit); }
void Text(const std::string& str, float x, float y, float unit, Col c, float a, int align = 0, float glow = 1.f) {
  pixfont::Text(g_ren, str, x, y, unit, {c.r, c.g, c.b}, a, align, glow);
}

// ---- backgrounds ----
float W() { return float(g_w); }
float H() { return float(g_h); }

void BgT81(float t, float a) {   // a slow vector web tube with one flipper
  Fill(0, 0, W(), H(), {0, 0, 8 / 255.f}, 0.86f * a);
  const float cx = W() / 2, cy = H() * 0.46f, rot = t * 0.04f, lw = std::max(1.f, H() / 700);
  const int n = 16;
  const Col blue{70 / 255.f, 110 / 255.f, 1.f};
  auto pt = [&](int i, float r) {
    const float q = rot + i / float(n) * 2 * kPi;
    return SDL_FPoint{cx + std::cos(q) * r, cy + std::sin(q) * r * 0.85f};
  };
  for (int i = 0; i < n; ++i) {
    for (float r : {H() * 0.62f, H() * 0.09f}) {
      const SDL_FPoint p = pt(i, r), q = pt(i + 1, r);
      Glow(p.x, p.y, q.x, q.y, lw, blue, 0.55f * a);
    }
    const SDL_FPoint p = pt(i, H() * 0.62f), q = pt(i, H() * 0.09f);
    Glow(p.x, p.y, q.x, q.y, lw, blue, 0.5f * a);
  }
  const int lane = int(t / 5) % n;
  const float z = std::fmod(t, 5.f) / 5.f, q = rot + (lane + 0.5f) / n * 2 * kPi, r = H() * (0.09f + 0.53f * z);
  const float fx = cx + std::cos(q) * r, fy = cy + std::sin(q) * r * 0.85f, s = H() * 0.008f * (0.4f + z * 2);
  const Col red{1, 60 / 255.f, 60 / 255.f};
  Glow(fx - s * 2, fy - s, fx + s * 2, fy + s, lw * 1.3f, red, 0.9f * a);
  Glow(fx - s * 2, fy + s, fx + s * 2, fy - s, lw * 1.3f, red, 0.9f * a);
}

void BgTxK(float t, float a) {   // drifting neon triangles, cool blue
  Fill(0, 0, W(), H(), {0, 4 / 255.f, 20 / 255.f}, 0.9f * a);
  Sprite(g_radial, W() / 2 - H(), H() * 0.45f - H(), 2 * H(), 2 * H(), {10 / 255.f, 60 / 255.f, 120 / 255.f}, 0.8f * a);
  const float cx = W() / 2, cy = H() * 0.47f;
  for (int i = 0; i < 9; ++i) {
    const float p = std::fmod(t * 0.03f + i / 9.f, 1.f), s = H() * (0.1f + p * 1.1f);
    const float al = 0.35f * std::sin(p * kPi) * a, lw = H() / 600 + p * H() / 300;
    SDL_FPoint v[3];
    for (int k = 0; k < 3; ++k) {
      const float q = -kPi / 2 + k * 2 * kPi / 3 + t * 0.02f;
      v[k] = {cx + std::cos(q) * s, cy + std::sin(q) * s};
    }
    for (int k = 0; k < 3; ++k) Glow(v[k].x, v[k].y, v[(k + 1) % 3].x, v[(k + 1) % 3].y, lw, {90 / 255.f, 200 / 255.f, 1}, al, 6);
  }
}

void BgT4K(float t, float a) {   // a glossy web tunnel with streaming particles
  Fill(0, 0, W(), H(), {8 / 255.f, 0, 24 / 255.f}, 0.85f * a);
  const float cx = W() / 2, cy = H() * 0.42f;
  for (int i = 0; i < 16; ++i) {
    const float q = i / 16.f * 2 * kPi + t * 0.05f;
    const Col c = Hsl(250 + 40 * std::sin(q * 2 + t), 0.9f, 0.6f);
    Glow(cx + std::cos(q) * H() * 0.05f, cy + std::sin(q) * H() * 0.05f, cx + std::cos(q) * W(), cy + std::sin(q) * W(),
         H() / 500, c, 0.35f * a);
  }
  for (int i = 0; i < 70; ++i) {
    const float q = i * 2.399f + t * 0.05f, z = std::fmod(i * 0.137f + t * 0.35f, 1.f), r = H() * 0.05f + z * z * W() * 0.7f;
    Dot(cx + std::cos(q) * r, cy + std::sin(q) * r, 3 + z * H() / 60, Hsl(float((i * 37) % 360), 1, 0.65f), 0.8f * z * a);
  }
}

void BgT2K(float t, float a) {   // Jaguar-era starfield warp and a rocking rainbow web
  Fill(0, 0, W(), H(), {0, 0, 0}, 0.8f * a);
  const float cx = W() / 2, cy = H() * 0.45f;
  for (int i = 0; i < 220; ++i) {
    const float q = i * 2.399f, z = std::fmod(i * 0.0917f + t * 0.55f, 1.f), r = z * z * W() * 0.8f;
    Beam(cx + std::cos(q) * r, cy + std::sin(q) * r * 0.8f, cx + std::cos(q) * r * 0.9f, cy + std::sin(q) * r * 0.8f * 0.9f,
         2 + z * 5, Hsl(t * 60 + i * 9, 1, 0.6f), z * a);
  }
  const float rk = std::sin(t * 0.6f) * 0.25f;
  for (int i = 0; i < 16; ++i) {
    const float q0 = i / 16.f * 2 * kPi + rk, q1 = (i + 1) / 16.f * 2 * kPi + rk;
    const Col c = Hsl(i * 22 + t * 90, 1, 0.55f);
    const float ax = cx + std::cos(q0) * H() * 0.07f, ay = cy + std::sin(q0) * H() * 0.07f;
    const float bx = cx + std::cos(q0) * H() * 0.75f, by = cy + std::sin(q0) * H() * 0.6f;
    const float ex = cx + std::cos(q1) * H() * 0.75f, ey = cy + std::sin(q1) * H() * 0.6f;
    Glow(ax, ay, bx, by, H() / 400, c, 0.45f * a);
    Glow(bx, by, ex, ey, H() / 400, c, 0.45f * a);
  }
}

// Feedback: the previous picture drawn into the other buffer zoomed, turned, faded (colour mod), then new sparks.
SDL_Texture* Feedback(Art& art, float zoom, float angle_deg, Col fade, const auto& sparks) {
  const int fw = 480, fh = std::max(120, int(480.f * g_h / std::max(1, g_w)));
  for (SDL_Texture*& t : art.fb) {
    float tw = 0, th = 0;
    if (t) SDL_GetTextureSize(t, &tw, &th);
    if (t && (int(tw) != fw || int(th) != fh)) { SDL_DestroyTexture(t); t = nullptr; }
    if (!t) {
      t = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, fw, fh);
      if (!t) {
        static bool told = false;
        if (!told) std::fprintf(stderr, "launcher: no feedback buffer (%s)\n", SDL_GetError());
        told = true;
        return nullptr;
      }
      SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
      art.fb_live = false;
    }
  }
  SDL_Texture* old_target = SDL_GetRenderTarget(g_ren);
  if (!art.fb_live) {
    for (SDL_Texture* t : art.fb) {
      SDL_SetRenderTarget(g_ren, t);
      SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
      SDL_RenderClear(g_ren);
    }
    art.fb_live = true;
  }
  SDL_Texture* src = art.fb[art.fb_cur];
  SDL_Texture* dst = art.fb[art.fb_cur ^ 1];
  SDL_SetRenderTarget(g_ren, dst);
  SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
  SDL_RenderClear(g_ren);
  SDL_SetTextureBlendMode(src, SDL_BLENDMODE_NONE);
  SDL_SetTextureColorModFloat(src, fade.r, fade.g, fade.b);
  SDL_SetTextureAlphaModFloat(src, 1.f);
  const SDL_FRect r{fw * (1 - zoom) / 2, fh * (1 - zoom) / 2, fw * zoom, fh * zoom};
  SDL_RenderTextureRotated(g_ren, src, nullptr, &r, angle_deg, nullptr, SDL_FLIP_NONE);
  SDL_SetTextureColorModFloat(src, 1, 1, 1);
  sparks(float(fw), float(fh));
  SDL_SetRenderTarget(g_ren, old_target);
  art.fb_cur ^= 1;
  return dst;
}

void BgT3K(Art& art, float t, float a, float dt) {   // Nuon psychedelia: a spinning zoom feedback fed by sparks
  const float k = std::clamp(dt * 60.f, 0.f, 3.f);   // steps scaled to 60 frames a second
  // (soft dots carry less light than the mockup's solid ones: bigger, drawn twice, a slower zoom and a longer fade)
  const float f = std::pow(0.955f, k);
  SDL_Texture* fb = Feedback(art, std::pow(1.028f, k), -0.8f * k, {f, f, f}, [&](float fw, float fh) {
    for (int i = 0; i < 6; ++i) {
      const float q = t * 1.3f + i * 1.0472f, r = fh * 0.34f;   // round the panel (the middle is behind it)
      const Col c = Hsl(t * 50 + i * 60, 1, 0.6f);
      Dot(fw / 2 + std::cos(q) * r, fh / 2 + std::sin(q) * r, 16, c, 1);
      Dot(fw / 2 + std::cos(q) * r, fh / 2 + std::sin(q) * r, 7, {1, 1, 1}, 0.8f);
    }
  });
  Fill(0, 0, W(), H(), {0, 0, 0}, 0.6f * a);
  if (fb) {
    static const bool debug = std::getenv("LAUNCHER_FB_DEBUG") != nullptr;   // the raw buffer, opaque (tests)
    SDL_SetTextureBlendMode(fb, debug ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_ADD);
    Sprite(fb, 0, 0, W(), H(), {1, 1, 1}, debug ? 1.f : 0.95f * a);
  }
}

void BgSG(Art& art, float t, float a, float dt) {   // Space Giraffe: beat-pulsed feedback bloom, rings on the beat
  const float k = std::clamp(dt * 60.f, 0.f, 3.f);
  const float beat = t * 126.f / 60.f, ph = beat - std::floor(beat), pulse = std::pow(1 - ph, 3.f);
  const float z = std::pow(1.02f + pulse * 0.05f, k);
  // a slow colour drift instead of the mockup's hue-rotate: the fade differs per channel, cycling
  const float f = std::pow(0.9f, k);
  const Col fade{f * (0.97f + 0.03f * std::sin(t * 0.9f)), f * (0.97f + 0.03f * std::sin(t * 1.3f + 2)),
                 f * (0.97f + 0.03f * std::sin(t * 1.1f + 4))};
  SDL_Texture* fb = Feedback(art, z, std::sin(t * 0.7f) * 1.2f * k, fade, [&](float fw, float fh) {
    if (ph < 0.08f) {   // a ring on the beat
      const Col c = Hsl(std::floor(beat) * 47, 1, 0.6f);
      for (int i = 0; i < 32; ++i) {
        const float q0 = i / 32.f * 2 * kPi, q1 = (i + 1) / 32.f * 2 * kPi;
        Beam(fw / 2 + std::cos(q0) * fw * 0.3f, fh * 0.45f + std::sin(q0) * fh * 0.36f, fw / 2 + std::cos(q1) * fw * 0.3f,
             fh * 0.45f + std::sin(q1) * fh * 0.36f, 7, c, 1);
      }
    }
    for (int i = 0; i < 12; ++i) {   // the giraffe's furry blobs
      const float q = t * 2 + i * 0.5236f, r = fh * (0.36f + 0.05f * std::sin(t * 3 + i));   // round the panel
      const Col c = i % 2 ? Col{120 / 255.f, 1, 120 / 255.f} : Col{1, 90 / 255.f, 200 / 255.f};
      Dot(fw / 2 + std::cos(q) * r * 1.3f, fh * 0.45f + std::sin(q) * r, 12 + pulse * 12, c, 0.7f + pulse * 0.3f);
    }
  });
  Fill(0, 0, W(), H(), {0, 0, 0}, 0.6f * a);
  if (fb) {
    SDL_SetTextureBlendMode(fb, SDL_BLENDMODE_ADD);
    Sprite(fb, 0, 0, W(), H(), {1, 1, 1}, a);
  }
  Fill(0, 0, W(), H(), {80 / 255.f, 1, 120 / 255.f}, 0.06f * pulse * a, SDL_BLENDMODE_ADD);   // the beat flashes
}

void BgMore(float t, float a) {   // Llamasoft's rainbow: soft colour bands drifting up, slowly cycling
  Fill(0, 0, W(), H(), {5 / 255.f, 3 / 255.f, 14 / 255.f}, 0.85f * a);
  const int n = 30;
  const float bh = H() / n;
  for (int i = 0; i <= n; ++i) {
    const float y = std::fmod(i * bh - t * H() * 0.015f + 100 * (H() + bh), H() + bh) - bh;
    const float al = 0.09f * a * (0.6f + 0.4f * std::sin(t * 0.7f + i * 0.45f));
    Fill(0, y, W(), bh * 0.5f, Hsl(i * 360.f / n + t * 18, 1, 0.5f), al, SDL_BLENDMODE_ADD);
  }
}

void Background(size_t i, float t, float a, float dt) {
  const std::string& id = g_entries[i].id;
  if (id == "t81") BgT81(t, a);
  else if (id == "t2k") BgT2K(t, a);
  else if (id == "t3k") BgT3K(g_art[i], t, a, dt);
  else if (id == "sg") BgSG(g_art[i], t, a, dt);
  else if (id == "txk") BgTxK(t, a);
  else if (id == "more") BgMore(t, a);
  else BgT4K(t, a);
}

// The logo, animated in the game's flavour, fitted in bw x bh with its top at y, centred on x; returns its height.
float Logo(size_t i, float x, float y, float bw, float bh, float t, float alpha) {
  const Art& art = g_art[i];
  const Entry& e = g_entries[i];
  const Style& st = StyleOf(e.id);
  if (!art.logo) {   // no logo: the title in the system font, large
    Text(e.title, x, y + bh * 0.25f, bh / 12, st.accent, alpha, 0, 1.5f);
    return bh * 0.85f;
  }
  // (Tempest 1981's slanted logo fills less of its box: a little larger)
  const float boost = e.id == "t81" ? 1.35f : 1.f;
  const float k = std::min(bw / art.logo_w, bh * boost / art.logo_h);
  const float w = art.logo_w * k, h = art.logo_h * k;
  const float beat = t * 126.f / 60.f, pulse = std::pow(1 - (beat - std::floor(beat)), 3.f);
  float rot = 0, sc = 1, a = 1, glow = 0, dy = 0;
  Col gc = st.accent;
  if (e.id == "t81") glow = 0.35f + 0.1f * std::sin(t * 1.2f);
  else if (e.id == "t2k") { dy = std::sin(t * 2.4f) * bh * 0.04f; rot = std::sin(t * 1.2f) * 0.02f; glow = 0.4f; gc = {1, 0.47f, 0}; }
  else if (e.id == "t3k") { glow = 0.6f + 0.4f * std::sin(t * 3); gc = Hsl(t * 60, 1, 0.6f); }
  else if (e.id == "sg") { sc = 1 + pulse * 0.07f; rot = std::sin(t * 3.1f) * 0.035f; dy = -pulse * bh * 0.05f;
                           glow = 0.5f + pulse; gc = Hsl(t * 120, 1, 0.6f); }
  else if (e.id == "txk") { a = 0.88f + 0.12f * std::sin(t * 13) * std::sin(t * 7.3f); glow = 0.6f; }
  else { glow = 0.45f + 0.2f * std::sin(t * 2); sc = 1 + 0.01f * std::sin(t * 2); }
  const float cx = x, cy = y + h / 2 + dy, dw = w * sc, dh = h * sc;
  const double deg = rot * 180.0 / kPi;
  if (art.logo_glow && glow > 0) {   // the glow texture is padded: 24 px around a 160 px wide logo
    const float px = dw * 24 / 160.f * 1.6f, py = px;
    Sprite(art.logo_glow, cx - dw / 2 - px, cy - dh / 2 - py, dw + 2 * px, dh + 2 * py, gc, std::min(1.f, glow) * alpha * a, deg);
  }
  Sprite(art.logo, cx - dw / 2, cy - dh / 2, dw, dh, {1, 1, 1}, alpha * a, deg);
  return h;
}

}  // namespace

bool Init(SDL_Renderer* ren, const fs::path& art_dir) {
  g_ren = ren;
  g_dir = art_dir;
  if (!ren) return false;
  g_dot = AlphaTexture(64, 64, [](float x, float y) {
    const float d2 = (x - 0.5f) * (x - 0.5f) + (y - 0.5f) * (y - 0.5f);
    return std::exp(-d2 * 18.f);
  });
  g_beam = AlphaTexture(4, 64, [](float, float y) { return std::exp(-(y - 0.5f) * (y - 0.5f) * 22.f); });
  g_vignette = AlphaTexture(128, 128, [](float x, float y) {
    const float d = std::sqrt((x - 0.5f) * (x - 0.5f) + (y - 0.5f) * (y - 0.5f)) * 2;
    return 0.8f * std::clamp((d - 0.45f) / 0.75f, 0.f, 1.f);
  }, {0, 0, 0}, SDL_BLENDMODE_BLEND);
  g_radial = AlphaTexture(128, 128, [](float x, float y) {
    const float d = std::sqrt((x - 0.5f) * (x - 0.5f) + (y - 0.5f) * (y - 0.5f)) * 2;
    return std::clamp(1 - d, 0.f, 1.f);
  }, {1, 1, 1}, SDL_BLENDMODE_BLEND);
  return true;
}

void Shutdown() {
  for (Art& a : g_art) {
    for (SDL_Texture* t : {a.cover, a.blur, a.logo, a.logo_glow, a.fb[0], a.fb[1]})
      if (t) SDL_DestroyTexture(t);
  }
  g_art.clear();
  for (SDL_Texture* t : g_more_art)
    if (t) SDL_DestroyTexture(t);
  g_more_art.clear();
  for (SDL_Texture* t : {g_dot, g_beam, g_vignette, g_radial})
    if (t) SDL_DestroyTexture(t);
  g_dot = g_beam = g_vignette = g_radial = nullptr;
  pixfont::Shutdown();
}

void SetEntries(const std::vector<Entry>& entries) {
  const bool reload = g_art.size() != entries.size() ||
                      !std::equal(entries.begin(), entries.end(), g_entries.begin(), g_entries.end(),
                                  [](const Entry& a, const Entry& b) { return a.id == b.id; });
  g_entries = entries;
  if (!reload) return;
  for (Art& a : g_art)
    for (SDL_Texture* t : {a.cover, a.blur, a.logo, a.logo_glow, a.fb[0], a.fb[1]})
      if (t) SDL_DestroyTexture(t);
  g_art.assign(entries.size(), Art{});
  for (size_t i = 0; i < entries.size(); ++i) LoadArt(i);
  g_fades.assign(entries.size(), 0.f);
}

void SetExtraHint(const std::string& hint) { g_extra_hint = hint; }
void SetMotion(float k) { g_motion = std::clamp(k, 0.f, 1.f); }

void StartWarp(int i) {
  g_warp = i;
  g_warp_t = 0.0;
}
bool Warping() { return g_warp >= 0; }

void Draw(SDL_Renderer* ren, int sel, double dt_in, const std::string& message) {
  if (g_entries.empty()) return;
  SDL_GetRenderOutputSize(ren, &g_w, &g_h);
  const float dt = float(std::min(dt_in, 0.05));
  const int n = int(g_entries.size());
  sel = std::clamp(sel, 0, n - 1);
  static bool first = true;
  if (first) {   // start on the selected one, no scroll in
    g_pos = float(sel);
    g_fades[sel] = 1.f;
    first = false;
  }
  g_time += dt * g_motion;
  // eased scroll
  g_pos += (sel - g_pos) * (1 - std::pow(0.0005f, dt));
  for (int i = 0; i < n; ++i) g_fades[i] += ((i == sel ? 1.f : 0.f) - g_fades[i]) * (1 - std::pow(0.0001f, dt));

  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
  SDL_RenderClear(ren);
  const float w = W(), h = H();

  // backdrop: the blurred covers, crossfaded, then each game's own motion
  for (int i = 0; i < n; ++i) {
    const float a = g_fades[i];
    if (a < 0.01f) {
      g_art[i].fb_live = false;
      continue;
    }
    if (g_art[i].blur) {
      const float s = std::max(w / 45.f, h / 60.f);
      Sprite(g_art[i].blur, (w - 45 * s) / 2, (h - 60 * s) / 2, 45 * s, 60 * s, {1, 1, 1}, a * 0.55f);
    }
    const float m = StyleOf(g_entries[i].id).motion;
    Background(size_t(i), float(g_time) * (0.25f + m), a * (0.5f + 0.5f * m), dt * g_motion);
  }
  Sprite(g_vignette, 0, 0, w, h, {1, 1, 1}, 1.f);

  // panels, the far ones first
  const float ph = h * 0.54f, pw = ph * 0.75f, gap = pw * 0.18f, cy = h * 0.4f;
  std::vector<int> order(n);
  for (int i = 0; i < n; ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [](int a, int b) { return std::fabs(a - g_pos) > std::fabs(b - g_pos); });
  for (int i : order) {
    const Art& art = g_art[i];
    const Entry& e = g_entries[i];
    const Col ac = StyleOf(e.id).accent;
    const float d = i - g_pos, ad = std::fabs(d), sc = 1 - std::min(ad, 2.f) * 0.2f;
    const float pwi = pw * sc, phi = ph * sc;
    const float x = w / 2 + d * (pw * 0.78f + gap) - pwi / 2 + (d > 0 ? 1 : d < 0 ? -1 : 0) * std::min(ad, 1.f) * pw * 0.08f;
    const float y = cy - phi / 2;
    if (x > w || x + pwi < 0) continue;
    const float focus = std::max(0.f, 1 - ad);
    Fill(x, y, pwi, phi, {0, 0, 0}, 1);
    if (art.cover) Sprite(art.cover, x, y, pwi, phi, {1, 1, 1}, 1);
    else Fill(x, y, pwi, phi, {0.13f, 0.13f, 0.13f}, 1);
    if (!e.ready) Fill(x, y, pwi, phi, {0, 0, 0}, 0.45f);   // can't start: greyed
    Fill(x, y, pwi, phi, {0, 0, 0}, std::min(0.65f, ad * 0.45f));
    const float lw = std::max(1.f, h / 600) * (1 + focus);
    const float ea = 0.35f + 0.6f * focus;
    Glow(x, y, x + pwi, y, lw, ac, ea, 3 + 5 * focus);
    Glow(x + pwi, y, x + pwi, y + phi, lw, ac, ea, 3 + 5 * focus);
    Glow(x + pwi, y + phi, x, y + phi, lw, ac, ea, 3 + 5 * focus);
    Glow(x, y + phi, x, y, lw, ac, ea, 3 + 5 * focus);
    // reflection: the cover's bottom third mirrored below it, fading out (vertex alpha: no dark box)
    if (art.cover) {
      const float ra = 0.2f * (0.4f + 0.6f * focus), rh = phi * 0.36f, v0 = 1.f - 0.36f;
      const SDL_FColor top{1, 1, 1, ra}, bot{1, 1, 1, 0};
      const SDL_Vertex v[4] = {{{x, y + phi}, top, {0, 1}}, {{x + pwi, y + phi}, top, {1, 1}},
                               {{x, y + phi + rh}, bot, {0, v0}}, {{x + pwi, y + phi + rh}, bot, {1, v0}}};
      const int idx[6] = {0, 1, 2, 1, 3, 2};
      SDL_SetTextureColorModFloat(art.cover, 1, 1, 1);
      SDL_SetTextureAlphaModFloat(art.cover, 1);
      SDL_RenderGeometry(g_ren, art.cover, v, 4, idx, 6);
    }
  }

  // the title and the info of the game in focus (fades in as the scroll settles)
  const Entry& e = g_entries[sel];
  const Col ac = StyleOf(e.id).accent;
  const float show = std::max(0.f, 1 - std::fabs(sel - g_pos) * 2.5f);
  const float m = StyleOf(e.id).motion;
  const float logo_top = cy + ph / 2 + h * 0.025f;
  // with setup help to show (a Steam game not set up) the logo is smaller and the maker line goes: room for the help
  const bool helping = !e.help.empty();
  const float logo_box = helping ? h * 0.06f : h * 0.11f;
  const float logo_h = Logo(size_t(sel), w / 2, logo_top, pw * 1.7f, logo_box, float(g_time) * (0.4f + m), show);
  const float u = std::max(1.5f, h / 330.f);
  float ty = logo_top + std::max(logo_h, logo_box) + h * 0.02f;   // under the logo, however tall it came out
  Text(e.line1, w / 2, ty, u, ac, show);
  ty += u * 11;
  if (!helping) {
    Text(e.line2, w / 2, ty, u * 0.8f, {180 / 255.f, 180 / 255.f, 200 / 255.f}, show * 0.8f);
    ty += u * 10;
  }
  if (!e.variant.empty()) {
    Text("^ " + e.variant + " v", w / 2, ty, u, {1, 1, 1}, show);
    ty += u * 11;
  }
  if (!e.status.empty()) {
    Text(e.status, w / 2, ty, u * 0.9f, {1, 160 / 255.f, 60 / 255.f}, show);
    ty += u * 10;
  }
  // the setup help: each line scaled to fit the width, the block kept clear of the hint row at the bottom
  if (!e.help.empty()) {
    const float hu = u * 0.7f;
    ty = std::min(ty, h * 0.94f - float(e.help.size()) * hu * 11.f / 0.7f * 0.7f - u * 2);
    for (const std::string& line : e.help) {
      const float fit = std::min(hu, w * 0.94f / std::max(1.f, float(line.size()) * 6.f));
      Text(line, w / 2, ty, fit, {1, 190 / 255.f, 110 / 255.f}, show, 0, 0.6f);
      ty += hu * 11.f;
    }
  }
  if (!message.empty()) Text(message, w / 2, ty + u * 2, u * 0.8f, {1, 1, 0.3f}, 1);

  // header, position dots, hints
  // the header, with the owner's jokes over the two games that aren't called Tempest (crossfading as the scroll
  // settles on them)
  const char* header = e.id == "sg" ? "IT'S NOT TEMPEST!!" : e.id == "txk" ? "IT'S LEGALLY NOT TEMPEST!" : nullptr;
  if (header) {
    Text("TEMPEST COLLECTION", w / 2, h * 0.035f, u * 1.2f, {1, 80 / 255.f, 1}, 1 - show);
    Text(header, w / 2, h * 0.035f, u * 1.2f, {1, 80 / 255.f, 1}, show);
  } else {
    Text("TEMPEST COLLECTION", w / 2, h * 0.035f, u * 1.2f, {1, 80 / 255.f, 1}, 1);
  }
  for (int i = 0; i < n; ++i) {
    const float s = i == sel ? u * 2.4f : u * 1.4f;
    Fill(w / 2 + (i - (n - 1) / 2.f) * u * 7 - s / 2, h * 0.085f - s / 2, s, s,
         i == sel ? StyleOf(g_entries[i].id).accent : Col{1, 1, 1}, i == sel ? 1.f : 0.25f);
  }
  std::string hints = e.variant.empty() ? "< >  CHOOSE     A  " + e.action
                                         : "< >  CHOOSE     ^ v  FLAVOUR     A  " + e.action;
  if (!g_extra_hint.empty()) hints += "     " + g_extra_hint;
  hints += "     BACK  SETTINGS     B  QUIT";
  Text(hints,
       w / 2, h - h * 0.04f, u * 0.8f, {120 / 255.f, 200 / 255.f, 1}, 0.8f);

  // the start warp: beams rushing out of the panel, then white
  if (g_warp >= 0) {
    g_warp_t += dt;
    const float k = float(std::min(1.0, g_warp_t / 0.7));
    const Col c = StyleOf(g_entries[g_warp].id).accent;
    for (int i = 0; i < 40; ++i) {
      const float q = i / 40.f * 2 * kPi, r0 = h * 0.4f * k, r1 = r0 + h * k * 1.2f;
      Beam(w / 2 + std::cos(q) * r0, cy + std::sin(q) * r0, w / 2 + std::cos(q) * r1, cy + std::sin(q) * r1, 4 + k * 14, c,
           0.7f * k);
    }
    Fill(0, 0, w, h, {1, 1, 1}, std::max(0.f, (k - 0.55f) / 0.45f));
    if (g_warp_t >= 0.75) g_warp = -1;
  }
}

void DrawPrompt(SDL_Renderer* ren, int i, const std::string& title, const std::vector<std::string>& lines,
                const std::string& buttons, float progress) {
  SDL_GetRenderOutputSize(ren, &g_w, &g_h);
  const float w = W(), h = H(), u = std::max(1.5f, h / 330.f);
  const Col ac = i >= 0 && i < int(g_entries.size()) ? StyleOf(g_entries[i].id).accent : Col{1, 80 / 255.f, 1};
  Fill(0, 0, w, h, {0, 0, 0}, 0.72f);
  const float pw = std::min(w * 0.9f, std::max(w * 0.6f, u * 6 * 62)), ph = h * 0.46f;
  const float x = (w - pw) / 2, y = (h - ph) / 2;
  Fill(x, y, pw, ph, {0.02f, 0.02f, 0.05f}, 0.94f);
  const float lw = std::max(1.f, h / 600) * 1.5f;
  Glow(x, y, x + pw, y, lw, ac, 0.9f, 6);
  Glow(x + pw, y, x + pw, y + ph, lw, ac, 0.9f, 6);
  Glow(x + pw, y + ph, x, y + ph, lw, ac, 0.9f, 6);
  Glow(x, y + ph, x, y, lw, ac, 0.9f, 6);
  // each line as big as fits the panel (up to the normal size)
  auto fit = [&](const std::string& s, float size) {
    return std::min(size, (pw - u * 16) / std::max(1.f, float(s.size()) * 6.f));
  };
  float ty = y + ph * 0.12f;
  Text(title, w / 2, ty, fit(title, u * 1.3f), ac, 1, 0, 1.2f);
  ty += u * 1.3f * 7 + u * 9;
  for (const std::string& line : lines) {
    Text(line, w / 2, ty, fit(line, u * 0.85f), {0.88f, 0.9f, 1.f}, 1, 0, 0.4f);
    ty += u * 0.85f * 11;
  }
  if (progress >= 0.f) {   // a bar with a glowing edge
    const float bw = pw * 0.7f, bh = u * 5, bx = (w - bw) / 2, by = y + ph * 0.66f;
    Fill(bx, by, bw, bh, {1, 1, 1}, 0.08f);
    Fill(bx, by, bw * std::clamp(progress, 0.f, 1.f), bh, ac, 0.85f);
    Glow(bx + bw * std::clamp(progress, 0.f, 1.f), by - u, bx + bw * std::clamp(progress, 0.f, 1.f), by + bh + u,
         lw, ac, 1.f, 5);
  }
  Text(buttons, w / 2, y + ph - u * 16, fit(buttons, u * 0.9f), {120 / 255.f, 200 / 255.f, 1}, 1);
}

void SetMore(const std::vector<MoreItem>& items, int first_ours) {
  for (SDL_Texture* t : g_more_art)
    if (t) SDL_DestroyTexture(t);
  g_more = items;
  g_more_ours = first_ours;
  g_more_art.assign(items.size(), nullptr);
  for (size_t i = 0; i < items.size(); ++i)
    if (SDL_Surface* s = LoadRGBA(g_dir / ("more_" + items[i].key + ".png"))) {
      g_more_art[i] = MakeTexture(s, SDL_BLENDMODE_BLEND);
      SDL_DestroySurface(s);
    }
  g_more_snap = true;
}

void DrawMore(SDL_Renderer* ren, int sel, double dt_in, const std::string& message) {
  if (g_more.empty()) return;
  SDL_GetRenderOutputSize(ren, &g_w, &g_h);
  const float dt = float(std::min(dt_in, 0.05));
  const float w = W(), h = H(), u = std::max(1.5f, h / 330.f);
  const Col ac = StyleOf("more").accent, magenta{1, 80 / 255.f, 1}, dim{0.7f, 0.72f, 0.8f};
  const int n = int(g_more.size());
  sel = std::clamp(sel, 0, n - 1);
  Fill(0, 0, w, h, {3 / 255.f, 2 / 255.f, 10 / 255.f}, 0.97f);
  BgMore(float(g_time) * 0.6f, 0.6f);
  auto fit = [&](const std::string& s, float size, float width) {
    return std::min(size, width / std::max(1.f, float(s.size()) * 6.f));
  };
  // the heading
  const std::string h1 = "IF YOU LIKE THIS, YOU SHOULD TRY THESE", h2 = "OTHER EXCELLENT LLAMASOFT GAMES:";
  const float hu = fit(h1, u * 1.2f, w * 0.94f);
  Text(h1, w / 2, h * 0.04f, hu, magenta, 1, 0, 1.2f);
  Text(h2, w / 2, h * 0.04f + hu * 11, hu, magenta, 1, 0, 1.2f);
  // the rows: the wide picture, then the title, its line and (selected) what A does
  const float top = h * 0.04f + hu * 24, bottom = h * 0.9f, view = bottom - top;
  const float rh = std::min(h * 0.17f, view / 3.2f), ah = rh * 0.84f, aw = ah * 920.f / 430.f;
  const float cw = std::min(w * 0.94f, aw + u * 6 * 36), x0 = (w - cw) / 2, tx = x0 + aw + u * 8, tw = cw - aw - u * 10;
  const float label_h = u * 18;
  auto row_y = [&](int i) { return i * rh + (g_more_ours >= 0 && i >= g_more_ours ? label_h : 0.f); };
  const float total = row_y(n - 1) + rh;
  const float target = std::clamp(row_y(sel) + rh / 2 - view / 2, 0.f, std::max(0.f, total - view));
  if (g_more_snap) g_more_scroll = target, g_more_snap = false;
  g_more_scroll += (target - g_more_scroll) * (1 - std::pow(0.0005f, dt));
  const SDL_Rect clip{0, int(top), g_w, int(view)};
  SDL_SetRenderClipRect(ren, &clip);
  for (int i = 0; i < n; ++i) {
    const float y = top + row_y(i) - g_more_scroll;
    if (g_more_ours >= 0 && i == g_more_ours) {
      const std::string lab = "OUR OTHER REMASTERED COLLECTIONS - FREE";
      Text(lab, w / 2, y - label_h + u * 5, fit(lab, u * 0.95f, cw), {120 / 255.f, 200 / 255.f, 1}, 1, 0, 0.8f);
    }
    if (y > bottom || y + rh < top) continue;
    const MoreItem& m = g_more[i];
    const bool on = i == sel;
    const float ay = y + (rh - ah) / 2;
    if (on) {   // the selected row: a lit band with glowing edges
      Fill(x0 - u * 3, y + u, cw + u * 6, rh - u * 2, ac, 0.10f);
      const float lw = std::max(1.f, h / 600) * 1.5f, y0 = y + u, y1 = y + rh - u;
      Glow(x0 - u * 3, y0, x0 + cw + u * 3, y0, lw, ac, 0.9f, 6);
      Glow(x0 - u * 3, y1, x0 + cw + u * 3, y1, lw, ac, 0.9f, 6);
    }
    if (g_more_art[i]) Sprite(g_more_art[i], x0, ay, aw, ah, {1, 1, 1}, on ? 1.f : 0.7f);
    else Fill(x0, ay, aw, ah, {0.15f, 0.15f, 0.2f}, 1);
    const float tu = fit(m.title, u * 1.3f, tw);
    float ty = y + rh * 0.24f;
    Text(m.title, tx, ty, tu, on ? ac : Col{1, 1, 1}, on ? 1.f : 0.8f, -1, on ? 1.2f : 0.5f);
    ty += tu * 7 + u * 6;
    Text(m.line, tx, ty, fit(m.line, u * 0.85f, tw), dim, on ? 1.f : 0.7f, -1, 0.3f);
    ty += u * 0.85f * 7 + u * 6;
    if (on) Text("A  " + m.open, tx, ty, fit("A  " + m.open, u * 0.85f, tw), {120 / 255.f, 200 / 255.f, 1}, 1, -1, 0.5f);
  }
  SDL_SetRenderClipRect(ren, nullptr);
  if (!message.empty()) Text(message, w / 2, h - h * 0.075f, u * 0.8f, {1, 1, 0.3f}, 1);
  Text("^ v  CHOOSE     A  OPEN THE PAGE     B  BACK", w / 2, h - h * 0.04f, u * 0.8f, {120 / 255.f, 200 / 255.f, 1}, 0.8f);
}

void DrawStarting(SDL_Renderer* ren, int i, const std::string& text) {
  SDL_GetRenderOutputSize(ren, &g_w, &g_h);
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
  SDL_RenderClear(ren);
  const float w = W(), h = H(), u = std::max(1.5f, h / 330.f);
  if (i >= 0 && i < int(g_entries.size())) {
    Logo(size_t(i), w / 2, h * 0.36f, w * 0.5f, h * 0.16f, 0.f, 1.f);
    Text(text, w / 2, h * 0.6f, u, StyleOf(g_entries[i].id).accent, 1);
  } else {
    Text(text, w / 2, h / 2, u, {1, 1, 0.3f}, 1);
  }
}

}  // namespace lview
