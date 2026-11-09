# Llamasoft on the Jaguar - Tempest 2000 (then Defender 2000) on PC

Owner's project, started 2026-11-08, same toolchain and spirit as Llamasoft on the ST (C:\LlamatronRecomp; its
REMASTER.md is the recipe for the native renderer). Goal: the best version of each game - sharp high-res
graphics, an unlocked (interpolated) frame rate, Xbox-pad stick controls, QoL. All modes first-class
(the owner plays Tempest 2000 mode). Defender 2000 later ("kind of terrible": look for real improvements).
**Entirely offline**: local git only, no remote, no push, no publishing until the owner decides.

Plan and open items: PLAN.md.

## Layout
- `Tempest 2000 (1994).jag` (MD5 602bc995..., == mwenge/tempest2k `make cartridge`), `Defender 2000 (1996).jag`
  (MD5 939f2446...). Never in git. `BigPEmu Jaguar\` = closed-source visual reference only.
- third_party/virtualjaguar: libretro/virtualjaguar-libretro clone (GPL-3), our branch `llamasoft-jaguar`
  from master 34696082 (v3.11.0). Built with its own app/cmake/VJCore.cmake. Not in the outer git.
- third_party/SDL: SDL 3.4.16, copied from the Llamatron repo. Not in the outer git.
- reference/tempest2k: mwenge's source (yak.s = 68K main, src/gpu/*.gas GPU modules). Not in the outer git.
- src/host.cpp: libretro host (env callbacks, options, video XRGB8888, audio, input, save states).
- src/main.cpp: SDL3 window + headless test mode.

## Build / test
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

## To do next (owner to pick the order tonight)
1. DONE 2026-11-09: dash lattice = drawmsg zoom text (REMASTER.md step 9); yellow warp lane lines fixed (vect3d list end).
2. DONE 2026-11-09: Superzapper lightning (ox fline, REMASTER.md step 7). Left: ox / horse psphere, ox starseg; survey
   with --gpulog over a game with zaps, deaths and power-ups.
3. DONE 2026-11-09: HUD layer from screen3 + sharp-bilinear picture (REMASTER.md step 10). Option: a pixel-art
   upscaler (xBR-style) for the HUD / text if the owner wants smoother than crisp pixels.
4. DONE 2026-11-09: 2-player team / Duel checked (REMASTER.md step 12). Goat vect3d (Duel webs) native too.
5. Unseen in tests: the "Excellent" and one-up rex graphics (code covers them) - ask the owner for a save state.
6. Menu items for run_ahead (0-4) and hud_corners; remember the window size / position.
7. Interpolation polish: a scene that didn't change (game slowdown, no buffer swap) still steps; objects drawn with
   several calls per a6 match by occurrence only.
8. Later: title / menu / options screens (antelope backgrounds, stoat text), glow / CRT option, Defender 2000.
- Owner's test states: logs/owner_star.state (warp starfield between levels), logs/play.state (in play, level 1),
  logs/demo6400.state (attract demo, pixel explosion at +104..+170).

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
