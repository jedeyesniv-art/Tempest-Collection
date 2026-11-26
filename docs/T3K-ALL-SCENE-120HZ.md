> The newer motion-blur trial uses per-update elapsed-time feedback in audited
> scopes. See `T3K-TIME-INTEGRATED-FEEDBACK.md` and the Motion-Blur launchers.
> The pacing profile below remains current.

# Whole-game 120 Hz checkpoint

The 120 Hz research mode now runs fresh native simulation, input polling and
geometry preparation throughout the game. It no longer falls back to original
frame cadence for menus, death, transitions, bonus, entry, game over or editors.
There is no presentation interpolation. This continues the existing collection
integration; John's frozen baseline patch/ZIP is unchanged.

## Agreed reference pacing

These are the user's chosen working pacing values, not claims of calibrated
hardware performance. Each row still receives 120 actual simulation samples
per second. Reference pacing describes how quickly its original algorithm
progresses within those samples.

| Phase | Equivalent original progression per second |
| --- | ---: |
| Regular gameplay | 24 |
| Menus and level select | High-rate menu setting, normally 24 |
| Opening particle crawl / primary selection screen | Same menu setting, normally 24 |
| Interactive tunnel travel at level end | 20 |
| Following level-clear effect | 48 |
| Noninteractive bonus presentation | 36 |
| Both death variants | 24 |
| Everything else | 24 |

The final category includes the opening logo, shared transitions, entry/spawn,
physical settling before an outcome, retry, asset/attract presentation, game
over, name/password entry, pause, calibration/tools and the interactive warp
minigame. The latter keeps 24; the 36 value applies to the noninteractive bonus
presentation. Interactive level-end travel is separated from the following
visual clear effect.

## Launch and settings

Use `Play-Tempest-120Hz.cmd` or `Play-Tempest-120Hz-Scenes.cmd` in the workspace.
The Scenes launcher explicitly switches CRT off. Their 60Display counterparts
run the same simulation with at most 60 displayed pictures per second. Existing
120 Hz user preferences/high scores and updated controller options are retained.

`--logic-hz=120` now enables the full scene profile automatically. The explicit
`--scene-hz=120` remains accepted. `--scene-hz=0` is a diagnostic comparison with
the earlier gameplay-only mode, not the default. The 24 Hz reference launcher
keeps the original update path and its separate settings/states.

The high-rate menu setting (`native_menu_speed`) defaults to 24 after a focused
Nuance comparison. It is separate from the original `menu_speed` setting (30),
which remains available in the reference mode. Nonpositive or invalid high-rate
values fall back to 24. Old Play
speed, Warp between levels and Speed change controls are hidden in whole-game
120 Hz mode because the explicit phase profile owns those rates. They remain
available in original-cadence mode. Existing 120 Hz save-state restrictions
remain; this change does not enable incomplete fractional-state serialization.

## Timing implementation

`SimulationStep` now carries an authored-time numerator and denominator. The
agreed rates reduce to 2/15, 1/6, 1/5, 1/4, 3/10 and 2/5 authored ticks per
physical sample. Every rate therefore retains exactly 120 Hz sampling, rather
than choosing an approximate integer multiple such as 128, 144 or 105 Hz.

Phase accumulation counts authored boundaries independently of samples. Signed
increments conserve their remainders; drag uses the actual elapsed fraction.
The existing bounded object integrator advances the required fractional units
without executing extra whole game frames. Continuous input/geometry/collision
work remains fresh while timers, palette/texture evolution, spawning and random
or repeated effects retain authored/event boundaries as appropriate.

Every frame-producing scene caller has an explicit timing scope, plus a 24 Hz
background policy for helper frames. Nested transitions/pause restore their
parent clock and the actual feedback phase. Level-end travel and physical
settling use the fractional generator of the original semi-implicit Euler map,
including its acceleration correction, so subdivision alone does not change
their authored trajectory.

The interactive warp drawing worker also owns obstacle/projectile simulation.
It now separates packed authored state updates from fine geometry: obstacle
motion/reversals, bullet age and ring-history shifts keep their authored rate;
geometry and spatial checks evaluate each fine state. Fatal contact is prompt;
row/score emission retains authored cadence, and projectile hit consumption
prevents repeated widening from the same hit.

## Geometry and framebuffer effects

The earlier all-scene vertex/origin/scale precision path remains enabled. Morph
generation additionally retains fixed-point vertices before packing at their
final external stream addresses. This does not alter original packed geometry
or collision RAM solely to gain rendering precision.

Framebuffer history is pinned across fractional previews and committed on
authored boundaries. Rational intervals retain any fractional carry; resuming
a parent scene starts feedback from its actual resumed phase. New geometry is
rendered from real states rather than blended between pictures.

Ripple feedback now scales displacement by elapsed authored time and derives
fade exponentially. This is an approximate fractional displacement generator,
not a pixel-exact fractional reconstruction of the original nonlinear mapping.
The full-step path remains unchanged. Zoom/fade/history deposits retain their
existing normalized behavior. CRT is separate and was off in all tests.

Brightening level-clear/shared-transition/entry feedback now commits discrete
source deposits at exact authored boundaries. Fine zoom/rotation/fade maps are
composed between boundaries without repeatedly filtering the persistent image.
This preserves separate score echoes; a 48/120 boundary is split at its exact
time instead of rounded into .8/1.2-tick feedback intervals. The previous
continuous-source experiment remains available only for diagnostic A/B runs.
See `T3K-SCORE-ECHOES.md`.

The particle crawl now shares the animated-menu setting (24 by default),
replacing the previous separate 16 reference. Menu-owned nested transitions
inherit that same pace, including when the menu setting is changed. The native
particle pattern and its intrinsic speed changes remain original algorithms.

## Validation

- Four CTest targets pass. Clock tests verify exactly the chosen number of
  authored boundaries and conserved signed increments over 120 samples, with
  actual bounded-object displacement following reference speed.
- The original 60-step bonus presentation produces exactly 200 actual samples
  at 36-reference / 120-simulation Hz, preserving award and one-shot events.
  Both death variants preserve life decrement, retry route and sound events:
  64/70 authored steps become 320/350 samples at a 24 Hz reference.
- Packed warp-obstacle tests conserve native motion and both edge reversals
  while extra drawing samples leave the authored packed state unchanged.
- All 32 original reference replays retain zero native-memory mismatches,
  rollback, framebuffer-independence and read-only decoding checks. Original
  GPU comparisons retain their prior tolerance.
- Renderer checks include 128 original interval/history comparisons and 1,080
  rational samples with irregular presentation counts; feedback is independent
  of extra display refreshes. GPU number/ring/web and same-tick mutation checks
  also pass.
- Nine scripted routes pass: level 1 progression, level 9, analog level 15,
  mouse, two-player, level select, interactive warp with firing, tools/rotary
  calibration and password entry. Trace analysis verifies every reached frame
  has a 120 Hz sample target and a non-coarse step.
- A nonstandard 21 Hz menu setting also retains exactly 120 Hz sampling,
  using a 7/40 step rather than rounding to an integer multiplier.
- Live checks retain approximately 120 simulation Hz with both 120 and 60 Hz
  presentation; the latter displays 600 pictures in 1,200 regular gameplay
  samples. Renderer history may still need draws at authored boundaries even
  when a picture is not presented.

Evidence, logs and domain coverage are in `research/tempest-profile`, including
`all-scene-domain-coverage.json`; the repeatable route suite is
`tools/run_t3k_all_scene_checks.ps1`.

## Practical limits

Hands-on scene pacing and visual feedback remain feel judgments; the chosen
profile deliberately does not emulate density-dependent slowdown. Asset/native
packing and explicitly authored discrete expressions can still produce visible
steps. This checkpoint does not claim every quantization source is removed.

The scripted warp route covers actual steering/depth/bullet processing and its
failure/return path. Its success/award branch is implemented at the new cadence
but has not been reached by that scripted route or manually signed off here.
The timing inventory remains an honest discovery aid: 446 units, 387 reviewed,
zero stale, 59 unreviewed and 23 open consumer decisions. Those remaining audit
items are not omitted runtime functions; they concern further transitive intent
and fine-grained precision/effect choices. This is not a claim of exhaustive
validation of every scene/level/interaction.
