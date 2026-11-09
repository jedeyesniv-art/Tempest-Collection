# Automating emulation++ (plan, 2026-11-09; not started)

Question from the owner: can the process of building native layers on top of the original game be automated, and
how do you get to see every gameplay screen in a large game? Short answer: yes, mostly. Coverage is a much smaller
problem than it looks. Planned trial run: Defender 2000, before any hand porting.

## Key idea: cover drawing paths, not screens
- Tempest 2000 has thousands of distinct screens but only a few dozen drawing paths: vect3d, polyo2d, the
  starfields, rex, texter, ripple, mode7, the melt feedback and the OP overlay. Once those were ported, screens we
  never saw worked too.
- The coverage measure works like code coverage: the set of {GPU module code hash, caller PC, mode / params} seen
  at least once. It levels off fast; a big game might have 100-300 signatures, most of them seen in the first hour.
- The design is fail-safe: anything unrecognised falls through to the emulated GPU and still draws correctly at the
  original resolution. Shipping doesn't need complete coverage, so automation is workable.

## Pipeline
1. **Signature logger (per platform, not per game).** Extend --gpulog / traps: every GPU start, blitter op and OP
   object logs a signature (hash of the uploaded GPU code, caller PC, mode registers, frame, mainloop_routine).
   Output: a machine-readable catalogue, not just a log.
2. **Explorer (save-state tree, Go-Explore style).**
   - Input sources: random / fuzzed play, attract demos, recorded human sessions (input logs), existing TAS movies,
     and cheats / RAM pokes for late content (warp, level select, cheat flags from the 68K source).
   - When a frame shows a new signature or new leftover pixels (step 3), save a state there and branch from it
     later. This gets past "you have to be good at the game".
   - Output: per signature its frequency, a contact sheet and the save states that reproduce it.
3. **Residual detector.** With the remaster ON, whatever the emulated path still draws is by definition not yet
   captured (that's how the HUD overlay and zoom text were found). Run it unattended over the whole state corpus;
   cluster the leftover pixels by signature / screen.
4. **Porting against an oracle.** The same-run GPU-vs-ours check (RM_NOSKIP + RM_OURS + --rmshot, pixel-exact) is
   a pass/fail test, so porting becomes a closed loop an agent can iterate on: translate the routine, diff it
   against the GPU on every saved state that uses it, fix, repeat. The state corpus becomes the regression suite.
   - Further step: statically recompile the GPU routine (Jaguar RISC -> C++) and swap only its final pixel / span
     writes for a high-res rasteriser. Routines that keep fixed-point sub-pixels until the last rounding (most of
     Llamasoft's) would go high-res with no hand porting.
5. **Library reuse.** Signatures are code hashes, so a ported routine is recognised wherever it appears again.
   Llamasoft reused GPU modules between games: measure how much of the Tempest 2000 work Defender 2000 reuses.
6. **Telemetry in the field.** Unknown signatures get logged during the owner's play, with a save state saved at
   the time (like crash reports). Covers the long tail.

## What stays human
- Taste: how to widen to 16:9, mip fades for far lines, CRY-style dimming, pacing render-bound screens to match the
  hardware (BigPEmu as reference). The pipeline only offers sliders and sensible defaults.
- Interpolation meaning: which object is which across frames; continuous motion vs teleports, wraps, respawns.
  Heuristics (discontinuity detection) can flag these but not always settle them.
- Whole-screen takeovers: feedback textures, OP-skip ranges, "this background is ours" gating. Architectural
  decisions, a few per platform.
- Missing source: we had mwenge's source + symbols for Tempest 2000. Without them, disassembly adds a lot of work
  (an LLM helps more than it used to).

## How far it generalises
- Drawing boundary in hardware (PS1, N64, Saturn, sprite / tile chips): almost fully generic (upscaling emulators
  already do this); only interpolation is per game.
- Drawing boundary in software on a programmable chip (Jaguar GPU, ST CPU blits, SNES Super FX, early PC): per
  game / per engine. Our method adds the most here, and the pipeline above helps most here too.
- Rough estimate for a Jaguar-class game: the pipeline does 70-80% of the mechanical work (survey, coverage,
  verification, most routine ports); the owner keeps the architecture and taste decisions. On large games the
  bottleneck shifts from "seeing every screen" to "deciding how each new kind of effect should look in high res".

## Trial plan (Defender 2000)
1. Build the signature logger + catalogue format; run it over the existing Tempest 2000 states first (it must
   rediscover every routine we ported by hand: that's the check).
2. Build the explorer (save-state tree, random input + attract + cheats); measure how the signature count levels
   off over time on Tempest 2000, then on Defender 2000.
3. Residual detector over the Defender 2000 corpus with the Tempest 2000 remaster on: how much already carries
   over by code hash?
4. Port one new Defender 2000 routine with the agent + oracle loop; note what needed a human.
5. Write down the result: how much was automatic, what wasn't, and whether static recompilation of GPU routines
   is worth building.
Everything stays offline and local, like the rest of the project.
