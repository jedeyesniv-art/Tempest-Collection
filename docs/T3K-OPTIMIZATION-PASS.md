# Measured pipeline optimization pass — 2026-11-25

All four targets from `T3K-STAGE-PROFILE.md` are implemented. The existing 120 Hz
launchers use this build. CRT was explicitly off in all measurements and checks;
its code was not changed. John's frozen handoff patch/ZIP remain unchanged.

## Changes

### Bulk media delivery and rollback tracking

`NativeMemoryJournal` snapshots 4 KiB pages on their first write, with independent
frame and phase journals. Slots and page storage are reused rather than allocating
and releasing an ordered-map node per word. Word masks preserve exact write-set
diagnostics; a page snapshot does not falsely mark every word as written.

`NativeFrameMemory::write_bytes` validates the entire aligned range before
mutation, captures its originals, preserves access tracking and invalidates render
precision metadata in that range. It publishes the original byte order directly.
`NativeMedia::deliver` batches contiguous sectors within the existing sector budget.
Completion callbacks and partial-delivery boundaries retain their order.

Phase snapshots and continuation files still contain the same four memory banks;
no state-format migration is needed. Tests interleave overlapping word/bulk writes,
page crossings, frame commit/rollback, bank-end ranges and phase restoration.

### Texture and ring-image reuse

Each number slot owns a persistent source atlas. Its content is compared before
upload, so different digits cannot overwrite a shared texture that another digit
expects to reuse. Uniforms/placement still update for actual new fine geometry.
Web/plane texture caching tracks their shared texture's actual current content.
The ring source texture is reused when unchanged; generated ring images are reused
only when parameters, texture content and target lifetime all match.

Caching is based on data, not the legacy picture tick. Twelve same-tick mutation
and type-switch states match fresh rendering exactly, including CPU/GPU image
switches and interleaved web/plane content.

### Preparation reads

Aligned preparation reads now check worker-local storage or staged writes once,
then read a complete external word. They no longer perform four byte reads and
four staged-map lookups for every word. Byte reads remain available where needed.
Endianness, read-after-write visibility and output publication remain unchanged.

### Audio staging

The supported NISE mixer has a 12-word master record and 32 24-word voice records.
Its staged overlay now uses indexed storage plus validity flags for that bounded
pool. The ordered write list is retained, as are the original mixer arithmetic,
voice/output publication boundaries, callbacks, rollback and audio clock.
Unsupported writes still fail; nothing is discarded to gain speed.

## Paired measurements

The preserved pre-pass executable and optimized executable ran the same scripted
level-9 route in alternating order, three trials each. Each trial contains 3,227
fine simulation/drawing samples and 5,889 total pictures. Settings: 1440x1080,
CRT/interpolation/VSync off, sharp-vector extras and widescreen off, no pacing,
real presentation on every fine sample, deterministic media/audio clock and no
audio-device queue. These are different from the older offscreen idle benchmark.

| Metric | Before | After |
| --- | ---: | ---: |
| Mean fine interval, median trial | 3.990 ms | 0.987 ms |
| p95 fine interval, median trial | 5.161 ms | 1.472 ms |
| p99 fine interval, median trial | 6.074 ms | 1.642 ms |
| Largest fine interval across three trials | 146.057 ms | 3.303 ms |
| Median 2 MiB music publication, 24 deliveries each | 128.155 ms | 0.468 ms |
| Largest music publication | 138.055 ms | 1.947 ms |
| Fine-gameplay source texture uploads per trial | 14,068 | 2,824 |

That is approximately 4x fine-step throughput on this route. Initial allocation
and page capture account for the slower approximately 2 ms publication cases;
most warmed deliveries are around 0.45–0.50 ms. It is not a promise of these
rates at every resolution, level, or graphics setting.

Median exclusive stage means across the paired level-9 trials:

| Stage | Before | After |
| --- | ---: | ---: |
| Core/orchestration excluding timed children | 0.661 ms | 0.083 ms |
| Preparation workers combined | 0.792 ms | 0.293 ms |
| Audio advancement/mixing | 0.482 ms | 0.112 ms |
| Web submission | 0.725 ms | 0.041 ms |
| Number/ring submission | 0.406 ms | 0.089 ms |
| Phase checkpoint | 0.103 ms | 0.003 ms |
| Transaction commit | 0.062 ms | 0.0003 ms |

These changes interact: for example, journals affect both game and preparation
writes. Stage deltas do not isolate a single change's contribution. SDL calls
include synchronous driver work and backpressure, not isolated GPU execution.

A separate counter-disabled control confirms the improvement without detailed
profiling: mean fine intervals 3.969 -> 0.992 ms; p99 6.002 -> 1.629 ms. It is
a single control pair, not an additional three-trial confidence estimate.

Level-1 before/after runs also cover level clear and transitions: 453 explicit
level-clear frames averaged 3.720 -> 1.403 ms, with their largest interval falling
from 144.241 -> 3.491 ms. Sixty-four explicit death-animation frames averaged
9.103 -> 2.677 ms, with the largest falling from 144.766 -> 4.546 ms. These are
one additional route pair; the older means include the music-delivery hitch.
Original scene cadences are retained; this pass removes processing stalls rather
than converting those sequences to 120 Hz.

## Correctness and practical checks

- All paired scene/caller/lives, geometry counts, logical write counts, first-write
  counts and delivered-byte metadata match: 25,879 total picture rows across the
  three level-9 pairs and one level-1 pair, zero metadata differences.
- All 32 original-reference frames: zero mismatches, exact frame rollback and
  original GPU comparisons, read-only decoding and framebuffer independence.
- All 32 feedback-history checks and twelve cache mutation/type-switch states pass.
- Four CTest targets pass, including mixed bulk-write/page-journal checks.
- Digital, analog/jump, zapper and mouse scripted gameplay checks pass.
- Paced play: 1,200 fine samples in 9.988 active seconds, 120.14 Hz, all 1,200
  pictures drawn/presented; 8,988 audio bytes queued at exit.
- Original-cadence native save/load: saved at frame 1400, resumed exactly at 1400.

Metadata equality is additional long-route evidence, not a substitute for exact
memory/reference tests or proof of exhaustive game coverage.

## Threading assessment

No new worker threads were added. Existing compressed-media prefetch remains
available. Moving disc reading alone could not remove the measured 128 ms stall;
it was ordered memory publication and journaling, which are now inexpensive.

Preparation jobs share persistent worker-local state and publish staged writes
in order. Audio mixing temporarily uses processor-local scratch and has observable
voice/master/output boundaries. Neither can simply run concurrently against live
memory while retaining the validated behavior. An asynchronous version would need
explicit immutable input capture, private working state and ordered publication.

After this pass the measured preparation total is about 0.293 ms and audio about
0.112 ms per fine sample. Speculative threading would add synchronization and
snapshot costs to already small jobs. A persistent worker pool remains possible
if later measurements expose a substantial independent batch; it is not currently
the highest-value change. SDL resource work remains on the rendering thread.

## Evidence and reproduction

Adjacent `tempest-profile/profiles/optimization-pass/` contains before/after
commands, per-picture stage CSVs, logs, the control pair, summaries and a
`checkpoint.json` receipt with executable/source hashes and validation results.
The original pre-pass executable is preserved under `profiles/native-stages/`.

Run `profile_optimization_pass.py`, `analyze_optimization_pass.py`, and optionally
`check_optimization_control.py` from the integration repository. These scripts
live in the adjacent `tempest-profile` research folder. Normal play does not
enable the profiler or benchmark environment variables.
