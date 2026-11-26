Current whole-game mode: [T3K-ALL-SCENE-120HZ.md](T3K-ALL-SCENE-120HZ.md). The notes below describe the earlier checkpoint.

# Native 120 Hz trial

Ready for initial hands-on testing, with presentation interpolation disabled.

Geometry precision now applies across scenes. For the separate higher-rate
opening/menu/transition trial and its coverage, see
[T3K-SCENE-PRECISION.md](T3K-SCENE-PRECISION.md).
This is exploratory work on the native collection integration. John's frozen
handoff patch/ZIP has not been replaced or changed.

From the Recomp workspace, double-click:

- `Play-Tempest-120Hz.cmd`: 120 Hz regular gameplay simulation and up to 120
  displayed pictures per second.
- `Play-Tempest-120Hz-60Display.cmd`: the same 120 Hz simulation, with 60 Hz
  presentation. Useful for verifying that motion speed and response do not
  depend on how often pictures are displayed.
- `Play-Tempest-24Hz-Reference.cmd`: the original update path capped at 24 Hz,
  with interpolation off, for feel comparison. This also caps its menus/warps
  to 24 for a consistent reference.

The trials use separate workspace settings/high-score folders. They do not
change the installed collection or the handoff. The two 120 Hz launchers share
their settings; the reference has its own settings.

## Controls and useful comparisons

Controller: A/south fires, RB jumps, B/east uses the zapper. Esc or Guide opens
the collection overlay; F11 toggles fullscreen. The overlay's input-device
choice switches to mouse/trackball: left fires, right jumps, middle uses zapper.
Keyboard controls include arrows to steer, Z to fire, E to jump and X for zapper.

Try sustained steering, reversals, small analog movements, short and long jumps,
repeated jump taps, firing while turning, and jump/zapper combinations. Compare
jump height, fuel/recharge duration, enemy speeds and collision difficulty with
the 24 Hz reference. For feedback, compare trail duration, brightness and spread;
pixel identity is not a target.

## Web precision

The 120 Hz trial now defaults to subpixel web rendering. In the overlay,
`120 Hz web precision` switches between `Subpixel` and `Original grid` for
comparison. Both the textured surface and vector edges retain coordinates
before native integer packing. This uses actual simulation geometry, with no
motion interpolation. Native RAM and collision values remain unchanged.
The 24 Hz reference retains original rendering precision. Actor coordinate
rounding is a separate remaining consideration. See `T3K-WEB-PRECISION.md`.

## GPU optimization checkpoint

The trial now uses shaders for textured rings, glowing numbers and convex web
texture mapping, and avoids full-frame history copies during fine simulation
drawing. See `T3K-GPU-NUMBERS-WEB.md` for validation, measured gains and comparison
switches; `T3K-GPU-EFFECTS.md` records the earlier ring checkpoint.

## Pipeline optimization checkpoint

Page-based rollback journals, bulk music-buffer publication, texture reuse,
aligned preparation reads and indexed audio staging are now enabled. See
`T3K-OPTIMIZATION-PASS.md` for paired measurements and validation. CRT remains
outside this optimization work and is off in the performance tests.

## Current boundaries

- Regular active gameplay uses five real simulation updates per 24 Hz reference
  interval. Input, physical values, collision checks, continuous steering and
  camera/geometry preparation use the fine steps. This is not generated motion
  between two pictures.
- Authored discrete AI/event opportunities, random emissions, held-fire repeat,
  countdowns, colour rotation and texture evolution retain original-time cadence.
  New fire presses and contacts can respond before the next original boundary.
- Persistent feedback commits at the original cadence. Intermediate pictures
  draw actual new geometry against the interval's fixed history; they do not
  accumulate additional deposits. History still commits when display refreshes
  are omitted.
- Menus, opening/death/level transitions and the complete warp minigame retain
  their existing scheduler. This first trial does not make every scene 120 Hz.
- Save/load is unavailable in the 120 Hz trial because fractional dynamics and
  partial feedback intervals need a versioned continuation format. Original
  native states remain available in the 24 Hz mode and are untouched.
- The trial deliberately separates presentation from simulation; SDL VSync is
  disabled in 120 Hz mode so a 60 Hz display cannot cap input/logic at 60 Hz.
  Intermediate drawing is omitted if the host falls behind. A CPU unable to
  sustain 120 simulation updates will still need optimization.

This is ready for a first feel test, not release validation. The intent audit
still has open transitive consumer questions and unchanged source units to
review. Wider level coverage, long-play balancing, transitions at higher rates,
save continuation and performance at large render resolutions remain follow-up.

## Validation

- Four native CTest targets pass, including bounded motion, alias order,
  rollback/pool reuse, input units, contact cadence, oscillator programs,
  colour rotation, jet fuel and trajectory checks.
- Five Python inventory tests pass; source changes invalidate review hashes.
- Original 1x reference replay: 32 cases, zero mismatches, with rollback,
  framebuffer independence and read-only decoding checks.
- GPU test: 32 states with darkening/brightening zoom, rotation, ripple and
  moving alpha deposits. Irregular fine samples/display schedules preserve exact
  feedback at original interval endpoints.
- 120 Hz digital, analog/jump and rotary/mouse checks passed; a Level 9 zapper
  case completed through original name entry and session return.
- Paced 720x480 gameplay measured 1,200 real simulation steps in 9.993 active
  seconds (120.08 Hz), with audio continuing. This is a short timing check,
  not a full-resolution performance guarantee.
- With presentation capped at 60 Hz, another paced run completed 1,200 steps in
  9.996 active seconds (120.05 Hz), with 601 displays and 720 history draws.
  The remaining history draws are required 24 Hz commits between display times.
- The ordinary 24 Hz mode saved and restored an active Level 9 session exactly,
  then continued through original name entry. The experimental save restriction
  does not retire baseline native states.
- All three Windows launchers resolve their paths; the chained 60 Hz display
  launcher completed a 120-step gameplay smoke check using isolated test data.

Reproduce gameplay checks with `tools/run_t3k_120_checks.ps1`, or add `-Live`
for the paced clock/audio check. `-Live -PresentHz 60` tests independent display
cadence. External owned game assets are required and are not included.
