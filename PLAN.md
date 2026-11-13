# Tempest 2000 PC: plan (draft, 2026-11-08)

Goal: play the original Tempest 2000 (Jaguar, Llamasoft 1994) on PC with sharp high-res graphics, an unlocked
frame rate, good pad and mouse controls, and quality-of-life features. Same approach as Llamasoft on the ST: the
original code runs unchanged in an emulator, and we add hooks around it.

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

6. **TxK (PS Vita, 2014)** in the same package (owner's plan 2026-11-11; start Saturday 2026-11-14). Details below.

## TxK plan (2026-11-11)
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

## Neon Mode plan (2026-11-13; work starts 2026-11-14)
Owner's idea: Neon, Minter's Xbox 360 Blades light synth (our port: C:\Completed Projects\Xblades, CLAUDE.md there
has the API and the pad map), as a live background in Tempest 2000. Neon descends from the VLM in the Jaguar CD.
Owner decisions
- A new mode, Neon Mode, picked in the menu. It replaces nothing else; Original mode stays untouched.
- In Neon Mode, Neon replaces the starfield ENTIRELY in play and on the level select screen. Bonus modes stay as
  they are for now. Open: the warp between levels (also the starfield) - suggested Neon, crossfading to the next
  level's preset there; owner to decide.
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
2. Level select screen; per-level presets (+ the warp crossfade if wanted); the resolution setting.
3. Virtual pad + gameplay links; tune with the owner in the window (menu sliders, sensible defaults).
4. Packaging: neon.exe + its DLLs + dash.xex in dist\ only (personal use; dash.xex is from the owner's dump, never in
   git, never published). Neon Mode greyed out when they're missing.
