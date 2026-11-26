Current whole-game mode: [T3K-ALL-SCENE-120HZ.md](T3K-ALL-SCENE-120HZ.md). The notes below describe the earlier checkpoint.

# All-scene precision and scene cadence checkpoint

The 120 Hz research build now enables geometry precision regardless of whether
its caller is gameplay, a menu, the opening, or a transition. Scene cadence is
a separate opt-in trial (`--logic-hz=120 --scene-hz=120`). Interpolation is off.
John's frozen integration patch remains unchanged.

## Geometry

The overlay option is now **Geometry precision** (legacy settings key
`native_precise_web`). It applies to the whole 120 Hz trial. The 24 Hz reference
keeps original decoding. Existing 120 Hz launchers get these precision changes
without opting into scene timing.

In addition to the existing web vertex fractions, the renderer retains:

- Generated shape vertices before integer coordinate/marker packing, including
  streams crossing preparation writer flushes.
- Projected object origins before screen-position packing.
- Projected object scales before their multiply/shift/size packing.
- Fractional intermediate vector transforms, including static shape templates.

Vertex hints and origin/scale hints have separate coordinate domains. Each is
stamped against its actual output RAM word and invalidated by native writes,
bulk publication, rollback or load. Preparation publishes hints after its RAM
writes, so internal buffering cannot discard fresh precision. Native game RAM,
collision geometry and original packed display lists are unchanged.

This retains fractions available in current simulation; it cannot reconstruct
fractions already lost in authored assets or every upstream camera/angle
quantization. Not every remaining visible step is therefore solved.

## Scene trial

Launch `Play-Tempest-120Hz-Scenes.cmd` from the workspace. The companion
`Play-Tempest-120Hz-Scenes-60Display.cmd` keeps the same simulation with 60 Hz
presentation. Both share existing 120 Hz trial preferences/high scores, force
CRT and interpolation off, and leave the normal launcher available for pacing
comparison. Scene timing is a command-line opt-in, not a saved overlay setting.

The opening, animated menus, and generic menu-transition driver now evaluate
fresh native states at a higher rate. Opening motion/fade formulas, menu preset
blends/fades, object simulation, input and geometry use actual fractional steps.
The original game's parameter blending is part of its animation algorithm;
there are no interpolated presentation pictures.

Menu/opening reference speed comes from `menu_speed` (normally 30 Hz); generic
transitions use `warp_speed` (normally 60 Hz). These preserve the chosen pacing,
not a claim that hardware ran every scene at exactly those rates. Nonpositive
settings fall back to gameplay speed; references are rounded and clamped to
15–60 Hz. Policy is captured when a scene starts; changes apply next entry.

Subdivisions are floor(120/reference Hz), so normal 30/60 Hz scenes both reach
120 Hz. A custom 21 Hz menu gets five subdivisions, or 105 Hz, preserving its
21 Hz reference duration. This first implementation chooses whole subdivisions
to retain authored event boundaries rather than silently alter pacing.

Timers and one-shot decisions remain distinct from fine animation. Menu idle
counts/timeout checks and preset selection retain authored boundaries. Random
choices retain their entry/exit or preset events. Generic transition termination
and budgets count authored duration; opening phase lengths scale together with
the number of samples. Renderer feedback uses the appropriate subdivision count
and closes its interval when scene ownership changes. A mixed-rate clock keeps
scripted video/audio and validation inputs consistent across scene rates.

## Coverage and remaining work

This checkpoint converts three explicit paths: opening, animated menus, and the
generic menu-transition driver. Static selection, level selection/entry, death,
custom level-clear/warp, bonus, game-over and name entry still need their own
clock/consumer audits. They benefit from the broader precision path but retain
the older cadence. Their visible pacing must be judged separately before
conversion; this trial does not yet provide 120 Hz throughout the entire game.

The intent inventory currently contains 437 discovered game-header units:
371 reviewed, zero stale, 66 unreviewed and 25 open consumer decisions. That is
an audit aid, not a claim of complete timing coverage.

## Validation

- Four CTest targets pass. Added contracts conserve opening sample positions
  and duration, menu idle/entry/exit duration and RNG sequence, generic transition
  duration, and mixed-rate virtual video/audio time. Precision tests cover
  writer flushes, identical packed-word movement/scale, separate coordinate
  domains, invalidation, and unchanged native RAM.
- All 32 original-frame replays retain native memory, rollback, read-only decoder
  and framebuffer-independence checks. Original GPU comparisons remain within
  the prior tolerance (maximum 2/255).
- Renderer history checks pass across subdivisions 2, 4, 5 and 8: 128 native
  states with irregular presentation counts produce exactly matching feedback.
  Existing GPU number/ring/web and same-tick cache mutation checks pass.
- Scripted level 9, analog level 15 and mouse routes pass with menu/warp references
  30/60 Hz and with custom 21/0 Hz at 60 Hz presentation. Earlier 24/24 Hz scene
  checks also passed. These cover flow and dependencies, not visual sign-off.
- Live gameplay reaches 1,200 fine steps in 9.988 active seconds (120.14 Hz)
  after the converted scenes, with 1,200 history draws and displayed pictures.
  The 60 Hz display trial reaches 120.16 simulation Hz with 600 displayed
  pictures in the same 1,200 fine steps.

Tests use CRT off and interpolation off. Hardware-based scene-speed calibration
and hands-on comparison of opening/menu motion are still needed.
