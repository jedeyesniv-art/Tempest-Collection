# First GPU effect and feedback optimization checkpoint

Later checkpoint: `T3K-GPU-NUMBERS-WEB.md` adds GPU numbers and scanline web
mapping. Statements below describe the earlier ring-only checkpoint.

The 120 Hz trial now generates the `00500005` textured ring with a fragment
shader instead of CPU per-pixel image generation. It retains the original 2x
sampling grid, ellipse coefficients, mirrored columns, texture wrapping and
bilinear lookup, byte colour rounding and premultiplied opacity. The generated
image uses the existing display operation and filtering. Only 128 original
texture words and 16 scalar parameters need to reach the renderer.

Glowing-number text remains CPU generated. This checkpoint does not offload
all image effects. Ring shader creation is optional: decoding falls back to
the CPU path if unavailable. Set `T3K_CPU_EFFECTS=1` before starting the game
to compare the old CPU ring path. The 24 Hz reference and validation hosts
retain CPU decoding; John's frozen handoff is unchanged. The existing 120 Hz
launchers use the new path, including their original-cadence scenes.

Fine-step feedback now pins immutable history by swapping texture ownership.
Two scratch targets alternate for actual new simulation samples. The decoder
never samples from its active render target, and only the final legacy endpoint
becomes history for the next interval. This removes the full-target copy per
drawn fine sample. All history textures keep linear sampling; using nearest
sampling on a swapped target changes zoom history and fails the regression.

## Evidence

- Twelve CPU/shader image comparisons: radii 10/50/100/200, elliptical aspect,
  three band widths, signed texture offsets and varying colour texture. Maximum
  displayed channel difference was 1/255; maximum mean absolute byte error was
  0.000127. These are constructed cases, not exhaustive original-game coverage.
- All 32 darkening/brightening zoom, ripple and moving-deposit history checks
  match exactly with irregular fine-sample skipping and display counts.
- Original-memory replay: 32 frames, zero mismatches, 32 rollback checks,
  framebuffer independence and read-only decoding retained.
- Synthetic large-ring CPU decoding: 10.7244 ms before versus 0.007097 ms for
  GPU parameters. Completed GPU drawing, including submission and final display,
  measured 0.356557 ms. RTX 4070 Ti machine; single diagnostic run, no game,
  audio or CRT in this benchmark. This is not a measured whole-game speedup.

The benchmark and image checks live in `tests/t3k_timing_render.cpp`.
`T3K_RENDER_BENCH=1` selects the opt-in benchmark; normal execution runs checks.
Raw logs: adjacent `tempest-profile/gpu-effects-bench.log` and
`gpu-effects-validation.log`.

## CPU parallelism assessment

The useful boundary is immutable render input followed by independent output
generation, not arbitrary gameplay objects. Callbacks and ordered aliased memory
writes must remain ordered. SDL GPU operations and texture lifetime stay on the
render thread.

Independent residual image jobs could run on a bounded worker pool after their
textures/parameters are copied, with results published in display-list order.
However, shader conversion removes that CPU work altogether and is preferable
for ring/text sampling. Native worker preparation is currently synchronous, with
explicit submit/fence ownership and isolated worker-local data. Before executing
jobs concurrently, establish their shared-memory read/write dependencies and
stage outputs for ordered commit; separate local windows alone do not prove
independence. Transaction maps are not thread safe.

Profile real scene stage costs before adding threads. Avoid spawning per frame,
overlapping dependent phases, or moving native audio behind an unbounded queue.
The next shader target is glowing-number `Text`; afterward, use measured CPU
preparation hotspots to decide whether worker parallelism pays for its overhead.
