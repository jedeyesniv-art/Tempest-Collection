# Cycle intent audit and effect comparison

This extends the first timing checkpoint. It deliberately does not enable 5x
execution of the unchanged game loop. Gameplay, input, geometry, visual state,
transition flow, RNG and object accounting must be distinguished by purpose.

## Review mechanism

`tools/t3k_timing_inventory.py` inventories discovered game functions, class methods
and individual callback cases, including functions with no lexical clock signal.
It annotates explicit clock/age reads, fixed arithmetic, state updates, random
draws and frame/input services. A clockless emitter can still have implicit
cadence through how often it is invoked. It is conservative: a
candidate may be a storage index or active-object count, not a timer.

`docs/t3k-cycle-intents.json` records source-body hashes, domain, decision and
intent for reviewed locations. Source changes invalidate their review. Unresolved
consumer questions stay explicit; no default classification makes them cosmetic.

```powershell
python tools/t3k_timing_inventory.py --output <report.json>
python tools/t3k_timing_inventory.py --require-reviewed
python -m unittest discover -s tests -p test_t3k_timing_inventory.py
```

Current checkpoint: 428 semantic review units; 362 reviewed, 66 unreviewed,
and 25 reviewed entries still needing consumer resolution. `--require-reviewed`
therefore fails, as intended. These numbers are not a claim of exhaustive
transitive coverage. Command programs, template destinations, aliases, worker
animation programs, host clocks and renderer/shader consumers need continued
review in addition to the source scan. Known split decisions still need to be
implemented and tested after review is complete.

## Fractional movement experiments

`src/t3k_fractional_motion.h` supplies the movement kernels. The first
free-flight experiment remains an isolated test/reference component.
The free-flight segment retains original-unit velocity, anchors integer damping
and the 255 cutoff once per original tick, and evaluates fractional exponential
motion within that tick. Five substeps reach the exact original endpoint without
losing slow velocities to division. This is a frozen-force segment experiment:
it does not yet handle changed forces/input mid-segment, boundaries, signed
overflow, grid snapping or pause. Such cases return failure instead of silently
claiming supported gameplay. It is not connected to the runtime.

The separate target-approach kernel accepts a new target every substep, carries
integer remainder, clamps without banking excess movement, and preserves pause.
Direction reversals and cadence changes explicitly discard incompatible residue.
Caller-side attribute identity, alias order and rollback/pool reuse now have
explicit native integration. Save state and broader destination-specific
clock review remain. Signed negative step and wrapping-distance
cases are explicitly unsupported for subdivision; the original 1x path delegates
to the recovered function.

The C++ test covers 275 free-flight combinations, independent damped midpoint and
slow-motion checks, 200 successive damped ticks without endpoint drift, target
approach at seven speeds in both directions over 31 ticks, fresh targets, pause,
clamping and cadence changes. All four native CTest targets pass.

### Integrated trial

`FractionalMovement` adds all six native boundary policies, topology selection,
grid snapping, pause and immediate rebasing on an actual force/input change.
It retains original-unit velocity and an original-time cutoff; a force smaller
than 255 per fine sample can accumulate rather than being erased every sample.
Very small transients before that discrete cutoff can differ from the coarse
map by subpixel amounts. Sustained clamped/outside contact is notified on entry
and original-time repeat boundaries, rather than emitting five copies of an
unchanged boundary event. Unsupported negative-scale roots, signed product
overflow and INT_MIN velocity reject subdivision explicitly.

`NativeFractionalValues` participates in the memory frame's begin/commit/abort
and receives pool-instantiation notifications. Original and fractional paths
share one ordered aliased-record traversal. The 1x adapter passes the complete
32-frame reference corpus, including forced rollback and retry.

`NativeJumpDynamics` uses a fractional generator of the original semi-implicit
jet map, retaining its coarse ballistic trajectory instead of changing jump
height merely by increasing frequency. Fuel follows accumulated actual use and
recharge time. Repeated short pulses cannot evade consumption by releasing at
the original tick boundary. Sound/flags/display remain in the recovered session
function. Original 1x jet arithmetic stays inline and unchanged.

`NativeSimulationTiming` supplies explicit legacy boundaries and immediate
fire/contact opportunities. The active session caller controls phase advancement;
menus and transition calls use the original scheduler. Continuous steering and
fresh spatial census are split from RNG, packed timers and trail-history shifts.
Unconditional discrete callbacks retain their original opportunities; conditional
collision/boundary event dispatch stays prompt. No callback bodies are replaced
by fabricated gameplay classes.

The GPU trial uses an interval history texture. Each actual fine picture draws
against the same committed history; only the interval endpoint becomes the next
history. Intermediate geometry can differ and intermediate draws can be omitted.
The test verifies exact committed feedback across 32 darkening/brightening zoom,
rotation, ripple and alpha-deposit states. This avoids both extra accumulation
and requiring opacity scaling to approximate all the ordered nonlinear effects.

Animation oscillators evaluate fractional phase for fresh vector/colour output
and publish persistent phase only at the original boundary. Spatial repeat offsets
retain full units. Colour rotation and texture evolution keep their original
cadence. Lightning endpoint geometry remains fresh while persistent visual seeds
advance on original boundaries. Packed bonus-slot physics is guarded against
subdivision; its complete minigame remains on the original scheduler in this trial.

See [T3K-120HZ-TRIAL.md](T3K-120HZ-TRIAL.md) for launchers, validation and explicit
first-test limitations. Enabling this trial is not a claim that the intent audit,
all-scene enhancements or release validation are complete.

## Contact response is also rate dependent

`resolve_lane_contact` combines a spatial overlap test with a repeated response.
For a qualifying non-player object it raises the stored lane height by 2, 4, 8
or 16 and plays a sound every invocation. The status bit it writes does not
suppress another invocation. A synthetic regression reproduces height
`50 -> 66 -> 82` on two consecutive calls with unchanged object/draw position.
Fine collision sampling therefore requires a separate policy for sustained
contact response cadence; multiplying the unchanged collision pass would also
multiply wall growth and sound. The player path has a separate global latch.

Template inspection now accepts `--initializer-override`, as queue payloads can
replace a template's default initializer. For example the burst path emits
`0x30104` before template `0x1B`: its effective initializer is 1 (`0x80010950`),
despite the template's default 0. The inspector also reports resolved value,
parameter, global and script locations and initial value records. These are
consumer-tracing aids, not proof that a non-colliding template has no gameplay
side effects through command programs or shared RNG.

## Findings that affect the design

| Mechanism | Intent and consequence |
| --- | --- |
| Driver digital ramp | Adds 20 per call; faster input sampling must not make the ramp five times faster. |
| Analog versus rotary | Analog encodes rate; mouse/rotary encodes relative displacement since sampling. A common time divisor would mishandle one path. Review per-poll clamps in time units. |
| Object value list | Spatial motion, appearance channels, picture stamps and aliased parameters coexist. Classify individual uses, not the whole object. |
| Picture/birth expressions | Their outputs feed arbitrary command destinations; some are gameplay. Preserve wrap and inspect consumer intent. |
| Worker animation language | Advances persistent oscillators and supports both vector and colour outputs. Slowing an entire worker as a texture effect can slow geometry. |
| Lane steering callback | Performs AI motion and shifts a three-position drawing history. Motion should update faster; history should represent comparable elapsed time. |
| Camera/frame finalization | Camera springs share a function with occupancy data and topology bits used by gameplay. It cannot be decimated wholesale. |
| Active-object/hit counts | A decrement is not automatically a duration. Event accounting stays per event; occupancy counts are rebuilt per actual pass. |
| Jump/session loop | Physical jump trajectory, fuel/recharge, zapper inventory, sound state and visual outputs are combined. Inventory changes remain event-once; resources and motion get explicit time semantics. |
| RNG | Cosmetic-looking and gameplay producers can consume persistent state. More visual evaluations must not silently change gameplay random draws. |
| Transition loops | Visual progression also owns skip/completion and scene handoff. Preserve authored duration and responsiveness together. |

### A resolved periodic-event consumer

`callback_optional_event` and `callback_periodic_event` emit queue event
`0x12602`. Queue family 1 activates template `0x26` from a pool. External original
reference RAM identifies its descriptor as `0x04040F8C`, with collision status
`0x20`, initializer 7, callbacks `0x800124C0` and `0x80012E7A`, and worker 12.
Initializer `0x800111DA` installs position but does not clear collision status.
The frame pass routes status bit `0x20` through collision processing.

This is a gameplay-relevant spawn cadence with sound/appearance consequences,
not an effect-only emission rate. The every-eight/every-sixteen picture tests and
RNG consumption must retain their intended cadence while fine simulation runs.
The optional path also mutates the shared engine-picture word, so its suppression
and downstream phase effects require an explicit split.

Reproduce template inspection using external original-reference banks:

```powershell
python tools/t3k_template_intent.py <reference-folder> --template 0x26
```

No captured bank or game asset is included with the audit tools.

## Motion blur: comparable behavior rather than exact pixels

Compare fixed seeds, input histories, elapsed time and game-space scale. For
controlled fixtures, measure:

- Decay over time after a source stops drawing (half-life and disappearance).
- Trail length/spread and direction behind a moving source, accounting for
  camera movement and zoom rather than relying only on screen-space length.
- Integrated brightness, peak brightness and clipped/saturated coverage.
- Event timing and source contribution per unit time, including overlapping
  objects, appearance/disappearance, burst particles and brightening warps.
- Dependence on simulation, effect and presentation rates separately, including
  missed display frames. Presentation must not advance any of those states.

These describe a visual matching target, not a promise of exact pixels. Tuning
must include visual review of real gameplay fixtures; dense brightening effects
and ripple need different treatment from ordinary darkening trails.

### Asset-free experiment

`tools/t3k_feedback_model.py` compares a moving Gaussian deposited into a 1D
alpha-blended feedback image at 24 and 120 updates/second. This is an explanatory
model, not the Tempest renderer or a production rebalancing change.

| Measure | Reference 24 Hz | Naive 120 Hz | Time-scaled 120 Hz |
| --- | ---: | ---: | ---: |
| 95% trail extent | 65 px | 13 px | 66 px |
| Total energy relative to reference | 100% | 68.1% | 96.0% |
| Peak relative to reference | 100% | 166.9% | 92.7% |
| Trail spread relative to reference | 100% | 21.6% | 100.8% |

The candidate uses `fade**dt` and `1-(1-opacity)**dt`, with the moving source
computed at each actual step. It illustrates why both decay and new drawing
contributions matter. It does not establish the correct formula for T3K's
ordered GPU operations. Independently scaling fade and opacity is not even
algebraically exact for their coupled stationary operator, and fixed-point
rounding, clipping, max/additive blending and ripple introduce other differences.

For example, a stationary linear recurrence `y_next=A*y+B` has fractional
operator `A_dt=A**dt`, `B_dt=B*(1-A_dt)/(1-A)` (or `B*dt` when A=1). A changing
source or nonlinear layer cannot automatically use that simplification.

The research build remains interpolation-free, and the handoff to John remains
unchanged. The next implementation work must use the intent ledger to adapt
movement and its consumers together; the remaining audit is substantial.


## Worker and implicit-cadence checkpoint

The review was expanded beyond lexical tick searches after the player emitter
proved an important omission: `callback_player_actions` can emit a projectile
and sound once per invocation while below capacity, without incrementing a timer
or reading a picture counter. Its original per-frame caller supplies cadence.
An executable contract calls it twice at the same clock and observes two queued
projectiles. High-frequency input/collision evaluation must therefore be split
from held-fire opportunities; keeping all explicit counters slow is insufficient.

Further worker findings are now covered by executable contracts:

- `PreparationDepth::bonus_slot` changes a slot's position/velocity before its
  visibility test. Five calls advance a hidden slot five times. Module 17's bonus
  path also owns projectile aging, expiry, hits, history and gameplay arrays;
  it cannot be treated as a low-frequency renderer.
- Web colour/serial progression mostly counts lanes and vertices. Module-10
  data upload resets local storage, including the colour index, on each job.
  The contract advances that index twice, reloads the module and verifies the
  original value returns. These are not all persistent animation clocks.
- Web construction publishes lane-angle data at `0x800DACC0` that steering reads.
  Fresh geometry is relevant to gameplay as well as appearance.
- Lightning advances persistent seeds at `0x8010CFC0` and consumes/resets an
  endpoint queue. Its stream is separate from callback RNG at `0x80112C50`.
  The contract verifies persistence and stream independence. A future effect
  epoch must hold random realization while evaluating fresh endpoints; caching
  the whole drawing would make motion/camera changes lag.
- Procedural particle helpers with local constant seeds rebuild a deterministic
  field each call. Their point-index increments are spatial construction, unlike
  lightning's persistent random advancement.
- The NISE block/resampling paths count audio samples, voices, sectors and I/O
  completions. Keep their independent 32 kHz / 64-sample block ownership;
  motion interpolation being disabled does not disable audio resampling.

Four inventory tests pass, including discovery of implicit emitters, custom
return types and default arguments, callback aliases, comment exclusion and
stale reviews. Three C++ test targets pass, with worker/cadence contracts added
to the prior state and control checks. No gameplay frequency change is enabled.
