# Llamasoft on the Jaguar - Tempest 2000 (then Defender 2000) on PC

Owner's project, started 2026-11-08, same toolchain and spirit as Llamasoft on the ST (C:\LlamatronRecomp; its
REMASTER.md is the recipe for the native renderer). Goal: the best version of each game - sharp high-res
graphics, an unlocked (interpolated) frame rate, Xbox-pad stick controls, QoL. All modes first-class
(the owner plays Tempest 2000 mode). Defender 2000 later ("kind of terrible": look for real improvements).
**Entirely offline**: local git only, no remote, no push, no publishing until the owner decides.

Plan and open items: PLAN.md. **Since 2026-11-13 this is a Tempest Collection** (owner): T2K (done), T3K (Nuon,
next), TxK (Vita), a Steam link to Tempest 4000, maybe arcade Tempest (1981); PLAN.md "Tempest Collection".

## Layout
- `Tempest 2000 (1994).jag` (MD5 602bc995..., == mwenge/tempest2k `make cartridge`), `Defender 2000 (1996).jag`
  (MD5 939f2446...). Never in git. `BigPEmu Jaguar\` = closed-source visual reference only.
- third_party/virtualjaguar: libretro/virtualjaguar-libretro clone (GPL-3), our branch `llamasoft-jaguar`
  from master 34696082 (v3.11.0). Built with its own app/cmake/VJCore.cmake. Not in the outer git.
- third_party/SDL: SDL 3.4.16, copied from the Llamatron repo. Not in the outer git.
- reference/tempest2k: mwenge's source (yak.s = 68K main, src/gpu/*.gas GPU modules). Not in the outer git.
- src/host.cpp: libretro host (env callbacks, options, video XRGB8888, audio, input, save states).
- src/main.cpp: SDL3 window + headless test mode.
- src/launcher.cpp: build\tempest.exe, the Tempest Collection launcher (starts t2k.exe / t3k.exe as processes,
  Steam link for T4K). PLAN.md "Tempest Collection".

## Build / test
Test build: powershell -ExecutionPolicy Bypass -File tools\package.ps1 -> dist\Tempest2000\ (Tempest2000.exe, the
owner's .jag, userdata\settings.txt fullscreen = 1, README with Steam Deck steps) + dist\Tempest2000-test.zip. dist\ is not in git (the ROM is in it: never publish). Icon: tools/make_icon.py (an original claw ship) -> res/t2k.ico
(exe, res/t2k.rc) + res/icon_rgba.h (window). userdata defaults to the exe's folder.
Performance (2026-11-09): T2K_PERF=1 logs emulate / compose / present per shown frame every 5 s (--novsync is ignored
by the compositor here). 4K fullscreen CRT++: 120 fps locked, emulate ~1.7 ms, compose < 0.7 ms. Headless in play:
1.2 ms an emulated frame (the remaster skips the GPU work: 3x faster than the original's 3.8 ms); run-ahead 2 =
3.6 ms a 60 Hz frame (pictures skipped for unseen frames via GET_AUDIO_VIDEO_ENABLE, state buffer reused).
`build.bat` (clang + Ninja in the VS 18 Community env) -> build\t2k.exe.
Headless: `build\t2k.exe "Tempest 2000 (1994).jag" --headless --frames=N --shot=F:file.bmp --press=F:btn[:len]
--wav=f.wav --opt=key=value --userdata=logs\ud`. Always use --userdata=logs\... for scripted runs (never the
owner's saves). Buttons are RetroPad names (b=Jag B, a=Jag A, y=Jag C, start=Option, select=Pause).
Known-good script: press b at 400/600/800/1000 (title -> level select), then any fire at 1500 -> in game by 2200.
Runs ~4-5x real time headless. Core says 60.054 fps, 48044 Hz; picture 326x240.

## Findings
- Game source: readpad/dopad at yak.s ~17428; pad_now bit layout xxApxxBx RLDU741* xxCxxxox 2580369#.
  `roconon` = rotary controller support built into the game.
- Core enhancement db turns True Color and risc idle skip on for Tempest 2000 automatically.

## Stick controls (2026-11-08)
- Core change (branch llamasoft-jaguar): 68K writes to $DFFE00-$DFFEFF land in jagMemSpace (host port; reads
  already came from there). Everything else is done host side in src/t2k.cpp.
- Claw hook: clawcon $80CCCA's first 8 bytes -> jsr $DFFD00 + nop. The stub (hand-assembled in t2k.cpp) does,
  for whichclaw=1 ($EA84) and not auto ($E9B6): claw ptr -> $DFFE10, ticks++ $DFFE14, and if $DFFE00 != 0 it
  sets velocity 20(a6) = $DFFE04 and returns into domove $80CDAC. Install() checks the original bytes first.
- Other addresses: web_x $C986, web_ptab $C988 (web_max+1 XY word pairs; lane i = points i, i+1; centre
  (web_x, 8)), web_max $C990, connect $DB3E (closed web). Claw: 16(a6) pos 16.16 lanes, 20 vel, 24 accel.
- Measured: the claw ticks once per 60 Hz frame; original pad accel = $400/tick up to $6000 (24 ticks to top
  speed) and the claw stops dead on release.
- Modes (userdata\settings.txt, F5 reloads, F2 toggles in the window): lr = deadzone, curve, smoothstep ramp
  from lr_ramp_start to lr_max_speed over lr_ramp_frames, lr_release_frames linear ease-off; aim = stick angle
  -> nearest lane by angle on closed webs (x position on open webs), hysteresis, proportional approach.
  Physical d-pad left/right disables the hook (the game's own pad code runs).
- Tests: --stick=F:x,y[:len], --set=stick_mode=aim, --trace=F0:F1 (pos / v / ticks per frame).
  Verified: lr ramps 0.073 -> 0.45 over 14 frames, 2-frame ease; aim up -> top lane, left -> left lane.
  Open webs are not verified yet.

## Buttons, menu, camera (2026-11-08)
- Game's own button meaning (fire_1/2/3 at yak.s ~23980, changeable in its options): Jag B = fire, A = jump,
  C = Superzapper. pad_now: bit 29 A, 28 Pause, 25 B, 13 C, 9 Option; keypad: 1/4/7 = view 0, 2/5/8 = view 1,
  3/6/9 = view 2 (donkeys, yak.s ~13814), 0 = music on/off, 6 = warp and Option = skip level (cheat mode
  only), * + # = reset, * + # + Option = clear EEPROM.
- Owner's layout (main.cpp ReadPad): A fire, X / RT jump, B / RB Superzapper, Y Option, Start Pause, Back = our
  settings menu (pauses emulation), LB = camera view cycle, LT free.
- Camera: `view` $E350 (0 standard follow, 1 whole web fixed, 2 close-up); the game resets it to 0 on game
  select. Setting camera_view is written every in-play frame, so the choice persists.
- Menu: src/menu.cpp (SDL debug font, placeholder look). Changes save at once via settings::Save (keeps the
  file's comments). Esc opens it on the keyboard. Owner's top speed choice: lr_max_speed 0.41.

## Pause line, quit to title, remapping, save states, EEPROM (2026-11-08)
- Pause screen hook: paustuff's npspri $80DE3C "tst vadj" ($E9F8) -> jsr $DFFD80: if port $DFFE20 != 0 it prints
  the 0-terminated string at $DFFC00 with the game's centext ($80408A) + cfont ($7B58) at y 200. Lower-case
  text = the font's small caps. Main shows "press <first Option button> to quit to title" while pawsed ($E854).
- Quit to title: write z ($E9B0) = 1 and auto ($E9B6) = 0, as checkpause ($80DD48) does for keypad * + #.
  Works from the pause screen (melt transition to the title). Also in the Back menu.
- Remapping: src/controls.cpp, settings bind_<a|b|x|y|lb|rb|lt|rt|start|ls|rs> = fire|jump|zap|option|pause|
  camera|none. Menu: A on a function, then the button (pressing a bound one again unbinds it); Back cancels.
  Back, d-pad and left stick are fixed. Defaults: A fire, X/LT/RT jump, B/RB zap, Y option, Start pause, LB cam.
- Save states: menu slot 1-5, userdata\states\slotN.state (~2.6 MB, core format v17). Headless
  --savestate=F:file / --loadstate=F:file: restored run is pixel-identical to the original 149 frames on.
- EEPROM: the frontend's job in libretro (the core packs eeprom_ram into its SAVE_RAM buffer on every write and
  unpacks the buffer on the first retro_run). userdata\tempest2000.eeprom, loaded before the first frame,
  written when changed (checked every 60 frames) and at exit, old copy -> .bak. Keep a missing file as the
  core's all-zero buffer, NOT 0xFF: eepromload rejects the chip unless word 63 = $6510, and eepromsave only
  writes words that differ from epromcopy, so an all-ones start leaves stale words -> checksum fail -> reset.
  The game saves on unpause (yak.s ~14325), option changes, high scores, clearee.

## Remaster renderer
Survey + design: REMASTER.md (frame structure, GPU modules, call sites, capture / compose / interpolate plan).
Tools: core line-A host traps (src/traps.cpp), `--gpulog=F0:F1:file` (src/gpulog.cpp).
reference/tools/rmac is a clone of the assembler; building / running it was refused by the permission
classifier (downloaded code) - ask the owner before trying again.

## Where we are (end of 2026-11-09 morning; start here next time)
- Done: stage 1 host, stick controls (owner's lr tuning 0.41, aim as alternate), owner's button layout + remapping,
  Back menu (settings, save states, return to title), quit to title on the pause screen in the game font, EEPROM
  saves, cursor hidden. Remaster (F8, default on), every primitive verified against the GPU in one run
  (RM_NOSKIP=1 RM_OURS=1 --rmshot): vect3d lines, solid web, starfield, solid claw / enemies, pixels / rings,
  camel rex sprites (shots, pixel explosions). Smooth motion (interpolation, menu toggle) + run-ahead 2, any
  aspect ratio (Hor+ / Vert+, HUD to the window corners). Details: REMASTER.md steps 2-6.
- Owner 2026-11-09: smooth motion + run-ahead "look and feel incredible"; asked for any aspect ratio (done).

## To do (refreshed 2026-11-09 evening; done items are in REMASTER.md steps 7-14)
1. DONE: text over our own backgrounds (REMASTER.md step 15).
2. DONE: pause / game options / high-score entry (step 15).
3. DONE: feedback steps crossfade. Left (low value): slowdown repeats, multi-call objects by occurrence.
4. Unseen in tests: "Excellent" / one-up graphics, the AI droid's zap, Beastly mode (cheat_beastly unlocks it now).
5. QoL: remember the window size / position; menu items for run_ahead (0-4) and hud_corners.
6. Performance: 4K with CRT++; a low-end GPU (Steam Deck class) if that's a target.
7. Open questions: the psycho bonus sphere invisible on the original (left to the GPU); the owner's "offset ghost
   image of the UI" (not reproduced; ceiling bonus mfudj merge 8 lines low is a candidate).
7b. FIRST from 2026-11-14: Tempest 3000 (Nuon) via the Nuance libretro core, performance first: PLAN.md
   "Tempest 3000 plan". Standalone: Nuance_067\Tempest3000.bat; source third_party\NuanceResurrection (branch
   llamasoft-nuon, not in the outer git). 2026-11-14: stage 1 done (build\t3k.exe plays it; tools\build_nuance.bat
   then build.bat), stage 0 measured, stage 2 emulated time done (deterministic; game speed = nuon_rounds): see PLAN.md
   "Stage 2 done". Next: stage 3 speed (PLAN.md "Stage 3 next").
8. TxK (PS Vita) via the Vita3K libretro core, after Tempest 3000: PLAN.md "TxK plan".
8b. Neon Mode (Neon light synth as the in-play background), after TxK works in the combined build: PLAN.md
   "Neon Mode plan".
9. Then Defender 2000, starting with the automation trial (AUTOMATION.md, owner's plan 2026-11-09).
- Owner's test states: logs/owner_star.state (warp starfield between levels), logs/play.state (in play, level 1),
  logs/demo6400.state (attract demo, pixel explosion at +104..+170).

## Porting recipe (learned on Tempest 2000, 2026-11-09; for Defender 2000 and later)
Finding things
- Survey first: --gpulog over a long run (attract + play + a scripted route), grouped by caller / module / mode / frame
  range; map callers to routines with reference/t2k.sym. A timeline contact sheet (--shot every N frames, a grid) shows
  which screen each range is. --peek / --poke (RAM long / word) and RM_TRACE (published scene per frame) for state.
- What draws a thing is often not what it looks like: the "dash lattice" was zoom text, the HUD is an OP overlay
  (screen3) not a GPU draw, the tunnel is two point routines, the bonus ground grid a starfield. Check the picture
  with the remaster ON (only uncaptured pixels remain) to see what is left.
- Read the GPU routine fully before porting; copy its integer set-up exactly (fmult / sdiv / >> 7 / table bytes) and
  its quirks (psphere steps its phase twice, texter's shrink hack keeps the narrower width, vect3d ends a list on a
  zero vertex BYTE). Cheats / test routes come from the 68K source (pad masks, flags like chenable / warpy).
Verifying
- Same run, GPU vs ours: RM_NOSKIP + RM_OURS + --rmshot, tools/side_by_side.py, and a red / green pixel overlay for
  exact positions. Window-only effects (feedback, ripple, mode7) need --state + --winshot comparisons against
  --set=native=1 (Original mode). Feedback chains need a state from BEFORE they start.
- Always test the path back (play -> quit to title), not just boot -> play; and run-ahead consistency
  (--runahead=2 picture == plain + 2 frames, byte-identical).
Architecture that worked
- Capture at gpurun, skip the GPU for captured modes into the play buffers, draw natively in window space; the core's
  picture over ours with black see-through. Interpolate captured inputs; hidden run-ahead frames rewind ALL host state.
- Screens the game builds by feedback (scar), full-screen warps (ripple, mode7) or the blitter: take over the
  full-overwrite op, keep our own window-sized feedback texture, and have the core's OP skip the stale buffers
  (vjOPSkip ranges); gate other takeovers (text, sprites) on "this buffer's background is ours".
- Overlays (HUD) as our own layer: skip the OP object in the core rather than masking pixels (masking leaves holes);
  move content clusters to the corners, never fixed zones.
- Emulated GPU / blitter work is nearly free: render-paced screens run too fast; pace them by holding the 68K in
  gpuwait (host port), measured against the hardware's look (BigPEmu as the owner's reference).
- Extending to wide screens: the game's world usually exists past 4:3 (widen culls); clipped textures can tile
  mirrored (wrap addressing); far / minified lines need mip levels and a fade.
- Small custom shaders (HLSL -> DXIL, tools/build_shaders.py) for CRY-style dimming (subtract from the brightest
  channel, keep hue) - multiplying or subtracting RGB looks wrong.
Working with the owner
- They test in the window and report with screenshots / save states (copy slotN.state to logs\ first); every tunable
  look gets a menu slider with a sensible default; "Original mode" must turn everything off.
Tooling pitfalls (cost time today)
- Inline python / heredocs mangle backslashes (\n): write edit scripts with Write and run python -I <file>.
- From bash give the exe forward-slash paths; check file times when a fix "shows no change" (stale screenshots).
- Headless runs keep only the LAST --savestate; window runs over ~100 s need the timeout raised.

## Lessons (2026-11-09)
- From bash, give the exe forward-slash paths (logs/x.bmp): a quoted backslash scratchpad path silently failed to
  write AND to delete, so I re-read stale screenshots for three rounds. Check file times when a fix shows no change.
- Menu tests: --menu / --menu=all open the Back menu at start (all sections), --winshot then counts presents.
- Emulated GPU / blitter work is near free: screens paced on render completion (melt, ripple) run too fast. Measure
  with a timeline contact sheet (--shot every N frames) and a brightness curve, fix with host pacing (gpuwait hold).
- Heavy (menu) screens can't afford run-ahead: check real-time cost per screen (headless fps with --runahead).
- Capture lists only refresh when the game clears with the GPU: screens drawn another way (blitter menus) leave
  stale lists, so anything keyed off "what we captured" needs a staleness check. Tell play from menus by the HUD
  overlay object (beasties+64 ObType 8/7) plus mainloop_routine == draw_objects; test the path play -> quit to
  title too (mainloop_routine is not reset), not just boot -> play.
- Run-ahead needs ALL host-side state rewound, not just the core's: our hooks keep state (t2k stick ramp, the
  $DFFC00 port area in jagMemSpace, which the core's state does not include; remaster capture lists). Test it as:
  run-ahead picture at f == plain run at f + N, audio and --trace byte-identical.
- Interpolate the captured INPUTS (camera-relative positions, angles), not projected output; keep headless at t = 1
  so the GPU-vs-ours checks stay exact. Angles need a fractional sine (SinT).
- Only take over rex calls from the draw_vex object routines (by caller range): the title / yak / level-select
  rex calls draw over backgrounds we don't capture, and skipping them there would hide them.
- The game's world exists beyond the 4:3 frame (web, enemies, stars): wide screens only needed the culls widened.
- Image previews may be scaled (e.g. 1.02x): measure offsets with tools (fit_origin.py), not by eye.
- Tools: tools/side_by_side.py (original | ours, crop), tools/check_interp.py, RM_T=<t>, --runahead=N,
  --aspect=W:H, --winsize=WxH.

## Lessons (2026-11-08)
- Verify each native primitive against the GPU in the SAME run: skipping GPU work shifts main-loop timing, so
  remaster-on and remaster-off runs drift apart (claw animation frames differ) and side-by-sides across runs mislead.
- The picture for a frame shows the buffer that was cscreen at the end of the PREVIOUS frame (one-frame lag).
- Intensity bytes: vect3d / pixels = (value >> 16) of the blitter I3 form; polyo2d intensity words are $xx00
  (byte = word >> 8); web3d bright = r0, far = r0 / 4.
- Read the .gas routine fully before porting; "comments say X" was wrong twice (llama mode list, warp streaks).
- When the owner reports a visual bug with a save state: copy build\userdata\states\slotN.state into logs\ and
  test from the copy (--loadstate=1:<file> headless, --state=<file> --winshot=N:<bmp> in the window).
- Python edit scripts with backslashes: write them with the Write tool (Bash heredocs mangle \n here); cmd won't
  run exes from the current folder (use full paths).
