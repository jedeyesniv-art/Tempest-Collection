# Reference scene pacing: corrected investigation

The preceding native performance profile answered the wrong question for
reference-speed selection. Its CPU/GPU cost figures remain useful for capacity,
but cannot justify scene playback speed. No native reference-speed defaults
have been changed by this investigation.

## Original game in Nuance

Three fresh complete original-code traces were captured: 8,000 emulated video
fields each at 100,000 and 200,000 dispatch rounds per field, plus a 6,500-field
100,000-round route releasing fire to reach deaths. The actual original game
and software drawing kernels execute; the native reconstruction/John's renderer
are not the measured execution path. Research replacement/parking options are
zero. MPE3 packet workaround is Enabled, as required by this emulator setup.
Read-only trace hooks now record scene initialization index, original gameplay
frame entry, original return PC and dispatch round. They do not edit guest RAM.

Original menu/game frame calls, scene changes, video fields and presentation
submissions can therefore be separated. Death and bonus both occur in the
captures. Per-call rates below exclude inter-phase gaps and controller discovery
when reporting the opening movement itself.

| Original phase | 100k rounds/field | 200k rounds/field |
| --- | ---: | ---: |
| Opening movement | 15.74 | 32.82 |
| Particle crawl idle | 6.50 | 14.04 |
| Regular gameplay, this route | 10.52 | 19.50 |
| Level-clear web expansion | 8.32 | 19.12 |
| Level-clear vortex | 24.59 | 49.68 |
| Bonus first phase | 19.56 | 40.43 |
| Bonus second phase | 17.93 | 36.09 |
| Level-select exit | 13.70 | 27.96 |

Units are **original calls per emulated second in this configuration**, not
hardware Hz. The separate death route reaches the 8001EC68 loop at 13.17 calls/s
at 100k. A death of the other type occurs in the 200k route at 19.15 calls/s.
Those are different encounters/death types, not a controlled speed ratio.

The scheduler is not cycle-accurate. Normal libretro execution uses a host-time
work budget. Our research scheduler replaces that with fixed dispatch rounds
and a virtual 60 Hz field; a dispatch executes a JIT block or interpreter packet,
which are not equivalent quantities of hardware time. The MPE3 workaround also
changes batching. Thus an emulator's measured scene rate is evidence about its
configuration, not an automatically accurate NUON playback target. One global
calibration multiplier must not be assumed to fix all scenes.

## Hardware recording

Primary reference: [Video Game Esoterica's NUON longplay](https://www.youtube.com/watch?v=qg1BxUnlMMQ).
The uploader describes real NUON hardware, and the video shows a physical player
before the game. The available 720p stream is encoded at 60 fps. This identifies a
credible reference candidate; the capture chain and any temporal processing
have not been independently verified.

Inspected windows include the opening at 14–32 s, menus/level selection and the
first level progression at 120–200 s, and later death/retry sequences at 630–710 s.
Quarter-second contact sheets were inspected for 150–165.75 s and 657–668.75 s.
These are actual footage timestamps, not native emulator timestamps.

| Phase | Footage window | What can currently be concluded |
| --- | --- | --- |
| Opening logo | ~17.5–24.5s | Movement and fade can anchor duration, but controller waiting/first visible frame must be ruled out before dividing126 authored steps by this window. |
| Particle crawl |25.5–31.8s | Strong visible changes recur roughly every 4 encoded frames. Detection gives roughly 13–15 changes/s depending on threshold; this is not yet a logic-tick measurement. |
| Level select |~131–137.5s | Entry, dwell and exit are distinct; user-controlled dwell prevents using total window duration as a rate. |
| Level-clear web expansion |~150.5–153.5s | Candidate alignment to45authored steps suggests roughly15–18reference ticks/s for this occurrence. |
| Following vortex |~153.5–156.75s | Candidate 106-step phase suggests roughly30–35reference ticks/s. |
| Bonus |~157–158.75s | Candidate 32+28-step YAY/4000 phases occupy roughly 1.5–1.75 s, suggesting 34–40 reference ticks/s. |
| Level entry |~158.75–164.25s | Bright transition, label and growing web must be timed separately; no single entry rate accepted. |
| Death/retry |~658.5–668.5s | About 4 seconds of web/death effect precede a faster bright retry/label/spawn sequence. Same displayed level supports the death classification; exact death reason/starting depth is unresolved. |

The numerical bands are **provisional inferences from phase alignment in one
recording**, not adopted settings or statistically validated hardware rates.
Compression, analog noise, repeated/blended video fields and possible skipped
presentations prevent simple image-difference counts from proving game logic
frequency. The full algorithm's phase duration is a better anchor when its
start/end, original iteration count and lack of user interruption are known.

## Implications for the next timing changes

Keep 120 Hz as the desired fine simulation/presentation capability, independently
of reference progression. Do not apply gameplay's 24 Hz reference—or the existing
30 Hz menu/60 Hz warp settings—to every scene by assumption. Even within a level
clear, web expansion, vortex, shared transition, bonus and entry are different
pacing domains. Death also needs a distinct retry/entry handoff.

Before promoting a value to a default: align a complete original loop to exact
footage boundaries, exclude controller waits/dwell/skips, repeat it across at
least another occurrence and ideally an independently captured hardware source,
then compare the resulting duration in our native implementation. Publish ranges
and confidence where the hardware varies. Preserve configurability rather than
pretend to reproduce a single fixed original frame rate.

The immediate result is a corrected reference-data collection and several
candidate bands, not a completed hardware timing calibration. In particular,
level select, exact death-loop identification and per-stage entry calibration
remain open. No density-based pacing or new cadence policy was enabled.

## Evidence and reproduction

Workspace evidence is in `research/tempest-profile/runs/reference-scene-cadence`:
original trace CSVs/run metadata in `100000`, `200000` and `death-100000`; derived
`emulated-scene-rates.json`; source metadata, frame-difference observations and
`hardware-phase-annotations.json`. Trace metadata retains core SHA256, work
budget, input script, options and completion status. Source acquisition and
contact-sheet procedures are in `research/tempest-profile`.

The Nuance instrumentation change is in`src/ResearchDriver.inl` of the local
`research/nuance-upstream` checkout. No installed collection files or the frozen
John integration patch were changed.
