# Ring mini-game audit — 2026-11-26

## Scope and confidence

The interactive ring course is original scene 12, with scene 13 for success.
It uses `SceneTimingKind::warpGame` and `native_warpgame_speed`, default 24.
The ordinary bonus presentation uses a separate 36-tick reference. Neither
setting should be substituted for the other. The ring game's 24 is an assumed
reference, not a measurement of original hardware performance.

No verified hardware recording of the ring course was located during this pass.
NUON-Dome's contemporary review describes the bonus courses and links older
footage, but that does not establish their update cadence:
https://www.nuon-dome.com/t3krev.html

## Recovered behavior

`TempestSessionWarpSkip.h` owns music, course selection, steering acceleration,
shots, ring-crossing events, scoring, failure and success. The course selector's
upper group increases initial speed and rotation; its low nibble selects course
data. Initial forward speed is `0x1000 + (group << 11)`. At group zero this is
1/16 of a course-depth unit per reference tick. This is not necessarily one ring
per unit: course layout determines actual ring spacing.

`PreparationDepth::bonus` is both geometry preparation and simulation. It owns
moving obstacles, shot aging and hits, player/ring contacts, and six trailing
player rings. Packed history and shot ages advance at authored boundaries;
geometry is evaluated on fine samples. Treating this worker as presentation
alone would change the game.

The ring outlines, grid and projectile marks are generated projected segments.
Textured/background drawing and framebuffer feedback are separate parts of the
scene. A visual problem with those layers need not be a ring-shape problem.
The wide-HUD feature deliberately excludes warpGame; it does not reposition
this scene's player rings or course.

## Confirmed corrections in experimental source

1. A speed-up crossing is a discrete `+0x200` event. The high-rate adaptation
   incorrectly divided this award by the timing denominator, while slowdown
   crossings still removed a full `0x200`. At 24/120 the award was about 102
   instead of 512. Restored the full award at every cadence. This is separate
   from selecting a better real-time reference speed.
2. Shot/ring collision originally reads the packed ring position before the
   worker's movement, while drawing uses the advanced position. Fine sampling
   had replaced that collision read with the end-of-sample drawing position.
   Restored the ordering and retained the original packed coordinate bits.
   Fine samples evaluate the start-of-sample moving coordinate.

The second correction restores exact full-rate equivalence on the captured
original worker corpus: 2,660 cases, zero mismatches (380 per worker module,
including module 17). Before correction, three module-17 ring cases differed,
including course state and generated drawing records. These are captured
Nuance execution results, not a newly measured hardware timing sample.

New executable contracts cover full speed awards at 1/1, 1/2, 1/5 and 3/10
steps, slowdown's lower bound, and the pre-movement shot-hit ordering.

## Remaining visual/timing investigation

### Direct Nuance speed comparison

An additional opt-in original-code run now supplies a practical emulator
guideline. After 50 normal gameplay calls the research probe redirects one
callee entry into the original `80016EC0` mini-game routine. Scene initialization,
course logic, preparation workers and software rasterization then run on Nuance.
The hook requires an early-return after research PC redirection in `MPE.cpp`;
the first attempted probe without that guard did not enter the course and is
not timing evidence. The tested DLL is `nuance_ring2.dll`, independently linked
from the existing profile build with only the probe translation units replaced.

Normal `retro_run` wall-clock execution, no fixed-round virtual clock, no native
worker replacements, no interpolation. Input is released when the ring course
starts. The first course's constant-speed approach produced 65 observations:

| Quantity | Result |
| --- | --- |
| Original updates between first and last observations | 64 |
| Nuance elapsed time | 3.474485 seconds |
| Nuance average update rate | 18.419996 ticks/s |
| First 30-update window | 19.599734 ticks/s |
| Next 30-update window | 17.617741 ticks/s |
| Course speed word throughout | 4096 (`0x1000`) |
| Progress difference | 262144, exactly 64 × 4096 |
| Equivalent elapsed time at native reference 24 | 2.666667 seconds |
| Native reference speed / observed Nuance speed | 1.302932, about 30% faster |

This covers the approach to the first contact, not a successfully completed
course or every later effect. The uncontrolled course fails there with steering
released. It is enough to establish that 24 currently runs faster than this
Nuance reference. A provisional 18–20 reference would better match this section;
no timing default has been changed. This is emulator guidance, not a hardware
measurement. Driver tracing and per-tick logging are enabled in this run.

Evidence: `ring-audit/nuance-speed/wall-valid-ring.csv` and `wall-analysis.json`.

### Pink-panel visual comparison

The user identified the large pink triangular panels as the suspect feature.
A further original-code Nuance run captured the active framebuffer at course
updates 12, 24, 36, 48 and 60. These are original software-rasterized pixels,
not output from the shared replacement renderer. Raw framebuffer snapshots and
diagnostic RGB conversions are under `nuance-speed/visual/ring-*`.

The captures show the projected rings, streaks and soft feedback/background,
without the large solid textured pink faces seen in the native screenshots.
This establishes a renderer regression rather than grounds for removing an
intentional game feature based on appearance alone. The two prior timing/
collision corrections do not repair this surface-decoding problem.

The native effect trace shows a `0x0011000B` web-surface operation on every
warpGame frame. That decoder consumes `0x800BACF0`, the web quad scratch stream.
The course's depth worker generates line/ring records and does not generate
those quads. The decoder can fall back to the last active web definition when
the original drawing-MPE context is unavailable. This is the same buffer and
fallback area implicated in the earlier Web-9 panel bug.

Disassembly of original module `806AD2B0` pins down the dispatch error:
`203003EA..20300400` (relocated) reads signed command word 1 from `20100034`.
Negative means immediate return; zero selects the ordinary web; positive
selects a separate explicit-quad mode. Native debug captures show the ring
course sets `FFFFFFFF` while disabled and `1` while active. The inherited
decoder ignored both modes and consumed sixteen stale quads instead.
This is missing mode dispatch, not a new segment-count mistake. The initial
negative-only experimental fix was insufficient; captures in `panel-fixed-*`
and `panel-debug-fixed` demonstrate that partial attempt, not the final fix.

The fix returns before texture/bounds/quad reads for every negative mode in
every scene. The ordinary zero-mode path also restores the original signed
`801095A0 > 127` early return at `20300450..20300470`. Existing geometric count
bounds remain necessary for enabled open webs. No RAM is cleared, no colours
or triangles are filtered, and no scene name is used to suppress geometry.

Positive mode now reads the four inline position/UV pairs from command words
4..11, exactly where `20300402..2030040E` copies them for the original rasterizer.
The supplied pairs describe the two near/far edges; convert strip order
0,1,2,3 to perimeter order 0,1,3,2 for the mesh/shader.
It does not consult web scratch or definitions and does not apply the ordinary
web's picture-clock texture scroll. Explicit quads are marked so the precision
adapter cannot substitute unrelated web-scratch positions.

GPU tests retain a valid definition and a fully populated stale quad buffer,
then verify three negative modes generate no surfaces and leave output clear.
They also test the ordinary level gate at 127/128, alongside the previous
open/closed-web bounds and both CPU/GPU mapping paths. Positive-mode fixtures
poison both scratch geometry and precision metadata and verify one inline quad,
correct positions, independence from the ordinary level gate, no clock-based
UV scroll, and CPU/GPU coverage at its intended location. No shader change is
made; pixel-exact equivalence of every explicit-quad rasterizer edge case is
not claimed. The full renderer suite and all four CTest suites pass.

### Score flash signedness

After restoring surface dispatch, some captures still contained a rectangular
score flash. This is independent of the stale web surfaces. The opacity at
command word 6 is signed in the original `ld_s` / `mul` path
(`20300978..20300982` of module `806AF1E8`). Flash decay may subtract past zero
for one frame before the next update applies its floor. `Text` converted that
word directly from unsigned, turning a small negative fade into huge positive
opacity. CPU and GPU paths shared the same erroneous decoded parameter.

The conversion now uses `int32_t` before conversion to float. Both paths clamp
the resulting negative coverage to zero. Regression fixtures use zero,
`FFFFFFFF` and `FFFFE3FF`; the pre-fix build fails the negative-opacity contract,
and the corrected build passes both CPU-image and GPU-output checks. No caller
timing or original flash decay arithmetic was changed to hide the renderer bug.

Per-operation diagnostic captures helped isolate the drawing order. A readback
at driver picture 2272 must not be equated with host screenshot frame 2272:
those counters differ by one, which matters for this one-picture flash.
Temporary per-operation readbacks and hardcoded diagnostic frame checks were
removed from the implementation after the investigation.

Player trail history remains authored-rate packed data, unlike continuously
sampled player movement. Its motion is an audit target; no speculative trail
replacement has been made. Fine-sample contact windows and the background/
feedback layers also need motion-based comparison on the reported defect.

The validation route's scheduled fire presses are counted in host samples.
Consequently Original and 120Hz runs do NOT receive the same timed input just
by sharing `--check-warp`. Their total run durations/results must not be used as
a direct cadence measurement or deterministic cross-mode equivalence claim.

For hardware calibration, measure repeated ring crossings and course depth
progress at a known selector/group, alongside real elapsed time. Keep capture
field rate, unique game pictures, and simulation progress separate. A recording
with different shooting or course group can change forward speed, so an average
bonus duration alone is not a reliable target.

## Evidence and release boundary

Local investigation artifacts: `research/tempest-profile/runs/ring-audit`:
Original and 120Hz traces/screenshots, before/after reference replay logs, and
a standalone replay of the current preparation headers against the original
captured corpus. The original screenshot at frame 1200 precedes the mini-game;
use `0/ring-game.bmp` (frame 2400) for a scene-12 still. These stills are not
time-aligned or evidence of identical input.

All four regular CTest suites and the complete GPU timing/render suite pass.
The GPU suite's ring shader comparisons are useful renderer checks but do not
constitute complete ring-mini-game visual validation.

Stable revision-2 builds and handoff ZIPs have not been modified. Experimental
executable: `build-native/Release/t3k-ringfix.exe`; workspace launcher:
`Play-Tempest-Ring-Fix.cmd`. It retains the user's normal settings and uses the
existing owned disc image and stable package assets. Timing targets are not
changed by this renderer correction.

Final runtime evidence is under `final-0`, `final-60`, `final-1` and `final-web9`.
All three frame modes complete the course validation, at 4:3, 16:9 and 21:9
respectively. Web 9 completes 1,800 fine gameplay updates. Final captures remove
both the stale web faces and the opaque rectangular score flash; the intended
inline textured course strip remains. The broad softness/glow of Nuance's
software output is not asserted to be pixel-identical to the replacement
renderer by these tests.

## Follow-up: corrected validation route

The broad coverage audit found that the old `--check-warp` route skipped normal
life settlement and inherited opaque-clear mode from gameplay. Production flow
already performs settlement. The diagnostic now invokes that same settlement
before entering the course. Corrected captures restore the broad soft feedback;
the old `final-*` stills above must not be used as evidence of missing glow.
Original, 60Hz and 120Hz corrected-route validations pass. See
`T3K-RENDER-COVERAGE-AUDIT.md` for matched original-command raster comparisons
and remaining mode/flag gaps. Stable packages remain unchanged.
