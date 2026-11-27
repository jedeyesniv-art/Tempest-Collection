# Rendering correction pass

2026-11-26. This implements verified gaps from the broad rendering audit.
Stable revision-2 packages and the frozen handoff patch remain unchanged.

## Web colour interpretation

Original raster module 11 changes UV chroma normalization for lanes selected
by the upper half of definition+6C. The native decoder now retains that mask.
Both the direct scanline shader and reference mesh mapping add 128 to texture
chroma before blending. The offset can exceed the ordinary byte range, so it
must not be baked into a clamped eight-bit texture. A GPU fixture with half
coverage checks Cr=200, versus 191 from premature source clipping. Selected
and unselected lanes are checked together on both mapping paths. Inline
course quads remain independent of the ordinary web definition.

An opt-in external Nuance fixture forces FFFF0000 immediately before the
original definition DMA. It then executes original raster code. The completed
second frame is valid matched evidence; the first renderer-start state was
captured before the forced write and is not matched flag input. Completed-frame
mean RGB difference falls from 11.173 to 0.992 byte levels after the full pass.

## Vector strokes

The original module's arithmetic is now distinguished from John's fitted
line appearance. Source colour Z is a signed 8.24 lane. Original endpoint
strength before coverage is:

    I = (word5 >> 2) / 2^30 * (1 - jitter / 65536)
    peak = I * signed_colour_Z / 32

Radius is the original width field, rather than width minus an empirical 0.06.
The shader evaluates linear radial distance to the segment, including circular
caps, and uses the original padded dominant-axis colour ramp. Peak weights
are not clipped before evaluating coverage. GPU float blending retains signed
weights and weights above one. Original subpixel geometry remains available;
there is no interpolation between game pictures.

A controlled external oracle single-steps the original vector module while
capturing segment/point/weight observation sites. Module identity is checked
against its original entry bytes, not the size of an IRAM allocation. The
first attempt to observe internal sites at compiled-block boundaries was
insufficient and is not used as the stroke oracle.

`tools/t3k_stroke_reference.py` checks the original integer weight chain:

    peak = signed32((normalization * colourZ) >> 29)
    alpha = signed32((peak * remaining_radius) >> 16)

26,523 packet weight observations give zero integer mismatches. They include
1,559 weights above one and 29 negative weights. These are observations,
not unique pixel counts. The continuous capsule's remaining-radius approximation
has about 0.018 game-pixel mean error against this fixture. Original fixed-point
edge walking, clipping, channel storage and roundoff are not reproduced exactly.

Two matched gameplay frames improve from mean RGB errors 2.058/2.143 to
0.868/0.883, and RMS 7.488/7.654 to 2.985/3.094. Two course-frame means are
2.238/1.853 versus the preceding 2.366/2.000. Additional original captures
of a level-entry picture and a plane/grid background produce means 0.537 and
1.669. These are localized comparisons, not a percentage of game coverage.

The GPU path batches 64 strokes per uniform upload and uses six vertices per
stroke. Unsupported non-GPU drawing backends retain an approximate mesh path.
The collection's optional sharpening still applies its existing core/glow
policy; reference comparisons disable it.

## Particle paths

A fragment shader now evaluates the original radial equation continuously:

    alpha = I * max(0, 1 - distance_squared / radius_squared)

This removes the old 64x64 eight-bit disc-mask sampling from the GPU path.
Original module 9 row selectors 0 and 1 are aliases of this same radial routine.
The adjacent pointer after these two is not assumed to be another valid row
selector solely because it points into code.

Module 14 row selector 1 is an original immediate return; title-field and shape
paths now respect it. Module 16 row selector 1 uses constant blending inside
circular coverage: target + (destination-target)*I. It now uses a distinct flat
disc profile with blend alpha 1-I. Radial coverage remains the default. The
non-GPU fallback approximates flat coverage with a circular mesh.

Module 9 primary mode 0 is also explicitly recognized as its original no-op.
It no longer appears as a missing primary implementation. This leaves seven
unverified primary table entries from the initial inventory, not seven proven
missing visible effects.

## Feedback and signed paint

The host previously enabled time-integrated deposition for a list of scene
names. Eligibility now follows the actual picture: one leading zoom or ripple,
followed by source drawing, with no interleaved clear or additional feedback.
The same supported composition therefore works for course, death, menus and
other particle scenes. Multiple/interleaved feedback operations retain their
existing composition; they are not flattened into one pass. Original-rate
updates still execute full updates.

The paint shader also preserves uncovered gain above one when the original
vector weight is negative. Clamping paint alpha to [0,1] previously discarded
that term. A frozen negative-weight stroke checks five fractional updates
against one original full update within four RGB byte levels. Weights beyond
one can produce a negative full affine multiplier, which has no real-valued
fractional power; that high-rate case remains a bounded approximation. Original
per-primitive byte clipping versus float intermediate storage is another known
fidelity limit.

A three-second course comparison uses 360 fresh 120Hz samples, identical
simulation/input and stroke rendering, with CRT, sharpening and interpolation
off. Frame/value/effect traces match byte-for-byte between the policies. Mean per-frame image change grouped by
five-sample phase ranges 2.157-3.453 with pinned history, versus 1.851-2.084 with
continuous deposition. Mean image brightness changes from 30.745 to 37.934
(+23.4%). This shows a temporal and energy change, not exact agreement with
hardware. The course input script is keyed to samples, so cross-rate runs
cannot be used as equivalent-input cadence or brightness oracles.

The local comparison clip is:
`deliverables/tempest-rendering-audit/ring-feedback-comparison-120Hz.mp4`.
Both columns use the new strokes; only feedback policy differs. Neither column
is a hardware recording.

## Verification and remaining scope

- GPU suite checks masked web blending, original vector decoding, circular
  stroke caps/strong sides, signed paint, radial/flat particles, feedback
  eligibility, original no-ops and existing timing/cache/viewport contracts.
- Original, 60Hz and 120Hz course validations pass. Additional 120Hz checks
  reach gameplay, death, game over, name entry, entry, travel, clear and bonus.
- CRT and interpolation are off in runtime/reference/performance checks.
- Synthetic 1,000-stroke workload completes in about 0.51-0.53ms at 1080p and
  0.93-1.01ms at 2160p on this machine. This is not whole-game FPS or a
  controlled old/new benchmark.

The unobserved rectangle/copy primary paths, module 9/14 helper-like primary
entries, alternate zoom selectors and conditional number-texture reuse remain
unverified. None of 19,355 observed number commands in the two original-code
runs requests a zero colour-texture pointer. No fabricated rendering approximations are installed for them.
Higher-rate moving feedback and exact original scanline/storage edge cases
remain visual refinement work. The audit report continues to track these
limits rather than declaring the renderer complete.

External RAM, original code and raw raster fixtures stay under
`research/tempest-profile/runs/render-coverage`, outside source/handoff assets.
