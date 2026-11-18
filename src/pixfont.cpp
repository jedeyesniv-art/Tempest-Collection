// pixfont.cpp - see pixfont.h. The font and the text / line drawing were launcher_view.cpp's (the launcher look the
// owner approved); the launcher draws through here now.
#include "pixfont.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace pixfont {
namespace {

// 35 characters a glyph: 7 rows of 5, '#' lit.
const std::map<char, std::string>& Glyphs() {
  static std::map<char, std::string> g;
  if (!g.empty()) return g;
  const std::pair<char, const char*> src[] = {
      {'A', ".###.#...##...#######...##...##...#"}, {'B', "####.#...##...#####.#...##...#####."},
      {'C', ".#####....#....#....#....#.....####"}, {'D', "####.#...##...##...##...##...#####."},
      {'E', "######....#....####.#....#....#####"}, {'F', "######....#....####.#....#....#...."},
      {'G', ".#####....#....#.####...##...#.###."}, {'H', "#...##...##...#######...##...##...#"},
      {'I', "#####..#....#....#....#....#..#####"}, {'J', "..###...#....#....#....##..#..##..."},
      {'K', "#...##..#.#.#..##...#.#..#..#.#...#"}, {'L', "#....#....#....#....#....#....#####"},
      {'M', "#...###.###.#.##.#.##...##...##...#"}, {'N', "#...###..##.#.##..###...##...##...#"},
      {'O', ".###.#...##...##...##...##...#.###."}, {'P', "####.#...##...#####.#....#....#...."},
      {'Q', ".###.#...##...##...##.#.##..#..##.#"}, {'R', "####.#...##...#####.#.#..#..#.#...#"},
      {'S', ".#####....#.....###.....#....#####."}, {'T', "#####..#....#....#....#....#....#.."},
      {'U', "#...##...##...##...##...##...#.###."}, {'V', "#...##...##...##...##...#.#.#...#.."},
      {'W', "#...##...##...##.#.##.#.###.###...#"}, {'X', "#...##...#.#.#...#...#.#.#...##...#"},
      {'Y', "#...##...#.#.#...#....#....#....#.."}, {'Z', "#####....#...#...#...#...#....#####"},
      {'0', ".###.#...##..###.#.###..##...#.###."}, {'1', "..#...##....#....#....#....#...###."},
      {'2', ".###.#...#....#...#...#...#...#####"}, {'3', "####.....#....#.###.....#....#####."},
      {'4', "...#...##..#.#.#..#.#####...#....#."}, {'5', "######....####.....#....##...#.###."},
      {'6', ".###.#....#....####.#...##...#.###."}, {'7', "#####....#...#...#...#....#....#..."},
      {'8', ".###.#...##...#.###.#...##...#.###."}, {'9', ".###.#...##...#.####....#....#.###."},
      {'-', "...............#####..............."}, {'.', "................................#.."},
      {':', ".......#..............#............"}, {'!', "..#....#....#....#....#.........#.."},
      {'\'', "..#....#..........................."}, {'/', "....#...#....#...#....#...#....#..."},
      {'<', "...#...#...#...#.....#.....#.....#."}, {'>', ".#.....#.....#.....#...#...#...#..."},
      {'(', "..#...#...#....#....#.....#.....#.."}, {')', "..#.....#.....#....#....#...#...#.."},
      {',', "..........................##..##..."}, {'"', ".#.#..#.#.........................."},
      {'=', "..........#####.....#####.........."}, {'%', "##...##..#...#...#...#...#..##...##"},
      {'_', "..............................#####"}, {'+', ".......#....#..#####..#....#......."},
      {'?', ".###.#...#....#...#...#.........#.."}, {'\\', "#.....#....#.....#.....#....#.....#"},
      {'&', ".##..#..#..##...#...#.#.##..#..##.#"}, {'^', "..#...#.#.#...#...................."},
      // lower case (setup help: Steam launch options are case-sensitive) and the shell's $ { } @
      {'a', "...........###.....#.#####...#.####"}, {'b', "#....#....####.#...##...##...#####."},
      {'c', "...........#####....#....#.....####"}, {'d', "....#....#.#####...##...##...#.####"},
      {'e', "...........###.#...#######.....###."}, {'f', "..##..#....#...####..#....#....#..."},
      {'g', "......#####...##...#.####....#.###."}, {'h', "#....#....####.#...##...##...##...#"},
      {'i', "..#........##....#....#....#...###."}, {'j', "...#.........##....#....##..#..##.."},
      {'k', "#....#....#..#.#.#..##...#.#..#..#."}, {'l', ".##....#....#....#....#....#...###."},
      {'m', "..........##.#.#.#.##.#.##.#.##.#.#"}, {'n', "..........####.#...##...##...##...#"},
      {'o', "...........###.#...##...##...#.###."}, {'p', ".....####.#...##...#####.#....#...."},
      {'q', "......#####...##...#.####....#....#"}, {'r', "..........#.##.##..##....#....#...."},
      {'s', "...........#####.....###.....#####."}, {'t', ".#....#...####..#....#....#..#..##."},
      {'u', "..........#...##...##...##..##.##.#"}, {'v', "..........#...##...##...#.#.#...#.."},
      {'w', "..........#...##.#.##.#.##.#.#.#.#."}, {'x', "..........#...#.#.#...#...#.#.#...#"},
      {'y', ".....#...##...##...#.####....#.###."}, {'z', "..........#####...#...#...#...#####"},
      {'$', "..#...#####.#...###...#.#####...#.."}, {'{', "...##..#....#..##.....#....#.....##"},
      {'}', "##.....#....#.....##..#....#..##..."}, {'@', ".###.#...##.####.#.##.####.....####"},
  };
  for (const auto& [c, s] : src) {
    std::string v(s);
    v.resize(35, '.');
    g[c] = v;
  }
  // added for the settings menus (row by row)
  auto add = [&](char c, std::initializer_list<const char*> r) {
    std::string v;
    for (const char* line : r) v += line;
    v.resize(35, '.');
    g[c] = v;
  };
  add(';', {".....", "..#..", ".....", ".....", "..#..", ".#...", "....."});
  add('[', {".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###."});
  add(']', {".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###."});
  add('|', {"..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."});
  add('*', {".....", "#.#.#", ".###.", "#####", ".###.", "#.#.#", "....."});
  add('#', {".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#."});
  add(kFolded, {".#...", ".##..", ".###.", ".####", ".###.", ".##..", ".#..."});
  add(kOpen, {".....", "#####", ".###.", "..#..", ".....", ".....", "....."});
  // lower case with descenders (drawn kDescent rows lower: x-height in rows 0-4, the tail in 5-6)
  add('g', {".####", "#...#", "#...#", "#...#", ".####", "....#", ".###."});
  add('p', {"####.", "#...#", "#...#", "#...#", "####.", "#....", "#...."});
  add('q', {".####", "#...#", "#...#", "#...#", ".####", "....#", "....#"});
  add('y', {"#...#", "#...#", "#...#", "#...#", ".####", "....#", ".###."});
  return g;
}

constexpr int kDescent = 2;
bool Descends(char c) { return c == 'g' || c == 'p' || c == 'q' || c == 'y'; }

// Textures, made for the renderer in use (a program has one renderer).
SDL_Renderer* g_ren = nullptr;
SDL_Texture* g_dot = nullptr;    // gaussian dot (white, alpha), additive
SDL_Texture* g_beam = nullptr;   // a line's cross profile (white, alpha by v), additive

template <typename F>
SDL_Texture* AlphaTexture(SDL_Renderer* ren, int w, int h, F f) {
  SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
  if (!s) return nullptr;
  for (int y = 0; y < h; ++y) {
    uint8_t* row = static_cast<uint8_t*>(s->pixels) + y * s->pitch;
    for (int x = 0; x < w; ++x) {
      const float a = std::clamp(f((x + 0.5f) / w, (y + 0.5f) / h), 0.f, 1.f);
      row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
      row[x * 4 + 3] = uint8_t(a * 255 + 0.5f);
    }
  }
  SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
  if (t) {
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
  }
  SDL_DestroySurface(s);
  return t;
}

void Ensure(SDL_Renderer* ren) {
  if (ren == g_ren && g_dot) return;
  Shutdown();
  g_ren = ren;
  g_dot = AlphaTexture(ren, 64, 64, [](float x, float y) {
    const float d2 = (x - 0.5f) * (x - 0.5f) + (y - 0.5f) * (y - 0.5f);
    return std::exp(-d2 * 18.f);
  });
  g_beam = AlphaTexture(ren, 4, 64, [](float, float y) { return std::exp(-(y - 0.5f) * (y - 0.5f) * 22.f); });
}

}  // namespace

bool HasGlyph(char c) { return Glyphs().count(c) != 0; }

float TextWidth(const std::string& s, float unit) { return s.empty() ? 0.f : (s.size() * 6 - 1) * unit; }

void Text(SDL_Renderer* ren, const std::string& str, float x, float y, float unit, Col c, float a, int align,
          float glow) {
  if (a <= 0.f || !ren) return;
  Ensure(ren);
  std::string s = str;
  // lower case where the font has it, else capitals
  const auto& gl = Glyphs();
  for (char& ch : s)
    if (!gl.count(ch)) ch = char(std::toupper(static_cast<unsigned char>(ch)));
  const float w = TextWidth(s, unit);
  float x0 = align == 0 ? x - w / 2 : align > 0 ? x - w : x;
  std::vector<SDL_FRect> rects;
  std::vector<SDL_FPoint> dots;
  for (char ch : s) {
    auto it = gl.find(ch);
    const float yc = y + (Descends(ch) ? kDescent : 0) * unit;
    if (it != gl.end())
      for (int r = 0; r < 7; ++r)
        for (int k = 0; k < 5; ++k)
          if (it->second[r * 5 + k] == '#') {
            rects.push_back({x0 + k * unit, yc + r * unit, unit * 0.92f, unit * 0.92f});
            dots.push_back({x0 + (k + 0.46f) * unit, yc + (r + 0.46f) * unit});
          }
    x0 += unit * 6;
  }
  if (glow > 0.f && g_dot) {
    SDL_SetTextureColorModFloat(g_dot, c.r, c.g, c.b);
    SDL_SetTextureAlphaModFloat(g_dot, std::min(1.f, 0.22f * a * glow));
    const float gs = unit * 4.f;
    for (const SDL_FPoint& p : dots) {
      const SDL_FRect r{p.x - gs / 2, p.y - gs / 2, gs, gs};
      SDL_RenderTexture(ren, g_dot, nullptr, &r);
    }
  }
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColorFloat(ren, std::min(1.f, c.r * 0.6f + 0.43f), std::min(1.f, c.g * 0.6f + 0.43f),
                              std::min(1.f, c.b * 0.6f + 0.43f), a);
  SDL_RenderFillRects(ren, rects.data(), int(rects.size()));
}

void Beam(SDL_Renderer* ren, float x0, float y0, float x1, float y1, float width, Col c, float a) {
  if (a <= 0.f || !ren) return;
  Ensure(ren);
  float dx = x1 - x0, dy = y1 - y0;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1e-3f) return;
  const float nx = -dy / len * width / 2, ny = dx / len * width / 2;
  const SDL_FColor fc{c.r, c.g, c.b, std::min(a, 1.f)};
  const SDL_Vertex v[4] = {{{x0 + nx, y0 + ny}, fc, {0, 0}}, {{x1 + nx, y1 + ny}, fc, {1, 0}},
                           {{x0 - nx, y0 - ny}, fc, {0, 1}}, {{x1 - nx, y1 - ny}, fc, {1, 1}}};
  const int idx[6] = {0, 1, 2, 1, 3, 2};
  SDL_RenderGeometry(ren, g_beam, v, 4, idx, 6);
}

void Glow(SDL_Renderer* ren, float x0, float y0, float x1, float y1, float width, Col c, float a, float glow) {
  Beam(ren, x0, y0, x1, y1, width * glow, c, a * 0.35f);
  Beam(ren, x0, y0, x1, y1, width * 1.6f,
       {std::min(1.f, c.r * 0.5f + 0.5f), std::min(1.f, c.g * 0.5f + 0.5f), std::min(1.f, c.b * 0.5f + 0.5f)}, a);
}

void Shutdown() {
  if (g_dot) SDL_DestroyTexture(g_dot);
  if (g_beam) SDL_DestroyTexture(g_beam);
  g_dot = g_beam = nullptr;
  g_ren = nullptr;
}

}  // namespace pixfont
