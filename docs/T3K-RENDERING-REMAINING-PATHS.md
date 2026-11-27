# Remaining rendering paths, interpolation retirement and colour timing

2026-11-26. This follows the broad coverage audit and rendering correction pass.
Stable handoff packages remain unchanged. The Rendering Trial launcher uses the
updated source-built executable.

## Interpolation retirement

Removed the native Smooth motion option, its default setting, F6 toggle,
intermediate-picture matching/blending functions and the host presentation
scheduler which generated those pictures. NativeFrameRenderer submits actual
simulation pictures directly. Live vector identity records are no longer built
for matching between frames. Sharp vector styling remains unchanged.

Old native_interpolate values in settings files have no effect. The legacy
archive layout is retained with empty previous-picture data and reserved timing
slots. Settled legacy states can load; unfinished generated-frame states are
rejected rather than resumed with an unrelated feedback image. Compatibility
is checked by decoding old-layout records and reading new records through the
old field sequence. No interpolation engine remains behind that file shim.

## Controlled original-code fixtures

An external Nuance export loads captured RAM, original module code/data and a
synthetic display entry, then executes original NUON packets with a bounded
budget. Both memory buses must be enabled: an early fixture version incorrectly
disabled other-bus DMA, so system-memory loads silently did nothing. Results
from that version are superseded. Successful fixtures must return to the sentinel;
a partial image from a budget-exhausted run is not an oracle.

Fixtures reference existing captured banks through base.txt rather than making
another full RAM copy for every case. Renderer-local results are CRC-verified
in post-local-evidence.zip. No original game data is added to source packages.
The replay tool's opt-in T3K_RASTER_FIXTURE applies the same external entry and
patches to captured input before using the production native decoder.

## Rectangle/copy primary paths (module 0)

| Mode | Recovered purpose | Current status |
| --- | --- | --- |
| 0 | General mapped rectangle, two optional resident 16x16 textures, polynomial mapping block and four row routines | Classified; not implemented. Zero-filled mapping fixture does not complete and is not valid evidence for pixel equivalence |
| 1 | Same-coordinate framebuffer copy, offset coordinates, blend toward a constant colour using a full signed 2.30 coefficient | Implemented; controlled original fixture has zero RGB differences across the entire 360x240 image |
| 2 | Bilinear procedural textured plane | Existing implementation |
| 3 | Solid rectangles / rectangle-list fill | Existing direct fill path; signed origins corrected |
| 4 | Packed 8x8 one-bit glyph/pattern stream, palette header and inline foreground/background changes | Classified and exercised in original code. Ordinary and palette fixtures work; clipping exposes scratch-buffer/addressing details still to recover. Experimental simplified decoder was removed rather than shipping an inaccurate clipped path |
| 5 | Nearest sampled procedural plane, blended using sampled Z | Implemented in a separate GPU path. Constant, curved/chunk-crossing and clipped original fixtures validate mapping and blend weights |

Mode 1 offsets both the source and the final destination coordinates. Treating
word 2 as only a source offset puts the result in the wrong place. Its source
uses the game's 360-pixel framebuffer pitch. The native implementation keeps
original signed arithmetic before byte saturation. Unsupported source memory
and unverified modes must not be mistaken for successful visual coverage.

## Module 9 and 14 helper-like entries

Module 9 primary 1 points to the register-driven rosette helper at 20300DAC.
Primary 5 (20300BBE) is a real higher-level list renderer: it prepares sixteen
source records, a palette/packed shape stream and calls that helper. The helper
places repeated points through mirrored/rotated positions before the ordinary
disc rasterizer. These two table entries are not two interchangeable ordinary
particle decoders. Primary 5 remains a genuine unimplemented path with no runtime
use seen in the original traces collected so far.

Module 14 primary 1 points inside the register-driven ring/petal loop starting
at 203005D8, after primary 2 has prepared its counts, steps and resource state.
The analogous calculations are part of the native FlowerAt implementation used
by normal shape drawing. A direct display-entry-only fixture lacks that caller
register contract and exhausts its budget. This does not establish another
missing visible scene or justify fabricating a new independent command format.

## Alternate zoom selectors

- Selector 0: ordinary bilinear single-source feedback, already implemented.
- Selector 1: raw nearest-neighbour word copying. Implemented separately; it
  bypasses the ordinary fade coefficient and uses integer-corner addressing.
  The controlled original fixture has mean RGB error 0.292 byte levels, RMS
  1.094 and 53 pixels over an eight-level difference. It is not enabled for
  unverified fractional feedback accumulation.
- Selector 2: additional image data is loaded and bilinearly composed using
  colour/Z and edge-dependent choices. The original fixture completes after
  2,501,286 packets; an earlier two-million budget was too short. This is not
  equivalent to ordinary zoom or a mere filtering toggle. Native decoding now
  rejects it explicitly instead of silently drawing selector 0. It remains open.

These fixtures are controlled inputs, not evidence that the original game uses
these selectors in the scenes sampled by the earlier runtime audit.

## Conditional number-texture reuse

Module 13 skips its colour texture upload when word 11 is zero. It still samples
16x16 texture data at local 20100930. The decoder now reads that existing local
texture instead of attempting to read an absent system pointer and obtaining
an all-zero colour map. CPU and GPU tests match explicit upload versus resident
texture input exactly. A controlled original retained-texture fixture has mean
RGB error 0.015802, RMS 0.153960 and no pixels differing by more than eight.

This closes the decoder's null-upload interpretation, not all persistent scratch
ownership. The same local area is overwritten by module uploads, procedural
plane/pattern operations, zoom tiles and shape preparation within raster modules.
A global last-number-texture cache would therefore be incorrect. Native orchestration
still needs a complete semantic account of those raster-local writes before
cross-module retention can be claimed faithful. None of the 19,355 observed
number commands in the earlier original-code runs used a zero upload pointer.

## Colour cycling at high frame rates

No shared-clock acceleration was found in the reviewed paths:

- finish_driver_frame advances the optional scene colour stream only on an
  authored boundary; presentation and input polling still occur every sample.
- Animation byte/palette rotation commits on authored boundaries.
- Oscillators evaluate fractional phase for fresh geometry/colour output while
  committing persistent phase at authored boundaries.
- Texture evolution is held between authored updates; the renderer's texture
  clock is explicitly separated from fine rendering samples.
- Web advance_colour counts adjacent lanes/vertices within a job and is reset
  from module data. Slowing that spatial counter would change the web's palette
  layout rather than correct an animation speed.
- Menu palette selection is driven by scene/setup events, not display refreshes.

Expanded tests exercise the actual frame driver at 120 samples/second for
16, 20, 24, 30, 36 and 48 authored Hz. Each presents 120 times, advances its
colour stream exactly the chosen number of times and rotates palettes only at
those same boundaries, including non-divisor rates. These checks preserve the
chosen per-scene pacing; they do not establish exact hardware speed or prove
every object-specific colour script has been observed. No blanket colour slowdown
has been applied without evidence of a remaining accelerated path.

## Release boundary

The source-built Rendering Trial is updated. The old stable binaries and handoff
ZIPs are untouched. Unknown primary formats, packed-pattern clipping, zoom
selector 2 and cross-module renderer-local retention remain explicit follow-up
work. Their classification is now narrower than the initial table inventory.

## Verification checkpoint

All four regular CTest suites, the GPU timing/render suite and the three
coverage-tool tests pass. The colour tests exercise finish_driver_frame itself,
not a test-side imitation of its gating. Original, 60Hz and 120Hz course checks
pass with an old native_interpolate=1 setting deliberately present. A final
Web 9 combat check uses the rebuilt executable. The binary remains reachable
through Play-Tempest-Rendering-Trial.cmd. No stable archive has been updated.

## Implemented Z-weighted plane follow-up

Mode 5 now has a dedicated fragment shader and keeps sampled Z separate from
texture colour. It uses nearest sampling and the original signed coefficient
Z/256 * 8191/8192. The ordinary opaque bilinear plane is unchanged. Raw mapping
parameters are decoded once; no CPU mesh is generated for the new shader.
Texture caching includes Z, so changing alpha without changing colour at the
same tick cannot reuse a stale upload.

A constant reference fixture gives mean RGB error 0.008889 byte levels. Varying
textures with curved mappings over a 130-column rectangle (crossing 64-pixel
chunks) give 0.017353 mean, 0.177691 RMS, and zero pixels above eight levels.
The clipped counterpart gives 0.014221 mean, 0.160247 RMS and zero above eight.
These reference cases use nonnegative signed Z. GPU contracts additionally
check signed -1, zero, 64 and 127 blend weights, rectangle coverage and cache
mutations.

Negative-Z stress inputs exposed a reference-emulator peculiarity: Nuance's
SaturateColorComponentsOrg uses YLookup={valid,255,255,255}, so negative
luminance can become white on storage. The native float path follows signed
blend arithmetic and does not reproduce that white underflow. Negative-stress
images therefore do not match Nuance at those pixels. This is an explicit
reference limitation, not evidence that the positive-Z mapping fixtures cover
all signed pixel-storage cases or prove real-hardware saturation behaviour.
