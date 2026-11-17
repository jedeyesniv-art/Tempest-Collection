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

### Go / no-go checks before the GPU renderer (2026-11-15; owner: "run the checks first")
- COST (core NUANCE_NODRAW=1: MPE0 / MPE2 see every entry as end-of-list, MPE1 at the list head $8004E040 - its
  drawing pass, started with r1 = $FFFFFFF8, begins there; its compute jobs fetch single entries. NODRAW=2: MPE0 / 2
  only). Play route, Deck defaults (flip_cap 3, 200k rounds), ms per field:
  | | menus mean / p95 | play mean / p95 / max |
  | threaded, today | 5.32 / 15.78 | 3.85 / 11.26 / 13.06 |
  | threaded, no drawing | 0.88 / 2.01 | 1.16 / 3.17 / 4.42 |
  -> 3.3-3.6x less CPU in play, ~6x in menus; identical flip count (the game logic doesn't depend on drawing).
  MPE1's own modules (loaded at local 0: 8069E070, 806AA120, 806A4F20, 806AB458, 806AC638, 806AE210, kernel 8003DA50)
  only compute (no pixel DMA while loaded) = ~12% of MPE1, stays emulated. PASSED with margin.
- INTERPOLATION (tools/t3k_interpcheck.py on an ENTRYLOG + ENTRYDATA play log, fields 2300-3800, 500 pictures):
  * list slots are NOT identities: 76% of entries keep slot + type + shape; 229 of 499 pairs keep the type sequence.
  * no object id in the entries (w14 is shared: 499 of 4258 vector entries unique by type + shape + w14).
  * matching each vector entry to the nearest same-shape entry of the next picture: 44 of 8660 unmatched; moves
    median 0, p90 2, p95 4, p99 14 px; 18% ambiguous (shapes drawn many times: 8010A3E4 up to 11x, 8010ADE4 6x).
  * 3D-placed objects have game-space inputs before the pre-pass (w8 lane 16.16, w9 depth 16.16; w7 an object
    index) - better to match and interpolate there, then project ourselves (port 806AE210's projection).
  * camera-like entries (003102FE / 00310108 w0 changes smoothly) and the runtime web shapes (same stream structure
    in 6495 of 6496 pairs) are unique: interpolate directly.
  -> FEASIBLE with a matching layer (type + shape + nearest in game space with velocity prediction), and a safe
  fallback: unmatched / ambiguous entries are drawn stepped at their latest position (today's look).
- Latency: interpolating between game steps shows the state up to one step late (50 ms at 20 pictures/s, the same
  as picture_blend today). With emulation this cheap, run-ahead becomes affordable (1 picture = 3 fields ~3.5 ms on
  this PC) but needs save states (retro_serialize, stage 4) - worth planning in.

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

### Native renderer in t3k.exe: the vector layer (2026-11-15)
- Core capture (src/capture.cpp, always on from t3k.exe: nuance_capture_enable / nuance_capture_get): MPE0's entry
  fetches (16 words into $20100030) + every other bus word it reads, published at the flip (VidConfig /
  VidChangeBase: CaptureFlip) = the picture that flip shows; capture.flip == RunStats::vid_flips of the field.
  Emulation thread only (MPE0 and MPE3 both run there with the renderer threads on).
- Host: src/t3k_native.{h,cpp} - C++ port of the bit-exact transform + stream interpreter (BuildVectors, once per
  new picture on the emulation thread; the field ring slots carry a shared_ptr), DrawVectors = SDL_RenderGeometry:
  per segment a tent (centre vertices = colour + opacity, side vertices +-1.94 game px at alpha 0) + end caps,
  BLEND mode (lerp in RGB == lerp in YCrCb: the conversion is affine), half a pixel shift (the module samples at pixel
  corners). Colour = Nuance's BT.601, alpha = 0.00888 x Z.
- Settings / keys: native_view (F7: 0 off, 1 the native vector layer alone, 2 over the emulated picture),
  native_interpolate (F6, default 1). Interpolate(): entries matched by shape (w7) + segment count, nearest first
  (first segment start + w0), each once; segment endpoints / colours lerped over the picture's slot (picture_blend's
  timer); moves > 30 game px or unmatched -> drawn as the latest picture. T3K_WINSEQ=<first>:<n>:<dir> saves n
  consecutive presents (tests).
- Verified in a window: native_view 2 lines sit on the emulated ones (level start zoom); native_view 1 in play
  (logs\t3k\rr\win_native.png: web, claw, zap line, enemies, star, streaks, HUD at 1440x1080). 120 Hz, 20 pictures/s,
  mean change per present: stepped 0 / 3.0-3.5 at each new picture; interpolated 0.3-0.9 every present, a few snaps
  (spawns, big moves).
- Not yet: the emulated renderers still run (no speed gain until whole pictures are native); the other types.

### Whole native pictures: fill + zoom feedback (2026-11-15)
- 00000300 (806A3108) = a plain fill: rectangle w0 (x, y) / w1 (w, h) in colour w2 (dup-mode pixel DMA of the one
  word, 8x4 tiles, no reads). In play after the level start every picture begins with it (black): no feedback trails.
- 00300004 (806A5280; tools/t3k_zoomfit.py on NUANCE_SPANLOG=00300004, which now also prints "spanentry" words and
  multi-row "spanb" blocks): out pixel p = previous picture at c + R(-a) (p - c + 1/2) / z (bilinear, 2026-11-17: the
  module's path A reads 4 pixels; nearest was the earlier fit); c = w0,
  z = w4 / w5 (16.16; 1.0625 steady, 1.4 -> 1.06 in transitions: free fits land on 1 / w4 wherever the source has
  detail), a = w6 (16 bits a turn), fade = w7 >> 16 as 2.14 (0.969 steady; > 1 = brightening at transition starts,
  clipped).
- t3knative::Picture = ops in list order (kFill, kZoom, kSegs); complete = every entry type known (FF / FE / 2FE draw
  nothing). Renderer: two window-sized target textures; zoom = the previous texture as one quad (uv per corner from
  the mapping, vertex colour = fade; > 1: an extra ADD pass). Mixed mode: when a native picture follows an emulated
  one, Seed() copies the emulated previous picture in. In the window (native_view 1): exactly one feedback step per
  game picture spread over its time on screen (native_step_left; a new picture first finishes the old step) - a
  fixed step per present ran ahead when the game slowed in transitions.
- Test: headless --nativeshot=F:<bmp> (hidden window renderer, one full step per picture, emulated pictures where not
  complete; T3K_NATIVE_RESEED=1 = every native picture builds on the emulated previous one). Title -> menu zoom:
  one step diff 1.1-1.3 (720x480 mean abs), no shift (after removing a wrong +1/2 texel: floor(S) + 1/2 averages
  to S); chained 5.0 after 5 steps, 14.5 after 25 (bilinear at 4x vs nearest at 1x, and the brightening clips per
  RGB channel instead of in YCrCb -> slightly greyer letters; a YCrCb fade shader would fix it).
- Web surface 0011000B (806AD2B0; tools/t3k_webfit.py, mean error 1.4-1.5 grey levels in Y / Cr / Cb): per lane a quad
  (records at $800BACF0, 8 words: 4 x (position y hi / x lo, signed half pixels about (180, 120); uv u hi / v lo,
  1/512 texel)) textured with the 16 x 16 texture at w0 (bilinear; texel i at coordinate i), u = 4 texels a lane,
  v 16 -> 32 outer -> inner, sampled at v + job / 2 (MPE0's job word at local $20100000, +1 a picture: the scroll),
  blended out = T + (dest - T) v / 32, interpolated by EDGE WALKING (per scanline along the left / right edges, then
  across) - the GPU version gives an 8 x 8 grid per quad the module's own (u, v). Lessons: the position word is y hi /
  x lo (as vector vertices) - swapped, the lanes come out reversed and look "mirrored"; signed per-pixel steps.
  Unused so far: per-lane colour pulses (triangle waves of a per-lane table + job x 128 -> $20100BC0 / A34 / A38) and a
  per-lane uvctl switch ($004CC010 vs $104CC010, lane mask from $800AF7B0): the surface pixels match without them.
  The texture is rewritten every picture (a lightsynth-style generator elsewhere): an endgame add-on would run it
  natively at display rate and higher resolution (owner 2026-11-15: later, renderer first).
- Ground truth (2026-11-15; owner: stuck comparing screenshots - build a measurement): nuance_capture_enable(2) keeps,
  at each flip, the frame buffer that picture was drawn into (the buffer of MPE0's last pixel writes; CaptureBiLinear),
  nuance_capture_frame(). NUANCE_ONLYTYPES=<types> makes the emulated drawing pass draw only those (MPE0 / MPE2
  always; MPE1 from its list-head fetch to the list end). t3k --nativecheck=F0:F1:<dir> (headless): every complete
  picture drawn natively at 360x240 on the previous picture's ground truth, against its own: mean error per picture
  and truth | native | difference images. Screenshot comparisons (--shot vs --nativeshot) were not reliably the same
  picture: a scrolling texture then looks like a model error.
  First result (ONLYTYPES vectors + fill + web + zoom, T3K_NATIVE_FORCE=1, fields 2300-2955): 218 pictures, mean
  error 3.8 (level start 0.7-1.6; play ~2.8, 4% of pixels > 24 off: line edges at 360x240, a few web patches).
- Complete pictures today: menu transitions (zoom + vectors) and some menu screens (fill + vectors). Play still needs
  0011000B (web surface), 0010000D (glowing text), 00310309 / 00310209 / 0031020E / 00500005 / 00310010 / 00310108
  (small objects, particles).

### 0010000D = glowing number (module 806AF1E8; 2026-11-15)
- One mode only (jump table at local $E78 has one entry): the decimal digits of the word at $800DC240 - the score in
  play, 3000 on the title (centred: w14 = 1). Per digit a 16 x 16 YCrCbZ glyph (1 KB at w10 + digit x 1024), cell
  16 x w2 by 16 x w3 at w0 (x hi, y lo), advance 12 x w2 (a 1: 4 x w2); centring moves x by w2 x (13 a digit, 5 a
  1) / 2, y by 8 x w3.
- Per pixel (806AF7AE, the only routine in the w13 table at $E74): G = bilinear glyph at (u, v); T = bilinear 16 x 16
  texture at w11 (the animated texture) at (U + w4 x G.Cr >> 20, V + w5 x G.Cb >> 20); out = dest + (T - dest) x
  (G.Z x w6 / 2^23). Coordinates 16.16 texels, stepped per pixel / row with a quadratic term (u += du >> 8, du +=
  d2u) from the 20-word block at w12 (to local $DF0: DF0 origins, E00 glyph a pixel = 1 / w2, E10 texture a pixel,
  E20 glyph a row (E24 = 1 / w3), E30 texture a row).
- Pixel loads (ld_pz, 32-bit): Y = byte << 22, Cr / Cb SIGNED (byte - 128) << 22, Z = byte << 24 (4x the others) -
  two wrong guesses cost time here; the NUANCE_PCLOG register dump settled both.
- Quirks kept: the reciprocal gives 0x7FFF for 1/2 (not 0x8000); each digit steps the texture origin by all the
  advance before it and writes it back (digit 3 of 3+ steps twice). Not kept: the row skip 806AF5A0 skips one row
  fewer than the band offset, so a band starting inside a cell repeats a row (sub-pixel; ours is continuous).
- Verified: tools/t3k_textfit.py (NUANCE_SPANLOG=0010000D + ENTRYLOG / ENTRYDATA, logs\t3k\gt\span.sh): 2616
  entries (title 3000 + score), 3.77M pixels, mean error 0.37 a channel, none off by > 3. Native: per digit a
  premultiplied RGBA image (2 samples a game pixel, 1 for cells > 64 px; the content is smooth), rebuilt per picture,
  uploaded once per picture, drawn as a blended quad. --nativecheck 2300-2955 with 0010000D added: 218 pictures,
  mean 3.84 (score box 4.7, lines crossing it included).
- PCLOG on this module needs NUANCE_NOJIT=1 NUANCE_AOT_SKIP=806AF (AOT modules are named by the chunk they start at,
  806AF2E8..806AF6E8 overlap the routine); the emulated timeline then slips (picture 775 by field 2960, not 975),
  so compare registers with spans from the SAME run. The log is ~2.4 GB to field 2960.

### The small types (2026-11-15, started)
- Shared layout: the type word's low byte names the module (08 -> 806A5FB0, 09 -> 806A8990, 0E -> 806AFA80), the middle
  byte (bits 8-14) the mode: a jump table at local $B80 (806A5FB0) / per module, dispatch at local $20300494. Find a
  mode's handler with --pc 20300494 --interp <module range>: r0 at that pc = the target.
- NUANCE_SPANLOG now covers MPE0-2 (NUANCE_SPANLOG_MPES mask, default 7; spanentry repeated when the MPE changes):
  MPE0 alone missed every type drawn outside its bands.
- 00310108 (806A5FB0 mode 1, handler 806A630A): draws only when w4 bit 0 is set - a 96-particle burst (LFSR 806A66D8
  positions, colour lerped between $B60 / $B64 by depth, perspective size 2..24). In play w4 = 00240100: nothing, in
  every logged play entry (500). Ported as "draws nothing" (bit set -> picture stays emulated).
- 00310309 (806A8990 mode 3, handler 806A8B68): particles from a list of 4-word records at w12 (16 bytes each, ends
  at x word $80000000): word 0 = x hi / y lo (signed), 1 = depth, 2 = colour (YCrCb), 3 = intensity hi / size lo
  (hi 0 -> full intensity, size word as is). Projection as the point modules: depth clamped >= $400000, k from the
  reciprocal 806A9566 of (depth + $800000) x $C000, position = ((x << 16) + camera x 8) x k >> 19 + (180.5, 120.5)
  (camera words at local $BE4 / $BE8), size / intensity x k >> 19, size <= 30 px; drawn by the blob routine 806A9124:
  centre = top-left + size / 2, R = size / 2, per pixel (integer corners) d^2 = dx^2 + dy^2, k = I x (1 - d^2 / R^2)
  (I 2.30; $B30 = R^2, $B34 = 1 / R^2), skipped where d >= R, out = dest + (colour - dest) x k; the box is size + 2
  wide, size + 1 high. Logged (logs\t3k\s309\pcs_span.txt, play_f3150, picture 1041): sprite calls at local
  20300578 (r4 / r5 top-left 16.16, r6 size, r13 I) next to the records (pc 203004C2: v7 = the PREVIOUS record).
  DONE: projection exact against the module's calls (raw intensity = hi << 16 exactly); tools/t3k_partfit.py.
  Ported as GPU discs (Op::kBlobs: a quad textured alpha = 1 - r^2, vertex colour = colour, alpha = I). The disc
  routine's rows sit one pixel above the corner reading (measured: y offset sweep, optimum exactly -1, columns 0):
  centres at (cx + 1/2, cy - 1/2). Residual: overlapping trail blobs - the list is rewritten by the game while
  the three renderers draw their bands, so the emulator's own result mixes two versions (the right picture's
  records fit 0.38-1.9; ~8% intensity differences between bands).
- 00310209 (806A8990 mode 2, 806A8DB4): an image as discs - DONE, tools/t3k_dotfit.py (the call list exact to
  0.003 px: count, intensity), 3.6 on its pixels.
- 0031020E (806AFA80 mode 2, 806AFC52): template rings of discs - DONE, tools/t3k_flowerfit.py (calls to 0.001 px,
  modelled literally from the code on the first try), 2.9 on its pixels.
- 00310208 (806A5FB0 mode 2, handler 806A6182): the powerup-collect burst at the claw (2026-11-16, from the owner's
  log_emulated dumps, logs\t3k\p208) - DONE, tools/t3k_burstfit.py (80 disc calls a picture to 0.0005 px, sizes and
  intensities exact): five rings of 16 discs (size 16 - 3n, radius $4000 + $3000 n, I 1/4/7/10/63 / 64, colour n of
  $8010B150) around (w0, w1, w2) with the camera at $800DAD80; disc row routine = w7 & $7F (table at local $B8C):
  0 = the plain disc blend (806A6944). Native check: 12 + 27 pictures complete over the four dumps. Seen there too:
  a bright pink flash screen whose native background is ~9% darker (254,121,254 vs 231,113,232; the open
  "brightest screens" item).
- State play_f2880 (just before an enemy explosion: 00310209 / 0031020E / 00500005 at pictures 951-962).
- Whole play pictures (no forcing, no type filter, full emulated truth): fields 2701-2960 from play_f2700: 85 of 85
  complete, mean error 2.86; 2881-2960 from play_f2880: 17 complete (2.87), 8 not (00500005); 3151-3299 from
  play_f3150: 39 complete (2.84), 10 not (00310010, the level-2 dot grid, 806A6E40).
- 00310010 (806A6E40 mode 0, 806A7012): DONE, tools/t3k_gridfit.py (318 of 318 calls exact): a 32 x 32 bitmask
  (w11) of 3D grid points walked from the middle out (rows and row words in two halves), quadratic steps (w2 / w4 per
  point, w3 / w5 per row, w13 hi / lo the first steps), each a 0031020E template (FlowerAt grid = true: start angle
  + w1 << 8, last rotation (x + y) >> 12, point added before the perspective, depth = point z, cap 30 px, at
  (192, 120)). Play from play_f3150 fields 3151-3299: 48 of 48 pictures complete, mean error 2.85.
- 00500005 ring: DONE (tools/t3k_ringfit.py, 283 entries / 9074 pixels at 0.48 / 0.54 / 0.62, none off by > 3;
  native 3.7 on its pixels; the explosion stretch 2881-2960: 25 of 25 pictures complete, 2.80). The model: centre
  w0, rx / ry = (w4 hi asr 8 / w4 lo << 8) x (w1 >> 16) >> 16, d = col^2 / rx^2 + row^2 / ry^2 (16.16, via
  2^28 / r^2 >> 12), t = w1 & $FFFF, band 1 - 2t < d, k = (t - |d - (1 - t)|) x ((w5 unsigned >> 2) / t - 1) / 2^30,
  T = bilinear 16 x 8 texture at w12 at u = |w13 hi x col + w2|, v = |w13 lo x row + w3|; each row computed for
  the left half (col = x - cx) and mirrored, strength included (col = cx - 1 - x). Ported as a premultiplied image
  per ring (2 samples a pixel, col = -(|X - cx| + 1/2) at pixel centres). Notes from the decode, kept: 806A7DC0, set-up
  806A7EDA: v1 = w0..w3 (r4..r7), v3 = w4..w7 (r12..r15); r11 = w1 >> 16 (radius px), r28 = w1 & $FFFF, r8 = w5 >> 2,
  r18 / r19 = w4 hi / lo x radius; per pixel (806A81D0..806A82A8): T = bilinear lookup of the 128-word texture at w12
  (DMA'd to local $330, uvctl 104CD010) at u = |w13 hi x column + w2|, v = |w13 lo x row + w3|; d = column^2 x r17 >>
  12 + row term (r24); k = r8 x (r28 - |d - (1 - r28)|) (r23 bit 0: ring; else a disc?); out = dest + (T - dest) k,
  Z written too. Drawn as a right half, then the left half mirrored from a row buffer at local $E40, with edge flags
  in r23. Logs: logs\t3k\s505 (sp_span / sp_ent from play_f2880). Next step for it: PCLOG at local 20300790 (the
  blend) with --pix for T / dest / r7, then the set-up integers.
  spanlib now pairs a write with the latest read of that row covering it (this module reads 32 px, writes pieces).

### Step 3: the draw gate - emulated drawing off for native pictures (2026-11-15)
- Core (capture.cpp, 91189ae): nuance_set_picture_callback(cb(mpe, flips)). The first drawing fetch of a picture (MPE0 /
  MPE2: any display list entry fetch; MPE1: from its fetch of the list head $8004E040, i.e. after its pre-pass jobs
  rewrote the entries, to the end of the list) calls the frontend once; non-zero = skip: every drawing fetch until the
  flip reads the end of the list (as NUANCE_NODRAW). Reset at each flip. MPE1's compute jobs and the game logic run as
  before. nuance_gate_stats.
- Host: t3knative::BuildPictureLive walks the list from $8004E040 in live memory (nuon::Memory: system / main bus, the
  fetching MPE's local memory for the job word) - the same builders, Read() over a Mem that is either the capture's
  word map or live memory. t3k gate::Callback files the picture under flip + 1; the window's emulation thread and the
  headless native paths take it at that flip (Take). Building costs 0.26-0.82 ms a picture.
- Checks: --gate=1 (build live, renderers still draw): live == captured for 151 of 151 play pictures. --gate=2: game
  logic unchanged (the native picture 150 fields on is byte-identical with and without skipping; same flip counts).
  Cost, play from play_f3150: emulation 3.71 -> 1.56 ms a field (p95 9.56 -> 3.96) headless; in the window
  (native_view 1, native_skip 0 / 1) 4.05 -> 1.88 (p95 10.35 -> 4.50).
- Window: native_skip = 1 (default) skips whenever native_view = 1 (F7 cycles; the gate follows). --loadstate works
  in the window too.
- Native -> emulated switches build the emulated picture's feedback on a buffer that was never drawn (seen at the level
  end and the warp before their types were ported). Fixed by porting what blocked them: 007100FE (a disabled entry: low
  byte FE), 00310008 point rings (806A5FB0 mode 0: tools/t3k_ringptfit.py, the calls exactly), 00100200 textured plane
  (806A3108 mode 2: tools/t3k_planefit.py, 0.7 a channel; quirk: the pipelined loop starts each 64-pixel chunk one step
  further on). The full route (boot, menus, level select, play, level end, warp, next level; 6000 fields): only the
  boot / title screens are emulated (0000000F feedback background, 0031000E title particles); from the menus on 1503
  pictures in a row are native. --nativecheck 1500-5999: 1500 of 1500 complete, mean 3.58 (1174 under 4).
- Line widths: the tent's radius is the width byte - 0.06 (2 -> 1.94, 13 -> 12.96, 14 -> 14.2; signed profile
  centred): the bonus screen's big numbers 18-28 -> 4-8.
- Known gaps (polish, step 7): the original works in YCrCb - brightening feedback (the warp: orange in the original,
  yellow natively) and lines whose peak exceeds 1 (the bonus numbers: flat saturated bars) clip there, not per RGB
  channel: keep native pictures in YCrCb and convert with a final shader. Boot / title types (0000000F, 0031000E).

### The hardware's picture rate, estimated from emulation (2026-11-15; owner: "what is the designed speed?")
- The game moves per picture, so its speed is the picture rate the real Nuon reached. tools/t3k_hwspeed.py <perflogs>:
  per picture the packets each MPE executed (RunStats), at one packet a cycle and 54 MHz, the busiest MPE's time ->
  whole 60 Hz fields. The packet counts are real work: the same at flip_cap 1 / 2 / 3 (1.06-1.21M a picture).
- Play (levels 1-2, an explosion; play_f2700 / f2880 / f3150): MPE0 ~1.06M, MPE1 ~1.15-1.24M (the busiest: the
  pre-pass + its band), MPE2 ~1.0M, MPE3 (game logic) ~0.13M packets a picture -> 1.2-1.4 fields of compute -> every
  picture 2 fields = **30 pictures/s** (a floor: memory stalls not counted). Warp / bonus screens: ~0.8M -> mostly
  1 field, ~52-54 pictures/s.
- Memory traffic (NUANCE_FBPROBE, level 1): ~106k pixels written + ~215k read (195k of them the zoom feedback's read
  of the previous picture) = ~1.3 MB a picture. Even fully serialised at a few hundred MB/s that adds a few ms:
  ~27 ms worst case against 33 ms for 2 fields. 20 pictures/s (3 fields) would need far bigger stalls.
- So the estimate: the original ran play at **30 pictures/s** (flip_cap 2), probably dropping to 20 in the busiest
  moments (the game would then have slowed down with them), 60-ish between levels. The owner plays at 2.5 (24) and
  finds enemies "a little fast" - at odds with 30; the feel may come from the controls (the claw) or the hardware
  was slower than this floor. Confirm with a 60 fps capture of a real player (count distinct pictures a second;
  time a flipper's climb / the level-start zoom).

### Stage 3 next: speed (the critical path - more rounds a field = faster, smoother game)
- NUANCE_PROFILE=1 (220k rounds): each MPE call ~20 ns for only 3-5 packets (MPE0-2) and MPE3 85% single-packet
  calls (the packet hack): per-call overhead dominates, not the work. ~35% of a field outside the MPE calls (loop,
  comm bus + interrupts ~7%, profiler overhead).
- Ideas, measure each: slim FetchDecodeExecute's per-call path (interrupt checks, cache lookup, TickMediaCallbacks);
  bigger blocks / block chaining for MPE0-2; the T3K packet-hack range; detect MPEs spinning on a mailbox / comm bus
  and skip ahead (must keep the interleaving the game depends on); profile-guided build. Target: 220k rounds in
  ~10 ms, then raise the default toward hardware speed.

### Native renderer status (end of 2026-11-15; the ordered to-do - start here)
Done (each verified; details in the sections above): research (picture = display list + feedback; 18 types / 18
modules; cost without emulated drawing 3.3-3.6x less in play, ~6x in menus), vector shapes 00710001, fill 00000300,
zoom feedback 00300004, web surface 0011000B, t3k.exe native view (F7) + interpolation (F6) + mixed-mode seeding, ground
truth check (--nativecheck: 218 play pictures with those four types, mean error 3.8).
Next, in order (owner 2026-11-15: tools first, then the work; estimate steps 2-4 ~5-8 sessions):
0. TOOLS (done 2026-11-15; how to use them: CLAUDE.md "T3K native renderer"):
   a. Before guessing an instruction's semantics (pixel load formats, mul_p / mul_sv shifts), read Nuance's own
      implementation in third_party\NuanceResurrection (two wrong guesses cost a third of the 0010000D session).
   b. DONE (2026-11-15): t3k save states. Core (llamasoft-nuon 6f65735..): retro_serialize walks RAM (16 + 8 MB),
      flash, BIOS heap, env, the host audio ring (its fill gates the audio interrupts: game-visible), MPE0-3 (registers,
      control, local memory), video channels + what VidConfig derived, media callbacks, syscall waits, emulated time,
      timer schedule, parked loops (src/savestate.h: one walk for save and load, section tags). On load every code
      cache is flushed, instruction RAM re-hashed, AOT re-validated. 62 MB a state (MPE local memory arrays dominate
      after RAM: trim for run-ahead). t3k --savestate=F:file (after field F) / --loadstate=F:file (load, continue at
      F + 1 without emulating the fields before). tools/t3k_statediff.py: two states compared by section / field /
      address. Properties: plain runs are deterministic (identical states); two loads of one state replay identically
      (pictures + states through 50 fields); a load is NOT bit-identical to the uninterrupted run (a fresh process has
      cold code caches, so interrupts land between different blocks: MPE3 rzi2 differs after one field, RAM after ~10)
      - fine for investigations, revisit for run-ahead (same process, warm caches: maybe skip the flush). The first
      picture after a load in a fresh process is drawn with stale GL texture state (two half-size copies), exact from
      the next field. Play states: logs\t3k\states\play_f2700 (level 1 start), f2900 (level 1 play), f3000 (level 2,
      green), f3150 (busier play), each with a .bmp. Payoff: the 0010000D register log from play_f2900 under NOJIT
      takes 1.5 s / 5 MB (was 56 s / 2.4 GB from boot, and the slipped timeline).
   c. DONE: NUANCE_PCLOG=pc,pc.. + NUANCE_PCLOG_PICTURES=P0:P1 + NUANCE_PCLOG_MPE + NUANCE_PCLOG_PIX (v0-v7 in byte
      units); NUANCE_AOT_SKIP=<lo>-<hi> library range (core 40737de). From a play state: 1.7 s, 4.7 MB.
   d. DONE: tools/t3k_spanlib.py (load_entries, entry_for, events = Entry / Pixels / Pc, fit(prepare, predict),
      bilinear16, as_loaded = what a pixel load puts in a register) + tools/t3k_logrun.py (one command: state, fields,
      span / entries / pc / interp / pictures / pix / only / shots / nativecheck). t3k_textfit.py moved onto it (same
      result: 2616 entries, 0.37); webfit / linefit / zoomfit still have their own parsers.
   e. DONE: T3K_CHECK_MASK=<span log> with --nativecheck: also the error over just that type's written pixels (MPE0's
      bands; a span log from an earlier run off the same state works - loads replay identically). First use showed
      0010000D's port at 9.2 there: all of MPE0's pixels of the score lie below a band start inside the cell, where
      the original repeats a row (806AF5A0); the Python model with the quirk fits 0.38, without it 2.5 / 1.5 / 4.7 -
      the port's continuous rows are deliberate (no seam).
   f. DONE: tools/t3k_annotate.py <disasm>: each line's local pc (for PCLOG), entry words, the standard prologue
      helpers (identical in every module: +$54 DMA write, +$A4 DMA read, +$24 wait, ...), call targets as lib addresses.
1. DONE: 0010000D glowing number (section above).
2. The small types (survey 2026-11-15, play fields 2300-2955, 217 pictures: 00310108 in 127 - tiny points;
   00310309 in 126 - particle dots; 00310209 in 26 - enemy parts; 0031020E in 24 - particle clouds; 00500005 in 8 -
   explosion rings; logs\t3k\gt\small_diff.png = what each draws, skip-run differences):: 00310309 / 00310209 (806A8990: object path, enemy parts), 0031020E (806AFA80), 00500005
   (806A7DC0: explosions), 00310010 (806A6E40: level-3 dot grid), 00310108 (806A5FB0: points; often draws nothing).
   Then play pictures are complete (check with --nativecheck WITHOUT T3K_NATIVE_FORCE).
3. Switch the emulated drawing off for complete pictures (the speed gain). The core must decide before drawing (scan
   the list at the head fetch for ported types) AND gather the data itself (today the capture records what MPE0
   fetches while drawing: follow shape pointers / palettes / web records + texture itself, or copy the runtime-shape
   region of main-bus RAM). MPE1's compute jobs (pre-pass) must keep running. Measure with the Deck defaults.
4. Interpolation for everything: matching by type + shape + nearest (3D-placed objects: game-space w8 / w9 before the
   pre-pass), velocity prediction, step what's ambiguous; continuous feedback is done.
5. Save states (retro_serialize) + run-ahead to cancel the interpolation delay (one game step, up to 50 ms at 20/s).
6. DONE 2026-11-16: menus / title / attract / boot. 0031000E title dot field (806AFA80 mode 0, handler 806AFECA;
   tools/t3k_fieldfit.py, 1424 of 1424 calls, colour pixel exact), 00310409 title wave (806A8990 mode 4, handler
   806A8C48: a fixed 96 + 32 dot shape at w0, LFSR jitter x w8; tools/t3k_wavefit.py, 128 of 128 calls; check 0.06),
   0000000F ripple feedback (806B0B30: per pixel A = (w10 << 17) + x w4, B = w11 + y w5 + A, bits 16-21 index
   T[i] = sin(2 pi (i + 1) / 64) px at local $201004C0 -> dx, dy; bilinear previous picture at (x + dx, y + dy);
   out = s + (w2 - s) w7; tools/t3k_ripplefit.py 0.33, shader ycc_ripple). Full route from boot (fields 60-9000,
   menus, play, warps, bonus, attract): 2980 of 2980 pictures native, mean error 2.78. Also 2026-11-16: YCrCb
   pipeline (step 7's fade shader, done), point-ring sign fix; open: orange patches on the brightest warp / bonus.
6b. DONE 2026-11-16: clarity - sharp vectors (t3k_native.cpp kSegs: items <= 100 game px across or averaging
   <= 12 px a stroke get a core at sharp_core x the width + the original line at sharp_glow; web / streaks untouched).
   Not yet: the glowing text 0010000D (images), discs / particles, the thick title logo (long strokes).
6c. DONE 2026-11-17: orange patches on the warp / bonus (see "Orange patches" below). Open: high_res has no native
   support (pictures are built from transformed entries) - native view should force 360x240 or high_res be dropped.
### Orange patches (2026-11-17, fixed)
- The zoom module (806A5280, warp row function = path A at 806A58DC, picked by w7 & 7 from the table at $20100EA0)
  samples BILINEAR (4 x ld_p (uv), ru / rv 14-bit fractions), then out = w11 + (s - w11) x f with w11 = 10 80 80
  (neutral), stored by st_p with chnorm (floor). tools/t3k_flatfade.py: on flat source areas the hardware is exactly
  floor(n + (s - n) f), no bias.
- Cause: floor about neutral is one-sided under a brightening fade - chroma 129 x 1.2 floors back to 129 (stable),
  127 runs away (126, 124, ...). The emulation is bit-exact, ours is not (+-1 on a third of pixels at window
  resolution), so every near-neutral pixel we put at 127 seeded an orange patch the warp grew; the window made it
  worse by flooring at every present (fade^step on 8-bit textures).
- tools/t3k_warpchain.py (CPU chain from the span log, models): floor drifts orange -> yellow -> green, truncation
  toward neutral stays pink like the original (orange 0.1-0.7% vs 80%).
- Fix: feedback textures RGBA64_FLOAT (created with colourspace sRGB - SDL's default linear colourspace for float
  textures wrecked the YCrCb values: all green), the fade exact between presents and rounded once per game picture
  (Renderer::Draw step_ends; t3k_main passes it when native_step_left runs out): truncate toward neutral when f > 1,
  the hardware's floor when f <= 1 (stable; play unchanged, nativecheck play 2.38). 8-bit fallback rounds in between.
  Window warp, orange share per present (2 runs each): old mean 7%, peak 47%, 68 presents > 5%; new 1%, 12%, 12-16
  (the original has pale peach patches too). Warp single steps vs the hardware 5.43 -> 5.67 (expected).
- Test switches: T3K_FADE_FLOOR=1 (the old floor every draw), T3K_FEEDBACK_8BIT=1, T3K_FEEDBACK_LOG=1.

7. Polish: YCrCb fade shader for the zoom's brightening phase; wide screen; CRT looks; the web module's per-lane
   colour pulses / special lane mode if any picture needs them.
8. Endgame add-on (owner: later): the web texture's lightsynth generator natively (find its writer with NUANCE_WATCH).

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
Open: tune the spinner feel with the owner; clones (tempest1..3, temptube) not selectable yet.

### t81 vectors drawn by us (2026-11-17)
Owner: on the Deck the picture is small in the centre; wants zoom, wide screens, readable vectors (width / glow).
- Core (third_party/mame-libretro, branch llamasoft-mame): src/osd/libretro/window.cpp copies the frame's LINE
  primitives when the OSD fetches them; export llama_mame_lines(count, w, h) -> 9 floats a line (x0 y0 x1 y1 in 0..1
  of the monitor, MAME width, a r g b; a = beam intensity 0.63 / 0.75 / 0.88 in Tempest).
- src/t81_vectors.cpp: beams = 3 quads (cap, body, cap) over a radial profile; glow pass ADD (custom blend ONE/ONE),
  core pass MAXIMUM (no hot spots where lines meet); white-hot core; width follows intensity. Settings vector_renderer
  framing aspect line_width glow glow_size brightness width_by_intensity hot_core (menu DISPLAY). MAME rasterises
  640x480 while ours is on (fallback only; 0.79 vs 1.14 ms a frame).
- Framing: arcade (whole monitor), fill (default, owner 2026-11-17, with aspect round; x 0.08..0.885, y 0.13..0.95: all the game uses), wide: in play
  the web fills the window and the HUD (leading lines in boxes x<0.37 / 0.37..0.66, y 0.12..0.2225) slides to the top
  corners. Play = left HUD drawn and < 25 short strokes below y 0.9 (attract credits), hysteresis 10 / 30 frames.
  Web box = bbox of world lines unchanged 10 frames (>= 30 lines, >= 0.3 x 0.3) + 0.06 margin, held while nothing
  is static (fly-in, fly-through, warp). No room beside the web (flat level 4 web): web + HUD band, HUD in place.
- Tests: --linedump=F0:F1:csv (T81_LINEDUMP_STEP), --autoplay=F (fire pulses + sweeps; reaches level 4, game over),
  --frameshot=F:bmp (window, repeatable), --winsize=WxH, --set=k=v, --loadstate in the window. Shots logs/t81/wide.
- Open: owner's look tuning on the Deck; phosphor persistence (trails) not done; levels 5-16 not seen by the tests.

## Claw feel study: TxK and Tempest 4000 (planned 2026-11-15, to do 2026-11-16)

### Results 2026-11-16 (done: TxK measured; T3K stick modes momentum + stiff; owner keeps STIFF)
- TxK claw in memory (Vita3K core export llama_vita_read / llama_vita_next_range, branch llamasoft-vita; txk.exe
  --memdump / --memseq / --peek with p>o pointer syntax): $810EC514 = floor(position), $810EC7F8 = round(position)
  as lane ints (24-lane level 1 web; together: the claw within half a lane, frame exact). $810E4970 = the game's
  processed stick (+-0.46 full). No sub-lane float found (the 810E6850>24 object and heap matches were camera values).
  Route: Start 900, Cross 1300, play ~1800; hold Cross (fire) from 1990 or the claw dies within seconds.
- TxK feel (tools/claw_fit.py, fitted to logs/claw/txk/run2.txt): the stick is a FORCE - acceleration ~ u^0.8
  (deadzone ~8%), full stick 0.0139 lanes/frame^2 (top speed in ~21 frames); top 0.2875 lanes/frame = 17.3 lanes/s,
  the same at half and full stick (a cap); pushing against the motion turns round in ~4 frames; on release friction
  x0.9435 a frame (half-life 0.2 s) + a weak spring to the lane centre: ~4-5 lanes of glide from top speed.
- T3K: stick_mode momentum = that model (mo_* settings, menu sliders). Owner: T3K is so fast on small webs that the
  glide overshoots the target - asked for the opposite, T2K's stiff handling.
- T3K stick_mode stiff (now the DEFAULT; st_* settings): T2K's lr law in real time with the owner's T2K tuning
  (24.6 lanes/s = 0.41 a frame, 0.33 s smoothstep ramp from 5%, curve 1.5, deadzone 0.2, 0.05 s ease-off), then the
  claw settles on a lane centre - the next one in the direction of travel once past it by st_snap_ahead 0.2 (a 4-6
  frame tap = exactly one lane). Owner 2026-11-16: "feels good", testing more.
- Shared driver (ClawControl::Momentum): our model runs per field; each picture the game is asked for (model - game
  position) + claw_lead (0.5) pictures ahead via the speed scale. The d-pad is applied a picture LATE: on a direction
  change that late picture is held still (scale 0) - this removed most overshoot. The claw signature blinks
  (flags 80020401) for a field at a closed web's wrap: state survives up to 8 absent fields.
- Tools: t3k --clawlog=F0:F1:file (field, stick, position, velocity, scale, picture, input, flags); tools/claw_fit.py,
  tools/claw_compare.py (same script, several games / modes), tools/txk_memsearch.py / memview / memtrace / memmove.
- Found: t3k headless --loadstate loses pad input (neither stick nor d-pad move the claw after a load) - use the
  boot route (start 400, a 900..2500; claw controllable from field ~3487).
- Open: T2K could get the same lane settle; Tempest 4000 not measured (needs the owner + recording); does TxK's
  speed scale with lane count (only the 24-lane web measured); T3K's claw still moves once a picture (interpolate-all,
  step 4, if it feels steppy); a practice aid (owner: "4 or 5 seconds to get a feel before I die") - e.g. an
  invulnerable / slow-enemies test option.

### The plan as written (2026-11-15)
Owner: T3K's controls are "still strange" and need significant tuning; T2K's lr tuning was a guess (a successful one).
TxK and T4K have a distinctive claw feel ("weight") worth measuring and offering in T2K / T3K. Do this BEFORE deciding on
T3K game speed (PLAN "The hardware's picture rate": the estimate says 30 pictures/s, the owner plays at 24 and finds
it a little fast - likely partly the claw) and before interpolate-all (step 4).

Goal: per game, the claw's response to controlled stick input as numbers, fitted to our stick model (t2k.cpp /
t3k_main.cpp ClawControl, settings lr_deadzone, lr_curve, lr_ramp_start, lr_max_speed, lr_ramp_frames,
lr_release_frames), extended if they do something the model can't (momentum, overshoot, easing into a lane, analog
proportional speed). Then a "TxK feel" / "T4K feel" option (or new defaults) in T2K and T3K for the owner to judge.

What to measure (each game, both closed and open webs if possible):
1. Start: delay to first movement; speed vs time while held (the acceleration curve), at full stick and at ~25 / 50 /
   75 % deflection (does speed scale with deflection, or only the ramp?). D-pad / digital too.
2. Top speed in lanes / second (and whether it depends on the web's lane count or width).
3. Release: instant stop / ease-out (how long) / overshoot / snap to the nearest lane centre (and how fast).
4. Taps: 1, 2, 4, 8-frame flicks - distance moved (a whole lane? a fraction?).
5. Reversal: full left -> full right - does it decelerate through zero or flip instantly?
6. Long sweeps on a closed web: constant speed? any lane "detents"?
Script (the same for every game, so the results line up): rest 1 s; full right 2 s; release 1 s; full left 2 s;
release; taps of 1 / 2 / 4 / 8 frames 0.5 s apart; half right 2 s; quarter right 2 s; full right 1 s then full left
1 s (reversal); repeat once.

A. TxK (txk.exe = src/lr_host.cpp + Vita3K core; fully ours):
   - Add test switches to lr_host (as t3k's): --headless / --frames=N (fast, no window; Vita3K needs its GL context:
     a hidden window may be needed), --stick=F:x,y[:len] and --press=F:btn[:len] (scripted pad: the claw script
     above as a file, --script=<file>), --shot=F:file.bmp and --frames-dir=<dir> (every frame), --savestate /
     --loadstate (the core supports serialisation? check retro_serialize_size; else drive from the attract / a menu
     route each run).
   - Claw position, two routes (do both if the first is quick):
     (a) memory: retro_get_memory_data / a core export for Vita guest memory; find the claw angle / lane by diffing
         snapshots while moving left / right (as tools/t3k_memsearch.py) - exact, per frame;
     (b) image tracking: tools/claw_track.py - find the claw (its colour / shape near the web rim) in each frame, take
         its angle about the web centre, convert to lanes (lane count from the web). Works for any game.
   - Get into play reproducibly: a route of presses from boot (start, menus) found once by looking at shots.
B. Tempest 4000 (Steam, native; our helper xinput1_4.dll / xinput1_3.dll = src/t4k/xinput_proxy.cpp is already loaded):
   - Scripted input in the proxy: T4K_SCRIPT=<file> (or a file next to the exe) - XInputGetState returns the script's
     stick values by time (QueryPerformanceCounter from the first poll after a hotkey / from a start delay), so the
     owner starts it in play with one button. Log each poll's time + the injected value to a file (input timeline).
   - Frames: the owner records with OBS at 60 / 120 fps (or we hook IDXGISwapChain::Present in the proxy to stamp a
     frame counter / save every Nth frame - only if OBS timing proves unreliable); align with the input timeline
     (a visible marker: the script starts with a fire press - the shot flash is easy to find).
   - Claw position by image tracking (claw_track.py), as A(b). Memory reading (ReadProcessMemory) only if tracking
     is too noisy.
C. Analysis (tools/claw_fit.py): per run, lanes vs time; fit the model's parameters (and extra ones if needed);
   plots of the four games side by side (T2K / T3K today with our settings vs TxK / T4K) - an artifact page for the
   owner.
D. Apply: settings presets ("txk", "t4k") for T2K / T3K's lr mode (+ any new model terms), selectable in the Back
   menu; the owner plays and picks. Then revisit T3K game speed (flip_cap; maybe the "hardware pacing" mode from the
   picture-rate estimate) with the new controls.

Order for tomorrow: A first (all under our control): lr_host switches -> route into play -> memory search or
tracking -> measurements. Then B (needs the owner at the PC for the OBS recording). Then C, D.

## T2K performance plan (2026-11-16; owner: "save this as a plan for the end" - the Deck is over 60, good enough now)
Tools: T2K_PERF=1 now breaks a frame down (src/perf.{h,cpp}): emulate (+ the run-ahead part), input + events, picture
upload, remaster build / draw per layer, picture + hud, crt + menu, present; counters (lines, triangles, stars, dots,
sprite rects); average and worst frame per report (T2K_PERF_SECONDS, 0 = at exit), T2K_PERF_FILE=<file> to a file
(Deck: launch options T2K_PERF=1 T2K_PERF_FILE=perf.txt %command%). The first 1.5 s and --winshot frames are left
out. tools/t2k_perfsurvey.py runs 19 screens (saved states + power-on) in a window and tabulates them; results of
2026-11-16 in logs/perf/ (1280x800, owner settings with fullscreen 0).
Survey (PC, 9700X / RTX 5070 Ti): every screen ~120 fps. Averages: emulate 1.4-2.2 ms (run-ahead ~1.2 of it), our
remaster 0.05-0.1 ms (0.6 on game select: 15,700 sprite rects of text), compose 0.3-0.9 ms. Already fixed: the tunnel
bonus's dots (72-vertex fans -> textured quads, compose 6.2 -> 0.9 ms).
To do, at the end:
1. Measure on the Deck (the file above): emulation + run-ahead per frame is the expected main cost there (~2.5-3x
   the PC's); worst frames here 6-7 ms when a late frame catches up (up to 4 frames + run-ahead) - on the Deck that
   can snowball past the 11 ms of 90 Hz.
2. A ~120 ms one-off stall in "input + events" (SDL_PollEvent) in several survey runs (play-l1, melt, play-shots,
   powerup); suspect SDL's periodic controller scan on Windows. Find it, check whether it happens on the Deck.
3. Depending on 1: run-ahead 1 on the Deck (or adaptive), cap catch-up so one late frame can't cascade.
4. Small: cache the game select menu's text (15,700 sprite rects drawn every frame) as a texture.

## Collection status and open items (refreshed 2026-11-17 with the owner; the 2026-11-15 list is below as history)

### Verified on the Steam Deck (owner, 2026-11-17)
- Launcher: new look, sound stings, takes the front and the pad back after every game (Steam games too); Y = Steam
  art works on the Deck.
- TxK: T4K-style buttons good, rewritten on-screen prompts correct (tap not reported separately).
- Tempest 2000: the 60 lock is "very good, even when the screen is at 90".
- Tempest 3000: "excellent on deck now, 90 all the way, around 30-40% CPU / GPU" (native renderer + draw gate).
- Tempest 4000: music plays (launcher renames Win7-8's Tracks_xwma under Proton) - all our games and both Steam games
  run from the launcher.

### To do (owner's decisions 2026-11-17)
1. MENUS (yes - marked to do): restyle the shared in-game settings menu (src/menu.cpp, still the SDL debug font) in
   the launcher's look (glowing pixel font, colours, panels), AND make the ordering and options consistent and useful
   across the games (t2k_menu / t3k_menu / t81_menu / txk_menu): same sections in the same order, same names for the
   same things (fullscreen, CRT, smooth motion, frame rate, controls, save states, return to launcher), drop what
   nobody uses. Survey all four menus side by side first, propose the common layout to the owner.
2. COMPRESS THE T3K DISC (yes): a tool that converts the owner's Tempest 3000 .iso (743 MB, almost all raw 16-bit
   PCM in nuon.dat) to a smaller form, then ASKS before deleting the original (always ask - never delete on its own).
   Needs the core (third_party/NuanceResurrection, media.cpp) to read the new form: a map of music vs data first
   (NUANCE_MEDIALOG=<file> logs every disc read), then compressed audio (FLAC lossless ~ -230 MB; lossy more) served
   back as the same bytes / or decoded where the game reads them. Verify: game plays identically (states / pictures)
   from both forms.
   MAPPED 2026-11-17 (tools/iso_list.py, tools/t3k_datmap.py, NUANCE_MEDIALOG with an ABSOLUTE path - t3k.exe changes
   directory): the ISO = /NUON/nuon.dat 736.6 MB + nuon.n16 0.2 MB + nuon.run 5.3 MB (program + all game data).
   nuon.dat is 100% music: 19 tracks, a (start, end) block table at nuon.run +0x104340 (19 pairs, contiguous, exactly
   0..359686 = the whole file), a 16-entry level play order just before it (+0x104300). The game streams a track with
   MediaRead into $40400000 / $40600000 (2 MB halves: 2048 blocks first, then 1024 at a time). 32 kHz (measured:
   tools/t3k_musicrate.py, correlation 0.996 against a --wav recording; 1.2-7.9 min songs, 96 min in all).
   DONE 2026-11-17 (owner: Vorbis, ~256 kbps): tools/t3k_compress.py -> "Tempest 3000.nuz" 195.5 MB (26%; level 0.2 =
   ~260 kbps average, VBR 231-335 per track), then asks before deleting the .iso (type "yes"; warns about hard links).
   Core: src/disc_image.{h,cpp} (NUZ = raw sector segments + one Ogg Vorbis stream per track, stb_vorbis from the
   Vita3K tree) under ISO9660Reader and MediaRead (one image kept open). A worker prefetches the chunk after each read:
   refills 0.1 ms on the emulation thread (decoding 15-18 ms on the worker); a track's first read (32 s of music) still
   decodes on the caller, 28-43 ms here, at the title / a level start (watch for a hitch on the Deck; fix would be a
   decoded head per track). NUANCE_NUZLOG=1 prints decode times. t3k FindDisc and package_collection.ps1 prefer the
   .nuz. Verified: same disc reads, pictures byte-identical to the .iso at fields 3000 / 5990 and from a state; audio
   aligned (lag 0, tools/wav_compare.py). Play folder 704 MB; owner tested it from Steam on Windows: works well. The .iso is still there (owner decides).
   For the publish version the tool needs Python + soundfile (libsndfile's Vorbis encoder).
3. T81 ALTERNATE VERSIONS (could be neat): pick the other sets in tempest.zip (rev 1, rev 2, Tempest Tubes ...) from
   t81's menu, with our vector renderer / framing on all of them (check their monitor geometry and HUD positions -
   the wide framing's HUD boxes were measured on the parent set).
4. PUBLISH VERSION (when the owner decides to publish): a blank package without ROMs, disc images, BIOS / firmware
   (KEEP the art: owner - it is all from public sites). The launcher then has to say what is missing and where each
   file goes (per game: file name, expected checksum or size, folder), and start the games once they are there.
   Never publish without the owner's go.
   NO AOT IN A RELEASE: the AOT modules (build_aot\) are translations of the game's code, compiled into
   nuance_libretro.dll - a public core must be built without them. Measured 2026-11-17 (headless --gate=2, desktop):
   AOT off costs ~15% (play 1.26 -> 1.47 ms a field mean, p99 3.6 -> 4.3; title 1.46 -> 1.67, p99 3.5 -> 4.1; budget
   16.7), pictures byte-identical; the JIT (built on the player's machine) runs the rest. No-AOT core: CMake without
   NUANCE_AOT_DIR -> build_nuance_noaot\nuance_libretro.dll (1.6 MB vs 34.6 MB). Owner tested it on the Deck 2026-11-17: works fine -> toolsbuild_nuance.bat builds without AOT by default now ("build_nuance.bat aot" -> build_nuance_aot for experiments).
   BEFORE RELEASE (owner 2026-11-17, on hold until then): T3K compression from the launcher - t3k_compress.py ported
   to C++ (a t3k_compress.exe loading libsndfile_x64.dll, LGPL, for the Vorbis encoder; works under Proton), offered
   on the T3K panel when there is an .iso and no .nuz, with a progress bar, then a Yes / No (default No) to delete the
   .iso.
5. LAUNCHER SETTINGS SCREEN (maybe - owner unsure what belongs there): the one idea so far is an overall resolution
   option - render below native resolution, applied to every game the launcher starts (each game's own render scale /
   window size from one launcher setting). Revisit when there are more candidates.
6. T3K CHEATS (maybe): infinite lives found ($800BAC7C, verified); god mode not found yet; max level candidates
   untested. See CLAUDE.md "T3K menu cleanup, save states".
7. NEON MODE (big maybe, much later): plan below ("Neon Mode plan").

### Decided against / parked (owner 2026-11-17)
- Launcher: high scores on the panels (too cluttered), remembering the last game, a controls card - no.
- Space Giraffe "hi-res objects" not saved, TxK save states: annoying but likely hard - parked.
- Defender 2000: not right now.
- T2K performance plan (above): not needed for now (Deck fine with the 60 lock); keep it for the end.

### Housekeeping
- CLAUDE.md "Where we are" is long session history: trim it to the current state + pointers here.
- A one-command smoke test before packaging (every game boots headless to play, the launcher renders, the README
  lines come out literally) - suggested, not decided.

### History: the list of 2026-11-15
(2026-11-15, after the owner's first Steam Deck test; items 1, 2, 5 (T3K), 6b and 7 are done since)
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
