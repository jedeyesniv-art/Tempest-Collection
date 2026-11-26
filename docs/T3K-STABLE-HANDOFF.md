# Tempest Collection — stable native T3K handoff

Checkpoint: **2026-11-26**. This is a contribution to John's Tempest Collection,
not a separate collection or competing project. The isolated binary package
updates only its T3K component. No game image, BIOS, private save, extracted asset
or captured reference fixture is included.

## Apply one patch

Repository: https://github.com/jedeyesniv-art/Tempest-Collection

- `tempest-native-stable-full.patch`: cumulative replacement for the original
  hybrid backend, based on `47bdc7a0e3c12d71ff99258b0e96b55875ed8fe0`.
- `tempest-native-stable-after-native.patch`: follow-up for that exact base plus
  the earlier `tempest-native-integration.patch` (SHA256
  `f13a0cd2729487cdef0f56b38520f266e42d4db829033d771a13207ce0d6b6f5`).

Choose the applicable patch, then run `git apply --check <patch>` and
`git apply <patch>` from the repository root. Do not apply both. These include
all stable renderer, timing, precision, control and web-panel fixes; do not apply
the earlier supplemental web patch again. If your branch already contains some
of these changes or later upstream edits, merge/review the overlapping hunks
instead of forcing an apply or overwriting your own work.

## Build

The collection launcher already starts `t3k.exe --from-launcher`, and the stable
binary honors that contract. No launcher change is necessary. Original data
is found using the existing disc search and decompression path.

The normal build enables scene timing by default. The existing CMake cache name
`TEMPEST_T3K_TIMING_RESEARCH` remains for compatibility but now defaults ON. For
an old build directory, explicitly configure it ON once; `build.bat` otherwise
keeps an existing cache. For an isolated native build:

```powershell
cmake -S . -B build-native -DTEMPEST_T3K_ONLY=ON -DTEMPEST_T3K_TIMING_RESEARCH=ON
cmake --build build-native --config Release --target t3k t3k_checkpoint_test t3k_controls_test t3k_fractional_motion_test t3k_cycle_contracts_test
ctest --test-dir build-native -C Release --output-on-failure
```

SDL sources default to `third_party/SDL`; an existing checkout can be selected
with `-DTEMPEST_SDL_SOURCE_DIR=<path>`. Precompiled shader headers are included;
a normal build does not require the shader compiler. Optional standalone GPU
checks are available with `-DTEMPEST_T3K_REFERENCE_TESTS=ON`, then build and run
`t3k_timing_render_test`. Whole-frame reference replay additionally requires
private fixtures; they are deliberately absent from this handoff.

Build the other collection programs using the existing build process. The
standalone package supplies Windows x64 `t3k.exe` for the collection folder.
It statically links SDL and the C++ runtime and imports only Windows system DLLs.
Keep the package's notices with a redistributed binary. No Nuance DLL is needed.

## Included changes

Native boot, processor/scene orchestration, menus, controller input, gameplay,
events/collisions/lifetimes, preparation workers and audio replace instruction
emulation. The existing renderer has GPU effects, numbers and web mapping,
reduced redundant feedback work, broader subpixel geometry and corrected web
segment bounds (the persistent purple panel on Web 9).

Picture > Framerate offers **Original, 60Hz, 120Hz**. Original runs one complete
simulation/draw per configured reference tick, with full original per-tick effect
strength. Both high-rate choices execute 120 Hz internal updates; 60Hz caps
presentation at 60. No interpolation is used. Feedback paint, decay and
transport are adjusted for elapsed time in the audited effects.

Timing follows Controls and replaces Game's Menu animation speed row. Defaults:
24 ticks/s for gameplay and menus, 20 for level travel, 48 for level clear,
36 for bonus sequences, and 24 for remaining phases. Further scene rows are
folded under More scenes. Edits save immediately and apply only on restart;
a conditional Restart to apply timing button appears for pending changes.

Controller selection includes mouse/trackball. Mouse buttons: left fire, right
jump, middle zapper. Gamepads use the native Nuon route (A fire, B zapper, RB
R/jump by default), with remapping. Stick curve is 1–5 (default 2.5); gain applies
to the selected stick/mouse path. Existing scores and ordinary picture/audio/
controller preferences remain in their collection locations. Saved On=1/Off=0
framerate settings migrate to 120Hz/Original; 60 selects the middle mode.

## Known limits

- Save/load is disabled in the normal scene-timing mode, including Original.
  The unusable slot rows are hidden there. Existing native and Nuance state files
  are untouched. The legacy `--logic-hz=24 --scene-hz=0` diagnostic path retains
  the older continuation implementation; it is not the same as Picture > Original.
- Framebuffer effects are tuned approximations at high rate. Repeated fractional
  spatial filtering can soften trails; exact pixel equivalence to NUON is not claimed.
- Reference speeds are agreed feel targets, not an exact simulation of original
  hardware slowdown. Variable enemy-density pacing is not implemented.
- This checkpoint was tested on Windows x64/D3D12. Proton/Deck validation and a
  full multi-game collection release assembly remain John's integration work.
- No new blanket license is assigned. Preserve John/collection, SDL, stb_vorbis
  and the inherited disc-reader notices and licensing terms.

The archive's `validation.json`, file hashes and source manifests identify the
frozen checkpoint. The earlier handoff remains unchanged. Future experiments
should build on this snapshot without replacing these stable archives.
