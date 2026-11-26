# Native stage profiling — 2026-11-25

CRT was explicitly disabled in every run. Its implementation was not modified
and it is outside this investigation. Interpolation, VSync, sharp-vector extras
and widescreen were also disabled. The production native renderer, preparation,
gameplay and music mixer ran, with scripted input and virtual audio/media time.
No audio-device queue or screenshots were used in measured runs.

## Highest-value finding: music-buffer publication stalls

Eight 2 MiB music-stream deliveries were observed on the detailed level-9 route.
Reading each buffer from the uncompressed ISO averaged 0.713 ms. Publishing it
to native memory averaged **125.1 ms**, ranging **116.1–135.6 ms**. Some frames
then spent another 12–18 ms committing/freeing transaction records. The largest
complete frame on this route was approximately 155 ms, including menu scenes.

The source path is `NativeMedia::deliver` -> 524,288 individual `write32` calls
for the stream payload -> `NativeFrameMemory::store`. Each first-written word
is tracked in ordered maps for frame and phase rollback. These events coincide
with approximately 530,000 newly tracked words including other frame work.
Both CPU allocation/tracking and later release of records contribute to stalls.
This happens during gameplay, transitions and menus, not only during death.

**First target:** bulk memory publication with efficient rollback tracking.
Snapshot ranges/pages or use a dense change journal rather than allocating a
tree node for each word of contiguous data. Retain exact frame rollback, phase
checkpoint and continuation semantics. Preserve sector ordering and completion
callbacks. Moving just disc reading to a thread would leave nearly all of the
measured stall intact. This profile does not measure an implemented remedy.

## Ordinary fine gameplay costs

Detailed level-9 route: 3,227 actual fine simulation/drawing samples, at 1440x1080.
Mean interval 3.898 ms, median 3.742 ms, p95 5.127 ms, p99 5.812 ms. Rare stream
bursts remain in the mean; the largest fine interval was 141.6 ms.

The following are exclusive wall-clock scope averages. Rendering calls include
CPU command generation and any synchronous backend flush/backpressure. They are
**not isolated GPU execution times**.

| Work | Mean ms per fine sample |
| --- | ---: |
| Core game/orchestration, excluding timed children | 0.655 |
| Native preparation workers combined | 0.789 |
| Web rendering submission | 0.652 |
| Audio advancement/mixing | 0.481 |
| Presentation call and overlay | 0.451 |
| Number/ring rendering submission | 0.405 |
| Phase checkpoint work | 0.097 |
| Transaction commit | 0.063 |
| Picture decoding | 0.061 |
| Vector geometry submission | 0.031 |
| Disc/particle geometry submission | 0.010 |

Worker detail: projected records (12) 0.210 ms; animation (6) 0.202 ms; shapes
(2) 0.129 ms; web preparation (10) 0.124 ms; depth (17) 0.093 ms; textures (3)
0.030 ms. The core bucket contains remaining game work and memory accesses;
it is not a claim that collision or callbacks individually cost 0.655 ms.

Average memory traffic at this boundary was 66,876 read operations and 8,681
word writes, with 6,049 newly tracked words per fine sample. Worker-local reads
are not all counted; some generic preparation word reads become four byte reads.

## Repeated texture uploads

There were 14,068 counted web/plane/number/ring texture uploads in fine gameplay.
11,244 (**79.9%**) had identical content to the preceding upload for the same
profile slot. This counts small source textures/atlases, not full scene targets.

For number atlases the comparison is against the same glyph slot in the prior
picture, not necessarily the texture currently bound: the implementation shares
one streaming atlas texture across digits. Skipping an update blindly would be
incorrect. Use persistent content-aware atlases/textures or combine the font and
colour data into shared resources, then update only changed content. Web/plane
texture ownership must likewise account for their shared texture.

**Second target:** reduce those repeated uploads and associated submission/flush
work. This is about call overhead and ownership, not large upload bandwidth.
An end-to-end A/B is required because eliminating a blocking call can move its
wait elsewhere. The full 1.057 ms web/image submission total is not automatically
recoverable.

**Third target:** streamline native preparation memory views and journal lookups.
`NativePreparationBackend::View::read32` currently performs four `read8` calls,
including repeated staged-write map lookups. Word-aligned access can potentially
do one lookup and one word read, with the same memory visibility and endianness.
Measure it against the 0.789 ms preparation total rather than assuming a 4x gain.
The CPU audio staging/mixer path is another significant target after that.

## Death, level clear and tail behavior

Caller PCs distinguish death-animation frames from generic transition helpers;
the outer session phase alone covers several different routines. On the initial
1080p level-9 route, 198 explicit death-animation frames averaged 4.012 ms,
p95 6.089 ms, p99 6.883 ms. Audio averaged 0.981 ms on these original-cadence
frames, with images 0.478 ms, web submission 0.479 ms and core 0.513 ms.

The detailed level-1 route includes 453 explicit level-clear animation frames:
mean 4.311 ms, p95 7.156 ms, p99 7.614 ms, maximum 139.6 ms. Image submission
averaged 1.114 ms and audio 0.923 ms. The extreme interval included a stream
delivery. The long initial route exercised multiple level-clear/entry sequences.
These groups exclude common transition-helper callers whose event ownership is
ambiguous; they are not complete end-to-end event durations.

Those sequences still run at their original scheduling cadence. Their perceived
lower frame rate is a separate problem from processing time, while music-buffer
publication causes actual deadline-sized stalls. This does not identify the
cause of every slowdown reported by the player.

## CPU versus GPU evidence

The same level-9 route averaged 3.554 ms per fine sample at 640x480 and 3.491 ms
at 1440x1080 in the initial scope pass. Identical memory-operation counts and
fine-sample counts were observed. There was no strong resolution sensitivity on
this RTX 4070 Ti machine, so a general fill-rate bottleneck is not supported by
these runs. This does not establish that the GPU is never limiting.

A separate 1080p diagnostic waited for GPU idle after each presentation. Its
average fine interval was 5.455 ms, including 2.211 ms in the added drain wait.
That wait includes queue/driver/fence latency and changes overlap; it must not be
called isolated GPU time or added to the asynchronous run as a predicted cost.
SDL's GPU API here does not provide the timestamp instrumentation needed for a
true per-pass GPU breakdown. A dedicated GPU capture would be a separate study.

## Measurement mechanism, overhead and reproduction

`native_profile.h` implements opt-in nested scopes with exclusive child
accounting, read/write counters, scene/caller metadata and texture-content reuse
counters. `T3K_PROFILE_CSV=<path>` enables it. `T3K_PROFILE_FENCE=1` adds the
explicit GPU-drain diagnostic. Normal play does not enable either variable.
The profiling path rejects an enabled CRT mode instead of silently including it.
The scripted regression-check tool also now explicitly sets CRT off.

Detailed profiling and a sequential, counter-disabled control produced the same
3,227 fine samples. Means were 3.898 versus 3.957 ms; medians 3.742 versus 3.769
ms. That single pair shows no large overhead uplift, but is not a statistical
overhead estimate. The initial and detailed passes were separate runs; exact
stage attribution can shift with asynchronous flushes and machine conditions.

Adjacent research folder `tempest-profile` contains:

- `profile_native_stages.py`: resolution pair, explicit drain, long level-1 run.
- `profile_native_detail.py`: media/texture detail, control and level-clear run.
- `analyze_native_stages.py`: groups and summarizes the raw CSVs.
- `profiles/native-stages/analysis.json`: stage distributions, counts and hashes.
- Per-case CSVs, complete commands, exit status and logs under the same folder.

All scripts run from this integration repository. The profiler does not change
logic or rendering algorithms. Four CTest targets pass; 32 original-reference
frames retain zero mismatches and exact rollback/GPU/independence checks. Shader
fixture and 32 feedback-history checks also pass. The frozen handoff is unchanged.

Threading remains an option for independent preparation, but the largest measured
stall is ordered publication into shared transactional memory. Fix its data
structure/transfer granularity before adding worker threads around it.
