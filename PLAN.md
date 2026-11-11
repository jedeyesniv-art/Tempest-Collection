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
