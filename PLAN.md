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
- 2026-11-14, decided: a front exe (build\tempest.exe, src/launcher.cpp) that starts each game as its own process
  (emulators keep lots of global state; separate processes stay independent, a crash can't take the others down).
  It hides while the game runs and comes back when it quits. Barebones: list (Tempest 1981 / TxK greyed "coming
  later"), keys + pad, F11 fullscreen (userdata\launcher_settings.txt), Tempest 4000 = steam://rungameid/688140
  (greyed unless HKCU\Software\Valve\Steam\Apps88140 Installed = 1), --shot=<bmp> for tests.
  Next: a shared in-game settings overlay (T2K's Back menu as a module both games link: remaster on / off,
  controls, game tweaks; T3K: high_res, picture_blend, flip_cap, stick), then the launcher's look and packaging
  (tools\package.ps1: all games in one folder).

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

### Next: claw (hook) controls - stage 5 (2026-11-14, in progress)
- Owner after testing: flip_cap 3 (20 pictures/s) "the best setting overall" - now the default; audio OK.
- Started: the claw's state in memory. A first dump series (fields 2500-2620) was spoiled by the player dying;
  the level starts ~field 2170 on the route and enemies stay far until ~2350, so redo it there: idle, right 30
  fields, idle, left 30 fields, dumping every field with T3K_MEMDUMP_REGIONS=sys,mpe3 (new; keeps it to ~16 MB a
  dump). Earlier candidate $800DC26C (16.16 lanes) still unconfirmed. Then: how the game reads the pad, the lane
  count / open webs, and T2K-style lr (ramped speed) and aim modes.

### Claw stick controls (2026-11-14): lr mode done
- The claw is a game object at $8003E9E0 (MPE3 object list; type handler 0 at $8001228A = a bounded value): +0
  position (16.16 lanes; 16 on level 1's closed web), +4 velocity, +8 speed SCALE (1.0), +12 flags 0x80000401.
  Each picture the game sets velocity = d-pad input x 768 (input $801133D8: 20/40, 60, 80 while held, 0 on release)
  and adds velocity x scale, wrapping / clamping on the web itself. $800DC100 is the renderer's copy, $8075FAB4 a
  stack copy of the lane. Found with NUANCE_WATCH=<addr> (core: logs every interpreted packet that changes a
  system-bus word; run with DynamicCompiler Disabled) -> $80012210 (object list store) -> handler table $8011332C.
- Analog: reporting CTRLR_ANALOG1 (T3K_PAD_PROPS=10) skips the "controller configuration" screen but the axes
  (d1-d4) aren't read in play (the claw spins at a fixed -69120 whatever they hold) - not pursued.
- t3k.exe ClawControl (stick_mode = lr, default): the stick presses the d-pad its way and sets the scale each field
  so speed = lr_max_speed (0.94 = the game's top) x deflection curve x ramp (lr_ramp_pictures, lr_ramp_start);
  the game's late d-pad picture after release is held to half the last speed (lr_release_pictures, 0 = stop dead).
  Only while the signature holds (flags + renderer copy == position); else the stick is the d-pad (menus). The
  physical d-pad keeps the original controls. Keyboard A / D = the stick. --stick=F:x[:len], --poke=F:addr:val[:len],
  --analog, T3K_MEMDUMP_REGIONS, tools/t3k_clawsearch.py. Measured: full 0.19 / 0.41 / 0.63 then 0.94; half 0.216
  steady (= curve); release eases 0.94 -> 0.12 -> 0.
- Next: owner's feel test; aim mode (needs the web's lane geometry); open webs (clamping) unverified.

### Smooth motion at a fixed game speed (2026-11-14)
- Owner: on the fence between flip_cap 2 and 3; real play probably floated between them, but floating speed would
  feel terrible: wants a FIXED ~2.5 speed with smooth motion at any display rate. One game step = one drawn picture
  (MPE3 moves everything, then the renderers draw that state), so smoothness needs made-up in-between frames.
- Option 1 (done, experiment): picture_blend = 1 (default; F3 toggles, saved): the previous picture stays underneath
  and the current one fades in over its slot (flip_cap fields, from its planned show time). ~1 picture more lag.
- Option 2 (the real one, research, shares the work with high resolution): capture two consecutive pictures' renderer
  job lists, interpolate the moving values, run our recompiled renderer modules on the in-between job lists; watch
  out for feedback / trail effects. Option 3 (slow every object's speed scale at 60 steps) rejected: timers,
  spawns and animations count steps.

### High resolution: first map (2026-11-14; owner: picture_blend "excellent, we'll use that for now")
- NUANCE_DEBUG_VIDCONFIG=1: display 720x480 (the video hardware scales up, 4-tap horizontal filter); main channel
  dmaflags $002DC840 = 360 wide (45 x 8), 32-bit pixels; triple buffers at $40010000 / $40064600 / $400B8C00
  (345600 bytes apart = 360 x 240 x 4). 720x480 would need ~4.1 MB of the main bus's 8 MB.
- Candidate parameters (system RAM, play): $800BA3B0/B4 = 360.0 / 240.0 (16.16, a screen-size record);
  $800B2E10/14 = 180.0 / 120.0 (screen centre = the projection origin); the job descriptor MPE3 sends the renderers
  ($8004E040) holds 360 / 240 (16-bit at +4/+6) and a buffer base (+0x24) -> the renderers take the size from their
  jobs; $800B1730 = channel dmaflags + a buffer base (video setup data). Plus 188 / 200 other 16-bit 360 / 240 hits.
- Experiment 1 (2026-11-14): pokes at field boundaries to the job area ($8004E080...) don't stick - MPE3 rebuilds
  it within each field, many jobs a picture (NUANCE_WATCH=8004E080: writers $80012100 = a command-string parser
  ('<', '>', '0'-'4') and $800117A2 = a 4-vector copy from a per-job parameter block at [r8+0x18]). The centres live
  in render-job TEMPLATES in system RAM, mostly $800B3000-$800BB000 (00B4 0078 x12, 00B4 0098 x11; 0168 00F0 =
  360x240 x10). Halving all those centres (static data: poked once, stays) moved the web's LINES to centre
  (90,60): the renderers draw from parameters (go). The web's filled surface, the claw, enemies and stars did not
  move: their centre / origin comes from other parameters still to find.
- Step 1b (2026-11-14): the job log now prints each job's 64-byte descriptor at hand-over (MPE start: r0; comm:
  word 3). One picture in play = 13 MPE1 jobs + 1 MPE0 job. MPE1 = the 2D layer (HUD digits / icons, bars,
  sprites, particles; modules 806AA120 / 806AE210 / 806AB458 / 8069E070): descriptor word 0 = position (x, y),
  word 4 = size / scale, word 7 = template pointer. NUANCE_HR_TEST=1 (halve words 0 and 4 of MPE1 jobs at start)
  moved the lives icon, the side bar and the floating glyphs exactly -> the 2D layer can be scaled by
  transforming descriptors at hand-over. The 3D layer (web, claw, enemies, star) = MPE0's one job ($8004E040:
  360x240, colours, target buffer at +0x24; comm words 0x332 + another frame buffer) run by MPE0 + MPE2 together
  through modules 806A3108, 806A3F50 (between each), 806A5FB0, 806AD2B0, 806A8990, 806AF1E8, reading a display
  list MPE3 builds (command-string parser $80012100?) whose entries reference the render templates.
- Step 1c (2026-11-14): NUANCE_DMALOG=1 (+ NUANCE_JOBLOG) logs the renderers' linear DMA reads. MPE0 / MPE2 read
  the module library's tables ($806A3-$806AF pages), objects ($8003E / $800DC), templates + video setup
  ($800AE000-$800B2FFF, $800BA, $800B6), tables ($80106-$80107, $4018x) and MPE0-only data ($404E5, $8074C/F,
  $802FE). No effect from: $800B2E10/14 + $800B6E40 (16.16 centres) poked from before the level starts, the object
  list's $8003EBC0 (B4/98). Changing per picture: ~470 words at $800B1Cxx and ~150 at $8010A-$8010D (12-byte
  records, not screen coordinates). => the 3D layer projects inside the renderer modules from parameters not yet
  found; next is reading the modules (806A5FB0 / 806AD2B0 / 806A8990 / 806AF1E8 / 806A3108) for where screen x / y
  come from - disassembly work, likely several sessions.
- Step 1d (2026-11-14): the 3D layer's centre is mostly CODE, not data. tools/t3k_immsearch.py searches the
  decoded module catalogue for operand values: 26 sites hold the 180 / 120 centre (add #180 / #120 after the
  projection multiply in 806AD2B0 x4, 806A8990, 806AEF0A; mv_s #180.0 / #120.0 16.16 in 806AA120, 806AE210,
  806A73AE). tools/t3k_centre_pokes.py re-encodes them (+ the template centres) to any (cx, cy): at (90, 60) the
  web surface, lines, claw, shots and star all moved. Left in place: the centre-hole enemies. NUANCE_PIXLOG=x,y
  (pixel-write DMAs covering a pixel, into the job log) showed them drawn by 806A8990's other path ($806A8E70):
  centre = frame size word ($20100050 = 0x016800F0) >> 1, so that path follows the frame size by itself.
  Disassemble library code with --disasm=F:3:<library address>:N (local pc = lib - module src + 203002D0).
  Also hard-coded: 22 pixel-DMA flag immediates with the 360 width (002DC840 x15, 002DE840 x6, 042DC840 x1).
- Step 3 patch list (720x480 attempt): centre sites -> 360 / 240; DMA flag sites -> width 720 (0x5A in bits
  16-23); frame job + render template sizes 016800F0 -> 02D001E0, template centres x2; projection scale x2 (r21
  in 806A8990 = frame-job derived, check others); MPE1 2D descriptors x2 at hand-over; three 720x480x4 buffers in
  free main-bus memory + the video channel (dmaflags, base, VidConfig size); host picture 720x480.
- Step 3 DONE in principle (2026-11-14): 720x480 mode, core src/hires.cpp (+ hires_table.inc from
  tools/t3k_hires_table.py), t3k setting high_res (default 0) / --hires; nuance_set_hires(). In play and attract
  mode the picture is the original's layout at twice the resolution (web lines + surface, claw, enemies, shots, star,
  HUD); original mode stays pixel-identical. How it works:
  * Memory: the main bus is backed by 16 MB (MAIN_BUS_ALLOC_SIZE; the game's allocator still sees 8 MB; free in the
    game's 8 MB was only 2.25 MB at $401C0000). The game's buffers $40010000 / $40064600 / $400B8C00 are remapped to
    $40800000 + n x 0x151800 at pixel DMA (DMABiLinear base) and in VidConfig / VidChangeBase (src 720x480, flags).
    The new buffers start black (feedback screens copy what is there; the original was cleared at boot).
  * The 3D layer is a display list of 64-byte entries at $8004E040 (type = last word), read by MPE0-2 (bands
    r0 = 0 / 1 / 2; MPE1 also runs the 2D jobs) into local $20100030; render target block $800B1720 -> local
    $20100000 (flags, buffer, clip limits at +0x18 / +0x1C). Entry transform on read (EntryTransform): full-screen
    rectangle (0,0,360,240) -> 720x480; 00710001 (2D sprites AND vector objects: web lines, claw, HUD) words 0 + 4
    doubled; 0000000F (feedback) word 1; 0010000D (effect) DMA flags in word 8. Renderers WRITE entries back
    (MPE1, 16 words from $20100030): the inverse runs then, or the claw doubled every pass (it ended in a corner).
    Game data patches (frame sizes, clip, flags) are applied per field AND to the local copy when DMA'd in.
  * Code (73 sites, checked against the catalogue's original words): centres 180/120 (add #imm, mv_s 16.16) and
    180.5/120.5 (0x00B48000 / 0x00788000 in 806A5FB0 points, 806A8990 objects, 806B0080); projection scale
    mv_s #$C000 (0.75) -> 1.5 before the >>19 projection multiplies (806A5FB0, 806A8990); web surface vertices are
    packed half pixels unpacked with asr #17 -> #16 (806AD2B0); the clear's tile row kept only 8 bits (bits #7 ->
    #8 in 806A3108: rows past 255 wrapped); DMA flag immediates 2D -> 5A; edge tests (title tiles 354/234, object
    edges 240.0 / 352.0).
  * Tools: NUANCE_PIXLOG=x,y (which DMA writes a pixel: module + pc), NUANCE_DMALOG (reads with local address,
    and writes), NUANCE_HIRES_SKIP=<addrs> / NUANCE_HIRES_NOENTRY (bisect), tools/t3k_libwords.py, a whole-library
    disassembly by module (--disasm per catalogue module; the one-range run crashes on data).
  * Cost: in play 9.5 ms a field (p95 26) vs 3.7 (p95 10) - patched modules miss the AOT catalogue (validated by
    bytes): collect a high-res catalogue next.
  Update (later 2026-11-14): fixed since - menu "3000" logo + big text (0010000D: position, 16.16 scales), purple HUD
  icon / score (same type), death + warp zoom feedback (00300004: centre doubled, word 1 NOT - the source offset is
  word 1 - centre, doubling it read (360, 240) off into unwritten memory = pink), title / boot feedback (0000000F:
  word 1 = 384 x (240 x 16), both halves doubled), any entry word holding the 360-wide frame-buffer DMA flags,
  object perspective path (mul >>#20 -> #19). A 3800-field timeline (play, 3 deaths, warps, levels 2-3, game over)
  looks right. Tools: NUANCE_BASELOG (each pixel-DMA base / flags once), NUANCE_RECTLOG=<field> (every pixel-DMA
  rectangle; read vs write ranges found the zoom offset at once).
  Owner test (2026-11-14): "really quite impressive"; bug: claw + enemies sat inward of the web. Cause: 806AE210
  projects 3D-placed objects and writes screen position (word 0) + size (word 4) into their entries itself; the
  centre was doubled, the position multiplies not (fixed: 12 x mul >>#19 -> >>#18; size multiplies stay because word
  4 is doubled on read). Found with NUANCE_HIRES_TRACE=<entry> (reads / write-backs with values, into the job log).
  Speed: window with the owner's settings (200k rounds) holds 60 fields/s after boot (field max 18-22 ms, the
  2-field buffer absorbs it); the game draws its 20 pictures/s in high res too (emulated budget is enough at 200k).
  F4 = on-screen counter (pictures/s, fields/s). High-res AOT catalogue: aot_catalog_hires -> build_aot/aoth_*
  (tools/t3k_aot_gen.py aot_catalog_hires build_aot aoth_; 29 patched modules) - small gain (8.1 vs 9.8 ms a field);
  AOT vs dynamic compiler runs drift apart in game state in high res (renderer timing), pictures both clean.
  Still open: 2D vector objects with size >= 256 stay at the old size (the spinning TEMPEST logo on the title and
  menu: doubled to 992 it vanished - an overflow in one of 806AA120's handlers, table at local $20100EC0 by word 5's
  low half); menu dot / ring sizes; point size clamps (806A5FB0 2..24); the AOT catalogue for the patched modules.
  Older notes: title transition warp only updated the left 360 columns (fixed); main menu "3000" logo (0010000D entry: word 0 is a
  centre there but not on the boot screen - read its module first) and maybe text sizes; the purple HUD icon is half
  size; sprite size clamps (806A5FB0: 2..24) not doubled; untested: level transitions, other screens, options,
  high-score entry; the window (host) path with high_res = 1.
- Earlier plan: decode the 3D display list (where, entry format, which fields are centres / scales / sizes), then step 2
  (buffers + frame job) and step 3 (720x480 attempt: 2D transform x2 + 3D templates / list x2 + buffers + channel).
- Experiments next (earlier list): (1b) find the remaining elements' parameters (same template region: other formats - 16.16,
  separate x / y words, per-object camera data); (2) where the buffers are allocated and the 360x240 frame job is
  filled; (3) a real 720x480 attempt with all found parameters doubled.
- (was:) (1) poke the projection centre / scale and see whether the renderers draw bigger (projection
  parameter-driven?); (2) find where the buffers are allocated and the job descriptor filled; (3) a real 720x480
  attempt: bigger buffers in free main-bus memory, job sizes, channel flags, projection x2, then fix what breaks
  (text, sprites, clears, feedback effects).

### Native renderer research (2026-11-15; owner: decouple game speed from frame rate, Deck = min spec)
- Owner: Deck at native res mostly 24 pictures/s, dips to 22; high res very slow there. 3x renderer work for
  interpolated pictures through the emulated renderers is out (too expensive) -> focus on a NATIVE renderer of the
  display list, with interpolation at any display rate.
- Re-render test part 1 (core src/fbprobe.cpp: NUANCE_FBPROBE=<file> per-field frame buffer reads / writes + reader pcs;
  NUANCE_CLEARTARGET=<hex> fills a buffer when the renderers switch to it; original res only, NUANCE_THREADS=0;
  tools/t3k_cleardiff.py; logs\t3k\rr\run.sh A|B = 3800-field route with fire, shots every 20 fields):
  * Runs stay in lockstep (perf stats identical). 184 of 185 pictures identical with the target pre-filled: every
    picture fully redraws its buffer, nothing older than the previous picture survives. Exception: the level-start
    zoom (f2760) leaves the right-most 1-2 columns undrawn.
  * But EVERY picture starts from the previous one: the first 3D display list entry ($8004E040) is a feedback pass.
    Play: type 00300004 (zoom feedback, module 806A5280: centre B4,78, zoom 16.16 in words 4/5, varies: 1.0009 at f2740, 1.0625, ~1.4 in transitions,
    source = the previous buffer in word 9) - ~2.4 screens of reads a picture; this is the trails / glow look. Title /
    menus: 0000000F (feedback background). Then the primitives draw over it, many read-modify-write (blended: pcs
    20300E22 / 20300D72 read the target before writing).
  * So picture n = F(picture n-1, display list n). The display list (+ the templates / tables it points at) is the
    whole input; ~10 entry types on the route to level 1 (00710001 2D sprites / vectors 2606, 00FF0004 862,
    00000300 460, 0000000F 251, 00300004 194, 003102FE / 00310108 / 000000FE 55 each).
- Consequences for the design:
  * T2K's per-primitive overlay (native + emulated picture composited, black see-through) does NOT carry over: the
    feedback pass and blended draws read the whole buffer, so a half-native picture would feed back / blend against
    the wrong content. Takeover must be per PICTURE: if every entry in this picture's list is a ported type, draw it
    all natively (feedback in our own texture, as T2K's feedback screens); otherwise run the emulated renderers for
    that picture. Port entry types screen by screen (play first).
  * Interpolation: the feedback is one step per game picture. At display rate it becomes continuous: zoom^(dt) and
    fade^(dt) per displayed frame with trail length matched per second, objects drawn at interpolated positions.
  * Speed: native pictures skip MPE0-2 (the bulk of the cost); MPE3 (game logic) stays emulated (4-12%).
- Census (2026-11-15; core fbprobe.cpp NUANCE_ENTRYLOG=<file>: every 16-word entry fetch into local $20100030,
  grouped per flip (g_vid_flips) = per picture, readers, write-backs, code loads into IRAM as src@offset;
  tools/t3k_census.py merges chunks into modules and prints types / words / screens). Runs: the 3800-field play route
  with fire + 40000 fields of attract (press start once at 400, then idle: the menu cycles into demos of levels 74 /
  ~120, bonus tunnel, high scores, credits, logo; WITHOUT that start press it stays on the title forever).
  14559 pictures: **18 entry types, 18 code modules, nothing new in the attract demos.** Per picture MPE1 first runs a
  pre-pass job (modules at local 0: 806AE210 projects 3D-placed objects and writes entries back, 806AA120 / 806A4F20 /
  8069E070 / 806AB458 / 806AC638 2D-layer handlers, 8003DA50 its kernel copy), then MPE0-2 draw the list in
  horizontal bands with modules at local $2D0.
- What each type draws (NUANCE_SKIPTYPE=<type>: MPE0 / MPE2 pass over it; logs\t3k\rr\skip.sh + skip_sheet.png):
  | type | module | draws |
  | 00710001 | 806A3F50 | THE workhorse (90k entries): vector lines + 2D sprites - web outline, claw, enemies' lines, text, HUD, stars |
  | 00000300 | 806A3108 | full-screen clear / fill (colour in word 2): menus and play background (blue on level 2) |
  | 00300004 | 806A5280 | zoom feedback (previous picture zoomed, faded): warp / level start, play trails |
  | 0000000F | 806B0B30 | feedback background (boot / title screens) |
  | 0011000B | 806AD2B0 | the web's filled, textured surface |
  | 0010000D | 806AF1E8 | big glowing text: TEMPEST logo, score digits |
  | 00310008 | 806A5FB0 | point sprites (level-select sparkles) |
  | 0031000E / 00310409 | 806AFA80 / 806A8990 | menu / title particle fields |
  | 00310010 | 806A6E40 | background dot grid (level 3) |
  | 0031020E, 00310209, 00310309, 00500005 | 806AFA80, 806A8990, 806A8990, 806A7DC0 | small objects: shots, enemy parts, particles, explosions |
  | 000000FE, 003102FE, 00310108 | (pre-pass / none) | nothing visible: control / camera entries handled by MPE1's pre-pass |
  | 000000FF | - | end of list |
- Next: decode the play screen in order of payoff - 00710001 (via 806A3F50: line vs sprite sub-kinds, template at word 7),
  00000300, 00300004, 0011000B, 0010000D - from the modules' disassembly and the entry words, then a native prototype of
  one play picture checked against the emulated one.

### 00710001 = vector shapes (module 806A3F50; 2026-11-15)
- Disassembly: logs\t3k\rr\m806A3F50.dis (t3k --disasm=2700:3:806A3F50:1400 on the play route; local pc = lib -
  806A3F50 + 203002D0). Investigation hooks (core): NUANCE_NOJIT=1 (interpreter; with NUANCE_NOAOT=1) and
  NUANCE_PCLOG=<local pc> (+ NUANCE_PCLOG_MEM=<local addrs>): MPE0's r0-r31, the entry at $20100030 and memory words
  each time it reaches that pc. Note a local pc is shared by every module loaded at $203002D0: filter on the entry type.
- The pre-pass (MPE1, 806AE210 etc.) turns object indexes into shape pointers and fills in angle / sizes; the drawing
  pass reads the rewritten entry. Words: w0 screen position (x hi, y lo), w2 / w3 colours A / B, w4 sizes (10-bit
  signed halves x hi / y lo, 8.8 of shape units), w5 width etc. (low byte = line width; w5 >> 2 = width factor),
  w6 angle (16 bits a turn), w7 shape stream (system RAM for fixed shapes, main bus $4018xxxx for runtime shapes such
  as the web outline), w12 jitter strength, w13 jitter amounts (bytes: angle, x, y).
- Shape stream (DMA'd in 64-word chunks): a word with $8000 in the top half is a command (low nibble, table at local
  $20100CC0): 0 / 5 set w5 low / high half from the next word, 1 end, 2 pen up, 3 colour A = next, 4 colour B, A =
  next two, 6 load a 16-colour palette from the next word's address, 7 colours from the palette (nibbles: A = bits
  8-11, B = bits 12-15), 8 add (cmd << 16 >> 20) to the jitter offset, 9 set it from the next word. Other words are
  vertices (y hi, x lo; signed 16-bit); each vertex after the first draws a line from the previous one.
- Transform, **bit-exact against 85,280 emulated segments** (tools/t3k_vecmodel.py): sin / cos = polynomial
  (806A4BBE, 2.30, quadrant stubs); object matrix from w4 + w6; per segment the endpoints are taken about the midpoint,
  rotated by a random angle and the midpoint moved by random x / y (LFSR $31415926 reset per entry, one step per
  segment: v >> 1, xor $A3000000 when bit 0 was clear), amounts = w13 bytes x clamp(w12 + offset, 0, $FFFF); then
  object matrix (>> 14) + w0. Screen units = shape units x size / 256.
- Raster (read, not yet modelled): thick antialiased lines in 32-pixel row spans, read-modify-write per pixel:
  dest + (colour - dest) x coverage, coverage from the distance to the line (reciprocal sqrt table at local
  $20100630), colour A -> B along the line. GPU equivalent: a quad per segment with a distance-falloff shader and
  alpha blending. Next: model the coverage profile + gradient, check against emulated pixels, then the native
  prototype.
- Raster model (2026-11-15; core NUANCE_SPANLOG=<type>: MPE0's read / write pixel rows per segment, after the
  NUANCE_PCLOG line; tools/t3k_linefit.py): every drawn line in play AND the attract demos has w5 = 4FFF0002 (width
  byte 2) and w12 = 0 (no base jitter). Coverage is a linear tent: alpha = 0.00888 x Z x (1 - d / 1.94), d = distance
  from the INTEGER pixel coordinate (corner, not centre: rms 0.053 vs 0.129) to the segment, Z = the colours' 4th byte
  (an opacity, $3F = 0.553 peak) interpolated from colour A at the segment's current vertex to B at the previous one,
  like Y / Cr / Cb. Blend: dest + (colour - dest) x alpha in YCrCb. Display: Nuance's BT.601 (Y x 255/219 - 16/219,
  Cr / Cb x 255/224 - 16/224, then the usual matrix).
- Prototype (tools/t3k_vecrender.py; log: NUANCE_ENTRYLOG + NUANCE_ENTRYDATA=1 = each entry plus the shape streams /
  palettes MPE0 fetched for it): redraws a picture's vector shapes from the display list alone at any scale. Play
  picture 902 (f2741): web outline, claw, star, streaks, "WELCOME TO THE MACHINE", HUD all match the emulated picture
  in place, colour and fade; the rest of that picture is other types + the zoom feedback trails. logs\t3k\rr\
  proto_902.png (emulated | ours 2x), proto_902_x4_crop.png (4x).
- Next: GPU version in t3k.exe - the core hands the host each picture's display list + fetched data (in memory, the
  ENTRYLOG path), a line pass (quad per segment, tent shader, alpha blend in YCrCb, then the BT.601 conversion);
  a debug view of the vector layer alone, interpolated between pictures; then the other play types (00000300 fill,
  00300004 zoom feedback, 0011000B web surface, 0010000D text) for the first full native picture.

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
Firmware (owner supplied 2026-11-14, never in git): `vita_firmware\PSVUPDAT.PUP` (system firmware, 134 MB) and
`vita_firmware\PSP2UPDAT.PUP` (font package, 57 MB) - the two files Vita3K installs.
State 2026-11-14: core cloned to third_party\vita3k-libretro (branch libretro, commit 29ad0e79, recursive, 590 MB, not
in the outer git). Windows build = upstream CI route: MSVC + preset windows-ninja + -DBUILD_LIBRETRO=ON, target
vita3k_libretro (Boost headers bundled; curl via FetchContent; prebuilt ffmpeg downloaded at configure). With MSVC +
libretro, external/CMakeLists.txt builds a static OpenSSL 3.6.1 from source and REQUIRES a native Windows Perl
(Strawberry; Git's perl is rejected). Blocked there: downloading portable Strawberry Perl was refused by the
permission classifier; the owner then approved it (third_party\strawberry). OpenSSL also wanted NASM: patched to
no-asm instead (branch llamasoft-vita). Step 1 DONE 2026-11-14: build_vita3k\bin\Release\vita3k_libretro.dll.
Step 2 DONE 2026-11-14: build\txk.exe (src/txk_main.cpp, src/vita_host.cpp) boots TxK and plays level 1 (headless and
window). Host: OpenGL 4.4 core context on a hidden SDL window (GET_PREFERRED_HW_RENDER = OPENGL_CORE), FBO at the
core's max size (3840x2176), readback flipped (bottom-left origin); core options = any vita3k_* settings key or
--opt=; headless runs are paced to real time (the emulator runs on its own threads). Firmware: vita_firmware\ PUPs
hard-linked into build\system\vita3k\ as firmware.pup / font.pup (PSP2UPDAT.PUP was the font package, confirmed:
sa0 installed). The preinst package (pd0) is missing and TxK runs without it.
The offline "Network features are disabled" notice (sceMsgDialog, OK only; wants a touch on the Vita, and while it
was up / after Cross closed Vita3K's overlay the game took no input) is answered OK without showing it: core patch
on llamasoft-vita reads VITA3K_AUTO_OK_DIALOG (Win32 GetEnvironmentVariable: the core's CRT copy of the environment
predates the host setting it), txk setting auto_ok_dialog (default: that text).
Route: Start at 900 (title -> Level Select), Cross at 1300 -> level 1 by ~1800 (frames from boot, fresh userdata).
Unhandled env (harmless so far): keyboard callback, core assets dir, sensors, VFS.
2026-11-14 later: shared Back menu (src/txk_menu.cpp): restart, internal resolution 1-4x (core option, applies on
restart = retro_reset, the core's deferred unload + load), anisotropic filtering, picture scaling, button remapping
(Select on LT by default: Back opens the menu). 2x and 4x hold 60 pictures/s in play (RTX 5070 Ti, readback
included). No save states: the core's retro_serialize is a stub (upstream Vita3K has none either). In the launcher.
Next: packaging with the MSVC runtime DLLs, then TxK QoL with the owner.

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

## Tempest (1981 arcade) (2026-11-15)
build\t81.exe (src/t81_main.cpp, src/t81_menu.cpp) on MAME's libretro core built with the Tempest driver only
(tools/build_mame.sh). Romset tempest.zip next to the exe, in game\ or the repo root. Core options (t81_settings.txt,
any mame_* key): rotation internal, alternate renderer at mame_altres (1920x1440 default; 2880x2160 for 4K; applies
live), mouse on. Spinner: the left stick (curve, spinner_speed) / d-pad as relative mouse movement a frame; MAME's
dial reads it. Buttons (remappable): A / RT fire, B / RB / X zap, Start, Y coin; Back = menu (reset, save states
5 slots in userdata\states\t81_slotN.state, spinner speed, buttons, fullscreen, vector resolution, scaling).
Verified: attract -> coin -> start -> rate yourself -> play; spinner moves the claw; save state restores (the core
restores at a scheduler-safe point: 1-2 frames off a plain run, fine for players); window keeps 3:4 (core aspect).
Open: tune the spinner feel with the owner; vector look (beam width / glow: MAME ini options); clones (tempest1..3,
temptube) not selectable yet.

## Collection status and open items (2026-11-15, after the owner's Steam Deck test)
Owner tested the play folder (dist\TempestCollection, built by tools\package_collection.ps1) on the Steam Deck: all our
games "work really well indeed". Tempest 3000 runs "just about at frame rate" (little headroom).

Open, roughly in order:
1. Tempest 4000 on the Deck is not listed as playable: the launcher's install check (src/launcher.cpp MakeGames)
   reads Windows Steam's registry (HKCU\Software\Valve\Steam\Apps\688140\Installed), which a Proton prefix does not
   have. Fix: under Wine (ntdll exports wine_get_version) don't trust the registry - check the Linux Steam library
   instead (Z:\home\deck\.local\share\Steam\steamapps\appmanifest_688140.acf, and the libraryfolders.vdf paths for SD
   cards) or just offer it. Then test that steam://rungameid/688140 from inside Proton reaches the Deck's Steam
   (SDL_OpenURL -> ShellExecute; Proton may need winebrowser / "start /unix xdg-open"). Untested so far.
2. Tempest 4000 helper on the Deck (Tempest 4000 helper\ in the play folder: two DLLs into Win10\ and Win7-8\, launch
   option WINEDLLOVERRIDES="xinput1_3,xinput1_4=n,b" %command%) - not tried there yet. Verified on Windows only.
3. Tempest 3000 speed on the Deck: at the edge. Options: the T3K performance items above (AOT coverage, MPE0-2
   renderer work), a Deck default (high_res off is already the default; flip_cap 3), measure with F4 on the Deck.
4. Size: the Tempest 3000 .iso is 743 MB of the 1.14 GB folder, almost all raw 16-bit BE stereo PCM in nuon.dat
   (measured 2026-11-14: FLAC lossless 69% -> saves ~230 MB; lossy ~630 MB, needs a map of music vs data first).
   Tool in the core: NUANCE_MEDIALOG=<file> logs every disc read (third_party\NuanceResurrection, media.cpp; not run
   yet). Nothing else in the folder compresses usefully (cores are code; TxK assets already compressed).
5. Arcade Tempest (t81): the CRT looks (owner asked 2026-11-15). src/crt.cpp is self-contained (crt::CreateRenderer /
   Init / LoadSettings / DrawPicture; SDL GPU renderer + DXIL shaders): use it in t81_main instead of SDL_CreateRenderer
   + SDL_RenderTexture, add crt.cpp to the t81 target, copy T2K's CRT menu section. A vector monitor has no scanlines
   or shadow mask: give t81 a "vector" preset (glow / bloom / CRT++ trails = phosphor persistence on, scanlines and
   mask off), the full sliders still there. Then the same CRT looks in T3K (owner: yes); not TxK (owner: not needed). Also the vector look in MAME
   itself (beam width / glow / flicker: ini options in system\mame\ini); the clones
   in tempest.zip (rev 1, rev 2, Tempest Tubes) not selectable; spinner: owner tuned it (1.0 = old 0.4).
6. TxK: no save states (core stub, upstream has none); preinst firmware part missing but not needed; QoL ideas from
   the owner still to come. Vita3K storage_location.txt lesson: CLAUDE.md Lessons 2026-11-15.
6b. Space Giraffe (Llamasoft, 2008; Steam app 27800, installed here in steamapps/common/Space Giraffe): a launcher
   entry that starts it through Steam, same rules as Tempest 4000 (owner 2026-11-15): Kind::Steam, steam://rungameid,
   the install check fixed for Proton as in item 1. Check whether it has a start-up dialog worth skipping like T4K's.
7. Launcher: still the placeholder look (SDL debug font); the Minotaur Project launcher (C:\Gridios\Launcher) is the
   model for behaviour and features (settings screen, Steam shortcut).
8. Then (owner's order): Neon Mode, Defender 2000.
