# Llamasoft on the Jaguar - Tempest 2000 (then Defender 2000) on PC

Owner's project, started 2026-11-08, same toolchain and spirit as Llamasoft on the ST (C:\LlamatronRecomp; its
REMASTER.md is the recipe for the native renderer). Goal: the best version of each game - sharp high-res
graphics, an unlocked (interpolated) frame rate, Xbox-pad stick controls, QoL. All modes first-class
(the owner plays Tempest 2000 mode). Defender 2000 later ("kind of terrible": look for real improvements).
**Entirely offline**: local git only, no remote, no push, no publishing until the owner decides.

Plan and open items: PLAN.md. **Since 2026-11-13 this is a Tempest Collection** (owner): arcade Tempest (1981, MAME),
T2K (Jaguar), T3K (Nuon), Space Giraffe (Steam), TxK (Vita), Tempest 4000 (Steam) - all six run from the launcher;
PLAN.md "Tempest Collection" and "Collection status and open items".

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
- third_party/mame-libretro: libretro/mame (current MAME 0.289, commit 5f5a0774, shallow; our branch llamasoft-mame:
  the vector line export llama_mame_lines for t81's own beams, PLAN.md "t81 vectors drawn by us") for arcade Tempest (1981),
  built with only src/mame/atari/tempest.cpp by tools/build_mame.sh (Git Bash + the MinGW gcc 13.2 / GNU make in
  third_party/strawberry; SDL3 headers via -DSDLMAME_SDL3 for the libretro OSD's key table) ->
  tempest_libretro.dll (43 MB, only Windows DLLs). Romset: tempest.zip, a MERGED set (the clones in sub-folders);
  t81 offers rev 3 and Tempest Tubes (menu Version; PLAN.md item 3) - not in git.
- T3K disc: Nuance_067\Tempest 3000.nuz (2026-11-17, tools/t3k_compress.py from the .iso: the music in nuon.dat as
  Ogg Vorbis, the rest raw; core src/disc_image.cpp). t3k.exe / the play folder prefer it over the .iso. PLAN.md item 2.
  The launcher does the same (src/t3k_compress.cpp, libsndfile_x64.dll (LGPL, third_party/libsndfile, copied next to
  tempest.exe) loaded at run time): starting T3K with only an .iso asks to compress (~31 s headless, ~70 s in the
  window, one thread: a 4-thread first run coincided with a hard machine freeze 2026-11-17, probably unrelated), then
  to delete the .iso (warns when it has other hard links); "no" = never asked again (launcher settings
  t3k_offer_compress / t3k_offer_delete). Output equals the Python tool's except the Ogg serials + page CRCs (random).
  Tests: tempest.exe --compress-test=<iso> (T3KC_THREADS=N), and in a throwaway root (dummy t3k.exe, game\ = a hard
  link of the .iso) --select=2 --press-start --answers=<y/n/c...> --prompt-shots=<prefix>.
- src/lr_host.cpp: the shared libretro host for DLL cores (txk.exe: Vita3K, OpenGL on demand; t81.exe: MAME,
  software frames, mouse deltas for the spinner, save states). GET_VARIABLE answers the defaults the core declared
  (MAME takes a missing answer as an empty value). LR_LOG_DEBUG=1 shows the cores' debug log.
- src/host.cpp: libretro host (env callbacks, options, video XRGB8888, audio, input, save states).
- src/main.cpp: SDL3 window + headless test mode.
- Shared by the programs: src/menu.cpp (the settings menu, below), src/pixfont.cpp (the launcher's glowing 5x7 font and
  glow lines - the launcher and the menus), src/crt.cpp + src/crt_menu.cpp (CRT looks and their menu section),
  src/controls.cpp (remapping), src/settings.cpp, src/runlog.cpp (started with --from-launcher: stderr and the core's
  messages -> userdata\logs\<game>.log, the previous run kept as <game>.old.log).
- src/launcher.cpp: build\tempest.exe, the Tempest Collection launcher (starts t2k.exe / t3k.exe as processes,
  Steam link for T4K). PLAN.md "Tempest Collection".
- Launcher look (2026-11-17, owner: "beautiful"): src/launcher_view.cpp, ported from the local design mockup
  tools/launcher_mockup/index.html (open in a browser; keys L titles / C covers / M motion). Portrait 3:4 cover panels
  scrolled LEFT / RIGHT (UP / DOWN = Space Giraffe's flavour), each game's logo animated in its flavour, moving
  backgrounds (motion: SG > T3K > T2K > T4K > TxK > T81; T3K / SG = feedback buffers, emitters orbit OUTSIDE the panel
  or they hide behind it), system text in a glowing 5x7 pixel font, a warp before a start. Art: tools/launcher_art.py
  (art\ -> art\panels\<id>.png + <id>_logo.png; art\ is in .gitignore, never in git); the play folder gets art\
  (package_collection.ps1). T81 cover = the colourful poster (owner). --shot saves the 90th frame;
  LAUNCHER_FB_DEBUG=1 shows the raw feedback buffer.
- Launcher stings (src/launcher_sound.cpp, tools/launcher_sounds.py -> art\sounds\, play folder sounds\): each game's
  lane sound on selecting it, its death on starting it. T81 / T3K recorded headless (T3K music level poked to 0 at
  boot), T2K from the raw samples ('tink' = a flipper landing on a lane - its claw moves silently; 'screeeam' = gotu;
  signed 8-bit, ~16 kHz by cross-correlation with a recording, tools/t2k_samplerate.py), TxK / T4K asset WAVs, SG from
  GRsounds.xwb (73 unnamed sounds, MS ADPCM decoded by the tool; SG_MOVE 21 (beep) / SG_LAUNCH 34 (phone), owner: perfect; all 73
  in art\sounds\sg_bank\). tools/wav_events.py finds onsets in recordings.
- Launcher settings menu (2026-11-18; PLAN.md item 5): Back / Esc, the shared src/menu.cpp page built in launcher.cpp
  main (sting volume, motion, hide_missing, the T3K disc actions); B still quits. Tests: --menu[=all],
  --menu-action=compress|delete (+ --answers / --prompt-shots).
- Loudness (2026-11-18; PLAN.md item 5 step 2): src/volume.cpp in t2k / t3k / t81 / txk - every game at -20.44 LUFS (owner: -16 was hot, 60% = normal; master 0..150%)
  (measured gains: tools/loudness.py on logs/loud/*.wav), x the launcher's master_volume / even_loudness (read from
  userdata\launcher_settings.txt at start), x the game's own "volume" setting; volume::Put replaces
  SDL_PutAudioStreamData (soft limiter). Prints "volume: ..." at start. Re-measure a game: record with --wav, run
  tools/loudness.py --frames file.wav:F0-F1 --target -16, update kMeasured.
- MORE LLAMASOFT (2026-11-18, owner): the last panel (Kind::Promo, id "more", gold, rainbow-band background); A opens a
  list (lview::DrawMore): Jeff's Steam games by year (kMore in launcher.cpp: Gridrunner Revolution .. I, Robot), then
  our two collections (GitHub). A on a row: steam://store/<id> when Steam is there (else the https store page);
  GitHub pages in the browser (under Wine via steam://openurl/). Art: tools/more_art.py [--download] -> art\panels\
  more.png (cover: a wall of the wide pictures) + more_<key>.png (920 x 430; Steam headers, ours from C:\Gridios\
  Launcher\assets\steam and C:\LlamatronRecomp\res\steam, copied into art\more_src\). Test: --select=6 --more=N.
- Launcher Y = Steam (src/steam_shortcut.cpp): adds TempestCollection.exe to the most recent account's shortcuts.vdf
  (binary VDF, current Steam's spelling: appname / exe lower case, StartDir with a trailing \, sortas; Steam closed
  with steam.exe -shutdown and restarted; .bak kept) + grid art (<appid>p.png / .png / _hero / _logo from steam_art\ =
  tools/steam_art.py); when already in Steam (the Deck) only the art. Owner verified on Windows 2026-11-17 (in Steam
  with the art). Deck art path untested. Tests: --steam-button, LAUNCHER_STEAM_NOT_RUNNING=1 + a fake root
  (LAUNCHER_NO_REGISTRY + STEAM_COMPAT_CLIENT_INSTALL_PATH), tools/vdf_dump.py.

## Build / test
Play folder (all games, for the Deck): powershell -ExecutionPolicy Bypass -File tools\package_collection.ps1 [-SkipBuild]
-> dist\TempestCollection\ (TempestCollection.exe launcher, t2k / t3k / txk / t81 + cores + C++ runtime, system\ with the
installed Vita firmware, game\ = the four games hard-linked (T3K as the .nuz), userdata\ = the play folder's own, KEPT across
packaging (2026-11-18: only files it lacks come from build\userdata - those start fullscreen, TxK at 1x for the Deck); ~0.67 GB, never publish). The Nuance core is built WITHOUT AOT (tools\build_nuance.bat;
"aot" argument -> build_nuance_aot\ for experiments): the AOT modules are translations of the game's code.
Test build: powershell -ExecutionPolicy Bypass -File tools\package.ps1 -> dist\Tempest2000\ (Tempest2000.exe, the
owner's .jag, userdata\settings.txt fullscreen = 1, README with Steam Deck steps) + dist\Tempest2000-test.zip. dist\ is not in git (the ROM is in it: never publish). Icon: tools/make_icon.py (an original claw ship) -> res/t2k.ico
(exe, res/t2k.rc) + res/icon_rgba.h (window). userdata defaults to the exe's folder.
Performance (2026-11-09): T2K_PERF=1 logs emulate / compose / present per shown frame every 5 s (--novsync is ignored
by the compositor here). 4K fullscreen CRT++: 120 fps locked, emulate ~1.7 ms, compose < 0.7 ms. Headless in play:
1.2 ms an emulated frame (the remaster skips the GPU work: 3x faster than the original's 3.8 ms); run-ahead 2 =
3.6 ms a 60 Hz frame (pictures skipped for unseen frames via GET_AUDIO_VIDEO_ENABLE, state buffer reused).
`build.bat` (clang + Ninja in the VS 18 Community env) -> build\t2k.exe.
SMOKE TEST before packaging: python -I tools/smoke_test.py [--dist] [--only ...] [--no-windows] (~1 min): every game
headless to play / attract / title, the launcher, each menu in a window, the play folder README; a table + logs/smoke/sheet.png.
Headless T2K pictures need --set=remaster=0 (with the remaster on, the native drawing is not in the picture: HUD only).
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
- Menu: src/menu.cpp, SHARED by every game program: a game registers its page with menu::SetPage
  (src/<game>_menu.cpp; items show via `when` lambdas, game actions numbered from menu::kGameAction; menu::SetAccent =
  its launcher colour) and its controls functions with controls::Setup. Look (2026-11-17): the launcher's (pixfont).
  One layout in every game: Resume / GAME / CONTROLS / PICTURE / CRT / CHEATS / Return to launcher; folds inside a
  section = Page::Sub ("Fine-tune stick", "Fine-tune look", "Fine-tune CRT", "Buttons" = Page::Binds); a label's
  leading spaces indent it under the item above; Fullscreen shows On / Off. The launcher passes --from-launcher (last
  item "Return to launcher"). t3k.exe: the emulation thread parks (emu_pause / emu_parked) before the menu may change
  settings. Changes save at once via settings::Save (keeps the file's comments). Esc opens it on the keyboard.
  Tests: --menu / --menu=all (sections open), MENU_ALL_SUBS=1 (folds too), MENU_SELECT=N (selection N rows down).
  Owner's T2K top speed choice: lr_max_speed 0.41.

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

## Where we are (end of 2026-11-17, evening; start here next time)
Latest (2026-11-19): Defender 2000 work - a standalone build\d2k.exe ("Defender 2000.bat"), deliberately NOT in the
collection (owner: "it's not Tempest", for now). State and next steps: "Defender 2000 renderer" below and PLAN.md
"Defender 2000"; waiting on the owner's play test (arcade Thrust / Reverse, wide screen, smooth motion).
All committed; play folder current (dist\TempestCollection, ~0.67 GB; the owner plays it from Steam on Windows and on
the Steam Deck). THE TO-DO LIST: PLAN.md "Collection status and open items". NEXT: item 5 step 2 (master volume + normalised loudness);
step 1 (the launcher's settings menu shell + T3K disc again + launcher options) done 2026-11-18. Also done late 2026-11-17: the launcher's T3K disc
compression (item 4). Done 2026-11-17 from it: the menus
(restyle + one layout), T3K disc compression, T81 versions, T3K infinite lives. Left: the PUBLISH version (only when
the owner decides; its prep notes are under item 4: no AOT, the launcher's T3K compression, no owner saves), maybe a
launcher settings screen, maybe more T3K cheats (god mode, max level), Neon Mode much later. PLAN.md "Watch list": a
one-off TxK no-music + hang (logs exist now). Decided against / parked: PLAN.md.
Waiting on the owner's Deck checks: the new menus with the pad (Fullscreen Left / Right, the folds); a possible T3K
hitch at a level start (a track's first 32 s of music decodes on the emulation thread, 28-43 ms here); T81's wide
framing in play on Tempest Tubes.
2026-11-17 evening's work (details in PLAN.md items 1-6 and the sections here):
- T3K: the disc as .nuz (tools/t3k_compress.py, core src/disc_image.cpp; nuon.dat = 19 music tracks, 32 kHz); the core
  without AOT; stick modes Step / Stiff / Left-right (Momentum and Off removed); "Restart the game" (power-on snapshot
  in memory, t3kscores::Restart); infinite lives (cheat_inf_lives).
- T2K: Renderer Remaster / Emulation (setting native), melt sliders gone. T81: Version (rev 3 / Tempest Tubes, reloaded
  in place, ROMs checked by CRC first), CRT+ / CRT++ (GPU renderer). All: the menus, the run logs.
- Tools from the session: tools/iso_list.py, t3k_datmap.py, t3k_musicrate.py, wav_compare.py.

## T3K run-ahead + Step nudge fix (2026-11-18; PLAN.md item 8)
- Lag: d-pad -> picture 5 fields inside the game. Step mode had 2 more + lost / half nudges (stuck check counted from the
  first picture, per field): fixed; tools/t3k_steptest.py [--state --f0 --phase] checks nudges / three / wiggle / sweep.
  T3K_STEP_LOG=1 logs the model.
- Run-ahead: setting run_ahead 0-4 (menu CONTROLS, default Off); core nuance_ra_save / nuance_ra_load (fast snapshot,
  no code flush), host nuon::RaSave / RaLoad, window emulation thread only (headless runs never run ahead). Auto-lowered
  per session when over budget. Tests: T3K_RA_SIM=N[:F0] (headless cost + T3K_RA_SLOW=1 for the file state),
  T3K_RA_LOG=1 (window: shown picture per field). Mind: a headless shot after hidden fields shows the hidden picture.
  Only in play (web on screen, not a warp). Core state outside the save state must go in the snapshot (capture.cpp
  CaptureRaState); the rewind clears interpreter caches + MPE3's compiled code. TEST FROM BOOT, IN THE WINDOW, WITH THE
  GATE (--gate=2 headless): play-state-only tests missed the owner's flicker and boot crash (2026-11-18). Core switches:
  NUANCE_RA_FLUSH / NUANCE_RA_FULLMEM / NUANCE_RA_STATS. The boot is not exact across any save / load (play is).
- After building the core, copy build_nuance\nuance_libretro.dll to build\ (t3k's copy step only runs when t3k relinks).

## T3K stick: step mode, owner's tuning, game-over input (2026-11-17)
- stick_mode step (default; menu STICK): a push past sp_deadzone (0.20, 30% hysteresis) = exactly one lane (rapid
  nudges add up); held sp_repeat_delay it sweeps (from sp_ramp_start of sp_top_speed over sp_ramp_time); release
  after a sweep: sp_release off (owner's default: stops where seen, mo_err = 0 as the model trails the game) / back
  (last lane passed by the SEEN claw, a centre within sp_back_grace 0.15 counts as reached) / nearest / ahead. Reverse
  = release + a nudge. No lead past a nudge's end (it overshot). Headless (logs/t3k/step, from play_f2700): nudge
  7 -> 8, three -> 10, wiggle RLRRL 8 7 8 9 8; first move 4 fields after the push (the game's late read).
- Owner's tune = the defaults ("as good as we can get it for now"): sp_release off, sp_repeat_delay 0.10,
  sp_ramp_time 0.30, sp_ramp_start 0.30, sp_step_time 0.09; stiff keeps the owner's earlier tune. (Momentum and Off
  were removed 2026-11-17.)
- No input on the title after a game over (every stick mode): the claw object stays in the pool from the first level
  select on, so ClawControl took every later menu for play. Now "in play" = the newest picture draws the web surface
  (SpeedControl::web, both renderers); else the stick is the d-pad. Rejected markers: pool handler $800B3090 (drops
  mid-play), a $800DC100 rewrite probe (not rewritten each picture). A save state from play_f2700 goes black after its
  game over even with the stick off (state artifact; from boot it's fine).
- Still open there: T3K cheats (infinite lives in the menu since 2026-11-17; god mode not found; max level
  candidates untested - "T3K menu cleanup, save states"); interpolation for everything + run-ahead (not needed now).

## T3K background spin / death "kaleidoscope" (2026-11-16, owner's state logs/t3k/rot/owner_rot.state)
- CORRECTED 2026-11-17: the kaleidoscope in the WINDOW was ours: the zoom angle was read unsigned ($FF70 = 359.21
  deg), fine for a whole step (headless) but the window splits a step across presents (angle x step) -> ~90 deg a
  present. Now signed (t3k_native.cpp). Test spin in the WINDOW (--winshot), not only headless --nativeshot.
  States: logs/t3k/states/l3warp_f2860 / l3death_f3450 (level 3).
  Then the warp's four-pointed star (original: a soft rounded square): tools/t3k_zoomfit2.py (per-picture zoom fit
  from --span 00300004) showed the zoom step exact (our map + entry fade about 16/128, floored: err 0.27); the cause
  was sharp_vectors - the thin centre sparkle, amplified x1.23 a picture, grew spikes. Pictures with a brightening
  zoom (fade > 1: only the warps, 84 pictures each) now draw vectors the original way. A warp seen from a save state
  starts its feedback from black (no history): compare from logs/t3k/states/lsel_f2400 (up, up, a = level 3). Original note: The zoom feedback's angle (entry 00300004 w6, copied each
  picture by $8001F736 from the background preset's +$40 into $800B84BC) comes from per-level presets: three blocks
  at $8075FB30 / FC30 / FD30 ($100 apart), loaded from disc data (ISO 0x2C041CCC: "0000 A1C0 0280 ... FF70 4C5F ...").
  "Crosseyed and Painless" has rotation $FF70 = -0.79 deg a picture; level 1 has 0 in play. A death's feedback keeps
  88% of each picture, so with the spin the trails become the kaleidoscope. T3K_ZOOM_LOG=1 prints every zoom entry.
- Level select with right presses 24 fields apart only reached levels 1, 7 (V Sign) and 15 (Dark Side of the Moo):
  the claw fix above was measured on those three.

## T3K claw on every level (2026-11-16)
- Owner: controls differed by level (close on some, slow / stiff on others). Cause: the claw is an object in the pool
  $8003C000..$80040000 and its slot depends on the level ($8003E9E0 on level 1-2, $8003EA50 on others); ClawControl
  only looked at $8003E9E0, so elsewhere the stick fell back to the plain d-pad. Now ClawControl::FindClaw finds the
  slot (flags +12 = $80000401, position +0 = $800DC100) and re-scans when it moves. --clawlog prints the slot too.
- Measured (tools/t3k_clawlevels.py; aligned pushes, levels 1 / 7 / 15): the same path until the web's end.
  The game moves ~1.69x the driver's GameSpeed per input on every level (the position feedback absorbs it).

## T3K menu cleanup, save states (2026-11-16)
- Menu DISPLAY: Renderer = native_view (1 Remaster, default now; 0 Emulation); Remaster options (wide screens, smooth
  motion = native_interpolate, vector lines) and Emulation's picture blend show only with their renderer. high_res
  setting / --hires removed (the core keeps its code). F7 still cycles the test views 2 / 3 (they name themselves).
- On screen: nothing while every picture is native; an orange "New effect - shown emulated (type X)" note for 3 s
  after an emulated picture in Remaster.
- Save states: menu GAME Save slot 1-5 -> userdata\states\t3k_slot<n>.state (~62 MB); done by the emulation thread
  between fields (also while parked for the menu). Test: --statetest=N:save|load (window, at present N).
- Core fix: retro_unserialize re-points the pad block (ControllerInitialize only runs at boot), so a session started
  from a state takes input now (the old "--press after --loadstate does nothing" bug).
- Cheats (paused by the owner): lives = $800BAC7C (a register spill of the game's context save at $80010200; holding
  it at 3 keeps the game going through deaths). No "caught" flag found in sys / MPE3 memory yet. Max level (15 at
  the menu) candidates: 800B2F4C 80139048 801B72AC 8024670C (untested).

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

## TxK buttons and prompts (2026-11-17)
- Owner's layout (T4K-like): A Cross, B Circle, X Square, Y Triangle, LB = Select (title options, level-select mode,
  pause quit), LT = L (hold + wiggle the claw), RT = R (jump), RB free, Back = our menu. txk_menu.cpp defaults (no
  bind_ lines in the owner's settings, so the defaults apply).
- The game's prompts are plain strings in eboot.bin's data (loaded as is around 0x81075000..0x810BB000); txk_main.cpp
  PatchPrompts rewrites them in guest memory through the core's new llama_vita_write (third_party/vita3k-libretro,
  branch llamasoft-vita): SELECT -> LB (3 exact strings), level select's '&' (title font's Cross) -> A, and in every
  text containing "button" the menu-font symbols 0x84 Cross A, 0x85 Circle B, 0x82 Triangle Y, 0x83 Square X,
  0x88 / 0x89 L / R -> LT / RT (a ~9xxx colour code dropped where the text would grow); d-pad arrows 0x80 0x81 0x86
  0x87 stay. Every 120 frames until done, again after a restart. Verified headless: title, options, level select.
  Mind: compare guest bytes as uint8_t (signed char missed every symbol string at first).
- Touchscreen (Supertapper = tap; the Vita3K core already reads RETRO_DEVICE_POINTER as the front touchscreen): host
  lr::SetTouch answers one pointer; txk.exe sends the left mouse button mapped onto the drawn picture (touches arrive
  as mouse: SDL's touch-to-mouse, and Proton on the Deck), not while our menu is open. Headless --touch=F:x,y[:len]
  (0..1 on the picture). Verified: a tap on HIGH SCORES in TxK Options opens it. Deck touch untested.

## Space Giraffe (Steam 27800) in the launcher (2026-11-17)
- Launcher entry SPACE GIRAFFE, LEFT / RIGHT = flavour (launcher setting sg_flavour, acid default). Start: finds the
  game folder (HKCU Steam SteamPath + libraryfolders.vdf + appmanifest_27800.acf), copies the helper d3d9.dll (from
  <launcher>\Space Giraffe helper\ or build\sg\) next to SpaceGiraffePC.exe when different, rewrites the flavour line
  of sg_helper.txt there, then steam://rungameid/27800. Owner checked: speed normal at 120 fps, picture fills, menus ok.
- Helper src/sg/d3d9_proxy.cpp (tools/build_sg_helper.bat -> build\sg\d3d9.dll, 32-bit). The exe is SteamStub-wrapped
  (.bind): runtime hooks only. Dialogs via DialogBoxParamA IAT: 131 language -> English (1008), 135 flavour -> the
  button whose text has "acid" (1014 "Acid-Mix") or not (1013 "Space Giraffe NUXX"), by a POSTED BM_CLICK (calling the
  game's dialog proc directly in WM_INITDIALOG broke its start-up). D3D9: CreateDevice / Reset (vtable 16 / 16) ->
  windowed, backbuffer = monitor, WS_POPUP over the monitor, interval ONE; Present (17) counted (log every 5 s) + an
  optional max_fps cap (windowed D3D9 refuses INTERVAL_TWO). DirectInput8: the game polls GetDeviceState, range
  -1000..1000, sets NO deadzone -> menus jumped; helper sets DIPROP_DEADZONE 20% after SetDataFormat.
- MUST forward every named d3d9 export (jump stubs): D3DX looks up Direct3DShaderValidatorCreate9 at run time; without
  it: "Impossible to initialise the Neon engine" (message box hidden behind the game window).
- Game data: settings / progress in %APPDATA%\SpaceGiraffe\userprofile*.dat (binary; config.txt never written);
  SGPCLogFile.txt next to the exe. The owner's in-game resolution is 4096x2160 fullscreen (made borderless 3840).
- Tests: tools/sg_test.ps1 -Name x -Settings "k = v; k = v" [-Keep] (installs, starts via Steam, DPI-aware screenshot,
  logs); tools/pe_dialogs.py (dialog resources); tools/dinput_probe.cpp (DirectInput pads + axes). An early erratic
  stick episode was not reproduced (not the helper: it happened without it too; pad reads clean).
- Deck: launch option WINEDLLOVERRIDES="d3d9=n,b" %command% (T4K: "xinput1_3,xinput1_4=n,b"), set once by hand.
  Launcher Steam lookup (2026-11-17): roots = STEAM_COMPAT_CLIENT_INSTALL_PATH (Steam gives it to every Proton game,
  Linux path -> Z:\...) + Windows registry SteamPath; libraryfolders.vdf paths mapped the same way; "installed" =
  appmanifest found (no more registry Apps key). Copies both helpers (T4K's xinput DLLs into Win10 + Win7-8) on every
  start. Under Wine (ntdll wine_get_version) no process wait (other prefix). Tested on Windows with
  LAUNCHER_NO_REGISTRY=1 STEAM_COMPAT_CLIENT_INSTALL_PATH=/Program Files (x86)/Steam LAUNCHER_LINUX_DRIVE=C: (--shot).
  Owner tested on the Deck: works (steam:// reaches Steam).
- Launcher on the Deck, round 2 (2026-11-17): list in date order (SG between T3K and TxK). Steam entries show what to
  do when selected: not installed (A opens steam://store/<id>), helper missing, or under Wine a missing launch option
  (read from Steam's userdata\*\config\localconfig.vdf apps\<id>\LaunchOptions - read only: Steam rewrites that
  file while running, so the launcher can't set it; VdfValue unescapes \" for it). "setup needed" still starts.
  Return focus: under Wine the launcher watches Linux's process list (Z:\proc\<pid>\cmdline names the exe); when the
  game is gone (2 checks, 1 s apart) it raises itself and takes the pad back (SDL background joystick events, only
  when no watched game may run). Never seen in 90 s -> not watched (tap needed, as before). Tests on Windows:
  LAUNCHER_FAKE_WINE=1, LAUNCHER_PROC_DIR=<fake proc dir>, LAUNCHER_DRY_URL=1, --select=N with --shot.
  NOT yet tested on the Deck: whether Z:\proc shows other Proton containers' processes.
- Front: the launcher calls AllowSetForegroundWindow(ASFW_ANY) before steam://, waits for the program (SteamExe:
  SpaceGiraffePC.exe / Tempest4000.exe) as its `child`, and pushes the game's window while the launcher itself is
  still in front (first 30 s). Measured: the helper's own SetForegroundWindow is refused; the launcher's push works.
- PARKED (owner, 2026-11-17): "hi-res objects" is never saved - a game bug (also on the Deck). The profile is a memory
  copy of $005091DC..$0050A0D8 and is byte-identical after toggling it. tools/sg_memwatch.py (snapshots of .data
  while the owner toggled) found three menu entries $004D9B3C / $004D9C64 / $004D9CF8 (value 1 off / 2 on, then 2, 1),
  but setting them to 2 at start (they stayed 2) still showed and played OFF: the real switch is elsewhere, probably
  heap -> next step would be a heap scan + a pointer from .data, or hooking the code that reads it.

## T3K options (2026-11-17, found, NOT kept - owner: not needed for now)
- Options word $801133A8: bit $10 = backgrounds on (no visible change in play even via the game's own menu), bit $02 =
  "nomen-clo-matic" level names; other bits are game state (in play it reads $14). FX level $8011344C (default 9),
  music level $80113450 (default 14), u32. Menu code: MPE3 $80020CB8.. (eor $10 / eor 2, then rewrites the menu text).
- Volumes reach the sound engine only by a call: start-up code $800206E6 calls $80022184(music << 25, fx << 25) once
  (between boot fields 10 and 34; the pause sliders call it too). Defaults are in memory from field 2, so a keeper
  would write the saved values at field 2 (untested). MediaOpen(2, "NUON.DAT") at boot is the disc's data file
  (device 2 = DVD), not a save. Tools: --memdump also writes _mpe1 / _mpe2; tools/t3k_optsearch.py (label search
  over dumps), tools/t3k_state3.py (A/B/A state compare); NUANCE_WATCH=<addr> + NUANCE_NOJIT=1 NUANCE_NOAOT=1 found
  the menu code.

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
  owner likes it (PLAN.md "Claw feel study" Results). Momentum (TxK feel) and Off were removed 2026-11-17 (owner): Step, Stiff, Left/right remain.
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

## T2K leftovers (from the 2026-11-09 list; the rest is done - REMASTER.md steps 7-15)
4. Unseen in tests: "Excellent" / one-up graphics, the AI droid's zap, Beastly mode (cheat_beastly unlocks it now).
5. QoL: remember the window size / position; menu items for run_ahead (0-4) and hud_corners.
6. Performance: 4K with CRT++; a low-end GPU (Steam Deck class) if that's a target.
7. Open questions: the psycho bonus sphere invisible on the original (left to the GPU); the owner's "offset ghost
   image of the UI" (not reproduced; ceiling bonus mfudj merge 8 lines low is a candidate).
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
  WM_INITDIALOG. Verified: 3840x2160 @ 120 full screen. Deck: WINEDLLOVERRIDES="xinput1_3,xinput1_4=n,b" bash -c
  'exec "${@/Win10/Win7-8}"' -- %command% (2026-11-17: no music on the Deck - Proton = "Windows 10" launch entry, whose
  music is xWMA only (Win10\Assets\Sounds\Tracks_xwma); Win7-8 has Tracks_adpcm WAVs; the launcher checks for "Win7-8").
  Owner: still no music with Win7-8 alone - that build plays Tracks_xwma whenever the folder exists (ProtonDB:
  protondb.com/app/688140). Now PrepareT4K under Wine renames Win7-8\Assets\Sounds\Tracks_xwma -> Tracks_xwma.proton-off
  (then it plays Tracks_adpcm). Owner verified on the Deck 2026-11-17: music plays ("worked flawlessly").

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

## Defender 2000 renderer (2026-11-18; PLAN.md "Defender 2000")
- Standalone d2k.exe (2026-11-19, owner: not in the collection for now): the build copies t2k.exe to build\d2k.exe;
  main.cpp g_defender = the exe's name has "d2k" / "defender" (FindRom then looks for "Defender") or the cartridge is
  Defender 2000 (d2k::IsDefender, by file name). Then: userdata\d2k_settings.txt, defender2000.eeprom,
  states\d2k_slotN.state, log d2k, volume key d2k (-24.05 LUFS measured), window title, src/d2k_menu.cpp page (GAME "Restart the game" =
  host::Reset, the core's retro_reset: the console's reset, EEPROM kept; headless --reset=F)
  (volume, save states, controls, picture: renderer / smooth motion / wide / pixels, CRT), ReadPadD2K; the T2K-only
  keys and hooks (F2, camera, pause line, quit to title) are off. "Defender 2000.bat" starts build\d2k.exe.
- Controls (manual + tests): d-pad left / right = the game's turn-and-thrust (push the way you face to thrust, the
  other way turns and thrusts), up / down move; Jaguar B fire, C smart bomb, A lightning laser (Plus only; nothing in
  2000 mode). No hyperspace button in any mode. The game's "Six Buttons" joypad option is Classic's Pro Controller
  arcade layout (A fire, B thrust, C bomb, left shoulder reverse); not used: our arcade buttons work in every mode.
  Arcade buttons (d2k::Arcade): the ship's facing is the word at $190630 (+1 / -1, every mode, set from boot on);
  Thrust holds the d-pad that way, Reverse holds the other way until the word flips (<= 6 frames; 2 always did it,
  the ship keeps most of its speed). Defaults: A fire, B / RB bomb, X laser, RT thrust, LB / LT reverse, Y Option,
  Start pause; setting d2k_stick_lr off = the stick moves up / down only (the d-pad still turns, for the game's
  menus). Bindings apply only in play (2026-11-21, owner's layout: RT fire, A thrust, X reverse, LB laser, B / RB bomb,
  Y option, LT reverse, Start pause): d2k::InPlay = longs $194E10 / $194E14 == $00140000 / $17000000 (a record that
  exists from a game's start to the menus after it, deaths / wave-complete / game over included, every mode + the
  attract demo; 0 on title and menus; RAM search logs/d2k/inplay/search3.py over 2000 / Classic / Plus / idle dumps,
  D2K_RAMDUMPS=F0:F1:step:prefix, D2K_INPLAY_LOG=1). The first marker ($18CD98 == $00760084) was a text position: Plus
  play changes it with its messages, so the owner's buttons fell back to the menu layout after a death. Outside play, fixed: A / Start = Jaguar B (select), B = Jaguar C (back: level
  select -> mode menu), Y = Option, d-pad + stick move. The pause keeps InPlay on. Keyboard: Z fire, C bomb,
  X laser, Space thrust, Shift reverse. Headless: --press=F:thrust / reverse.
- No source exists. Survey: t2k --survey=F0:F1:file (src/jagsurvey.cpp, core hooks vjHostGpuGo / vjHostBlit /
  vjHostOpObject). The picture is all OP bitmaps (CRY, VMODE $06C1, BGEN, per-line BG = the sky gradient set from GPU
  objects); the only RMW object is the scanner bar. Game updates at 30 Hz (every 2nd frame) in play.
- Capture: each bitmap row decoded when the OP reads it (the OP's own written-back DATA / HEIGHT / REMAINDER, live
  CLUT) - the game redraws the stars / scanner-dot buffer during the frame. Draw order = topological sort of the
  per-line walk orders. Line buffer -> core picture offset x 16, row 16 (D2K_CALIBRATE=1 re-measures).
- Oracle: --d2kcheck=F:prefix (software render vs the core's picture; writes images only on a mismatch): 439 / 439 and
  814 / 815 frames exact over a 6000-frame play sweep (the one: a 2x2 near-black speck).
- Window: d2k::Draw = atlas (edge-copy padding) + quads, RMW as add / reverse-subtract deltas, positions blended between
  updates (one update behind). Motion: same image (clipping by DATA / width recognised; same size + moved DATA =
  another animation frame, not motion), else predicted (still / own last motion / a layer's) within 10 px, else a
  layer sibling's. Lead-ins + ghosts fill the left edge while blending. Settings d2k_native (F8), d2k_interpolate (F7),
  d2k_widescreen (F6), d2k_filter sharp (SDL PIXELART) / smooth / nearest (no D2K menu page yet). Tests: D2K_FORCE_A=<0..1> (window), D2K_MOTION_LOG=1,
  D2K_WHO=x,y (with --d2kcheck). Cost ~0.4 ms a present at 1920x1440.
- Playfield layer (2026-11-19): the 8-bit 384x238 bitmap is cleared (blitter) and redrawn every update: sky stars
  (GPU tables, several parallax speeds), scanner blips, lasers (blitter rows built in a scratch row at $18E166..),
  explosion particles. Window: cut into items per update (8-connected pixel groups; a row of dashes = one beam) and
  matched to the update before's: (1) bursts - an explosion's pieces as one group moved by its centroid and scaled
  about it (one scale fits every ring: same start point and time; particles change colour as they age, so grouped by
  outline), its unpaired pieces moved by the model; (2) shape near a prediction (still / own last motion / this
  update's common displacements / the sprite layers'); (3) mutual nearest; (4) beams by row, ends blended. New items
  fade in, vanished ones fade out over the first half of an update. D2K_ITEM_LOG=1 prints per update; D2K_LAYER_DUMP=
  <prefix> writes the layer's bytes per update. Known: a burst leaving the layer's left edge (x < 0) breaks up.
- Matching fixes (2026-11-21, owner: Classic / Plus enemies jumped briefly): SameImage picked the smaller of its two
  readings of a DATA move and THEN rejected it (identical DATA on a 16 px lander moving > 8 px read as "a line down"),
  so landers took a neighbour's place; now invalid readings drop out first. Step 1 is global best-pairs-first, scored
  distance + half the miss from the sprite's own last motion (landers fly in tight pairs at 9 px a frame); a
  same-image match yields to a same-size sprite > 10 px nearer (an animation frame flip); step 2 needs the same
  reflect flag. D2K_MOTION_LOG shows s<step>#<prev index> per sprite. Classic route: matches implying >= 12 px a
  frame 41 -> 10 (rest: world wrap at x ~2040, landers appearing); Plus unmatched 516 -> 179, 2000 505 -> 220.
  Step 4 (same day): a row of strips (same row / height / depth, 32+ px wide) takes the motion most of its same-image
  (step 1) matches agree on, over members matched by steps 2 / 3. 2000's foreground rocks (64x22 at row 232) scroll
  ~+58 an update while thrusting (measured from the pictures: logs/d2k/hum/rowshift.py; the pattern repeats ~64 px, so
  0 / -6 fit nearly as well - don't trust a single shift fit); step 2 had given the left strips another layer's -6.
  Rows split between motions on the routes: 0 (was a few a second while thrusting).
- Scanner (2026-11-21, owner: radar jittery in all modes): the game steps blips a whole scanner pixel every few updates
  (and blinks enemy blips red / yellow <-> green every update - the game's own). 2000 / Plus: the blips are playfield
  layer items; SmoothRadar draws items in line-buffer rows 26..61 at the average of their last 4 positions along the
  match chain, blended between averages (1 px per 4 updates -> 0.25 px each update, ~2 updates late; smooth motion
  only). Classic: NOT done - its radar (and landscape line) is inside the 2-bpp playfield buffer, which is drawn whole
  (IsLayer needs depth 3) and updates at 60 Hz: it steps like the original (owner: looks good, leave it).
  Blip pairing (2026-11-21, owner: 2000's bottom-row blips "move quickly or swap positions"): the humanoid blips (2x2)
  sit in row 54 ~15-40 px apart and a star layer's displacement (a prediction) paired them with a neighbour (205 jumps
  of 16-43 px from the owner's level-2 state). Now blips (3+ px in the scanner rows, BlipOnly) pair only within 4 px
  across / 2 down (BlipMove), stay out of burst grouping, compare by outline (they blink colour), ties go to the
  shorter move; SmoothRadar averages only blip-sized moves (stars crossing the scanner rows are drawn as stars).
  Row-54 jumps 205 -> 3 (4 px steps).
- Plus sky (2026-11-21, owner: aurora cut at 4:3, starfield "swapping pixels"): the aurora is a mirrored pair of
  SCALED strips (type 1, 48 px source drawn 168 wide, row 178) - the wide pads now take type 0 and 1. The sky has
  ~370 items (three star layers at ~1-2 / 3-4 / 6-7 px a frame, ~100 stars each, colour by row and changing): in a
  dense layer (> 150 items of 1-2 px) single pixels only take a motion >= 20 position-only pairs share (h1 in
  MatchItems; colour-blind) and skip the mutual-nearest step; with g_period 1 new items show at once and vanished
  ones go (no half-frame fade flashes). Plus frame 2400 state: new / vanished per frame ~30 -> ~10, odd motions
  (-38..+48) gone. 2000 mode: only big explosions differ (~1 item an update).
- Plus hills past the layer's edges (2026-11-21, owner: "presentation is good"): the hills are one playfield-layer
  item as wide as the 384 px layer, which ends ~20 px short of a 16:9 window's edges. EdgeHistory measures their scroll
  each update by the OUTLINE only (the texture is an animated surface: no shift, palette or 2-D period matches it
  between frames) and keeps a history kEdgeM = 96 px wider each side, drawn past the trailing edge (the leading edge
  has never been seen); columns out of the layer > 3 updates take live colours mirrored about the edge at the same
  depth below the outline. Stars leaving across an edge carry on. The ground blends with the measured scroll (vy 0).
  Rejects fills (2000's full-screen flashes) and ambiguous shifts; a failed measurement (~1%: explosions over the hills,
  a jump every ~100 frames that no shift fits) restarts the history (carrying on with the last scroll was worse).
  D2K_EDGE_LOG=1. Wide only (g_wide_on).
- Plus enemies past 16:9 edges (2026-11-21, owner: they popped in short of the edges): Plus's (arcade-logic) objects
  are NOT the 2000 sprite overlay's: 64 slots x $40 at $190884 (x / y words +2 / +4, flags +6), turned into display
  objects by the GPU overlay copied from $0AF988 ($1AF6xx / $C0xx -> the resident copier $F030B6 -> list $8000). It
  kept x -64..383 (($7FF-masked x + $40) < $1C0) and set flags bit 8 "on screen" (read by other overlays, e.g. the
  smart bomb). d2k.cpp kWideArcade (own patch group, wide only) makes it -128..511: enemies now first appear at
  x -144..-128 / 480..512 (were -16..0 / 368..384); Plus check sweep 169 / 169 exact. Classic draws its enemies another
  way (unchanged). Tools: logs/d2k/pluswide/ (slots.py, dis68k.py 68K via capstone, track*.py), D2K_GPULOG + the
  survey's overlay blits (a1=F031C0, a2 = source) to find which overlay wrote an address.
- Level-end slowdown (2026-11-21, owner's state logs/d2k/hang/owner_l2end.state, end of 2000 level 2): the screen
  fills with ~6500 playfield particles; MatchItems' burst fit / nearest searches are quadratic (BurstFit ~95 scales x
  particles^2): ~2 s an update, the window stopped responding. Over 2000 items (play: a few hundred) an update is not
  matched (drawn as the game puts it). That state: 14 -> 170 fps headless.
- Wide per mode (2026-11-21, owner: Classic and Plus at 4:3, 2000 wide): d2k_widescreen 0 off / 1 all modes / 2
  Defender 2000 only (default; menu PICTURE "Wide screen", F6 cycles 2 -> 1 -> 0). The mode played = byte $18CD4B (0
  Classic, 1 Plus, 2 Defender 2000, 255 on menus; logs/d2k/inplay/modesearch.py); wide is off while InPlay() in
  Classic / Plus (menus and the attract demo stay wide). The owner's settings file was set to 2.
- Wide screens (2026-11-19; setting d2k_widescreen, F6): the object list is built by GPU overlays the 68K
  copies into $F031C0 from RAM - layers $0B2B50 (records -> 128/256 px strips, cull 0..383, left clip by DATA),
  sprites $0A5938 (world x - camera & 4095 - 255, kept while -w < x < 384 + w, left clip). d2k.cpp kWide patches their
  RAM sources (only over the original bytes): no left clips (the OP draws negative XPOS), strips up to 300 left,
  sprites -96..512. Our picture then fills the window around the 4:3 dst (Draw's view); a layer's end strips continue
  in mirrored copies where its record runs short (21:9). HUD stays in the 4:3 place; the playfield layer (stars,
  shots) only covers line-buffer 0..383. The 4:3 core picture is unchanged (sweep 94 / 94 exact with the patches).
  Side effect: the sprite overlay steps animations only for kept objects, now a wider window (no game logic seen).
- Art dump (2026-11-19, for an offline-upscaled replacement pack: the owner's choice): D2K_ART_DUMP=<dir> writes the
  source rectangle of every object drawn (DATA, DWIDTH, first pixel, w x h; stored values + RGBA) once per content to
  <dir>/rects.bin + index.txt; places whose content keeps changing (any shared pixel differs, 6 times in 300 frames)
  are drawing buffers (buffers.txt: Classic playfield, radar, HUD, menu text). tools/d2k_art_sheet.py <dirs> --out X
  joins rectangles overlapping in memory with the same values (backdrop strips -> whole panoramas; animation frames
  stay apart) -> X_pics/<n>.png + contact sheets X_N / X_big_N / X_buffers_N.png + X.txt. Routes:
  tools/d2k_art_route.py <m2000|classic|plus> (16000 frames). First census (attract + 3 modes): 434
  pictures, ~0.95 Mpx (25 big: title pictures, logos, backdrops); 2000 / Plus art is pre-rendered CGI (16-bit),
  Classic is tiny arcade pixel art; 28 seen in several colourings.
- Wide edges (2026-11-19 later; owner: "pop in at the edges", ground short of the border): each backdrop band is a
  window of N strips over a tile map, first strip at x in (-tile, 0], so its right end jumped between 384 and 512 and
  new strips appeared inside a 16:9 view (mirrored pads filled the gap until then). The layer builder's first part
  (per layer descriptor: speed long, count, mask, shift, map mask bytes, map long; record + 24 = strip ids, record
  + 10 = the second part's own count, bit 15 a flag) is patched (kWide 88-byte block at F03218: one tile further
  left; a mask-0 layer stays) and LayerCounts raises both counts by 2 (7 at most). The level's layer list is the long
  at $B811C + 16 * word $B822E (the 68K at $A4EEE puts it in $B8118 just before the builder runs; $B8118 holds the
  sprite list the rest of the frame - never patch through it). Tile table: {DATA, height, id} entries.
  The ground (8-bit 384 x 20 buffer at row 234, redrawn each update: rows of colour ramps, each periodic, 32 + 4 *
  row px) carries on 160 px past both ends, each row repeated at its period (Sprite lead / tail columns). IsLayer
  (the playfield layer) now needs h >= 200. 4:3 checks unchanged with the patches (same results on / off).
- Ridges: tools/d2k_ridges.py (run by d2k_art_pack.py) smooths stair-stepped backdrop outlines in the 4x pack: lines
  through run middles where neighbouring runs differ by <= 3 px (bigger jumps stay vertical: cacti, rocks), ends
  pinned (tiles meet), --ridge-detail 0.5 px rocky roughness, anti-aliased edge (the loader premultiplies real alpha).
  Chosen pictures: >= 128 wide, bottom row solid, >= 12 steps, big jumps <= steps / 4 (desert mountains / dunes; city
  skylines and logos untouched).
- FIXED 2026-11-21 (was the "Plus nebula / radar line gap"; owner: a big black square over Classic's playfield):
  TRANS makes a palettised pixel see-through when its own BITS are 0 (core op.c), not its CLUT entry; Classic / Plus
  2-bpp buffers have a non-zero black at index 0. --d2kcheck sweeps (route, 1800-6000 every 25): Classic, Plus, 2000
  all 169 / 169 exact.
- High-res art (2026-11-19; setting d2k_hires default 1, F4, menu PICTURE "High-res art"): pack build\d2k_art (never
  in git / published: made from the cartridge) = pack.txt (each picture's memory place: depth, DWIDTH, OP pitch, row /
  column = pixel address // and % the line pitch in pixels) + src\<n>.png (1x original) + hi\<n>.png (4x). Built by
  tools/d2k_art_pack.py <census>_pics (Real-ESRGAN realesrgan-x4plus via tools/d2k_upscale.py: colours spread into
  see-through pixels, mask upscaled as grey and cut at 50%, 8 px border; ~15 s for 388 pictures on the GPU; the
  anime model posterises). Real-ESRGAN ncnn-vulkan 20220424 in third_party\realesrgan (owner approved the download).
  Left out: drawing buffers, Classic-only pictures (arcade pixel art, kept blocky) and tiny pictures (both sides <= 24,
  --small: 128 of 388; 2026-11-21, owner: humanoids strobed - the upscaler redraws 1-2 px faces / limbs differently in
  each animation frame). 260 pictures now. d2k.cpp HiFor: a sprite whose
  memory rectangle lies in a picture and whose pixels equal the original there (1% slack) is drawn from the 4x texture
  (linear); scaled objects (the title is one, HSCALE 33) map drawn -> source by 32 / HSCALE, 32 / VSCALE and are
  checked from memory (every 3rd pixel); lead-ins, ghosts and wide-screen mirrored strips use it too; anything else
  (colour changes, unknown art) draws as before. Coverage from play states: 2000 / Plus every art sprite; left: the
  playfield layer, 320x10 message text, a scaled buffer (1CB000 / 1E5400 180x238). Cost ~0.07 ms a present at 1080p.
  Test: D2K_HI_LOG=1 (window: matched / not per update, every 60 frames). Window tests from boot drift from headless
  timing: use a state (logs/d2k/hires/title500.state = the title).
- City buildings jumping at the wide edges (2026-11-21, owner: level 6). Two causes: (1) LayerCounts capped a layer
  at 7 strips, so the 7-strip city (64 px tiles) got the tile-left shift and no extra strips: its right end fell short
  of even the 4:3 edge (buildings popped in inside 4:3, in the game's own picture). Records are 32 bytes with the
  descriptor right after, so the ids at +24 have room for 8: cap 8 there (core picture wide on == off again in the
  background). (2) A 16:9 view still needs up to ~45 px more on one side or the other: ExtendLayers adds the next real
  tiles from the level's map (builder part 1: pos = camera * speed >> 6, strip i = map[(pos >> shift) + i & mapmask],
  signed id, 0 empty, < 0 reflected; tile table at record +16, 8 bytes {DATA, height, palette word}; tile step record +8)
  as scene strips, after checking the shown strips / gaps fit one map offset and decode identically; pads now only from a
  row's outermost strips. D2K_NO_EXTEND=1 turns it off. Tools: logs/d2k/city/ (layers.py RAM inspector, winscroll.py
  edge-vs-centre shift per present, odd.py motion-log outliers); t2k/d2k --winshots=F0:F1:prefix (every present).
- Black screen / GPU runaway in 2000 mode (2026-11-21, owner's level-6 state logs/d2k/black/owner_black.state, already
  dead): a CORE bug, not ours (crashed with wide / renderer / idle skip off, any timing model). The core's late load
  write-back (issue #811, gpu.c gpu_ldx_slot) also fired for a load in the delay slot of a TAKEN jump: the object task
  $A61B0 ends a handler `jump eq,(r7) / load (r14+56),r0` into `movei #$F03296,r0 / jump ne,(r0)`, so r0 kept the
  loaded data and the GPU ran into main RAM. Now armed only in program order (gpu.c + dsp.c). Repro harness:
  logs/d2k/black/bot.py (random play from the owner's slot-1 state, 5000-frame segments chained by save states,
  CRASH-DETECT lines) + replay.py; before: 4 of 14 seeds crashed, after: 0 of 20 (400k frames). D2K_RAMDUMP(S) also
  write <file>.gpu (GPU RAM + pc + both register banks). GPU task protocol: code copied (blitter) to $F031C0, long
  $F03FFC = go (resident loop at $F03072 polls), the task clears it; the 68K waits ($A423A + 18 inline tst.l).
- Survey tools: D2K_GPULOG=F0:F1:file (GPU loads / stores outside its RAM + blits; core hook vjHostGpuMem),
  D2K_M68KLOG=F0:F1:lo:hi:file (writes into a range by the 68K with its pc, or by another unit as $FF0000xx | who;
  core hook vjHostM68kWrite), D2K_RAMDUMP=F:file (2 MB), D2K_WIDESHOT=F:margin:file (software picture, wider),
  tools/jrisc_dis.py (GPU disassembler: python -I tools/jrisc_dis.py ram.bin <src> <len> <run address>), capstone
  (installed) for 68K.
