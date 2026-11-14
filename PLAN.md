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

### Stage 2 done: emulated time (2026-11-14)
- Owner tried stage 1: "significant improvement already", ~84 fps in play but speed still uneven; CPU / GPU usage low.
- Core (llamasoft-nuon): `nuance_set_timing(emulated, rounds_per_field)`. Emulated (default): one retro_run = one
  field = a FIXED number of rounds (one FetchDecodeExecute per MPE a round, MPE3 skipped while in VidSync: Nuance's
  own interleaving); the 54 MHz emulated clock advances evenly over the rounds and drives timer0 / timer1 / audio
  periods (checked every 64 rounds) and TimeElapsed (timer.cpp g_emu_time; real_useconds_since_start for the PC).
  Real: Nuance's loop (RunRealTime). **Runs are deterministic** (two runs: identical shots and stats).
- Tried first and dropped: per-MPE packet clocks (each MPE runs 900k packets a field, halted MPEs idle). It changes
  the interleaving the game was tested against: hang at the level-select transition (MPE2 never restarted), and with
  "halted MPEs keep up" a wild store crash (Execute_StoreScalarLinear) by field ~736. Don't go back to it without a
  real reason.
- **The game's speed follows the emulation work, not time**: at 180k rounds the menus / level start come far later
  than at 220k (zoom_180000.png vs zoom_220000.png in logs\t3k). No TimeElapsed or VidSync calls at all. So rounds per
  field = game speed; Nuance's average on this PC was ~220k (default `nuon_rounds = 220000` in settings.txt). Real
  hardware is likely faster (220k rounds = MPE0-2 ~43 MHz-equivalent, MPE3 ~17 with the packet hack) -> more speed
  from stage 3 = closer to hardware; calibrate against a real-hardware video if one turns up.
- Host: the emulation runs on its own thread paced by the clock (field k at start + k/60 s); the main thread presents
  every vblank the newest field due one field ago, so at 120 Hz each field shows for exactly 2 refreshes when it costs
  < 16.7 ms. Measured: 220k rounds 90% of fields exactly 2 refreshes (p50 field 16-17 ms, p95 20 ms); 180k 98%
  (game at a steady 30 pictures/s, but slower). Exit prints the refresh histogram. Crash handler prints module+offset
  (symbolize with llvm-symbolizer and tools\build_nuance_dbg.bat's PDB; lldb here lacks python311.dll).
- Game pictures in play at 220k: ~39/s, 1- and 2-field holds; MPE3PacketHack = T3K: 42.5/s, ~10% more PC time.

### Stage 3 progress (2026-11-14; owner: "220k feels much more even now")
- Benchmark: tools/t3k_bench.py (route to level 1 headless, ms per field, picture at field 2399 must stay md5
  b8309089...). 17.4 -> 16.2 ms a field: TickMediaCallbacks fast path (-5%), plain read before the locked exchange
  in TriggerScheduledInterrupts, instrumentation compiled out of the round loop (RunRounds<kInstr>). No gain from
  -march=x86-64-v3 or ThinLTO (tried; logs\t3k\bx.bat builds variants into build_nuance_<name>).
- Sampling profiler: T3K_SAMPLE=<abs file> (headless) + tools/t3k_sample.py with the debug core's PDB
  (logs\t3k\dbgrun has exe + system + debug DLL). ntdll samples (ZwQueryInformationThread) are the sampler itself.
- Where the time goes: compiled game code ~37%, the dispatcher ~45%. Calls that run UNCOMPILED code: MPE0 40%,
  MPE1/2 26-29% (NUANCE_PROFILE dispatch line; NUANCE_UNCOMPILED=1 lists the pcs): mostly packets marked
  NEVERCOMPILE - branches whose delay slots hold control-register (I/O) or indirect memory ops, and I/O packets -
  a long tail of ~18k pcs. Upstream's fix for that (FetchSuperBlock "IL downgrade", bForceILBlock) is disabled
  because it flickered in T3K's first level. Also a stale-COMPILED bug: 5 MPE0 overlay pcs marked compiled but
  never found in the native cache (FlushRegion doesn't clear the icache flag) -> interpreted forever (~2% of calls).
  Spinning is small (self / 2-block loops 0-5%); MPE3 calls are cheap (9 ns).
- Host: buffer_fields (default 2) lets a field start one field early (slow fields borrow from fast ones, +16.7 ms
  input lag), and the field grid is nudged half a refresh away from the vblanks. Window run: 95.4% of fields exactly
  2 refreshes; ALL odd holds were in the first ~900 fields (boot / title / intro: the title is the heaviest scene,
  18-21 ms a field for long stretches); menus + play 100%. T3K_HOLDS=<abs file> lists odd-hold fields.
- Next speed options, biggest first: make the uncompiled packets compile (I/O ops as IL calls inside native blocks,
  or fix the IL downgrade's T3K flicker - verify against the picture md5 AND by eye); the stale-COMPILED fix; a
  per-MPE lookup cache in front of PageMap::FindEntry.

### How the game times itself, and the flip cap (2026-11-14, after the owner's play test)
- Owner: "a large improvement but still not quite there": in play with a web + enemies it feels sluggish and the
  speed uneven; between levels (no geometry) it gets smooth and speeds up; the claw moves much too fast.
- Memory (core export nuance_get_memory, t3k --memdump=F:prefix, tools/t3k_memsearch.py, tools/t3k_dtsearch.py):
  $80110AA0 = the game's picture counter (+1 per picture; mirrored at MPE3 $20000FF0), $807FFDC0 = BIOS field
  counter, $800DC26C = claw position (16.16 lanes, likely; to confirm for stage 5 stick controls).
- **The game moves per picture**: the claw steps 0.106 lanes per picture held right, whether the picture took 1 or 2
  fields; no delta-time variable. So game speed = picture rate. It flips with **VidConfig** from MPE3 (three display
  structures in turn), never waits for the field, and Nuance applies the flip at once - so with enough emulation it
  draws 65-77 pictures a second. The hardware shows a flipped buffer from the next vblank: real T3K must have been
  capped at 60 (and, being per-picture, was surely meant to run at a locked 60).
- **Flip cap** (core g_flip_cap, setting flip_cap = 1, --flipcap): a second VidConfig / VidChangeBase by MPE3 within
  a field is held - MPE3 sleeps until the next field (MPE3wait_fieldCounter, as VidSync) and the BIOS call is
  retried (bCallingMediaCallback suppresses the implicit rts). NUANCE_DEBUG_FLIPS=1 logs flips.
- Result, level 1 in play, flip cap on: 400k rounds = **59.7 pictures/s, 237 of 238 pictures exactly one field**
  (locked 60, the intended speed); 300k / 350k = ~51/s (a picture's work just misses one field). MPE3PacketHack =
  T3K doesn't lower the threshold (MPE3 isn't the bottleneck: the MPE0-2 renderers are).
- So the target: ~400k rounds (more for busier levels) inside 16.7 ms. Today 400k = 25.5 ms a field here: needs
  ~1.6x. Above the threshold extra rounds don't change the game (MPE3 sleeps), so idle-skipping is a lever too.
- Host time per picture ~ the MPE0-2 calls a picture needs x ~20 ns: fewer, bigger calls (compile the uncompiled
  26-40% - branch + I/O delay slots, I/O packets; the stale-COMPILED fix), cheaper calls (dispatch), and skipping
  rounds where MPE3 sleeps and MPE0-2 are halted.
- Claw "much too fast": at 60 pictures/s it would be 6.4 lanes/s with the d-pad; stage 5 stick modes (as T2K's lr
  ramp) on $800DC26C.

### Parking wait loops (2026-11-14; owner: "something fundamental needs to shift" - a 9700X is top-end)
- MPE packets per field at the 60-picture threshold (1.2-1.4M) exceed what a 54 MHz MPE can do: much of it is
  waiting. --disasm=F:mpe:addr:count (core nuance_disasm) showed: MPE3 $800150A0 = jsr DCacheFlush (a no-op) + OR
  the three renderer-completion words $8003DE50-58, loop while non-zero; MPE0 $20300000 = dequeue from its comm
  job queue ($20101C10: write index $..1C18, read index $..1C1C), loop until a command (r4 == 3).
- Parking (core, on by default; NUANCE_NOPARK=1 off): an MPE at such a loop head (code bytes checked: overlays)
  with nothing to do is skipped each round until the condition can pass or it has an enabled interrupt, then
  resumes at the loop head. 400k rounds: 25.6 -> 19.9 ms a field, same picture rate. Benchmark at 220k: 16.2 ->
  12.7 ms (picture md5 now ad6a8115... - timing shifted by parking / flip cap; re-baseline when settled).
- What remains is real renderer work: MPE0-2 run the same overlay renderer (top block $20300D8C, 17 packets), ~276k
  calls each a field at 400k (~18 ns), one after another on one core: ~15 of the 20 ms. The real hardware runs them
  at the same time -> next: MPE0-2 on three threads in windows of W rounds, MPE3 + comm bus + interrupts between
  windows (latency W rounds; deterministic if the renderers don't race on shared memory). Needs a thread-safety
  audit of the core first (JIT, DMA, comm bus, BIOS handlers, statics).

### Renderer job probe (2026-11-14; owner chose: probe before threading / recompiling)
- NUANCE_JOBLOG=<abs file> (core): comm packets, MPE starts (pc, r0-3) / halts, overlay switches, and every DMA
  into an MPE's instruction RAM ("codeload": target, source, destination, length). tools/t3k_modules.py merges
  chunks into module loads.
- Architecture: MPE3 sends jobs (comm packets 3>0 of 4 words, e.g. 0 / $782 / $40064600 / $8004E040; MPE1 is
  started ~40x a field at $20300000 with a job pointer in r0, MPE2 with the comm words in r0-r3). Each renderer runs
  a resident kernel ($20300000-$203002D0) that DMA-loads CODE MODULES from a library at $8069E000-$806B1000 (~76 KB)
  into IRAM (at $203002D0 / $20B00000-style global addresses) plus parameter blocks from $8003Dxxx, then runs them.
  30-40 module loads per renderer per field.
- Library size: ~20 distinct full modules over boot -> title -> menus -> level 1 (0.9-4 KB of VLIW code each; e.g.
  $806A3F50 3372 B loaded 5.9x a field, $806AA120 3620 B, $806AE210 4060 B, $806AF1E8 1856 B, $806AD2B0, $806A5FB0,
  $806A8990 ~3 KB); later levels may add a few. No source for any of it.
- Assessment: HLE (rewriting each module's effect natively / on the GPU, as T2K) means reverse engineering ~20
  hand-optimised VLIW routines (~15-20k instructions) without source - weeks, effect by effect; worth it later for
  high resolution, module by module. For SPEED the better fit is ahead-of-time recompilation of exactly this small,
  fixed library: each module (keyed by source address + hash) compiled to a native function with its loops kept
  native (no per-block dispatch, registers across packets), yielding to the scheduler at loop back-edges by packet
  budget, verified against the interpreter in lockstep (deterministic runs). Pixel-exact, game-agnostic within T3K,
  and it is ~75% of the cost.

### AOT-compiled renderer modules (2026-11-14; owner: "start with the hottest module")
- Core (llamasoft-nuon): src/aot.{h,cpp} - module registry, per-MPE validation (g_iramVersion bumped in
  UpdateInvalidateRegion; byte compare only after a write), candidate index over IRAM $20300000-$20302000,
  AotTry() before FetchDecodeExecute in the round loop. nuance_aot_dump(mpe, src, len, base, path) decodes a module
  with the interpreter's options (+ .bin of its bytes). CMake NUANCE_AOT_DIR: every .cpp there is compiled in.
- Generated code (tools/t3k_aot.py; output in build_aot\ = game code translation, NOT in git): per packet the
  interpreter's exact steps (pcroute / pcexec, tempreg copy or tempCC, the same Execute_* handlers called directly
  with constexpr fields; pointer fields rebuilt per MPE: D+ = own dtrom, E+ = fixed place in nuonEnv; INFO fields
  holding host pointers are stale scheduler data -> 0); packets fall through, per packet only the delay-slot
  countdown / exception / jump check; block ends at a taken jump or NUANCE_AOT_MAX (64) packets; media tick + Halt
  once per block. The interrupt test mirrors FetchDecodeExecute exactly (AotInterruptTaken) - the renderers
  always have masked intsrc bits, a plain intsrc test kept AOT to 2-packet runs.
- Workflow: NUANCE_JOBLOG=<abs> run -> tools/t3k_aot_targets.py jobs.txt F dir -> t3k.exe ... --aotdump=... (all
  targets in one run) -> tools/t3k_aot.py each dump -> build_aot\aot_<src>_<base>.cpp -> tools\build_nuance.bat
  (ThinLTO + lld now: inlining the handlers is what makes AOT fast; delete build_nuance\ after option changes).
  30 (module, base) pairs from the route to level 1 (dumped at field 2600); coverage 98-100% of renderer packets
  to field 2700, 33-40% after (level 2's modules aren't in the catalogue yet).
- **Exact**: DynamicCompiler Disabled + NUANCE_AOT_MAX=1 vs NUANCE_NOAOT=1: pictures at fields 1500 / 1799 and all
  per-field stats byte-identical over 1800 fields, with 895M packets run through AOT.
- Speed (headless, flip cap on): 60 pictures/s in early play needs 400k rounds with the JIT (20.6 ms a field) but
  220k with AOT (15.5 ms, fields 2300-2700). Still over budget at 220k: title 25.6 ms, later play 17-18 ms (p95
  26). NUANCE_AOT_STATS=1 prints per-module validations / runs / packets at exit; NUANCE_PROFILE shows "mpeN AOT x%".
- Next: catalogue all levels' modules (longer job-log runs; maybe dump at several fields); codegen (cc / tempCC
  traffic, the remaining per-packet checks, bigger blocks across jumps within a module); then the three renderers
  on threads.

### Full module catalogue, threads, idle skip (2026-11-14; owner: "full module catalogue, then threading")
- Catalogue: NUANCE_AOT_COLLECT=<abs dir> (core aot.cpp) merges code-load DMA chunks per target MPE and dumps each
  new (source, local base, length >= 512) as m<src>_<base>_<len>.txt/.bin; flushed every field. aot_catalog\ (game
  data, git-ignored) = 329 variants from: 18000 + 36000 fields of attract mode (the demos play late levels, e.g.
  74 / 76) + the play route. tools/t3k_aot_gen.py: catalogue -> build_aot\*.cpp (incremental). New fixup kind M+
  (a field of the executing MPE, e.g. a mapped register). Registry: dynamic, longest module first per 16-byte
  step, per-MPE "last module" fast path and "none valid at this version" cache; behind a pointer (static-init
  order). Coverage: 96-100% of all three renderers' packets everywhere on the route.
- Threads (core, default on; NUANCE_THREADS=0 off, NUANCE_WINDOW, default 64): MPE0 on the emulation thread, MPE1 /
  MPE2 on two workers, a window of rounds in parallel; then MPE3 alone with the comm bus + interrupts. Safe because
  every cross-MPE effect comes from MPE3 (job log: MPE3 loads MPE1/2's code and starts them, sends MPE0 jobs; a
  renderer loads only its own code; no cross-MPE control-register DMA - logged as "ctlw"). Window 256 crashes at
  boot (a handshake); 32 / 64 run the route. **Threaded pictures are identical to serial** (same md5) and runs are
  repeatable. NUANCE_PROFILE_PAR=1: MPE0 is the bottleneck (61-80% of the time on the emulation thread), waiting
  for MPE1/2 13-24%, MPE3 4-12%.
- Idle fast-forward (NUANCE_NOIDLESKIP=1 off): MPE3 stalled + renderers halted / parked -> jump to the next timer /
  audio / field-end check point (at the top of the loop, so events fire at the same round: stats and picture
  identical with and without). Saves ~0.5 ms: little idle left.
- Numbers (headless, 300k rounds, flip cap): locked 60 pictures/s on title, menus and play (every picture one field);
  run ms: title 15.5 (p95 19.1), menus 10.4, play 11.5 (p95 19.1). Serial at 220k was 22.6 / 15.6 / 17.9. Window
  run at 300k: 94.6% of fields exactly 2 refreshes, odd holds in the title and late play, 21 fields dropped of 3300.
- Defaults now: nuon_rounds 300000; t3k.exe uses userdata\t3k_settings.txt (build\userdata is shared with t2k.exe).
- Next: cheaper generated code (MPE0 bound): per-packet pcexec/pcroute stores, tempCC, check what ThinLTO inlines
  (a debug-symbol LTO build + the sampler), longer blocks across in-module jumps. Then Steam Deck class numbers.

### Game speed: 30 pictures/s (2026-11-14)
- Owner after the locked-60 build: "noticeably better all round" but the game "appears massively too fast, I would
  guess 2x at least". The game moves per picture, so flip_cap became the minimum fields per picture (core
  g_flip_cap / nuance_set_flip_cap(fields); setting flip_cap, default 2 = 30 pictures/s, every picture exactly 2
  fields; 1 = 60; 3 = 20). Likely the original behaviour: a flip waits for the vblank, so work just over one field
  rounds up to two. Only whole divisors of 60 are even (30 / 20 / 15); in-between rates would mix holds again.
- Owner: "2 feels a little quick and 3 feels a little slow". flip_cap may now be fractional (core: a 2.5-field grid,
  pictures 2, 3, 2, 3 fields apart = 24 a second; restarts from the picture if the game falls behind), and t3k.exe
  paces pictures, not fields: picture n is shown at max(finished + 1 field, picture n-1 + flip_cap fields), so at
  120 Hz every picture stays up 5 refreshes (window test: 1308 of 1310 pictures exactly 5; at 2: 887/888 exactly 4).
  Default flip_cap 2.5. (The old build truncated 2.5 to 2 - the owner had already tried it.)
- Cost at 30: 5-7 ms a field mean (the idle skip removes the waiting half), p95 13.5 ms at 150k rounds, no fields
  over budget in menus / play; default nuon_rounds 200000 for margin in busier levels.

### Stage 3 next: speed (the critical path - more rounds a field = faster, smoother game)
- NUANCE_PROFILE=1 (220k rounds): each MPE call ~20 ns for only 3-5 packets (MPE0-2) and MPE3 85% single-packet
  calls (the packet hack): per-call overhead dominates, not the work. ~35% of a field outside the MPE calls (loop,
  comm bus + interrupts ~7%, profiler overhead).
- Ideas, measure each: slim FetchDecodeExecute's per-call path (interrupt checks, cache lookup, TickMediaCallbacks);
  bigger blocks / block chaining for MPE0-2; the T3K packet-hack range; detect MPEs spinning on a mailbox / comm bus
  and skip ahead (must keep the interleaving the game depends on); profile-guided build. Target: 220k rounds in
  ~10 ms, then raise the default toward hardware speed.

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
