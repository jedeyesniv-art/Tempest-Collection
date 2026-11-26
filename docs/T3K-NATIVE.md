# Native Tempest 3000 integration

**Stable follow-up:** see [T3K-STABLE-HANDOFF.md](T3K-STABLE-HANDOFF.md)
for the cumulative patch, current default build, timing modes and limitations.
The original integration notes below describe the earlier 24 Hz handoff; where
they differ, the stable follow-up takes precedence.

This contribution replaces the T3K hybrid Nuance runtime inside Tempest Collection.
The launcher still starts `t3k.exe --from-launcher`. The other games keep their
existing backends. No second launcher or competing release is introduced.

The native program runs reconstructed game functions for boot, menus, scene and
processor coordination, preparation, gameplay, collisions, callbacks, effects,
and NISE audio. It hands complete display lists to the collection's GPU renderer.
It never loads Nuance, executes NUON instructions, or uses captured state to boot.
The player's disc supplies the original executable data and assets.

## Build and apply

The accompanying patch is based on collection commit
`47bdc7a0e3c12d71ff99258b0e96b55875ed8fe0`. Apply from the repository root:

```powershell
git apply --check tempest-native-integration.patch
git apply tempest-native-integration.patch
```

Use the collection's normal `build.bat` and packaging flow. T3K no longer requires
`tools/build_nuance.bat`, `nuance_libretro.dll`, or `system/nuance`. Those source
and research tools can remain for reference validation. Packaging no longer
copies the unused DLL or BIOS replacement.

For a standalone source check with Visual Studio and SDL available:

```powershell
cmake -S . -B build-native -DTEMPEST_T3K_ONLY=ON
cmake --build build-native --config Release --target t3k t3k_checkpoint_test t3k_controls_test
ctest --test-dir build-native -C Release --output-on-failure
```

SDL defaults to `third_party/SDL`; `-DTEMPEST_SDL_SOURCE_DIR=<SDL source folder>`
allows an existing SDL checkout. This does not require any other game's core.
The executable still needs the player's T3K image to play.

## Settings and controls

Existing `userdata/t3k_settings.txt`, `t3k_highscores.txt`, launcher master volume,
and even-loudness settings are used. Existing keys and comments remain on disk.
Default userdata is beside the executable, with `--userdata=<folder>` supported.
Disc discovery retains the launcher's folders and prefers `.nuz`; plain
2048-byte-sector `.iso`, `.mdf`, and `.img` are also supported. CHD is not a
supported disc reader in this native backend.

| Setting or option | Native behavior |
| --- | --- |
| Play/menu/warp speeds and ramp | Retained; defaults 21/30/60 pictures per second and a 0.5 second ramp. These control original logic-picture steps, not a new 120 Hz simulation. |
| Resolution, widescreen, vector core/glow/size | Retained on the GPU renderer. |
| Optional smooth motion | Retained as presentation interpolation. It does not generate logic ticks or change game movement rules. Existing settings retain their choice. |
| CRT+ and its tuning | Retained through the collection's CRT pipeline. |
| Volume and even loudness | Retained through the shared collection volume helper; original stereo mixing is native NISE at 32 kHz. |
| Fullscreen and vsync | Retained. `--winsize=WxH` starts windowed; `--novsync` disables display sync. |
| Controller selection and `bind_*` mappings | Retained with hardware NUON button codes instead of emulator RetroPad codes. |
| Infinite lives and persistent high scores | Retained. Loading a historical state does not replace the persistent score file with that state's history. |
| Five Save/Load slots | Retained with separate native files; see below. |
| Frame-rate display | Reports logic pictures and actual presentation rate. |

Gamepad mode sends signed native NUON analog input, plus the original digital
buttons. Defaults: south/A fire, east/B zapper, right shoulder/R jump, Start start,
left shoulder/LB NUON. The other physical buttons retain their native C-button functions.
Existing remaps, including a previously assigned trigger for jump, remain valid.
Back, Guide or Escape opens the collection overlay, as in every collection game
(2026-11-25: Back did nothing in play as NUON, and Steam keeps Guide on the Deck).

Select **Mouse / trackball** in the same overlay for the native rotary device:
relative horizontal movement turns, left click fires, right click jumps via the
NUON rotary path, and middle click fires the zapper. Wheel scrolling is not
rotation. Keyboard arrows/Enter remain usable for menus. The two device identities
are selected by this toggle, rather than OR-ing analog and spinner capabilities.
Keyboard gameplay also retains Z fire, X zapper, E jump, Tab NUON, Q L, J/I/K/L
C-buttons, arrows and Enter.

F11 toggles fullscreen, F5 reloads settings, F4 toggles rates, F6 toggles optional
interpolation, and F7 switches the two presentation modes.

### Explicit migrations

- `native_view=1` remains Remaster. `native_view=0` becomes **Original resolution
  (native)**: the complete GPU picture at 360x240, original line styling and 4:3.
  It is not the old Nuance software framebuffer and does not claim pixel identity
  with it. Diagnostic overlay views 2/3 and software-picture blending are retired.
- Emulator work scheduling (`timing`, `nuon_rounds`, `buffer_fields`, `native_skip`,
  `run_ahead`, emulated-frame logging) is no longer a runtime option. Its old keys
  are left untouched. F3's software-frame cross-fade is retired; F6 remains the
  optional native presentation interpolation control.
- The old host-side claw models (`stick_mode`, `sp_*`, `st_*`, `lr_*`) are replaced
  by the game's native analog/digital handling. Their tuning keys are preserved
  in the file but not applied to the native controller.
- The frontend exposes one stable logical controller backed by the selected pad,
  keyboard, or mouse. Hardware discovery/BIOS configuration screens are not the
  mechanism for selecting modern devices; the overlay controls that selection.
- Emulator diagnostics such as instruction disassembly, AOT dumping, emulated
  field captures, and Nuance state files remain reference-tool concerns. This
  executable does not pretend to implement those emulator-only CLI operations.

## Native states

Slots use `userdata/states/t3k_slot<N>.native-state`, N=1..5. Existing
`t3k_slot<N>.state` Nuance files are never opened, migrated, or overwritten.

A native state stores a semantic phase checkpoint, native RAM and worker/mixer/
media state, the current phase's exact controller and clock service journal, and
the GPU presentation/feedback continuation. Loading reconstructs the original
function continuation by replaying only that phase, with device output suppressed.
It does not serialize a C++ stack or replay the entire session from boot. Normal
active gameplay checkpoints are short; a menu or transition helper left running
for a long time can produce a larger journal and a slower load.

The loader verifies reproduced RAM and native services before resuming, and
checks the restored raw floating-point feedback bytes. State archives have a
version, executable-data identity, bounded lengths, and corruption checksum.
Writes use a temporary file and atomic replacement. The maximum archive is
256 MiB; an oversized save fails without replacing the previous slot. Saves are
intended for this native format version, not portable across arbitrary future
reconstruction changes.

## Validation

The integration is built against the collection's current renderer, retaining its
newer interpolation and point-sprite work. The validated jump math, module-8 row
aliases/square blending, and disabled/terminal descriptor handling are merged in.
The GPU rasterizer remains an approximation of the original software renderer;
function/memory equivalence is a different claim from exact software/GPU pixels.

Included checks cover native controller/remapping contracts, signed axes and rotary identity, transaction/phase rollback, all four native memory banks,
audio/stream checkpoint boundaries, archive replacement, wrong-game and damaged
file rejection, and continuation service ordering.

Optional original-reference tools can be built with
`-DTEMPEST_T3K_REFERENCE_TESTS=ON`: `t3k_jump_replay` and
`t3k_whole_frame_replay`. They require independently captured reference fixtures
and the player's disc; no original game memory or fixtures ship in this patch.
Integration validation includes 128 jump cases / 9,792 original points with zero
failures (maximum position difference 0.000900 game pixels), and a 32-frame
original gameplay corpus with zero mismatches and successful rollback,
framebuffer-independence, and read-only-decoder checks.

Runtime validation exercises boot, menus, level selection, name entry, session
return, native controller and mouse paths, tools, two-player flow, jumping and
zapper input. Disk save/load checks include opening and session phases, a fresh
process loading an earlier process's state, and live interpolation/audio pacing.
Each accepted load verifies RAM, mixer/preparation/media/driver state and raw GPU
feedback. This is regression evidence, not exhaustive all-level/input coverage.

`--check`, `--check-two-player`, `--check-level`, `--check-mouse`, `--check-tools`,
`--check-combat` and `--live-check` are automated native checks. They are not the
normal launch path. `--restarttest=N` exercises the same restart action; `--statetest=N:save|load` exercises the same slot actions at
native picture N; `--winshot=N:file.bmp` captures a rendered picture. `--set=key=value`
applies an in-memory settings override for validation.

See `src/t3k_runtime/THIRD-PARTY.txt` for source attribution. The import manifest
records original source hashes for provenance, before include-path/integration
edits. No game image, BIOS, build output, user settings, or save state belongs in
the contribution patch.
