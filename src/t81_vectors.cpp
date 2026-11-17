// t81_vectors.cpp - see t81_vectors.h.
#include "t81_vectors.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "settings.h"

namespace t81_vectors {
namespace {

struct Line { float x0, y0, x1, y1, mame_w, a, r, g, b; };
struct Rect { float x0, y0, x1, y1; };

// Monitor-space boxes (0..1, measured from the line lists: logs/t81/look, 2026-11-17).
constexpr Rect kWhole{0.f, 0.f, 1.f, 1.f};
constexpr Rect kUsed{0.08f, 0.13f, 0.885f, 0.95f};       // everything the game draws (attract, menus, play)
constexpr Rect kWeb{0.175f, 0.218f, 0.825f, 0.848f};     // play, until a web is seen: level 1's web + the claw
constexpr float kWebMargin = 0.06f;                      // the claw and its shots stand out past the web's rim
constexpr int kStaticFrames = 10;                        // a line this many frames unchanged = part of the web
// Play HUD: player score + lives (left), high score + initials + level (centre). Drawn first in every play frame.
constexpr Rect kHudLeft{0.f, 0.12f, 0.37f, 0.2225f};
constexpr Rect kHudRight{0.37f, 0.12f, 0.66f, 0.2225f};
constexpr float kHudLeftAnchorX = 0.08f, kHudRightAnchorX = 0.61f, kHudAnchorY = 0.135f;

std::vector<Line> g_lines;
std::vector<uint8_t> g_hud;          // per line: 0 world, 1 left HUD, 2 right HUD
int g_play_run = 0, g_other_run = 0;
bool g_play = false;
// Lines unchanged for kStaticFrames frames (key = quantised end points -> frames seen in a row).
std::unordered_map<uint64_t, int> g_seen, g_seen_next;
Rect g_web = kWeb;                   // the current level's web (+ margin), held through transitions

// Eased framing state.
Rect g_view = kUsed;
float g_hud_w = 0.f;                 // 0 = HUD where the game put it, 1 = in the corners
bool g_view_set = false;

SDL_Texture* g_core_tex = nullptr;
SDL_Texture* g_glow_tex = nullptr;
SDL_Renderer* g_tex_owner = nullptr;
SDL_BlendMode g_add = SDL_BLENDMODE_INVALID, g_max = SDL_BLENDMODE_INVALID;
bool g_max_ok = true;

bool Inside(const Line& l, const Rect& r) {
  return std::min(l.x0, l.x1) >= r.x0 && std::max(l.x0, l.x1) <= r.x1 && std::min(l.y0, l.y1) >= r.y0 &&
         std::max(l.y0, l.y1) <= r.y1;
}

// A radial beam profile, RGB premultiplied (the blend modes add / max the colour straight in).
SDL_Texture* MakeProfile(SDL_Renderer* ren, bool glow) {
  const int n = 64;
  std::vector<uint8_t> px(size_t(n) * n * 4);
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      const float dx = (x + 0.5f) / n * 2 - 1, dy = (y + 0.5f) / n * 2 - 1;
      const float d = std::sqrt(dx * dx + dy * dy);
      float v;
      if (glow) {
        v = std::exp(-d * d * 6.f) - std::exp(-6.f);   // gaussian, 0 at the edge
      } else {
        const float t = std::clamp((d - 0.45f) / 0.55f, 0.f, 1.f);   // flat centre, smooth edge
        v = 1.f - t * t * (3 - 2 * t);
      }
      const uint8_t c = uint8_t(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
      uint8_t* p = &px[(size_t(y) * n + x) * 4];
      p[0] = p[1] = p[2] = p[3] = c;
    }
  SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, n, n);
  if (!t) return nullptr;
  SDL_UpdateTexture(t, nullptr, px.data(), n * 4);
  SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
  return t;
}

void EnsureTextures(SDL_Renderer* ren) {
  if (g_tex_owner == ren && g_core_tex) return;
  Shutdown();
  g_tex_owner = ren;
  g_core_tex = MakeProfile(ren, false);
  g_glow_tex = MakeProfile(ren, true);
  g_add = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                     SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
  g_max = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MAXIMUM,
                                     SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MAXIMUM);
  if (g_glow_tex && !SDL_SetTextureBlendMode(g_glow_tex, g_add)) SDL_SetTextureBlendMode(g_glow_tex, SDL_BLENDMODE_ADD);
  g_max_ok = g_core_tex && SDL_SetTextureBlendMode(g_core_tex, g_max);
  if (g_core_tex && !g_max_ok && !SDL_SetTextureBlendMode(g_core_tex, g_add))
    SDL_SetTextureBlendMode(g_core_tex, SDL_BLENDMODE_ADD);
}

// A beam from a to b (window pixels), half width h: start cap, body, end cap over the radial profile.
void AddBeam(std::vector<SDL_Vertex>& v, std::vector<int>& idx, float ax, float ay, float bx, float by, float h,
             SDL_FColor c) {
  float dx = bx - ax, dy = by - ay;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len > 1e-4f) { dx /= len; dy /= len; } else { dx = 1.f; dy = 0.f; }
  const float nx = -dy * h, ny = dx * h, ex = dx * h, ey = dy * h;
  const int base = int(v.size());
  auto put = [&](float x, float y, float u, float t) { v.push_back({{x, y}, c, {u, t}}); };
  put(ax - ex + nx, ay - ey + ny, 0.f, 0.f);
  put(ax - ex - nx, ay - ey - ny, 0.f, 1.f);
  put(ax + nx, ay + ny, 0.5f, 0.f);
  put(ax - nx, ay - ny, 0.5f, 1.f);
  put(bx + nx, by + ny, 0.5f, 0.f);
  put(bx - nx, by - ny, 0.5f, 1.f);
  put(bx + ex + nx, by + ey + ny, 1.f, 0.f);
  put(bx + ex - nx, by + ey - ny, 1.f, 1.f);
  for (int q = 0; q < 3; ++q) {
    const int i = base + q * 2;
    idx.insert(idx.end(), {i, i + 1, i + 2, i + 1, i + 3, i + 2});
  }
}

Rect Lerp(const Rect& a, const Rect& b, float t) {
  return {a.x0 + (b.x0 - a.x0) * t, a.y0 + (b.y0 - a.y0) * t, a.x1 + (b.x1 - a.x1) * t, a.y1 + (b.y1 - a.y1) * t};
}

}  // namespace

void NewFrame(const Frame& f) {
  g_lines.assign(reinterpret_cast<const Line*>(f.lines), reinterpret_cast<const Line*>(f.lines) + f.count);
  // HUD lines: the leading run of the frame inside the HUD boxes (the game draws its HUD first in play).
  g_hud.assign(g_lines.size(), 0);
  for (size_t i = 0; i < g_lines.size(); ++i) {
    if (Inside(g_lines[i], kHudLeft)) g_hud[i] = 1;
    else if (Inside(g_lines[i], kHudRight)) g_hud[i] = 2;
    else break;
  }
  // Play: the score / lives HUD is drawn and there is no attract text at the bottom (the credits / bonus / copyright
  // lines: 25+ short strokes below y 0.9; the claw and shots there are a few). 10 frames in a row to start, 30
  // without to end (a level's fly-through or a death must not drop it).
  bool hud = false;
  int bottom_strokes = 0;
  for (const Line& l : g_lines) {
    if (Inside(l, kHudLeft)) hud = true;
    if (std::min(l.y0, l.y1) > 0.9f && std::fabs(l.x1 - l.x0) + std::fabs(l.y1 - l.y0) < 0.04f) ++bottom_strokes;
  }
  if (hud && bottom_strokes < 25) { ++g_play_run; g_other_run = 0; } else { ++g_other_run; g_play_run = 0; }
  if (!g_play && g_play_run >= 10) g_play = true;
  if (g_play && g_other_run >= 30) g_play = false;
  // The web: the bounding box of the world lines that have not moved for kStaticFrames frames (enemies, shots and
  // the claw move; webs fly in and out between levels, so then nothing is static and the last box is held). Small
  // static sets (the "SUPERZAPPER RECHARGE" text in the warp) don't count.
  g_seen_next.clear();
  float x0 = 1, y0 = 1, x1 = 0, y1 = 0;
  int n_static = 0;
  for (size_t i = 0; i < g_lines.size(); ++i) {
    if (g_hud[i]) continue;
    const Line& l = g_lines[i];
    auto q = [](float v) { return uint64_t(std::clamp(v, 0.f, 1.f) * 65535.f + 0.5f); };
    const uint64_t key = q(l.x0) | q(l.y0) << 16 | q(l.x1) << 32 | q(l.y1) << 48;
    auto it = g_seen.find(key);
    const int n = (it == g_seen.end() ? 0 : it->second) + 1;
    g_seen_next[key] = n;
    if (n >= kStaticFrames) {
      ++n_static;
      x0 = std::min({x0, l.x0, l.x1}); x1 = std::max({x1, l.x0, l.x1});
      y0 = std::min({y0, l.y0, l.y1}); y1 = std::max({y1, l.y0, l.y1});
    }
  }
  g_seen.swap(g_seen_next);
  if (g_play && n_static >= 30 && y1 - y0 >= 0.3f && x1 - x0 >= 0.3f)
    g_web = {std::max(x0 - kWebMargin, kUsed.x0), std::max(y0 - kWebMargin, 0.f), std::min(x1 + kWebMargin, kUsed.x1),
             std::min(y1 + kWebMargin, kUsed.y1)};
}

void Draw(SDL_Renderer* ren, int ww, int wh, float dt_s) {
  if (ww <= 0 || wh <= 0) return;
  EnsureTextures(ren);
  if (!g_core_tex || !g_glow_tex) return;
  const std::string framing = settings::Str("framing", "fill");
  // Physical width / height of one monitor unit: the cabinet's 3:4 tube, or the game's own square units (round webs;
  // Tempest's visible area is 580 x 570).
  const float aspect = settings::Str("aspect", "round") == "round" ? 580.f / 570.f : 0.75f;
  const bool wide = framing == "wide" && g_play;

  // Wide: the web fills the window when there is room beside it for the HUD (at 60% of the web's size or more);
  // else the web plus the HUD band above it, the HUD where the game puts it.
  Rect target = framing == "arcade" ? kWhole : kUsed;
  float hud_target = 0.f;
  if (wide) {
    const Rect& w = g_web;
    const float s_web = std::min(wh / (w.y1 - w.y0), ww / ((w.x1 - w.x0) * aspect));
    const float room = (ww - (w.x1 - w.x0) * aspect * s_web) / 2;
    const float hud_px = (kHudLeft.x1 - kHudLeftAnchorX) * aspect * s_web;
    if (room >= hud_px * 0.6f) { target = w; hud_target = 1.f; }
    else target = {std::min(w.x0, kUsed.x0), kUsed.y0, std::max(w.x1, kUsed.x1), w.y1};
  }
  const float k = g_view_set ? 1.f - std::exp(-std::max(dt_s, 0.f) / 0.25f) : 1.f;
  g_view = Lerp(g_view, target, k);
  g_hud_w += (hud_target - g_hud_w) * k;
  g_view_set = true;

  const float vw = g_view.x1 - g_view.x0, vh = g_view.y1 - g_view.y0;
  const float s = std::min(wh / vh, ww / (vw * aspect));   // pixels per monitor unit (vertical)
  const float ox = (ww - vw * aspect * s) / 2 - g_view.x0 * aspect * s, oy = (wh - vh * s) / 2 - g_view.y0 * s;
  // The HUD in the corners: the same size as the web, made smaller if the room beside it is short.
  const float web_px = (g_view.x1 - g_view.x0) * aspect * s;
  const float margin = wh * 0.03f;
  const float room = std::max((ww - web_px) / 2 - margin * 2, 1.f);
  const float hs = std::min(s, room / ((kHudLeft.x1 - kHudLeftAnchorX) * aspect));

  const float unit = wh / 1080.f;
  const float line_w = std::max(float(settings::Num("line_width", 2.5)) * unit, 1.2f);
  const float glow = float(settings::Num("glow", 1.0));
  const float glow_size = float(settings::Num("glow_size", 6.0));
  const float bright = float(settings::Num("brightness", 1.2));
  const float hot = float(settings::Num("hot_core", 0.2));
  const float by_int = float(settings::Num("width_by_intensity", 1.0));

  std::vector<SDL_Vertex> core, halo;
  std::vector<int> core_i, halo_i;
  core.reserve(g_lines.size() * 8);
  core_i.reserve(g_lines.size() * 18);
  if (glow > 0) { halo.reserve(g_lines.size() * 8); halo_i.reserve(g_lines.size() * 18); }
  for (size_t i = 0; i < g_lines.size(); ++i) {
    const Line& l = g_lines[i];
    auto world = [&](float u, float v, float& x, float& y) { x = ox + u * aspect * s; y = oy + v * s; };
    float ax, ay, bx, by;
    world(l.x0, l.y0, ax, ay);
    world(l.x1, l.y1, bx, by);
    float scale = 1.f;
    if (g_hud[i] && g_hud_w > 0.001f) {   // slide into its corner
      const bool left = g_hud[i] == 1;
      auto corner = [&](float u, float v, float& x, float& y) {
        x = left ? margin + (u - kHudLeftAnchorX) * aspect * hs : ww - margin - (kHudRightAnchorX - u) * aspect * hs;
        y = margin + (v - kHudAnchorY) * hs;
      };
      float cax, cay, cbx, cby;
      corner(l.x0, l.y0, cax, cay);
      corner(l.x1, l.y1, cbx, cby);
      ax += (cax - ax) * g_hud_w; ay += (cay - ay) * g_hud_w;
      bx += (cbx - bx) * g_hud_w; by += (cby - by) * g_hud_w;
      scale = 1.f + (std::min(hs / s, 1.f) - 1.f) * g_hud_w;
    }
    // Beam width follows the beam's intensity (Tempest uses 3 levels around 0.75).
    const float wmul = std::pow(std::max(l.a, 0.05f) / 0.75f, 2.f * by_int);
    const float h = std::max(line_w * wmul * std::sqrt(scale), 1.2f) / 2;
    const float in = l.a * bright;
    const SDL_FColor c{l.r * in, l.g * in, l.b * in, 1.f};
    // The core runs a little white-hot (a real tube's bright line centre), the glow keeps the pure colour.
    const SDL_FColor cc{c.r + (in - c.r) * hot, c.g + (in - c.g) * hot, c.b + (in - c.b) * hot, 1.f};
    AddBeam(core, core_i, ax, ay, bx, by, h, cc);
    if (glow > 0) {
      const SDL_FColor gc{c.r * glow * 0.6f, c.g * glow * 0.6f, c.b * glow * 0.6f, 1.f};
      AddBeam(halo, halo_i, ax, ay, bx, by, h * glow_size, gc);
    }
  }
  if (!halo.empty())
    SDL_RenderGeometry(ren, g_glow_tex, halo.data(), int(halo.size()), halo_i.data(), int(halo_i.size()));
  if (!core.empty())
    SDL_RenderGeometry(ren, g_core_tex, core.data(), int(core.size()), core_i.data(), int(core_i.size()));
}

void Shutdown() {
  if (g_core_tex) SDL_DestroyTexture(g_core_tex);
  if (g_glow_tex) SDL_DestroyTexture(g_glow_tex);
  g_core_tex = g_glow_tex = nullptr;
  g_tex_owner = nullptr;
}

}  // namespace t81_vectors
