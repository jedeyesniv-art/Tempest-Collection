> Superseded visual policy: see `T3K-TIME-INTEGRATED-FEEDBACK.md` for the
> current elapsed-time accumulation trial and continuous motion comparisons.

> Historical continuous-blur experiment. The score-echo comparison supersedes
> this policy and its image-difference metric; see `T3K-SCORE-ECHOES.md`.

# Player selection and transition feedback comparison

## Player-select cadence

A fresh original-code Nuance capture holds the One Player/Two Player screen
without input for a longer idle interval. Research substitutions are off
(options 0); the existing MPE3 workaround remains enabled. The fixed scheduler
uses 200,000 dispatch rounds per virtual 60 Hz field. This is a repeatable
emulator configuration, not calibrated hardware timing.

| Phase | Intervals | Original calls per emulated second |
| --- | ---: | ---: |
| Entry/fade | 59 | 24.51 |
| Idle animated presets | 348 | 24.81 |
| Exit/fade | 63 | 28.40 |

The high-rate native mode had inherited the legacy 30 menu setting. Its idle
progression was therefore about 21% faster than this emulator capture. A
64-authored-step interval took 2.13 s at 30, versus about 2.58 s in emulation.
At the new 24 default it takes 2.67 s. This is a useful working match, not a
claim that every menu phase should have exactly the same hardware rate.

The high-rate overlay now uses its own `native_menu_speed`, default 24, for
animated menus and level selection. Original mode retains `menu_speed` and its
existing default 30. This keeps saved emulator preferences from silently
selecting the high-rate reference speed. Input and geometry still run at 120 Hz.

Read-only native equal-authored-time comparisons also match the animated
menu's live object value state; the speed correction does not replace its
preset algorithms or interpolate motion. A diagnostic-only
`T3K_CHECK_PLAYER_MENU_HOLD=320` extends the scripted animated-menu dwell.

## Transition accumulation

Previously, each fine output drew fresh geometry over history pinned until an
authored boundary. That preserves original endpoint deposits, but concentrates
new persistent trail contributions at those boundaries. At 48/120, those
boundaries are reached after alternating groups of two and three samples.
This is a plausible contributor to the reported pulsating blur; prior
redraw-independence tests alone did not prove temporal visual quality.

For level-clear, shared transition and entry scopes, eligible brightening zoom
pictures now accumulate on every actual fine sample. The renderer first draws
the current geometry at full strength. Separately, it advances feedback by the
actual elapsed fraction and adds an elapsed-time share of the new source to
persistent history. It uses two scratch targets and float YCrCb history.
The final picture remains a fresh current state, without motion interpolation.

This is approximate source integration. The linear elapsed share is a
quadrature of the coupled paint operation, not an exact fractional alpha
operator for every overlapping primitive. It is deliberately restricted to one
leading brightening zoom followed by drawing. Full fills, interleaved feedback,
ripple and darkening pictures retain the existing path. Original-rate rendering
is unchanged. Relevant accumulation samples are drawn even when presentation
is capped at 60, so display frequency does not remove trail contributions.

## Matched comparison

Two deterministic level-1 runs use identical native logic, 24 menu pacing,
120 Hz sampling, original disc data, CRT off and interpolation off. Only the
history policy differs. The frame, object-value and effect-command CSVs match
byte for byte. Across two level-clear effects (528 samples at 360x240):

| Measurement | Previous pinned history | Continuous accumulation |
| --- | ---: | ---: |
| Mean RGB channel value (0-255) | 153.827 | 153.677 |
| Mean absolute RGB change per adjacent sample | 7.297 | 5.672 |

Average brightness differs by about 0.10%; adjacent-sample change decreases by
22.3%. This measures temporal image difference, not perceptual flicker or
hardware equivalence. Matched 720x480 spot captures retain the same central
vortex and palette, with a denser, more continuous trail. Motion feel still
needs hands-on review.

The synthetic temporal probe uses reached level-clear zoom/fade parameters,
but is a separate asset-free experiment. Its results are not substituted for
the matched game measurements above.

## Evidence and reproduction

All evidence is under `research/tempest-profile`:

- `runs/reference-scene-cadence/player-select-200000`: fresh emulator capture,
  core hash, input script, original caller traces and snapshots.
- `runs/player-menu-compare-24` and `player-menu-compare-120`: held native menu
  traces and equal-authored-time image captures from before the speed correction.
- `runs/transition-compare-pinned` and `transition-compare-continuous`: matched
  native frame/value/effect traces, temporal readback CSVs and visual spot captures.
- `menu-transition-comparison.json`: combined comparison report.

Run `tools/t3k_scene_comparison.py` to regenerate the report from these captures.
Set `T3K_TEMPORAL_TRACE` to a CSV path for diagnostic per-sample GPU readback;
this intentionally stalls rendering and must not be used for performance tests.
`T3K_PINNED_TRANSITION_FEEDBACK=1` selects the previous history policy for A/B
runs. Neither diagnostic is enabled by the normal launchers.

Renderer clock tests cover both history policies at 16/36/48 references,
irregular extra redraws, a full fill, brightening/darkening switches and a
partial-interval handoff. Existing reference replay and memory checks remain
separate from the high-rate visual comparison. John's frozen baseline handoff
is unchanged.

Final checks: nine scripted routes passed (100,833 traced pictures, all at a
120 Hz sampling target), four CTest targets passed, 32 original GPU/memory
replays matched, and 848 renderer-state comparisons passed. The 60 Hz-display
live check delivered 1,200 regular-gameplay updates in 9.986 s (120.16 Hz),
with 600 presentations. This live statistic measures gameplay; scripted
transition runs and renderer checks cover the changed accumulation path.
