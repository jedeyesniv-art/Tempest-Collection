#include "perf.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace perf {
namespace {

struct Slot {
  const char* name;
  bool timer;
  double cur = 0, sum = 0, max = 0;
};

std::vector<Slot> g_slots;
uint64_t g_from = 0, g_frames = 0;
double g_total_sum = 0, g_total_max = 0;
int g_over = 0;   // frames over the display's budget (1 / 60 s here: the core's rate)
int g_w = 0, g_h = 0;

Slot& Find(const char* name, bool timer) {
  for (auto& s : g_slots)
    if (s.name == name || std::strcmp(s.name, name) == 0) return s;
  g_slots.push_back({name, timer});
  return g_slots.back();
}

double Period() {
  static const double p = [] {
    const char* e = std::getenv("T2K_PERF_SECONDS");
    return e ? std::atof(e) : 5.0;
  }();
  return p;
}

void Report() {
  if (!g_frames) return;
  static const char* path = std::getenv("T2K_PERF_FILE");
  FILE* f = path ? std::fopen(path, "a") : stderr;
  if (!f) return;
  const double n = double(g_frames), secs = double(Now() - g_from) / 1e9;
  std::fprintf(f, "perf %dx%d: %.1f fps shown over %.1f s | frame %.2f ms avg, %.2f ms worst, %d over 16.7 ms\n", g_w,
               g_h, secs > 0 ? n / secs : 0.0, secs, g_total_sum / n / 1e6, g_total_max / 1e6, g_over);
  for (auto& s : g_slots) {
    if (s.sum == 0 && s.max == 0) continue;
    if (s.timer)
      std::fprintf(f, "  %-18s %7.3f ms avg %7.3f ms worst\n", s.name, s.sum / n / 1e6, s.max / 1e6);
    else
      std::fprintf(f, "  %-18s %9.0f avg %9.0f worst\n", s.name, s.sum / n, s.max);
    s.sum = s.max = 0;
  }
  if (f != stderr) std::fclose(f);
  g_frames = 0;
  g_total_sum = g_total_max = 0;
  g_over = 0;
  g_from = Now();
}

}  // namespace

bool On() {
  static const bool on = std::getenv("T2K_PERF") != nullptr;
  return on;
}

uint64_t Now() { return SDL_GetTicksNS(); }

void Add(const char* name, uint64_t ns) {
  if (On()) Find(name, true).cur += double(ns);
}

void Count(const char* name, double v) {
  if (On()) Find(name, false).cur += v;
}

void EndFrame(uint64_t total_ns, int w, int h) {
  if (!On()) return;
  static int warm = 0;   // the first 1.5 s (texture / shader set-up) is left out
  if (warm < 90) {
    ++warm;
    for (auto& s : g_slots) s.cur = 0;
    g_from = Now();
    return;
  }
  if (!g_from) g_from = Now();
  g_w = w, g_h = h;
  for (auto& s : g_slots) {
    s.sum += s.cur;
    s.max = std::max(s.max, s.cur);
    s.cur = 0;
  }
  ++g_frames;
  g_total_sum += double(total_ns);
  g_total_max = std::max(g_total_max, double(total_ns));
  if (total_ns > 16666667) ++g_over;
  if (Period() > 0 && double(Now() - g_from) / 1e9 >= Period()) Report();
}

void DiscardFrame() {
  for (auto& s : g_slots) s.cur = 0;
}

void Flush() {
  if (On()) Report();
}

}  // namespace perf
