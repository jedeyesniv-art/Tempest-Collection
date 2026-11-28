# Tempest Collection on PC (started as "Llamasoft on the Jaguar - Tempest 2000")

Owner's project, started 2026-11-08 (same toolchain and spirit as Llamasoft on the ST, C:\LlamatronRecomp). Goal: the
best version of each game - sharp high-res graphics, high frame rates, Xbox-pad stick controls, QoL; all modes
first-class. Since 2026-11-13 a COLLECTION, all six started from one launcher (build\tempest.exe =
TempestCollection.exe): Tempest (1981 arcade, MAME core), Tempest 2000 (Jaguar, Virtual Jaguar core + our native
renderer), Tempest 3000 (Nuon: Frogacuda's native reconstruction + our GPU renderer), Space Giraffe and Tempest 4000
(the player's own PC copies, patched by small helper DLLs), TxK (PS Vita, Vita3K core).

**Details live elsewhere:** NOTES.md = every feature's findings, measurements, test switches and history (moved out
of this file 2026-11-27; search it before re-deriving anything). PLAN.md = the to-do list and plans ("Native T3K:
next", "Collection status and open items", "Ideas from Frogacuda's native T3K"). REMASTER.md = the T2K renderer.

## Rules
- PUBLIC repo: https://github.com/jedeyesniv-art/Tempest-Collection (origin, main; release v1.0.0). PUSH AND RELEASES
  ONLY WHEN THE OWNER ASKS. Commits use the GitHub no-reply email.
- Never in git or a release: game images / ROMs / BIOS / Vita firmware / save states / owner userdata / dist\ / art\
  source pictures. Jeff Minter has blessed work on these games: game-CODE reconstructions (native T3K) may go in the
  repo; game data never. The Nuance core stays built WITHOUT AOT (the AOT modules are translated game code).
- Credit native T3K to **Frogacuda** (never his real name), linking https://github.com/Frogacuda/OpenMinotaur.
- Sources are in the main repo: vendored cores (third_party/virtualjaguar, NuanceResurrection; changes committed here);
  MAME + Vita3K are fetched git clones with our commits as third_party/patches/<core>/*.patch - re-export after a
  new commit in them (package_collection.ps1 warns). Never split cores into separate fork repos (owner was criticised).
- Scripted runs ALWAYS use --userdata=logs\... (never the owner's saves). Owner bug reports with a save state: copy it
  into logs\ first and test from the copy.
- OWNER'S PLAY FOLDER: C:\JohnGames\TempestCollection (hooked to Steam, played most; the owner copies it to the Deck).
  "Refresh / update the play copy" = copy the changed exes / DLLs from build\ (tempest.exe -> TempestCollection.exe)
  with a dated .bak of each replaced file; NEVER touch its userdata; then a quick run from it with --userdata=logs\...
  dist\TempestCollection is only packaging output.
- Defender 2000 moved to C:\LlamatronRecomp (2026-11-22; D2K.md there). Core fixes in virtualjaguar may belong there too.

## Working here (pitfalls that cost time)
- Python edit scripts: write them with the Write tool and run `python -I <file>` (bash heredocs mangle backslashes).
  README.md is not pure UTF-8 (a cp1252 dash): edit it as latin-1 with newline=''.
- From bash give exes forward-slash paths; check file times when a fix "shows no change" (stale screenshots).
- The exes are GUI programs: capture their stdout with PowerShell Start-Process -RedirectStandardOutput (a plain `>`
  gets nothing). Time a single run before starting a batch; put a timeout on every run; give status in long loops, and
  after 2-3 failed guesses build a measurement instead.
- Ask before spawning subagents (token cost). Window-only bugs hide from headless shots (the owner's real screenshots:
  Videos\Captures).

## Layout (details: NOTES.md "Collection, launcher and build")
- src/launcher.cpp (+ launcher_view / launcher_sound / steam_shortcut / t3k_compress): the launcher. Settings in
  userdata\launcher_settings.txt (Back opens its menu).
- src/main.cpp + host.cpp + t2k*.cpp: t2k.exe (libretro host for Virtual Jaguar, remaster renderer, stick hook).
- src/t3k_main.cpp + src/t3k_runtime/ (game/ = T3K's code reconstructed, original addresses in comments;
  native_live_host.h = the frame loop, input, nudge assist) + t3k_native.cpp (GPU renderer) + t3k_menu.cpp: t3k.exe.
- src/lr_host.cpp: libretro host for DLL cores (t81.exe = MAME tempest_libretro.dll, txk.exe = vita3k_libretro.dll).
- src/sg/d3d9_proxy.cpp, src/t4k/xinput_proxy.cpp: the Space Giraffe / Tempest 4000 helpers (32-bit DLLs).
- Shared: src/menu.cpp (every program's settings menu; a game registers its page), pixfont, crt, controls (remapping),
  settings, volume (even loudness), runlog (--from-launcher logs to userdata\logs\<game>.log).
- third_party/: virtualjaguar, NuanceResurrection (vendored), mame-libretro, vita3k-libretro (clones), SDL 3.4.16,
  libsndfile, strawberry (portable Perl + MinGW for the core builds). reference/tempest2k = mwenge's T2K source.
- Game files (never in git): `Tempest 2000 (1994).jag`, Nuance_067\Tempest 3000.nuz (compressed disc; .iso also read),
  tempest.zip (MAME, merged set), game\PCSB00494 (TxK), vita_firmware\.

## Build / test
- `build.bat` (clang + Ninja in the VS 18 env) -> build\ (tempest.exe, t2k.exe, t3k.exe, t81.exe, txk.exe + tests).
  Cores: tools\build_nuance.bat, tools\build_vita3k.bat, tools/build_mame.sh, tools\build_sg_helper.bat,
  tools\build_t4k_helper.bat; shaders: tools/build_shaders.py (HLSL -> src/shaders/*.dxil.h).
- `ctest` in build\ (4 T3K tests). SMOKE TEST before packaging: `python -I tools/smoke_test.py [--only ...] [--dist]`
  (~1 min; logs/smoke/sheet.png). Its "t3k play" can read 9.8% lit vs its 10% rule on a normal play picture.
- T2K headless: `build\t2k.exe "Tempest 2000 (1994).jag" --headless --frames=N --shot=F:f.bmp --press=F:btn[:len]
  --userdata=logs\ud` (--set=remaster=0 for full pictures). T3K: no headless mode - `t3k.exe --check
  --fine-check-limit=N` plays a scripted game in a small window (~35 s per 600); --menu=all, --winshot=N:f.bmp.
  Launcher: --select=N --press-start --shot=f.bmp, LAUNCHER_DRY_URL=1 (prints what it would start).
- Packaging: tools\package_collection.ps1 [-SkipBuild] -> dist\TempestCollection (the full play folder, never publish);
  -Release -> dist\release + dist\TempestCollection-<date>.zip (no game files; ends with a check that refuses them).

## Where we are (2026-11-27)
- 2026-11-28: **v1.3.0 released** (native T3K on main, pushed). Native T3K = native T3K = Frogacuda's reconstruction (initial 2026-11-25,
  stable checkpoint 2026-11-26, final revision R3 2026-11-27; as received = local branches frog-stable / frog-r3). R3
  fixed the leftover web and the bonus rounds (owner tested). His work is finished; T3K is ours from here.
- Done 2026-11-27: T3K nudge assist (a tap = one lane, hold 0.08 s to sweep; owner: "perfect"); the launcher runs
  Space Giraffe / Tempest 4000 copies from outside Steam (Amazon / GOG; untested with a real copy - wait for reports).
- Next (PLAN.md "Native T3K: next"): general performance; before a release: README (T3K paragraphs), credit Frogacuda
  + OpenMinotaur in the README and the launcher's MORE LLAMASOFT list, merge t3k-native, package + smoke test, Deck.
