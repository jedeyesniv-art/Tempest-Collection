> Superseded visual policy: see `T3K-TIME-INTEGRATED-FEEDBACK.md` for the
> current elapsed-time accumulation trial and continuous motion comparisons.

# Menu handoff and score echoes

This checkpoint supersedes the continuous-source blur experiment documented in
`T3K-MENU-TRANSITION-COMPARISON.md`. Reducing adjacent-frame image difference
was not the right objective: the reference score trail contains distinct,
stable echoes, not simply a smooth streak.

## Menu sequence

The primary particle menu/crawl, its exit, player selection and level selection
now use the same `native_menu_speed` setting (24 by default). This deliberately
replaces the separate 16-equivalent crawl clock. Menu-owned transition calls
inherit the enclosing menu's cadence, so a custom menu speed does not introduce
a brief switch back to 24 during a fade. Other scene pacing is unchanged.

The original particle patterns, random selections and their intrinsic motion
changes are preserved. There is no additional scheduler-speed change at the
old outer-caller boundary. Actual sampling/input/geometry remains 120 Hz.

## Discrete feedback history

The original pinned implementation committed at whichever fine sample crossed
an authored boundary. At 48/120, two- and three-sample groups became .8- and
1.2-tick feedback steps. Echo spacing and history sampling therefore varied.
The later continuous-source experiment avoided those concentrated deposits,
but replaced separate echoes with a denser smear and repeatedly filtered
history at fine cadence. Neither property matches the score reference well.

The new clocked path splits each fine interval at the exact authored boundary.
It composes inverse affine zoom/rotation/centre maps and fade gains in double
precision, while keeping the underlying history image unfiltered between
commits. At the exact boundary it transforms/quantizes history and deposits
current drawing once. It then renders any remaining fine phase from that
history, with fresh full-strength geometry. It does not interpolate motion.

Thus a 48-reference effect still makes 48 evenly timed persistent deposits per
second while producing 120 genuinely updated pictures. Display refreshes do
not advance history. Skipped display presentations do not omit the transforms
needed by this path.

The policy remains restricted to a single leading brightening zoom followed by
drawing, in level-clear/shared-transition/entry scopes. Unsupported operation
orders and original-rate rendering retain their prior paths. The continuous
experiment is retained for diagnostics via `T3K_CONTINUOUS_TRANSITION_FEEDBACK=1`;
`T3K_PINNED_TRANSITION_FEEDBACK=1` selects the earlier pinned comparison.
Neither override is used by normal launchers.

## Validation and limits

A new GPU echo oracle compares stationary score-like strokes against original-
rate discrete history at every fine phase, including a fractional original-
feedback preview between authored boundaries. It checks 120 samples each at
1/5, 2/5 and 3/10 steps. The worst mean channel difference is below 0.0001 on a
0-255 scale; the largest individual channel difference is 2. The fixture uses
constant isotropic zoom, rotation and fade. It validates spacing and filtering,
not just brightness or independence from display frequency.

Another 1,208 renderer states cover all three policies, irregular display
refreshes, full fills, brightening/darkening changes and partial handoffs.
All 32 original-frame GPU/memory replays still match, with rollback, read-only
decoding and framebuffer-independence checks. Four CTest targets pass.

Matched continuous/clocked native game runs have byte-identical frame,
object-value and effect-command traces. Actual 720x480 captures at frames
4560/4630/4740 show the score separating into copies in the clocked version,
rather than the previous continuous smear. Nine scripted game/menu/input routes
pass. All reached samples remain at 120 Hz.

These are renderer/reference-algorithm checks, not a claim of pixel-identical
hardware reproduction. Varying coefficients are integrated from fresh fine
samples. Moving geometry deposited at a boundary uses the available current
fine state (within one simulation sample of that boundary); the stationary
score is not subject to that positional sampling difference. General
anisotropic scale/rotation composition is supported, but the constant-rate
pixel oracle specifically covers isotropic zoom. Hands-on motion comparison
remains the acceptance check for visual feel.

Evidence is in `research/tempest-profile`: `score-echo-comparison.json`,
`runs/score-echo-continuous`, `runs/score-echo-clocked`, the nine all-scene route
traces, and `runs/unified-menu-21` for nonstandard menu cadence. Existing
hardware reference captures and original-code Nuance evidence remain available
in `runs/reference-scene-cadence`. CRT and interpolation are off in these runs.
John's frozen integration patch is unchanged.

Final live check: 1,200 regular-gameplay updates in 9.986 seconds (120.16 Hz),
with 600 displayed pictures. The custom 21 menu trace confirms 21-equivalent
pacing through all three menu domains and their nested transitions, with
120 Hz actual sampling. The nine current route traces contain 98,593 pictures.

