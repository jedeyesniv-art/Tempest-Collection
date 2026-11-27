# Wide HUD trial (after the frozen stable checkpoint)

Scope: 4:3, 16:9, 21:9. No 32:9 commitment, world/camera zoom, menu redesign,
particle duplication, particle-density changes or movement/lifetime changes.
This trial was incorporated into stable revision 2 at the user's request.
Revision 1 archives are preserved in previous-revision-1; updated packages
include the explicit 4:3 / 16:9 / 21:9 aspect selector.

Run `Play-Tempest-Wide-HUD.cmd` from the workspace root. This selects the separate
`t3k-wide.exe`, enables widescreen/remaster and disables CRT for comparison. It
retains the current settings directory and selected Original/60Hz/120Hz mode.
Picture > HUD layout chooses Wide corners (default) or Original. The stable
launcher now uses the revision 2 packaged executable.

## HUD implementation

The original object pass now records render-only provenance beside its draw
records: original object, source template, resolved parameters and globals.
The GPU decoder retains each drawing operation's display-list source address.
This lets the bridge identify HUD groups by their semantic data sources:
score parameters, lives string, level string, jump/resource gauge and status
icon/gauge. It does not classify arbitrary numbers/vectors by screen position.
Provenance is cleared for each new frame and on rollback, load and restore.

Only HUD primitives are translated. Left/right offsets equal the additional
horizontal visible margin, preserving original size, vertical position and
corner inset. World geometry, game coordinates and collision/input state are
unchanged. The primitives move before feedback rendering, so their original
trails and score echoes remain in that pipeline. Title and menu compositions
are excluded; gameplay, entry/death, level travel/clear and pause permit HUD
anchoring. Shared generic transitions are conservative and not reclassified.

## Particle finding

No particle culling was relaxed because an original 4:3 cutoff was not
reproduced in the tested paths. The original stable game already draws gameplay
star streaks into both outer regions (matched frames 3847 and 3860). The older
frame-3800 screenshot happened to show a compact moment of that field.

At frames 1086 (opening particle menu), 1803 (player selection), and 2478 (level
selection), all 16:9/21:9 captures contain visible particles on both sides of the
old 4:3 area. The cropped central composition matches the 4:3 version to within
1–2 byte channel steps, with near-zero mean difference. No menu layout or
particle generation/lifetime change was needed. This establishes coverage for
these effects, not a guarantee about every effect in every scene.

A GPU regression also constructs the actual original 00310309 particle stream,
decodes points beyond both old horizontal edges, and verifies that those points
are drawn at both target ratios. Thus both decoding and final rendering are
checked, rather than just a synthetic screen-space rectangle.

## Validation and remaining review

- Four CTest suites pass, including HUD semantic classification, fixed-size
  corner mapping, world geometry preservation and frame-owned provenance.
- A matched full 8 MiB game-RAM snapshot is byte-identical to the prior build.
- The complete 4:3 frame and selected central world areas at 16:9/21:9 are
  pixel-identical to matched stable captures.
- All three frame modes complete gameplay checks; the 120 Hz Level 9 combat
  route reaches original name entry at frame 14410 without a native failure.
- Existing effect/GPU tests and the new particle-edge checks pass on two reruns.
  The first GPU suite run had a transient coupled-ripple analytic failure
  (measured 16 instead of approximately 111), not reproduced or explained.
  The cause was subsequently fixed for revision 2: repeated initialization
  replaced shared shaders still referenced by live states and the SDL cache.
  See T3K-RENDERER-LIFETIME.md; eight full suite runs passed after that fix.
- Responsive placement and score ghosts still need user review in motion,
  especially death and level transitions. Other effects with a visible cutoff
  should be investigated by scene before changing their lifetime rules.

Private evidence: `../tempest-profile/runs/aspect-audit/`. The
`wide-hud-validation.json` file records the checks and stable archive hashes.
