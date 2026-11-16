// Frame cost breakdown (T2K_PERF=1). Named timers and counters, summed per shown frame; every report gives each
// one's average and worst frame. Reports every 5 s (T2K_PERF_SECONDS to change, 0 = only at exit) to stderr, or
// appended to T2K_PERF_FILE (e.g. on the Steam Deck).
#pragma once

#include <cstdint>

namespace perf {

bool On();
uint64_t Now();   // ns
// Adds to a timer (name: a string literal; the same literal for the same timer).
void Add(const char* name, uint64_t ns);
// A counter's value this frame (added up when set more than once in a frame).
void Count(const char* name, double v);
// Ends a shown frame: total = its whole time; reports when the period is over.
void EndFrame(uint64_t total_ns, int w, int h);
// The last report now (at exit).
void Flush();
// Leaves the current frame out (e.g. a frame that read the window back for a screenshot).
void DiscardFrame();

struct Span {
  const char* name;
  uint64_t t0;
  explicit Span(const char* n) : name(n), t0(On() ? Now() : 0) {}
  ~Span() { if (t0) Add(name, Now() - t0); }
};

}  // namespace perf
