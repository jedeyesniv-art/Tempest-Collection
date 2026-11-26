# Native scene performance and cadence candidates

For the corrected reference-speed investigation, see
[T3K-REFERENCE-SCENE-PACING.md](T3K-REFERENCE-SCENE-PACING.md).
The measurements below are native performance capacity, not reference pacing.

These measurements profile our **native runtime and John's renderer**, not
Nuance and not NUON hardware. They answer whether higher native update rates
fit on the test PC and where work is spent. They do not establish original
scene playback speed, slowdown or intended pacing. Nuance/reference behavior
and hardware footage remain separate evidence for choosing reference speeds.

## Method

15 serial runs at 1440×1080: level 1 and level 9 routes, each with the original
scene scheduler and the opt-in scene trial. Each combination has two ordinary
runs and one GPU-completion-wait run. Three additional runs hold the opening
selection/particle scene for 600 authored input-delay frames (two ordinary, one
GPU wait), covering 664 frames per full crawl rather than only its fades.

CRT, interpolation, VSync, sharp-vector extras and widescreen are off. Menu
reference is 30 Hz, generic warp reference 60 Hz, gameplay reference 24 Hz and
fine gameplay simulation 120 Hz. Scripted validation uses virtual video/audio
time and no real audio-device queue. Measured runs take no screenshots.
Bonus screens are reached through normal level progression, not substituted
scene data. Runs and raw CSVs live under `research/tempest-profile/profiles/
scene-cadence` in the workspace. `analysis.json` contains counts, percentiles,
exclusive stage costs, geometry maxima and coverage per run. `profiled.exe`
preserves the binary used for the initial 12 runs; the longer crawl runs use
the subsequent controls build with unchanged default input and render paths.

Scene initialization index is appended to profiler records. Caller PC plus
session phase separates death, level-clear warp and transition frames, which
otherwise share outer phases. The first zero-duration row is excluded. Frame
intervals include initialization or other work occurring between pictures;
exclusive stage averages identify only work inside instrumented scopes.

The GPU-wait runs drain outstanding GPU work after each presented frame.
Their time includes CPU work, GPU completion waits and instrumentation; it is
not an isolated GPU execution timer. Ordinary submission scopes can themselves
include flush/backpressure. These comparisons cannot cleanly label the whole
game CPU-bound or GPU-bound.

## Results

The table uses the scene-trial routes, except the particle crawl's longer held
runs. Death, level entry/clear, bonus, level select and crawl still use their
original scene cadence in this build. The costs are **per current scene frame**;
converting them to 120 Hz changes audio, feedback and event scheduling, so this
is a feasibility measurement rather than a prediction of the final cost.

| Scene | Mean ms | p99 ms | p99 with GPU wait ms |
| --- | ---: | ---: | ---: |
| Death | 2.99 | 4.78 | 6.89 |
| Level-clear warp | 2.54 | 3.96 | 5.89 |
| Level-clear transition | 1.46 | 2.67 | 4.39 |
| Level entry | 1.44 | 2.78 | 4.06 |
| Bonus screen | 1.28 | 6.93 | 6.52 |
| Level select | 1.23 | 3.77 | 3.60 |
| Long particle crawl | 1.79 | 2.65 | 2.56 |
| Opening logo, already 120 Hz | 0.63 | 1.07 | 1.48 |
| Animated menus, already 120 Hz | 1.00 | 1.19 | 1.24 |

A 120 Hz frame budget is 8.33 ms. The full crawl reaches 749 line segments and
1,648 blobs, with no observed frame above 8.33 ms in its 1,328 ordinary or 664
GPU-wait samples. The particle crawl is scene 14, separate from the opening
logo's scene 0; the previous opening conversion did not convert the crawl.

Tail costs still matter. Level select has first-entry frames around 9 ms;
the largest measured level-entry frame is 17.02 ms. That entry frame spends
7.41 ms in rendering submission, 5.59 ms in number/ring image submission and
3.41 ms in presentation. No disc delivery occurred on it. The largest bonus
frame is 7.08 ms, mostly submission/presentation. These observations identify
render resource/flush/backpressure investigation points, not a proven shader
compilation or allocation cause. GPU-wait runs also alter where waits appear.

## Work distribution and decisions

Death has both CPU and GPU-side work. In the scene-trial route, ordinary audio
advancement averages 0.61 ms, number/ring submission 0.54 ms, and web submission
0.53 ms. GPU-wait runs average 1.56 ms waiting for completion. Level-clear warp
audio averages 0.69 ms, with rendering also substantial. Audio per sample will
change when the same elapsed duration is divided into finer steps.

The long crawl averages 0.44 ms in image submission, 0.37 ms in presentation,
0.24 ms in generated-shape preparation and 0.12 ms in decoding. Its GPU-wait
average is 0.36 ms. Level select's ordinary preparation and web submission are
about 0.15 ms each; bonus screen cost is mostly rendering submission rather
than a large game-logic workload.

Recommended cadence work, preserving separate reference-speed policy:

1. **Level select and particle crawl:** target actual 120 Hz state/input/geometry.
   Performance supports proceeding. Audit selection latches, authored fades,
   preset/particle timers and RNG before scaling their callers. Keep crawl
   pacing independent of gameplay speed.
2. **Bonus screens and level entry/transition:** target 120 Hz, retaining authored
   score/count decisions and elapsed animation duration. Investigate first-use
   rendering stalls separately; an average-rate cap would not fix them.
3. **Death and level-clear warp:** target 120 Hz, with close attention to GPU
   feedback, authored audio/event cadence and tail costs. These have less spare
   budget than level select. Measure again after their time-dependent work is
   split; do not multiply all effect/audio work blindly.

There is no native performance evidence here requiring an intentionally low
simulation rate for a particular scene. There is also no evidence here that a
NUON reference scene should advance at 24, 30 or 60 authored ticks per second.
Those are separate questions. This pass does not change scene cadence or enable
density-based slowdown.

## Reproduction and validation

`runs.json` and `particle-runs.json` retain exact commands, binary hashes and
exit codes. All 15 runs exit successfully. The workspace's
`profiles/run_scene_cadence.py`, `profiles/run_particle_profile.py` and
`profiles/analyze_scene_cadence.py` retain the run/analysis procedures.
Set `T3K_PROFILE_CSV` for stage profiling and optionally `T3K_PROFILE_FENCE=1`.
`T3K_PROFILE_MENU_HOLD=600` extends only the scripted selection input delay while
profiling is enabled; normal play and ordinary checks are unaffected.

Counts differ between scene-trial and original-scheduler routes because they
advance at different relative scene/video/audio clocks and reach different
native events. Their tables compare observed workloads, not paired identical
pixel states or original-behavior validation. Earlier reference replay tests
remain the validation evidence for the reconstructed original path.
