# Tempest 2000 PC: plan (draft, 2026-11-08)

Goal: play the original Tempest 2000 (Jaguar, Llamasoft 1994) on PC with sharp high-res graphics, an unlocked
frame rate, good pad and mouse controls, and quality-of-life features. Same approach as Llamasoft on the ST: the
original code runs unchanged in an emulator, and we add hooks around it.

## Tempest Collection (owner's plan 2026-11-13)
The project is now a Tempest collection: one launcher, each game the best version of itself.
1. **Tempest 2000** (Jaguar, 1994): done in this repo (remaster renderer, smooth motion, stick controls).
2. **Tempest 3000** (Nuon, 2000): next, from 2026-11-14 (plan below).
3. **TxK** (PS Vita, 2014): after T3K (plan below).
4. **Tempest 4000** (2018): not emulated - the owner buys it on Steam; the launcher starts it through Steam
   (a steam://rungameid/<appid> link; look up the app ID, and grey the entry out when Steam / the game isn't
   installed).
5. **Arcade Tempest** (Atari, 1981), maybe and fun to have: the original that the others build on. The owner
   supplies the ROM set (never in git, never published). It is a vector game, so it suits native rendering: draw
   the Atari vector generator's lines ourselves at any resolution and frame rate, with a vector-monitor glow
   look. Options: an existing core (MAME / MAME 2003-Plus libretro, which emulate Tempest) or our own small
   emulator (6502, vector generator, mathbox, 2 POKEYs, spinner), which is easier to make deterministic and to
   hook. Decide after a short survey; spinner -> stick / mouse controls as on T2K.
Launcher: one exe with a game picker (or a front exe that starts each game); shared settings, controls and
menu look. Neon Mode and Defender 2000 stay on the list after these.

## What we have
- ROM: `Tempest 2000 (1994).jag`, 2 MB, MD5 602bc9953d3737b1ba52b2a0d9932f7c.
- **Source code that rebuilds this ROM byte for byte**: github.com/mwenge/tempest2k (`make cartridge` checks for
  this exact MD5). It contains the 68000 code (yak.s), the GPU modules (src/gpu/*.gas: camel, llama, ox, goat...),
  the object and web data, and the EEPROM code. With rmac's listing output we get a symbol for every address,
  so hooks can be placed by name. Llamatron needed reverse engineering for this.
- Emulator base: libretro/virtualjaguar-libretro (GPL-3.0, very active; develop was pushed 2026-11-08). It
  emulates the 68K, GPU, DSP, Object Processor and blitter, and has save states, run-ahead, rotary-controller
  support, 2x internal resolution, a high-level BIOS (no BIOS file needed) and an SDL3 + OpenGL desktop app in
  `app/`. Its Tempest 2000 playfield-flicker bug (issue #187) was fixed in d394a3e.
- BigPEmu 1.054 (closed source): the reference for how the game should look and sound.

## Stages
1. **Base**: a private fork of the core, pinned to one commit, built with MSVC. A small SDL3 front end of our own
   that runs only this game. Check it against BigPEmu: speed, audio (the DSP music), EEPROM high scores.
   Run-ahead on by default.
2. **Controls**: the stick angle picks a lane directly on closed webs (aim where you want to go); a sensitivity
   curve for the analog spinner; mouse as a spinner; triggers for fire and Superzapper; remapping. Start by
   tracing the game's own input code (yak.s) before tuning anything.
3. **Remaster renderer** (the main feature): catch the game's draw calls for the web, the enemies, the claw,
   shots and particles in its frame loop. From the source we know the object list and the GPU draw routines.
   Draw them natively with OpenGL at full resolution as lines and polygons (MSAA, optional glow), with positions
   interpolated between game frames for an unlocked frame rate. The feedback trails, melt and starfield are
   redone as shader passes. Text and HUD come from the game's own font data, scaled up. The emulated
   framebuffer stays as the reference for pixel comparisons, and the renderer falls back to it on any screen we
   don't capture.
4. **QoL**: no slowdown (GPU and 68K clock options, or hooks), skip the boot logos, save states, save slots for the
   game's own high scores and keys, a CRT / bloom option, widescreen if the web projection allows it.

5. **Automation** (later, trial on Defender 2000): signature logger, save-state explorer, residual detector,
   porting against the GPU oracle. Plan: AUTOMATION.md.

## Owner decisions (2026-11-08)
- Entirely offline for now: local git only, publishing decided later.
- Project becomes "Llamasoft on the Jaguar": Tempest 2000 first, Defender 2000 later (low priority; the owner
  finds D2K weak and wants real improvements to it, not only a sharper picture).
- Xbox pad first. Stick controls are the main control problem to solve: both games are hard on a stick.
- The owner plays 2000 mode, but every mode (Traditional, Plus, Duel) should be first-class: the aim is the
  best version of the game, as with the ST ports.

6. **Tempest 3000 (Nuon, 2000)**: the first project from 2026-11-14 (owner 2026-11-13). Details below.
7. **TxK (PS Vita, 2014)** in the same package (owner's plan 2026-11-11), after Tempest 3000. Details below.

## Tempest 3000 plan (2026-11-13; the first project from 2026-11-14, before TxK)
Owner: "this changes everything" - Tempest 3000 (Nuon, 2000) comes first. **Priority: smooth, even performance in
play** (it "feels uneven in play" in Nuance today). Higher resolution would be nice but is not needed: the game looks
good as it is. Same approach as before: the original code runs unchanged, and our hooks go around it.

### What we have (2026-11-13)
- Disc: `Nuance_067\Tempest 3000.mdf/.mds` (Alcohol image; the .mdf is a plain 2048-byte-sector ISO, volume
  "Tempest (NUON)"), hard-linked as `Tempest 3000.iso`. Never in git (Nuance_067/ is ignored).
- Nuance 0.6.7 standalone (Windows, asmjit JIT) runs it: boot -> title -> level select -> play, dynamic compiler on.
  Launcher `Nuance_067\Tempest3000.bat`. Pad: nuance.cfg binds no stick axes (Nuance's DirectInput axis hack makes
  stick-down read as held, and the main menu then ignores A); C buttons are on X / Y / L3 / R3.
- Source: `third_party\NuanceResurrection` (github andkrau/NuanceResurrection @12ece85, not in the outer git).
  The libretro core is `src/libretro.cpp` (CMake `-DBUILD_LIBRETRO=ON` -> nuance_libretro.dll):
  - needs a host OpenGL 2.1 context (SET_HW_RENDER; without one the picture is black), 720x480, 32 kHz audio;
  - input: RetroPad digital only (A, B, Start, Select = Nuon, L, R, X = C-up, Y = C-left, L2 = C-down,
    R2 = C-right, d-pad);
  - **timing runs on the PC's clock** (`useconds_since_start`): timers and the 60 Hz field come from the host
    clock, and one retro_run is whatever fits in a 16 ms budget. So runs aren't repeatable, and there's no fast
    headless run, no run-ahead and no save states (retro_serialize returns false);
  - reads nuance.cfg from its working directory (DynamicCompiler, MPE3PacketHack: Enabled / Disabled / **T3K**,
    an undocumented setting for this game);
  - older cycle-based timing code still exists (`NuanceProcessorThread.cpp`: 54 MHz, cycleCounter per vblank),
    a starting point for stage 2.
- Standalone readings: ~12,000 Kc/s at the title (main-loop iterations, 4 MPEs each), one emulation thread.
- The Nuon has no GPU: the game draws in software on its 4 MPEs. The Tempest 2000 capture-and-redraw remaster
  doesn't carry over; higher resolution would be a research project (stage 6).

### Why it may feel uneven (hypotheses: measure before fixing anything)
1. Field timing jitter: the 60 Hz video interrupt comes from the PC clock, checked every 500 (standalone) / 5000
   (libretro) loop iterations, and drawing on the same thread eats into emulation time.
2. Frame rate vs display: Nuance shows ~59 fps; on a 120 Hz display, frames held for 2 or 3 refreshes judder.
3. Not enough emulation speed in busy scenes: if a field's MPE work doesn't fit in 16.7 ms, the game drops frames.
4. The game itself: real hardware may slow down in busy scenes too. We need a reference (a real-hardware video,
   or the owner's memory of it) for how smooth T3K should be.

### Stages
0. **Measure** (first thing): in the standalone, log per field: real time, loop iterations, time spent drawing,
   presented frames vs display refreshes. Note where play feels uneven (busy levels, explosions). Find a
   real-hardware reference.
1. **Core in our host**: build nuance_libretro.dll with clang + Ninja (VS env), on our own branch
   `llamasoft-nuon`, pinned. Host side: a hidden OpenGL 2.1 context (SDL3), the core draws into our FBO, and
   glReadPixels (720x480) feeds the existing XRGB path (measure the cost; share the texture later if it matters).
   BIOS stubs (bios.cof, minibios.cof, minibiosX.cof) and nuance.cfg in its system folder. The owner's Xbox layout
   through RetroPad (check which Nuon buttons T3K uses in its controller options), our Back menu, fullscreen and
   settings. Done when T3K boots and plays in our window with sound and the pad.
2. **Emulated time** (the performance foundation): count MPE cycles (54 MHz) instead of reading the PC clock; the
   timers (sysTimer0 200 Hz, sysTimer1, vidTimer 60 Hz, audio periods) fire on emulated cycles, and one retro_run =
   exactly one field. That gives repeatable runs, a fast headless mode, and our test tools (--shot, --press,
   --wav, contact sheets). Risk: Nuance's cycle counts are approximate (JIT blocks, packet timing), and games may
   rely on "as fast as the PC allows"; make cycles per field a setting and calibrate it against the reference.
3. **Performance** (the owner's priority):
   - per-field profile (each MPE, JIT vs interpreter, comm bus, drawing, readback), like T2K_PERF;
   - idle skipping: MPEs spinning on the comm bus or waiting for the field (like the Jaguar core's RISC idle skip;
     the MPE3 VidSync wait already exists) are fast-forwarded instead of emulated;
   - our host paces presentation: one field per 60 Hz frame, each shown exactly twice at 120 Hz, no judder;
   - if the game itself drops frames in busy scenes, an "MPE overclock" (more cycles per field), only possible
     once stage 2 is done;
   - target: the worst field well under 16.7 ms on the owner's PC; Steam Deck class after that.
4. **Save states and run-ahead**: retro_serialize for the MPEs (registers, local memory; drop JIT caches on
   load), system / main bus memory, comm bus, timers, audio, video state, flash EEPROM. Then menu slots and
   run-ahead 1-2 as on T2K (test: run-ahead picture == plain run + N frames).
5. **Controls and QoL**: find the claw position / velocity in RAM and add the lr / aim stick modes from T2K;
   EEPROM / flash saves in userdata; quit to title; packaging as dist\Tempest3000 (never published: it holds the
   disc image).
6. **Later, optional**: higher resolution / widescreen (find the MPE drawing routines first); an "Original mode"
   switch for anything we change.

### Owner's answers (2026-11-13)
- Display: 120 Hz, no VRR. Nuance reports a steady 60 fps.
- What feels wrong: the picture is smooth, but **the game's speed fluctuates** (slows down / speeds up), noticed
  mostly moving between menus. That points at timing (hypotheses 1 and 3: the game's clock follows the PC clock
  and emulation speed varies), not presentation (hypothesis 2). Stage 0: log the game's own frame counter
  against fields to see when it falls behind or catches up; stage 2 (emulated time) is the likely fix.
- No real-hardware reference: the owner only just found the game. Use consistency as the target (a steady
  game speed, every field the same length) and check emulated cycles per field against 54 MHz.
- Collection: one launcher for T2K, T3K, TxK, a Steam link to T4K, maybe arcade Tempest (see the top of
  this plan).

### Stage 1 done, stage 0 measured (2026-11-14)
- Core: third_party\NuanceResurrection branch `llamasoft-nuon` (from 12ece85; asmjit submodule fetched at its pinned
  0bd5787). `tools\build_nuance.bat` -> build_nuance\nuance_libretro.dll (clang + Ninja). Changes: MSVC-ABI build fixes
  (no unistd.h / strcasecmp; WINDOWS_EXPORT_ALL_SYMBOLS - without it the DLL exports nothing), and run statistics
  exported as `nuance_get_run_stats` (iterations, MPE3 VidSync stalls, packets per MPE, field counter, VidSync calls,
  game flips = VidChangeBase + VidConfig calls).
- Host: `t3k.exe` (src/t3k_main.cpp, src/nuon_host.cpp), a separate program: both cores export retro_*, so the
  Nuance core is loaded at run time from the DLL. A hidden SDL OpenGL 2.1 window + a 720x480 FBO, glReadPixels -> the
  XRGB texture (rows flipped: bottom-left origin). build.bat builds it; target t3k_runtime copies the DLL, the BIOS
  stubs (bios.cof differs from the 0.6.7 standalone's: the repo's is newer; minibios files match) and res/nuance/nuance.cfg
  to build\system\nuance. Owner's Xbox layout (as their Nuance cfg), stick = d-pad, F11, Esc quits (no menu yet).
  Note: the core chdir()s to system\nuance - every path the host passes must be absolute.
- Test tools: t3k.exe --headless --frames --shot --press --wav --perflog=<csv> (--frames / --press work in the window
  too), tools/t3k_perf.py (per field: iterations, packets per MPE as MHz, game pictures/s and fields per picture),
  tools/contact_sheet.py. Route (approximate, runs aren't repeatable): start at 400, a at 900 / 1300 / 1700 / 2100 /
  2300 / 2500 -> in play by ~3000. Runs take real time (the core is paced by the PC clock).
- Findings (headless, this PC):
  1. Fields are 17.0-18.0 ms on average, not 16.67: the core checks the clock every 5000 iterations and restarts the
     next field from that late moment (lateness never made up), plus the 16 ms budget splits a field over two
     retro_runs. With vsync on (window), fields spanning a present get as little as 85-100k iterations (normal ~220k)
     and last up to 25-27 ms.
  2. The emulation loop runs flat out at ~13M iterations/s: MPE0-2 (the software renderers, JIT blocks) get
     40-66 MHz-equivalent packets, MPE3 (game logic) only ~17 MHz with MPE3PacketHack = Enabled (all its system-bus
     code capped to 1-packet blocks), ~45 MHz with the undocumented `T3K` value (cap only on 0x80011B6C..0x80011F59,
     the level-select handshake). Real hardware: 54 MHz each.
  3. **The game's own frame rate is the uneven part**: in play 40-42 new pictures/s with each held 1, 2 or 3 fields
     (e.g. {1: 227, 2: 171, 3: 23}); menus 17-24 pictures/s, 2-5 fields each. The T3K value barely changes it (42.3
     vs 40.2/s): MPE3 waits on MPE0-2, whose speed is set by how fast the PC emulates. No VidSync calls at all (the
     game paces itself some other way: find it).
- Next (stage 2 + 3 together): emulated time with a per-MPE cycle budget per field (54 MHz = 900k packets each), one
  field per retro_run; then make the emulator fast enough to deliver that (today MPE0-2 reach ~700k packets in a
  field's real time: profile the dispatch loop, idle-skip spinning MPEs, the T3K hack value). Find how the game
  paces (field counter polling on MPE3?) to know whether 30 or 60 pictures/s is its real-hardware target.

## TxK plan (2026-11-11; after Tempest 3000)
Game: `PCSB00494\` = EU Llamasoft TxK v01.03 (EP4461-PCSB00494_00-LLAMASOFTTXK0001), a decrypted folder dump
(plain eboot.bin + sce_module\*.suprx), 104 MB. Never in git. The owner has run it well on the Deck in Vita3K.
Firmware (owner supplies, never in git): PSP2UPDAT.PUP + PSP2UPDAT_PREINST.PUP (required), PSP2UPDAT_FONT.PUP
(optional), planned at `vita_firmware\`.

Route: the Vita3K libretro core (docs.libretro.com/library/vita3k, source github.com/WizzardSK/vita3k-libretro,
`libretro` branch; upstream Vita3K v0.2.1 build 4123, GPL-2). It loads a game folder / eboot.bin directly. It needs
the firmware + the core's `data` and `shaders-builtin` folders in <system>/vita3k/, and installs them on first run.
Options worth having: resolution multiplier 1-4x, filtering, V-Sync, FPS hack. Missing: save states (so no run-ahead
or menu slots for TxK; the game's own saves live in <system>/vita3k/ux0/). There are no Windows release DLLs (CI
artifacts only), so we build it ourselves. Ask the owner before cloning / building (downloaded-code rule) - OK in
principle 2026-11-11.

Steps
1. Clone into third_party\vita3k-libretro (recursive, not in the outer git), build the DLL with our clang / Ninja
   (CMake -DBUILD_LIBRETRO=ON, target vita3k_libretro). Record the commit in CLAUDE.md.
2. Host: load a second core (the core is a DLL, not linked in), set the system directory, boot TxK headless,
   firmware install, screenshot.
3. Host: libretro hardware rendering (RETRO_ENVIRONMENT_SET_HW_RENDER). Start with OpenGL core (glcore) on a hidden
   context: the core draws into our FBO, we read the frame back and upload it into the SDL GPU (D3D12) pipeline.
   Cost: 960x544 = ~2 MB/frame at 1x, ~8 MB at 2x (cheap); 4x ~33 MB.
   Vulkan zero-copy is not on with SDL GPU: SDL 3.4 has no public VkDevice / VkImage access (creation options only),
   so it would need our own Vulkan renderer. Tempest 2000 on SDL's Vulkan backend is possible (shaders to SPIR-V with
   the Vulkan SDK's dxc; the Windows SDK's dxc has no SPIR-V) but does not remove the copy. Later, optional.
4. Audio, input (Vita sticks map to the pad's sticks; touch / rear touch / motion: check what TxK uses), our Back
   menu, settings in userdata\, packaging (tools\package.ps1: game + firmware in dist\ only, never published).
5. Then TxK QoL ideas from the owner.
Fallback if the core won't build or is unstable on Windows: ship standalone Vita3K (official prebuilt) in portable
mode with a launcher (game copied into ux0/app/PCSB00494, run by title id, fullscreen).

## Neon Mode plan (2026-11-13; after TxK: owner wants TxK set up and a working combined build first)
Owner's idea: Neon, Minter's Xbox 360 Blades light synth (our port: C:\Completed Projects\Xblades, CLAUDE.md there
has the API and the pad map), as a live background in Tempest 2000. Neon descends from the VLM in the Jaguar CD.
Owner decisions
- A new mode, Neon Mode, picked in the menu. It replaces nothing else; Original mode stays untouched.
- In Neon Mode, Neon replaces the starfield ENTIRELY in play and on the level select screen. Bonus modes stay as
  they are for now. The warp between levels (also the starfield) is Neon too, crossfading to the next level's
  preset (owner, 2026-11-13).
- In play specifically: the readability challenge is part of the fun (owner). Still offer a brightness slider.
- One preset per level (100 presets, 99 levels: start with level n -> preset n, curate later: too dark / white /
  web-coloured ones swapped out).
- Neon's controls get driven: randomised and / or hooked into gameplay.
Route: helper process (not in-process: ReXGlue / Xenia's own D3D12 device + threads vs Virtual Jaguar + SDL GPU).
- neon.exe gets a background mode: no window, no live pads; reads audio bands + control values from shared memory,
  writes each frame (BGRA) to shared memory; T2K uploads it into a texture drawn under its scene (the remaster
  already composes with black see-through). Neon runs at 60 fps while T2K runs at 120 (check trails don't judder).
  Not rewound by run-ahead (visual only). SDL GPU can't import another process's texture, so the copy goes via the CPU.
- Resolution as a setting: 540p (~2 MB/frame) / 720p / 1080p (~8 MB/frame; measure). 540p looks soft, not blocky
  (bilinear; Neon's feedback layers are 512x512 anyway); its sharp geometry loses most. Owner fine with soft in
  theory: judge in the window.
- Audio: T2K's own output (music + SFX) -> Neon's FFT bands (Xblades src/audio.cpp: 2048 Hann, 20 log bands).
Controls (Neon reads them only through XInput)
- First: a virtual pad - T2K sends values over the link and the helper feeds them in as a held pad. Neon's map: sticks
  smoothed, A / B held = ramps, controller 2 d-pad edits a layer's parameter pairs 0-7 (X / Y pick pairs), A / B / Back
  = per-layer randomise / cycle, Start = reset layer parameters. Later: find the layer parameters in Neon's state
  and set them directly.
- Gameplay links to try: claw position around the web -> stick angle; firing -> A ramp; Superzapper -> randomise a
  layer; enemies left -> intensity (rising as the level clears, or calming before the warp); death -> reset + fade;
  random parameter drift when idle. Explosions pulse it for free through the audio link.
Steps
1. Neon's background mode + the shared-memory link (frame out, audio bands in); T2K draws it behind play at 540p in
   Neon Mode (menu item), starfield capture skipped there.
2. Level select screen; per-level presets + the warp crossfade; the resolution setting.
3. Virtual pad + gameplay links; tune with the owner in the window (menu sliders, sensible defaults).
4. Packaging: neon.exe + its DLLs + dash.xex in dist\ only (personal use; dash.xex is from the owner's dump, never in
   git, never published). Neon Mode greyed out when they're missing.
