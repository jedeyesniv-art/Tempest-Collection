# Elapsed-time feedback and motion comparison

This trial supersedes the clocked-echo visual policy. The old policy remains
available for comparison, and its tests still verify that original-rate
rendering has not regressed. Matching those checkpoints was insufficient to
establish high-rate motion quality.

## Scope and model

The opening title uses a ripple/colour-relaxation feedback operation with a
0.1875 blend toward its target colour. The previous transition-only changes
covered brightening zooms, so the title still used history pinned for five
samples at the default 24/120 ratio.

The trial now updates history at every fresh sample in opening, level-end travel, level-clear,
shared-transition and entry scopes, for pictures containing one leading zoom
or ripple followed by drawing. Gameplay and other unreviewed feedback scopes
retain their previous policy. Interleaved feedback/fill sequences are not
silently treated as one compositing operation. CRT is off in these tests.

The source is rendered once into a transparent premultiplied layer using the
original drawing paths and overlap order. This yields colour P and coverage a
per pixel, after overlaps have been composed. The transport pass advances the
feedback's zoom, rotation or ripple displacement by elapsed authored time.

For a frozen local paint operation with feedback retention/gain f and neutral
colour n, the full update is H_next = A H + B, where:

    A = f (1 - a)
    B = P + (1 - a) (1 - f) n

A fine interval dt uses A**dt and B (A**dt - 1) / (A - 1), with the dt limit at
A=1. This preserves the coupled frozen-pose fixed point, rather than scaling
each primitive separately or linearly mixing a finished picture with its
background. Fully opaque coverage remains opaque; small contributions are
approximately time-weighted. There is no geometry interpolation or five-sample
paint/deposit gate.

Transport and local paint are split approximations of the full spatial
operator. Moving, overlapping geometry, feedback clipping and repeated spatial
filtering prevent a claim of exact fractional reconstruction of the original
renderer. That is why motion clips and real-time trail measurements accompany
the frozen-pose arithmetic checks.

Brightening zooms also need the existing reference renderer's colour-growth
threshold: that renderer deliberately truncates brightening byte deltas toward
neutral to avoid colour drift. Naively exponentiating a
gain can turn a one-byte colour difference into a large colour wash. The new
shader preserves that small-signal threshold with a per-channel effective gain
while accumulating paint every sample. A quarter-byte neutral deadband rejects
floating-storage noise. This does not quantize whole pictures or delay deposits.
The source pass retains original brightening stroke widths and uses the
existing vector, number, web, disc, glyph and plane drawing paths.

## Motion evidence

`deliverables/tempest-motion-blur` contains equal-duration, normal-speed MP4s:

- Opening: 5.25 seconds, original-rate native renderer at 24 Hz versus previous
  120 Hz pinned history versus the new 120 Hz accumulation. The reference
  pictures are held for display, never motion-interpolated. This reference is
  an original-rate rendering path, not a hardware recording or Nuance trace.
- Level transition: 5 seconds of identical scripted gameplay, previous
  clocked echoes versus new accumulation. Enlarged score crops remain animated
  alongside the complete pictures.

Both comparisons have 60 and 120 fps exports. The source always captures fresh
actual simulation pictures; the 60 fps export selects alternate samples.
Native frame/value/effect traces match byte-for-byte between the two transition
runs. Encoding/decode checks confirm expected frame counts and durations.

During the opening movement, consecutive-frame change is normalized by each
five-sample block's mean to remove slower scene evolution. The old phase means
range from 0.777 to 1.525; the new means range from 0.988 to 1.017. Thus the
large old-cadence pulse is absent from this diagnostic. Mean RGB during that
window changes from 7.045 to 6.651 (about -5.6%, on a 0-255 channel scale).
This measures cadence-linked variation, not perceptual quality or hardware
fidelity. The clips and hands-on playback remain the visual acceptance check.

## Checks and practical limits

- A frozen overlapping-paint GPU fixture follows the coupled analytic fixed
  point for ripple decay and neutral-gain zoom, within three output levels.
  Half-float storage and native raster paint quantization remain measurable.
- A moving two-pixel stroke fills 30/30 trailing sample columns, versus 12/30
  with pinned history. Whole-trail mean is about 3.8 versus 2.9 on a 0-255
  channel scale: roughly one output level higher, although a substantial
  relative difference in this deliberately faint fixture. This is not an
  exact-energy claim; original quantization and effect-specific tuning remain.
- The 0.8125 retention half-life is 17 fine samples, versus 16.69 analytically,
  retaining the intended real-time duration at fivefold sampling.
- A one-byte colour deviation remains stable through 300 fine brightening
  updates, rather than becoming an expanding pigment wash.
- Renderer tests cover all four feedback policies and extra display refreshes;
  original-rate memory/GPU replay checks remain separate.

The 60 Hz-display path still performs every needed 120 Hz history update.
This trial does not claim all framebuffer effects have been audited or that
score-ghost motion is signed off. Repeated fractional bilinear transport can
change spatial softness, and further effect-specific work may be needed.

## Reproduction

The candidate executable is `build-native/Release/t3k-motion.exe`; the existing
`t3k.exe` is retained for comparison. Build it with:

    cmake --build build-native --config Release --target t3k --parallel 4 -- /nr:false /p:TargetName=t3k-motion /p:BuildProjectReferences=false

The existing checks accept `-Binary build-native/Release/t3k-motion.exe`.
`T3K_PINNED_TRANSITION_FEEDBACK=1` and
`T3K_CLOCKED_TRANSITION_FEEDBACK=1` select old diagnostic policies; normal trial
launchers use the elapsed-time model. Original 24 Hz rendering is unchanged.

Read-only raw captures use T3K_MOTION_CAPTURE, T3K_MOTION_START and
T3K_MOTION_COUNT; each raw RGBA file has a frame-metadata CSV beside it. These
GPU readbacks deliberately stall execution and must not be used for performance
measurements. `tools/t3k_motion_blur_compare.py` encodes the captured datasets
and writes motion diagnostics. Raw evidence is under
`research/tempest-profile/runs/motion-blur-*`. John's frozen handoff is unchanged.

## Separating displacement from accumulation

A pure-transport fixture adds no new paint and uses a constant one-pixel ripple
translation per authored tick. Eight original updates move a two-pixel stroke
with horizontal variance 0.25. Forty one-fifth updates reach a centroid within
0.097 pixel of the original, retain 98.24% of the visible signal, but increase
horizontal variance to 6.143. Repeated fractional bilinear sampling therefore
introduces substantial extra spatial diffusion even when timing and paint
accumulation are removed from the problem.

This diagnostic deliberately reports rather than blesses the variance
increase. The current gameplay movies include the effect of that changed
resampling as well as the new source integration; they are not an isolated
proof of accumulation fidelity. Frozen overlapping-paint, zero-displacement
moving-source and no-paint transport fixtures separate those questions.
Reducing numerical diffusion while retaining the intended native filtering
is the next transport target. A generic sharpening filter would not establish
that result.

## Final checkpoint checks

Four CTest targets and nine scripted routes pass. Original-rate replay retains
32 zero-mismatch GPU/memory comparisons. Renderer tests retain 1,568 display-
independence states plus dedicated paint, decay, pigment and transport checks.
Live simulation measures 120.17 updates/s with 60 presentations/s. A separate
GPU-fenced 1440x1080 capacity check, CRT off and no raw readback, records p95
costs of 1.432 ms for original scene 0 (opening) and 1.722 ms for original
scene 7 (including relevant entry/transition work), excluding the first 30
samples of each scene. These are capacity figures on this machine, not pacing
reference measurements. Evidence is in `time-integrated-1080-cost.json` and
its CSV in `research/tempest-profile`.
