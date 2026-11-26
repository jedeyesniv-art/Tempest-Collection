# Renderer review and optimization candidates

Follow-up implementation: see `T3K-GPU-EFFECTS.md` for the first ring shader
and copy-free fine-step feedback checkpoint. The findings below are the review
that preceded those changes.

2026-11-25. Source review plus synthetic measurements; not a profile of the
player's reported death/transition scenes. No production rendering behavior was
changed in this review. Interpolation is excluded.

## Architecture and documentation

The native backend generates display lists and decodes them into ordered drawing
operations. SDL's GPU renderer executes vector triangles, textured web meshes,
soft particle discs, feedback zoom/fade, ripple and final YCrCb conversion.
It is already a GPU renderer, but not every effect is GPU generated.

`src/t3k_native.h` is the most useful per-operation specification, with recovered
module addresses and comparison tools. `CLAUDE.md`'s T3K section records historical
validation methods and the hybrid runtime. `REMASTER.md` primarily describes
Tempest 2000, not T3K. Old claims of emulated fallback in these documents/header
comments do not describe the native integration: `native_renderer_bridge.cpp`
rejects incomplete pictures and rejects force/omit test overrides.

## Remaining coverage and approximation boundaries

- Burst and jump-burst support is conditional on `BurstDrawable`: the low seven
  bits of word 7 must be <= 2. Other row routines fail explicitly. This is a
  coverage boundary, not evidence that a known active scene is currently missing.
- Unknown display-list types fail explicitly. Tested complete pictures do not
  prove every parameter variant in all levels has been exercised.
- Web texture mapping approximates the original scanline mapping with an 8x8
  mesh per lane. Reducing it to two triangles would change texture placement.
- Vector coverage is a triangular tent with caps; particles use a shared disc
  texture. These are reconstructions, not the original raster kernels.
- The nonshader ripple fallback omits displacement, but the native backend
  requires the complete YCrCb shader pipeline, so it is not the normal path.
- Shader initialization currently requires DXIL. Portability to other shader
  formats is separate from Windows performance work.

## Measurements

Opt-in benchmark in `tests/t3k_timing_render.cpp`: set `T3K_RENDER_BENCH=1`
and run `t3k_timing_render_test.exe`. Raw output is in the adjacent research
folder's `renderer-cost-bench.log`. GPU adapter inventory: RTX 4070 Ti and Meta
Virtual Monitor; backend reports `gpu`. Measurements are single-run diagnostics,
not release performance guarantees or dedicated GPU timestamp measurements.

CPU-only synthetic ring decoding, using the real display-list decoder and
its CPU bilinear sampling/image-generation path:

| Radius, game pixels | Generated image | Mean decode time |
| --- | --- | --- |
| 10 | 48x48 | 0.033 ms |
| 50 | 208x208 | 0.649 ms |
| 100 | 408x408 | 2.579 ms |
| 200 | 808x808 | 10.742 ms |

These are valid constructed inputs, not observed death-scene radii. Upload,
simulation, audio, drawing and CRT are excluded. They demonstrate a real CPU
scaling problem without establishing its prevalence in actual gameplay.

Synthetic drawing of zoom/fade feedback plus 100 or 1,000 vector segments,
at target heights 240/480/1080/2160, took about 0.08-0.38 ms per sample in this
run. Both original single-step rendering and five-substep history rendering
were tested. Each case warms up, submits 60 real frames with VSync disabled,
and waits for GPU idle at batch end. Completed wall time includes CPU submission
and backpressure; it is not isolated GPU execution time. Text, rings, web
meshes, game/audio and CRT are excluded. Submission microtimings are noisy.

The 120 Hz frame budget is 8.33 ms. These results make CPU image generation a
serious candidate; they cannot classify the complete game as CPU or GPU bound.

## Prioritized work

1. Instrument real scenes by stage: simulation/preparation/audio, picture decode,
   GPU command generation, CRT and present/waits. Record p50/p95/p99, scene state,
   ring/glyph dimensions, segment/blob counts, render resolution and scheduler
   divisions. Pair with GPU timestamps/capture rather than interpreting time
   inside asynchronous SDL drawing calls as GPU time. Compare identical scripted
   scenes across resolutions and CRT modes. Death/transitions still have their
   original scheduler, which must be distinguished from missed deadlines.
2. Move ring and glowing-number image generation to shaders. `Ring` and `Text`
   still perform per-pixel CPU sampling; their RGBA images are then uploaded.
   Preserve YCrCb, neutral chroma, premultiplied opacity, quadratic mappings,
   original parameter evolution and operation order. Validate against the CPU
   reference before replacing it. This removes work rather than reducing quality.
3. Cache content by actual dependencies. Web/plane 16x16 textures are converted
   and uploaded repeatedly even when unchanged. Fine simulation rendering also
   forces glyph freshness, so it reuploads glyphs on each drawn fine sample.
   Cache texel/parameter content, not just the legacy picture number: fresh fine
   states may legitimately change content within that number. Cache at decode
   as well as upload to avoid merely hiding the smaller half of the expense.
4. Reduce transient allocations and copies: segment ranges are copied for each
   operation, vector vertices are rebuilt in temporary vectors, and web mesh
   scratch storage is allocated repeatedly. Use spans/reusable buffers, retain
   draw ordering and only batch compatible adjacent operations. There are 24
   emitted vector vertices per segment; indexed vertices or GPU expansion may
   reduce uploads, but should follow measurements.
5. Rework fine-step feedback ownership to remove full-target history copies.
   `DrawSimulationSample` currently copies the interval base back into a ping-pong
   target for intermediate samples. Bind the immutable history directly while
   rendering into a separate output, committing only the legacy endpoint. Must
   retain the existing exact feedback/presentation-independence tests. This has
   increasing value at large resolutions and on integrated GPUs.
6. Profile native transaction overhead. Writes pass through virtual memory
   interfaces and ordered maps that retain original words for rollback; worker
   preparation also stages writes and keeps its own local data. These are CPU
   costs inherited from correctness-oriented reconstruction. Optimize tracking
   with measured access patterns while retaining rollback/reference tests.
7. Evaluate CRT separately. It can add glow-pyramid and multiple fullscreen
   passes; psychedelic mode has additional history. It can be the GPU bottleneck
   even when the game renderer is inexpensive. Current feedback uses RGBA16F
   targets at scene resolution, plus simulation history and ARGB scene output.
   Do not switch to 8-bit history as a blanket optimization: rounding changes
   accumulation, especially brightening and partial fade behavior.

No broad multithreading or renderer replacement is justified yet. The strongest
initial implementation target is shader-based CPU-painted effects, with real
scene measurements to determine priority and validate the benefit.
