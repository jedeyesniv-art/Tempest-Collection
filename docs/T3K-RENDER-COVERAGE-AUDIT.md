# Rendering coverage audit

2026-11-26, updated after the rendering correction pass. Scope: the whole game, including preparation, display-list
construction, raster dispatch, pixel blending and feedback. The ring course
is the first detailed matched-output case, not the audit boundary.

## Evidence standard

Keep these separate:

1. A drawing command is recognized.
2. Its primary mode, secondary selectors, signed fields and guards are handled.
3. Its output agrees with original NUON rasterization on captured input.
4. Its higher-frequency feedback behaves correctly over time.

CPU/GPU agreement alone cannot establish item 3: both paths shared the score
opacity signedness bug. A small mean image error also cannot establish complete
coverage; a localized or brief omission can be visually important.

The inventory reads the original 19-module descriptor table at `800B14A0`.
Disassembly is bounded to each descriptor's exact code length, and code hashes
are recorded. Reading a fixed-size block past the end would misattribute the
next module's instructions. Adjacent pointer runs can combine primary dispatch
tables with pixel-row/format helpers; they are not automatically extra command
modes. The reviewed primary-table boundaries are explicit in the audit tool.

## Original module map

| Module | Responsibility | Current assessment |
| --- | --- | --- |
| 0 | Rectangle, copy, textured plane dispatch | Main clear and plane commands mapped; other original modes need usage/semantic review |
| 1 | Vector stream and stroke rasterization | Stream commands 0–9 implemented; transforms and stroke/blend fidelity need original-output cases |
| 2 | Shapes/text/lightning preparation | Native semantic worker; reference data-equivalence coverage exists |
| 3 | Texture preparation | Native semantic worker; reference data-equivalence coverage exists |
| 4 | Zoom/rotate/fade feedback | Observed row selector 0 mapped; other row behavior and borders need review |
| 5 | Textured rings | Mapped; retain original-output and signed-intensity boundary tests |
| 6 | Data-animation language | Native semantic worker; reference data-equivalence coverage exists |
| 7 | Numeric-overlay preparation | Native semantic worker; reference data-equivalence coverage exists |
| 8 | Point rings, jump, burst | All three primary modes mapped; radial aliases and inverse square path have contracts |
| 9 | Particles, point images, title wave | Observed modes mapped; additional dispatch entries and pixel-row selectors require review |
| 10 | Web geometry preparation | Native semantic worker; bounds and fractional sidecar tested separately |
| 11 | Textured surfaces | Signed dispatch and per-lane chroma implemented; original-raster fixtures retained |
| 12 | Projection preparation | Native semantic worker; reference data-equivalence coverage exists |
| 13 | Glowing numbers | Decimal mode mapped; signed opacity corrected; conditional texture reuse and unusual mapping blocks remain review targets |
| 14 | Disc fields and shapes | Observed modes 0 and 2 mapped; mode 1 and secondary row variants unverified |
| 15 | Ripple feedback | Mapped; retain original-output tests and separate cadence validation |
| 16 | Disc grid | Observed primary mode mapped; template/row variants need semantic review |
| 17 | Depth effects and ring-course geometry/simulation | Native semantic worker; motion, collision ordering and discrete events tested |
| 18 | Drawing coordinator/worker distribution | Host replaces hardware scheduling; depth/work-buffer writes retained; no blanket claim that this proves every raster side effect |

The seven preparation workers have a separate original-reference corpus:
2,660 cases currently match with zero data mismatches. This is not 2,660
independent pixel-rendering tests.

## Primary dispatch entries not mapped by the shared decoder

The reviewed tables expose these entries without an equivalent full command
case in `BuildFrom`:

| Module | Mode | Interpretation of current evidence |
| --- | --- | --- |
| 0 | 0, 1, 4, 5 | Additional rectangle/copy-family paths; no usage observed in the first active-game trace |
| 9 | 1, 5 | Original entries require semantic classification and usage evidence |
| 14 | 1 | Original entry requires semantic classification and usage evidence |

The initial audit found eight unmapped entries. Module 9 mode 0 is now
explicitly implemented as its original no-op, leaving seven unverified entries,
**not seven proven missing game features**. Some are unused or alias/helper paths. Do not create speculative
approximations or claim they explain a screenshot until an invocation or
controlled original-code case establishes that connection.

Secondary selectors deserve the same attention even inside recognized types.
In the first active-game trace, zoom uses selector 0, particle drawing uses
selector 1, point rings use selector 1, and the observed jump, disc-shape/grid,
point-image and title paths use selector 0. Unobserved variants are not proven
correct by those observations. The inventory preserves complete command words
so further flag/signedness checks need not rerun the game.

### Closed source-level omission: per-lane web colour interpretation

Original module 11 at `20300586..203005A8` reads a lane mask and conditionally
changes `uvctl` from `104CC010` to `004CC010`. This changes the texture load's
chroma normalization; it is not another primary command type. The mask is read
through the active definition's `+6C`, normally `800AF7F4`, with the upper
sixteen bits selecting lanes. The normal frame begins by clearing this word,
and callback `800140DE` can populate both its lower and upper halves or set all
bits. Therefore it is not merely a hypothetical hardware capability.

The decoder now carries the upper lane mask into both shader mapping and
mesh mapping. Texture chroma receives the original +128 offset before blending;
it is not clipped back to an eight-bit source texture. A half-coverage GPU test
distinguishes correct Cr=200 from the incorrect early-clipped Cr=191.

A controlled original-code fixture forces the mask at the original definition
DMA boundary. On its completed captured picture, mean RGB error falls from
11.173 to 0.992 byte levels after the complete correction pass. This is a
controlled flag fixture, not evidence that every normal frame sets the mask.
The first renderer-start snapshot precedes the forced mask write and must not
be treated as matched input for that flag. The previous stale-quad bound fix
remains intact.

### Other concrete review targets

- Module 13 conditionally uploads its colour texture only when command word 11
  is nonzero. Native `Text` starts with a zeroed texture array. The corresponding
  original local-texture retention path needs classification and a controlled
  fixture. No zero colour-texture pointer occurs in the first active-game trace.
- Module 4 selects a pixel routine from table `20100EA0` using the low bits of
  command word 7. Native zoom decoding uses its high-half fade but does not
  dispatch these low-bit variants. All observed active-game invocations use 0.
- Module 9 row selectors 0 and 1 resolve to the same radial routine. The next
  adjacent pointer targets a higher-level helper; adjacency alone does not
  establish another valid row mode. Module 14 selector 1 is now implemented as
  its original immediate-return row, and module 16 selector 1 as its flat
  circular blend (target + (destination-target)*I). Primary modes and secondary
  row selectors remain distinct.

## Runtime coverage

The first original-code active-game run recorded 75,454 drawing-entry
observations and 16 distinct command types. All 16 have shared-decoder cases.
Its scenes include opening, particle crawl, animated menus, level selection,
gameplay, level entry/clear, transitions and ordinary bonus presentation.
The driver trace distinguishes active gameplay, both entry phases, level-end
travel and the level-clear animation. Entry observations can repeat at dispatch
boundaries; the count is not a performance measurement or a count of unique
pictures.

A completed no-fire run adds 49,364 entry observations, including death and
game-over. Together the two runs contain 124,818 observations and the same
16 recognized command types, with no unrecognized type observed. Original
death return sites `8001EDC0` and `8001EC68` establish actual death coverage.
An earlier no-fire attempt stayed in level selection because its final button
press occurred too early. It is menu evidence, not death evidence.

This is a broad first sweep, not exhaustive proof over 128 webs, every tool,
every high-score/name-entry state, every two-player route and every randomized
effect preset. Scene coverage and mode/flag coverage are independent axes.

## Ring-course finding: the validation route was misleading

The original captured ring commands start with `00300004` zoom feedback, while
the old native `--check-warp` route started with `00000300` opaque clear.
The relevant original background-selection word is `800B850C`: 0 selects the
feedback object template `800B3090`; 1 selects clear template `800B2E50`.

The native diagnostic jumped directly from active gameplay into the course,
bypassing `settle_session_life`, which normally restores that word to zero
before the level-clear/bonus sequence. The production state machine already
performs this cleanup. Therefore this discrepancy was a test-route error,
not evidence of an absent blur shader or a production scene-port omission.

The route now calls the real settlement function before the existing direct
course invocation. It does not force a rendering flag in the course or GPU.
With normal settlement and optional vector sharpening disabled, the broad
soft feedback is present in the native course capture too.

## Matched original raster comparisons

`t3k_renderer_replay` loads captured original system/main/renderer-local state
and decodes through production renderer code. It does not execute Nuance.
Previous feedback is imported as raw YCrCb into a float target, without an RGB
round trip that would clip latent chroma. Output is compared at 360×240, with
CRT, sharpening and interpolation absent. `t3k_render_reference.py` associates
renderer captures with the correct original presented framebuffer and records
whether memory was captured at renderer start or the completed boundary.

The two captured course frames currently give mean absolute RGB differences
of 2.366 and 2.000 byte levels, with RMS differences of 8.114 and 7.224. These
numbers locate residual approximation error; they are not acceptance thresholds
or a completeness score. Thresholded faint-pixel counts are particularly
sensitive near black and should not be presented as a percentage of missing
glow.

The isolated leading zoom operation on the first course frame gives mean
absolute error 0.498 and RMS 0.939. Its intermediate original snapshot can have
other worker activity, so this is diagnostic localization rather than a
standalone golden unit contract. Stage snapshots must respect original
parallel raster ownership when making stronger per-operation claims.

The capture records four pixel programs: feedback, numbers, textured surface,
vectors. No additional pixel program was observed in these two course frames.
That does not establish that other course states cannot use additional paths.

## Next high-value work

1. Expand original mode/flag observations across name entry, two-player states
   and additional effect presets. The web chroma fixture is now implemented.
2. Classify the eight unmapped primary-table entries and the unobserved
   secondary row routines; establish actual use before implementing them.
3. Grow matched original-raster fixtures, especially vector stroke coverage,
   number mapping/opacity extremes and explicit-quad texture blending.
4. Audit feedback sampling policy independently. Supported single-leading-feedback pictures now use
   time-integrated deposition in every scene; interleaved operations retain
   their reference composition. A high-rate glow difference there is an adaptation question, not
   necessarily an unimplemented original drawing function.

## Tools and local evidence

- `tools/t3k_render_coverage.py`: original module/code-boundary inventory,
  reviewed primary modes, snapshot signatures and complete entry-trace counts.
- `tests/test_t3k_render_coverage.py`: synthetic checks for exclusive code ends,
  out-of-segment dispatch pointers and incomplete trace records.
- Optional CMake reference target `t3k_renderer_replay` and
  `tools/t3k_render_reference.py`: matched original raster comparisons.
- `T3K_RENDER_CAPTURE` / `T3K_RENDER_CAPTURE_FRAME`: read-only native memory
  and drawing-source provenance capture at a published picture boundary.
- Local external artifacts: `research/tempest-profile/runs/render-coverage`.

Original assets, raw RAM and disassembly stay external to the source/handoff
package. Stable revision-2 deliverables are unchanged.

## Implemented correction pass

See `T3K-RENDERING-CORRECTIONS.md` for the original stroke-weight oracle,
particle equations, operation-based feedback routing, matched image results,
motion comparison and regression checks. Remaining unused primary modes and
conditional number-texture reuse are still explicitly unverified; this pass
does not silently approximate those functions or claim complete game coverage.

## Subsequent investigation

See `T3K-RENDERING-REMAINING-PATHS.md`. Frame interpolation is retired, module-0
copy mode 1 and raw zoom selector 1 are implemented, conditional number texture
loads use existing renderer-local input, and remaining table entries have more
specific classifications. The renderer-local ownership and unimplemented mode
limits remain explicit. Colour timing checks cover six authored rates.
