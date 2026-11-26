# Native timing research: first checkpoint

Historical first checkpoint. The current opt-in 120 Hz build and test launchers
are described in [T3K-120HZ-TRIAL.md](T3K-120HZ-TRIAL.md). The default 1x path
continues to preserve this original-cadence baseline.

This work continues the native collection integration. The handoff ZIP/patch in
`deliverables/tempest-collection-native` is frozen. These exploratory changes are
working-tree changes after that staged baseline, not a competing release.

## Development mode

Configure `-DTEMPEST_T3K_TIMING_RESEARCH=ON`. This build forbids presentation
interpolation regardless of `native_interpolate`, hides its menu item, and
ignores F6. Existing settings files are not rewritten. Native states with a
pending interpolated presentation are rejected explicitly in this mode.
The option defaults OFF for normal collection builds.

This checkpoint retains original gameplay cadence. It is not a 120 Hz mode.

## Concrete clock split

`PictureBuildClocks` is an explicit renderer input, available through
`NativeFrameIntegration::set_picture_clocks`. Its first member separates the
web texture's UV scroll from the renderer job/picture clock. Leaving it unset
retains the exact previous decoder behavior. Holding the web tick changes only
that operation's texture phase, not its vertices, geometry, camera, object ages,
or the original job word in RAM. This is specific to web scroll; texture
animations implemented by object commands still require separate treatment.

A higher-frequency caller can eventually hold this tick across smaller geometry
steps. No such steps are scheduled yet; changing texture phase alone is not a
higher-frequency simulation.

## Read-only evidence

`--timing-trace=<prefix>` records:

- `.frames.csv`: original driver/engine/job/video counters, decoded control,
  player position, camera words, packed lifetime, and picture complexity.
- `.effects.csv`: web phases, zoom/fade/angle, ripple operations, and particle
  or square-blend drawing operations in display-list order.
- `.values.csv`: active object attribute positions, velocities, scales and flags.

The trace reads owned memory without calling game update functions or adding
instrumentation writes. It omits save-continuation catch-up output. Active-value
samples are post-frame observations, not a claim to record every transient
attribute that lived and died during a frame.

A Level 9 digital movement/fire/jump run yielded 2,217 native pictures, including
1,420 through the gameplay-frame path and 52,385 active value observations.
Movement scales observed were `0x10000` (1), `0xF000` (0.9375), and `0x1000`
(0.0625). Approach-to-parameter handlers also occurred. This is one input trace,
not exhaustive coverage of handler variants or all levels.

5,011 movement observations had nonzero velocities below five times the native
cutoff of 255. Dividing stored velocity by five would incorrectly stop these
attributes. Likewise, applying the original damping five times would accelerate
decay. Fractional integration must retain original-unit velocities, account for
fixed-point remainder, and adapt damping/cutoff and boundary response together.

The renderer job matched the engine picture in every trace sample. The driver
counter was normally one ahead at this completed-frame boundary because the
outer driver had already advanced it. Do not treat the snapshot columns as
identical logical phases merely because they derive from picture counts.

## Recovered update boundaries

| Boundary | Current coupling | Required treatment |
| --- | --- | --- |
| `prepare_driver_game_frame` / `finish_driver_game_frame` | Control decode, pause, frame clock and sound consumption surround each picture. | Fresh samples/edges per simulation step, without multiplying button-repeat or action cadence. |
| `update_object_values` / `movement_step` | Position increment, velocity multiplier, low-speed cutoff, boundary flags and topology are combined. | Complete fractional movement semantics, not a scaled final picture or only velocity division. |
| `approach_parameter` | A fixed increment per call, clamped at target. | Fractional time/remainder with the original endpoint rule. |
| `evaluate_attribute` | Attribute phase derives from 16-bit picture/birth arithmetic. | Explicit legacy-time phase plus substep phase; preserve wrap semantics. |
| `frame_countdown`, session loops, callbacks | Packed decrementing timers, random draws and event creation occur per call. | Preserve durations and event/RNG cadence while allowing fine collision/input response. |
| `frame_finish` | Camera approach steps and damped spring arrays update per picture. | Audit/adapt camera dynamics with gameplay; geometry must use each actual new state. |
| preparation worker | Generates geometry from current state; follows object processing. | Recompute after each real simulation update. |
| web texture phase | Renderer job count divided by two becomes UV scroll. | Independent explicit texture clock (first split implemented). |
| GPU feedback | Ordered fills, feedback transforms and drawing deposits share one persistent destination. | Preserve operation order and distinguish transformation rate from new drawing contributions. |

## Feedback findings

`Renderer::Redraw` only displays the existing feedback image. It does not
advance it. A synthetic GPU test applies 32 native states with moving blend
rectangles and zoom/fade/rotation, then varies presentations per state through
0, 1, 2, 5 and 9. Raw floating-point feedback remains byte-identical.

`Renderer::Draw`, in contrast, deposits geometry/particles again on every call.
Its zoom transform/fade supports fractional steps, but that does not normalize
new drawing contributions. Ripple explicitly applies its entire transform on
each call, regardless of fractional step. Thus the interpolation path cannot
be used as a substitute for correctly timed effect updates.

## Validation and next checkpoint

- Original-reference replay: 32 frames, zero mismatches; rollback and decoder
  read-only/framebuffer-independence checks all passed.
- Held web clock: 18 web operations checked; only UV phase changed, with
  geometry and RAM unchanged.
- GPU presentation independence: 32 states, exact feedback bytes after irregular
  display schedules.
- Existing checkpoint/control tests: 2/2 passed.
- Level 9 trace run completed through original name entry/session return with
  interpolation disabled even when its setting was explicitly supplied as 1.

Next is a complete fractional movement/value-update kernel and counter/event
inventory, tested against original-tick endpoints and intermediate boundary
crossings. Do not enable five copies of the current frame loop as a playable
shortcut: it would also multiply lifetimes, spawning, RNG consumption, camera
springs, and persistent drawing deposits.
