# GPU numbers and scanline web mapping

The existing 120 Hz launchers now enable both optimizations. Original 24 Hz
reference rendering and John's frozen handoff remain unchanged. No interpolation
is used. Native RAM, logic, texture animation timing and feedback cadence are
unchanged by these rendering changes.

## Glowing numbers

The CPU retains digit selection, placement, accumulated texture-origin stepping,
native reciprocal arithmetic and cell sizing. It sends a small glyph/colour atlas
and quadratic mapping parameters instead of painting and uploading a large RGBA
image per digit. The shader reproduces bilinear lookup, signed chroma distortion,
opacity and byte rounding on the original 1x/2x sampling grid. It evaluates the
four surrounding original image samples to preserve the previous image filtering.

Numbers draw directly into the scene feedback target. A separate generated target
per digit was correct but slower in initial measurements; it is not the final path.
Shader initialization failure retains CPU generation. `T3K_CPU_EFFECTS=1` selects
CPU numbers and rings for comparison in the trial.

## Web texture mapping

For convex lanes, a fragment shader computes left/right edge intersections at the
current scanline and interpolates native texture coordinates across that span.
This replaces the approximate 8x8 CPU-generated mesh with two triangles per lane.
Alpha derives from unscrolled v; texture scrolling and wrapping remain separate.
Current precise geometry still supplies both surface and vector edges.

All lanes share one uniform block and one indexed geometry submission. Per-lane
uniform changes caused costly renderer flushes in an initial implementation;
batching is essential. Normalized position coordinates and an explicit addressing
mode avoid SDL's automatic texture tiling. The lane index is encoded in vertex
colour, which the custom shader reads without modulating the resulting colour.

Concave/folded lanes and inputs exceeding the supported 16-lane batch retain the
existing mesh. Missing web shader support also retains that path.
`T3K_CPU_WEB=1` selects the old CPU mesh for comparison.

## Validation and limits

- Nine synthetic number cases: zero, mixed digits with repeated 1s, maximum
  unsigned number, three scales spanning original 1x/2x sampling. CPU and GPU
  displayed output matched exactly with signed texture offsets and distortion.
- All 32 original-reference frames retain exact memory/rollback comparisons and
  original GPU comparisons. A further CPU versus GPU-number/ring comparison on
  those actual frames differed by at most 2/255 in a displayed channel, with
  maximum mean byte error of 0.000002.
- Four independent analytic CPU scanline fixtures cover rectangles, tapered and
  rotated quads, subpixel positions, wrapping, scrolling and multiple lanes.
  Maximum interior displayed channel difference was 3/255. The CPU reference
  quantizes a premultiplied image, while the direct web shader blends float colour.
  A one-game-pixel region around polygon edges is excluded from this numerical
  comparison because coverage rules differ. This is not pixel-exact web coverage
  validation, and the old mesh is not the correctness oracle for texture mapping.
- All 32 feedback-history checks still match exactly. Four gameplay scenarios
  (including level 9 zapper and level 15 analog/jump) and four CTest targets pass.
  The level-9 screenshot was visually inspected for surface/vector alignment.

## Matched idle benchmark

Three interleaved trials at 24 Hz virtual timing, 1440x1080 offscreen GPU target,
32 warmup plus 128 measured pictures, no interpolation, presentation or pacing:

| Configuration | Median pictures/s |
| --- | ---: |
| Preserved build immediately before numbers/web | 440.6 |
| GPU numbers, old web mesh | 492.1 |
| GPU numbers and shader web mapping | 510.7 |

This is about 16% greater throughput for this specific matched idle workload.
It is an offscreen submission benchmark, not fence-completed GPU execution or
120 Hz combat performance. Other scenes/settings need their own measurements.
The baseline binary was hash-checked against the preceding benchmark receipt
before rebuilding. Raw logs and hashes are in adjacent research directory
`tempest-profile/benchmarks/numbers-web/results.json`; reproduction script is
`tempest-profile/benchmark_numbers_web.py`, run from the integration repository.
