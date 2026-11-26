# Uncapped native benchmark — 2026-11-25

For the subsequently repeated original idle comparison, see
`T3K-MATCHED-IDLE-BENCHMARK.md`.

Current playable executable, 1440x1080, no frame pacing or VSync, interpolation off,
CRT off, sharp vectors off, 4:3. Three trials per case, alternating CPU/GPU-ring
order. Each fine trial draws all 1,200 real fine simulation steps, excluding the
first 100 from statistics. Reported FPS is median trial throughput.

| Mode | Level | Updates/drawn frames per second | p95 interval, ms |
| --- | --- | ---: | ---: |
| 24-reference | 1 | 188.4 | 7.341 |
| 120-cpu-ring | 1 | 241.1 | 5.230 |
| 120-gpu-ring | 1 | 231.2 | 5.456 |
| 24-reference | 9 | 138.8 | 9.740 |
| 120-cpu-ring | 9 | 230.2 | 5.065 |
| 120-gpu-ring | 9 | 230.5 | 5.000 |

The optimized 120 Hz build sustains about 230–231 fine updates/draws per second
in these windows, about 1.92x the 120 Hz target. All individual optimized trial
rates were 226.5–248.0 for level 1 and 230.0–237.8 for level 9. This is not a
guarantee for all scenes, graphics settings, or tail latency.

The 24-reference path updates original game time once per picture; the fine
path advances one fifth of an original tick. Those FPS numbers represent
different work and cannot be compared as equal game-time throughput. The
24-reference post-frame-1700 window also includes a different simulation trace.

CPU-ring versus GPU-ring medians were 241.1 vs 231.2 on level 1 and 230.2 vs
230.5 on level 9. These scenes do not establish a whole-game gain from ring
offloading; the level-1 difference overlaps observed run variation. Large-ring
synthetic results remain evidence for that specific effect, not every scene.
The copy-free feedback path is enabled in both A/B variants.

This differs from the historical 432.9 FPS native / 188.5 FPS John benchmark,
which used a short early idle window and offscreen submission rather than this
longer scripted combat route and real presentation. Do not calculate a regression
or speedup between those figures. John was not rerun in this matrix.

The native mixer/media path runs with a deterministic virtual clock, without
queueing to the playback device. Timing spans consecutive presentation completions,
including intervening scene-driver work. SDL presentation is asynchronous; sustained
throughput includes submission/backpressure, not isolated GPU execution time.
Processes ran sequentially, without pinned CPU affinity. No GPU readback or
screenshots occur in the measured windows. CSV logging itself adds small overhead.

Reproduction script: `../tempest-profile/benchmark_latest_native.py` (run from
this repository). Raw per-frame CSVs, logs, executable hash and summaries:
`../tempest-profile/benchmarks/latest-native/results.json`. Opt-in CSV logging
uses `T3K_BENCH_CSV`; `T3K_BENCH_FRAMES` bounds validation-mode runs only.
Normal play does not enable either variable.
