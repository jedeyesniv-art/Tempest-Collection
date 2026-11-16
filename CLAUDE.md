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
- third_party/vita3k-libretro: WizzardSK/vita3k-libretro (GPL-2) for TxK, our branch `llamasoft-vita` from libretro
  29ad0e79 (one patch: OpenSSL no-asm, so no NASM). tools\build_vita3k.bat (MSVC, Ninja Multi-Config, as upstream CI)
  -> build_vita3k\bin\Release\vita3k_libretro.dll (32 MB; needs the MSVC C++ runtime DLLs). Configure needs the
  portable Strawberry Perl in third_party\strawberry (owner approved the download) and downloads FFmpeg + curl.
  Firmware in vita_firmware\ (PSVUPDAT.PUP system, PSP2UPDAT.PUP fonts). None of these in the outer git.
- third_party/mame-libretro: libretro/mame (current MAME 0.289, commit 5f5a0774, shallow) for arcade Tempest (1981),
  built with only src/mame/atari/tempest.cpp by tools/build_mame.sh (Git Bash + the MinGW gcc 13.2 / GNU make in
  third_party/strawberry; SDL3 headers via -DSDLMAME_SDL3 for the libretro OSD's key table) ->
  tempest_libretro.dll (43 MB, only Windows DLLs). Romset: tempest.zip (MAME's current split set) - not in git.
- src/lr_host.cpp: the shared libretro host for DLL cores (txk.exe: Vita3K, OpenGL on demand; t81.exe: MAME,
  software frames, mouse deltas for the spinner, save states). GET_VARIABLE answers the defaults the core declared
  (MAME takes a missing answer as an empty value). LR_LOG_DEBUG=1 shows the cores' debug log.
- src/host.cpp: libretro host (env callbacks, options, video XRGB8888, audio, input, save states).
- src/main.cpp: SDL3 window + headless test mode.
- src/launcher.cpp: build\tempest.exe, the Tempest Collection launcher (starts t2k.exe / t3k.exe as processes,
  Steam link for T4K). PLAN.md "Tempest Collection".

## Build / test
Play folder (all games, for the Deck): powershell -ExecutionPolicy Bypass -File tools\package_collection.ps1 [-SkipBuild]
-> dist\TempestCollection\ (TempestCollection.exe launcher, t2k / t3k / txk / t81 + cores + C++ runtime, system\ with the
installed Vita firmware, game\ = the four games hard-linked, userdata\ = the owner's, all fullscreen; ~1.14 GB, never publish).
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
- Menu: src/menu.cpp, SHARED by every game program since 2026-11-14 (SDL debug font, placeholder look): a game
  registers its page with menu::SetPage (src/t2k_menu.cpp, src/t3k_menu.cpp; items show via `when` lambdas, game
  actions numbered from menu::kGameAction) and its controls functions with controls::Setup. The launcher passes
  --from-launcher (last item "Return to launcher"). t3k.exe: Back / Esc opens it, the emulation thread parks
  (emu_pause / emu_parked) before the menu may change settings. Changes save at once via settings::Save (keeps the
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

## Where we are (end of 2026-11-16; start here next time)
T3K, all committed:
- Every picture is native from power-on (all 18 + the last three types: 0031000E title field, 00310409 title wave,
  0000000F ripple feedback). Full route fields 60-9000: 2980 of 2980 native, mean error 2.78 against the emulator.
- YCrCb pipeline: native pictures + feedback kept in YCrCb like the hardware (fade about 16 / 128, clip per byte,
  truncating), SDL GPU renderer + src/shaders/ycc_{fade,out,in,ripple}.hlsl (tools/build_shaders.py; afterwards
  `git checkout` T2K's regenerated *.dxil.h - newer dxc output, same source). The light machine is no longer white.
- Sharp vectors (owner: "gorgeous"): small items / short-stroke items (text, claw, enemies, HUD) = thin core + glow;
  web and streaks keep the 240p look. Settings sharp_vectors / sharp_core / sharp_glow (menu Display).
- Controls: stick_mode stiff (default) with a fine zone (<= 60%: creep + click lane to lane, T2K style) and a sweep
  zone (owner's tuning: top 21 lanes/s, ramp 1 s, curve 1.5, deadzone 0.10) - owner happy, still tweaking.
  Momentum (TxK feel, measured) kept as an option. PLAN.md "Claw feel study" Results.
- No controller-changed nag at boot (skip_controller_screen). log_emulated: unported pictures -> userdata\emulated\
  (log + save state + shot of the first per type).
- Bug found + fixed: point rings 00310008 sign (level-select dots). Lesson: whole-picture mean error hides sparse
  errors and the checks never ran the native feedback CHAIN or high_res - use the sweep (emulated | native every 300
  fields from boot) and per-type checks; the native renderer has no high_res handling (owner set high_res 0).
Next candidates (owner picks; PLAN.md "Native renderer status" + "Collection status"): T3K high scores / settings
(flash not saved), repackage the play folder for the Deck (today's work isn't on it), interpolation for everything +
run-ahead, orange patches on the brightest warp / bonus screens, sharpen the glowing text (0010000D) if wanted,
T2K to-do batch DONE 2026-11-16 (B / Backspace = back in menus, enemy bullets at the original size
(remaster_shot_size), bonus 1 pink line (image row 128), bonus 2 = the tunnel's dot drawing cost; T3K powerup burst
00310208 native); T2K performance plan for the end (PLAN.md "T2K performance plan": Deck measurement, a ~120 ms
event stall, run-ahead / catch-up),
T4K on the Deck, Space Giraffe entry, t81 CRT, TxK saves.

## T3K game speed + warp speed (2026-11-16)
- game_speed = pictures a second, any 15-60 (default 24, owner); replaces flip_cap (--flipcap=F still works: 60 / F).
  warp_speed (default 60, 0 = off) + warp_ramp (s, default 0.5): src/t3k_main.cpp SpeedControl sets the core's flip
  interval after every field. Warp = a picture with zoom feedback and no web surface (warps between levels and after a
  death, also game over); from the capture's entries headless, from the native picture in the window (the draw gate
  leaves the capture empty). Pacing: each ring Slot carries its interval. Measured: warps 43-58 pictures/s (the core's
  round budget caps them; the hardware estimate was 52-54), window holds 2 refreshes in the warp, 5 in play (120 Hz).
- Display list types per screen: NUANCE_ENTRYLOG + tools/t3k_census.py. Memory searches by label:
  tools/t3k_classsearch.py, tools/t3k_labelsearch.py (no single warp flag in memory: object-pool words only).
  T3K_SPEED_LOG=1 (headless): warp / rate / interval / flips every 10 fields.

## T3K wide screens (2026-11-16)
- Setting widescreen (default 1, menu Display "Wide screens"): native pictures fill any window wider than 4:3 (16:10,
  16:9, 21:9 checked); narrower windows get bars. Renderer::Draw / Seed / Redraw take a view around the 4:3 dst; the
  feedback textures cover the view, full-width fills / planes (built 360 px wider each side) / zoom / ripple (shader
  view uniform) carry on past the edges, and the game already places particles, dots, streaks there. HUD stays in its
  4:3 spots. 4:3 unchanged (nativecheck play 2.39, boot 1.09). Tests: --nativeaspect=W:H with --nativeshot,
  --winsize=WxH (window, not fullscreen). Shots: logs/t3k/wide/.

## T3K high scores (2026-11-16)
- The game saves nothing (BIOS log: no flash / MemDev calls; it has level passwords). Table: 8 x u32 scores at
  $80113420 (defaults 10000 9123 8456 7789 6123 5456 4789 3123); names only in the high-score screen text at $8010C3A0
  ("^Pn:^X " + 14-char name + "^M" + 10-char score digits). src/t3k_scores.cpp restores userdata\t3k_highscores.txt
  (score|name lines) once the defaults are in memory, saves on change (every 60 fields) and at exit; a run whose
  table isn't the defaults (a later save state) neither restores nor saves. Setting keep_high_scores.
- Score $800DC240 (u32), lives probably $800DC274. Core NUANCE_BIOSLOG=1 (first 20 calls per BIOS index, 2 = all).
- Known bug: headless --press after --loadstate does nothing (from boot it works). Options / fx / music levels
  not saved yet.

## T3K native renderer (reference; state end of 2026-11-15, see "Where we are" above for newer)
Owner: decouple game speed from frame rate, Steam Deck = min spec -> draw T3K's display lists natively on the GPU
instead of emulating its renderer MPEs; the renderer work is the priority. Ordered to-do: PLAN.md "Native renderer
status" (end of the T3K section); per-type details in the PLAN sections above it.
- Model: every picture = a display list (64-byte entries at $8004E040, type = last word) drawn by MPE0-2 in bands after
  an MPE1 pre-pass; often feedback of the previous picture. 18 entry types in all. Takeover is per WHOLE picture:
  a picture is native only if every entry type in it is ported, else the emulated picture is shown.
- Code: core third_party\NuanceResurrection (branch llamasoft-nuon) src/capture.cpp publishes each picture's entries
  + the bus words MPE0 read (+ mode 2: its frame buffer = ground truth) at the flip; host src/t3k_native.{h,cpp}
  (BuildPicture -> ops; Renderer with two window-sized feedback textures; Interpolate). t3k.exe keys: F7 native_view
  (0 off, 1 native pictures, 2 vectors over emulated, 3 vectors alone), F6 native_interpolate.
- Ported + verified: 00710001 vector shapes, 00000300 fill, 00300004 zoom feedback, 0011000B web surface, 0010000D
  glowing number (score / title 3000; tools/t3k_textfit.py).
- VERIFY every type with ground truth, never with screenshots: NUANCE_ONLYTYPES=<done types> T3K_NATIVE_FORCE=1
  t3k.exe --headless ... --nativecheck=F0:F1:<dir> (per-picture mean error + truth | native | diff images). Decode
  a module with --disasm=F:3:<lib addr>:N, NUANCE_SPANLOG=<type> (pixel rows read / written + entry words, job word),
  NUANCE_PCLOG=<local pc> + NUANCE_NOJIT=1 + NUANCE_AOT_SKIP=<module prefix> (registers inside one module), NUANCE_ENTRYLOG
  + NUANCE_ENTRYDATA=1 (lists + data per picture), NUANCE_SKIPTYPE (what a type draws), NUANCE_NODRAW (cost).
- Python models: tools/t3k_vecmodel.py, t3k_linefit.py, t3k_vecrender.py, t3k_zoomfit.py, t3k_webfit.py,
  t3k_census.py, t3k_interpcheck.py.
- Ported + verified 2026-11-15 (later): 00310108 (draws nothing in play), 00310309 particles, 00310209 dot images,
  0031020E disc shapes, 00310010 disc grid (all GPU discs, Op::kBlobs), 00500005 ring (an image, the glyph path).
  Whole play pictures, no forcing: every play picture tested is complete (2701-2960, 2881-2960 with an explosion,
  3151-3299), mean error 2.80-2.86 against the full emulated pictures.
- Step 3 DONE (PLAN.md "Step 3: the draw gate"): with native_view 1 (F7) + native_skip 1 the emulated renderers skip
  every native picture (core nuance_set_picture_callback; pictures built from live memory): play 2.2-2.4x less
  emulation time. Native from the menus on (boot / title still emulated). Tests: t3k --gate=1 (live == captured) /
  --gate=2 (skip) headless, T3K_GATE_LOG=1.
- 2026-11-16: no "controller configuration has changed" screen at boot (core nuance_set_pad_present_at_boot; t3k setting
  skip_controller_screen, default 1). Boot route now: a at 900 / 1300 / 1700 / 2100 / 2500 (no start at 400); play
  ~3000. Every picture is native now, boot logo included (0031000E, 00310409, 0000000F ported). Native: YCrCb pipeline (GPU renderer + src/shaders/ycc_*.hlsl), point-ring sign fix; open: orange patches
  on the brightest warp / bonus screens.
- 2026-11-16: claw feel study done for TxK; T3K stick_mode stiff (T2K feel + lane settle) is the default and the
  owner likes it (PLAN.md "Claw feel study" Results). Momentum (TxK feel) stays as an option.
- Was next (owner 2026-11-15): the claw feel study (PLAN.md "Claw feel study: TxK and Tempest 4000") - measure TxK / T4K
  claw response, tune T2K / T3K controls; then T3K game speed; step 4 (interpolate-all) only if still needed; YCrCb
  pipeline (polish).
- Workflow per type (tools from 2026-11-15): t3k --disasm=F:3:<lib>:N > m.dis; tools/t3k_annotate.py m.dis (local pcs,
  helpers); tools/t3k_logrun.py --state play_f2900 --span <type> --entries --out logs/t3k/<x>/run; a fit script on
  tools/t3k_spanlib.py (prepare / predict); unsure of a value -> --pc <local pcs> --interp <lib lo>-<hi> --pictures
  P0:P1 --pix; port; --only <done types> --nativecheck with T3K_CHECK_MASK=<span log>. Read Nuance's instruction code
  before guessing semantics.
- Save states: t3k --savestate=F:file / --loadstate=F:file (continues at F + 1); play states in logs\t3k\states\
  (play_f2700 / 2900 / 3000 / 3150). Start investigation runs from these, not from boot.
- Pixel loads in the MPE modules: Y byte << 22, Cr / Cb signed (byte - 128) << 22, Z byte << 24. PCLOG inside a module:
  NUANCE_NOJIT=1 NUANCE_AOT_SKIP=<lib prefix covering every chunk-start module> (timeline slips: same-run data only).

## Collection state (2026-11-15)
All five entries run from the launcher (tempest.exe; TempestCollection.exe in the play folder): Tempest (1981, t81 /
MAME), Tempest 2000 (t2k), Tempest 3000 (t3k / Nuance), TxK (txk / Vita3K), Tempest 4000 (Steam, + helper DLL).
The owner played the play folder on the Steam Deck: all good. Open items, in order: PLAN.md "Collection status and
open items" (first: Tempest 4000 not offered on the Deck - the launcher's install check reads Windows Steam's
registry, absent under Proton).

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
7b. Tempest 3000 (Nuon) via the Nuance libretro core (third_party\NuanceResurrection, branch llamasoft-nuon, not in
   the outer git; tools\build_nuance.bat then build.bat). State 2026-11-14 (details: PLAN.md "Tempest 3000 plan" and
   "High resolution"): plays at full speed (emulated time, AOT, parallel renderers), flip_cap 3 (owner's choice,
   20 pictures/s), picture_blend (F3), lr stick controls, F4 on-screen counter (pictures/s, fields/s).
   HIGH RES 720x480 works (owner: "stunning"): setting high_res = 1 / --hires; core src/hires.cpp + hires_table.inc
   (tools/t3k_hires_table.py: 87 code patches checked against original words), display list entry transforms by type,
   buffers remapped above 8 MB. Original mode stays pixel-identical. Investigation tools: NUANCE_PIXLOG, _RECTLOG,
   _BASELOG, _DMALOG, _HIRES_TRACE, _HIRES_SKIP (PLAN.md lists them). Open: big vector objects (size >= 256, the
   spinning TEMPEST logo) stay old size; menu dot sizes; a high-res AOT catalogue exists (aot_catalog_hires ->
   build_aot\aoth_*; small gain).
   Launcher: build\tempest.exe (src/launcher.cpp), barebones; games run as separate processes. NEXT (owner agreed
   direction): a shared in-game settings overlay (T2K's Back menu as a module; T3K: high_res, blend, flip_cap, stick,
   controls, return to launcher), then the launcher's look and packaging all games together (local only).
   Also open for T3K: high scores / options not saved (flash EEPROM not persisted), aim stick mode, open webs.
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

## Lessons (2026-11-15)
- Vita3K core: system\vita3k\storage_location.txt names the folder the core last used; a core started with another
  system folder MOVES the data from there. A copied system\vita3k must not carry that file (package_collection.ps1
  leaves it out) - a test run from dist\ once moved build\system\vita3k away (reinstalled from vita_firmware\).
- Tempest 4000 (Steam 688140, 32-bit, DXGI): its start-up video-mode dialog (resource 130) always runs and its
  mode list keeps only the first 256 DXGI modes (no 4K @ 120 on a big monitor). src\t4k\xinput_proxy.cpp
  (tools\build_t4k_helper.bat -> build\t4k\xinput1_4.dll / xinput1_3.dll, next to Win10\ / Win7-8\
  Tempest4000.exe; Steam runs Win7-8 here) hooks DialogBoxParamW, picks the desktop mode, rewrites the chosen
  entry of the game's mode table (found by matching the whole list) to the desktop mode, presses Start Game during
  WM_INITDIALOG. Verified: 3840x2160 @ 120 full screen. Deck: WINEDLLOVERRIDES="xinput1_3,xinput1_4=n,b" %command%.

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
