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
