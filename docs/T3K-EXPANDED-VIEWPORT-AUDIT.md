# Expanded viewport investigation — experimental, after stable

Historical audit of stable revision 1; the requested HUD/aspect changes were
subsequently incorporated into stable revision 2. This initial investigation predates
the narrower HUD prototype: see [T3K-WIDE-HUD-TRIAL.md](T3K-WIDE-HUD-TRIAL.md).
The current implementation scope is 4:3, 16:9 and 21:9; the 32:9 measurements
below are historical observations only. Captures and measurements are private evidence at
`../tempest-profile/runs/aspect-audit/`; they are not added to the stable handoff.

## What the current native path actually does

`native_live_host.h` gives the original 360x240 coordinate canvas a centered
4:3 display rectangle. With widescreen enabled, it passes a larger surface as
`view` while preserving that original rectangle as `dst`.
`Renderer::Draw` uses `dst` for coordinate scale and `view` for feedback allocation
and visibility. Vectors, blobs and web geometry can therefore appear outside the
old 4:3 bounds; the completed 4:3 image is not simply stretched across the window.
The code treats 360x240 coordinates as a 4:3 presentation; preserve that convention
rather than assuming square source pixels and introducing a new distortion.

Matched native captures: frame 3800, same scripted Level 9 session, height 540,
CRT off, widescreen on, original-resolution presentation off. Tested 720x540,
960x540, 1260x540 and 1920x540 (4:3, 16:9, 21:9 and 32:9). Cropping each wider
image to its central 720 pixels matches the 4:3 reference with maximum channel
error 1/255. This is consistent with unchanged object scale, not horizontal stretch.
The added side areas in this particular gameplay frame are black: the scene's
visible geometry and HUD fit inside the authored 4:3 composition. Opening frame
300 compared at 4:3 and 32:9 likewise retains its center to within 1/255 and has
empty side areas. These samples do not establish coverage for all game scenes.

At unchanged vertical framing, the current mapping exposes logical x ranges:

| Display | Visible width in source coordinates | Bounds |
|---|---:|---|
| 4:3 | 360 | 0 .. 360 |
| 16:9 | 480 | -60 .. 420 |
| 21:9 | 630 | -135 .. 495 |
| 32:9 | 960 | -300 .. 660 |

The missing control is primarily scene framing and layout, rather than a new
GPU projection implementation. Widening alone cannot invent more web geometry
where the finite game world has none.

## Limitations to address

- There is no explicit viewport policy object. Output dimensions, original image
  bounds, visible game bounds, camera framing and HUD safe area are implicit.
- `PlaneOp` extends full-screen quadratic backgrounds a fixed 360 logical pixels
  on either side, instead of building to the actual visible bounds. Some wide
  views fit that extension today, but wider/zoomed-out views can exceed it.
- Fills are expanded by recognizing full-screen 360x240 rectangles; this needs
  explicit background semantics when world and HUD transforms diverge.
- Numbers, vectors, particles, web surfaces and planes all share a draw transform.
  World zoom or pan would move/scale the HUD unless it is deliberately separated.
- The glowing-number decoder reads the same `0x800DC240` value for title and score.
  Neither drawing type nor location alone reliably identifies a HUD element.
- Framebuffer zoom/ripple already carry game-space extents, which is useful, but
  all layers must agree on those extents and effect centers under a new policy.
- Game-side projection/culling still needs a complete audit before claiming that
  zooming out reveals every object. The inspected vector decoder preserves
  negative/out-of-frame positions; that does not prove every producer does.

## Recommended implementation order

1. Add one explicit viewport description: output surface, canonical 4:3 mapping,
   world bounds, and independent HUD safe rectangle. Default behavior must match
   the current renderer. Keep simulation and collision coordinates unchanged.
2. Add controlled world framing (zoom/recenter) and make background meshes,
   full-screen operations and feedback sampling consume those actual bounds.
   Preserve vertical framing by default while expanding horizontal visibility.
   A render-space pan/zoom is not a promise of a new depth-dependent 3D camera.
3. Retain display-list source addresses and scene context in decoded operations,
   then identify score/lives/jump/level readouts by provenance. Apply anchor
   changes to complete groups, not individual digits or guessed screen corners.
   Menus and cinematic title compositions need their own layout policy.
4. Move HUD geometry before the original feedback pass where appropriate. A
   final crisp HUD overlay would remove the score ghosts the stable version
   deliberately preserves. Transition placement/anchor changes must be tested
   in motion, with no sudden jump when entering a different scene policy.
5. Validate 4:3, 16:9 and 21:9, resize changes, gameplay, jumps, death, level
   travel/clear, bonus backgrounds, menu/title scenes and all three frame modes.
   Check extra-world visibility, no anisotropic stretch, no uncovered background
   seams, no HUD clipping and no simulation/input changes. Keep CRT off initially.

Useful user-facing controls would be viewport mode, world zoom, and HUD safe
width/inset. At ultrawide ratios, optional HUD safe-width limits avoid placing
readouts at extreme peripheral edges. Exact defaults should follow motion tests.
No change to the frozen stable package is needed for this work.
