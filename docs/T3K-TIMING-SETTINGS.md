# Timing settings checkpoint

Use `Play-Tempest-Timing-Settings.cmd` from the workspace root. It starts
`build-native/Release/t3k-framerate.exe`, reuses the existing `timing-120-play`
preferences and scores, and does not override picture or timing settings.
Interpolation remains disabled by the timing build.

## Overlay

GAME no longer contains Menu animation speed in the scene-profile build.
TIMING follows CONTROLS. Values are reference ticks per second, adjustable
from 15 to 60 in whole increments. More scenes folds the less common phases.

| Timing | Default |
|---|---:|
| Gameplay | 24 |
| Menus / level select, including particle crawl and player-count selection | 24 |
| Opening title | 24 |
| Level-end travel | 20 |
| Level clear | 48 |
| Bonus sequence | 36 |
| Death, entry and all remaining phases | 24 |

The remaining rows expose shared transitions, attract/assets, game over,
name/password entry, tools/calibration, pause, the interactive warp game,
life settling, and fallback scenes. Menu-owned fades retain their parent
menu's pace rather than jumping to the shared-transition setting.
Existing `native_menu_speed` choices carry forward.

PICTURE contains Framerate: Original, 60Hz, 120Hz (default 120Hz).
Original executes exactly one full update and draw per configured reference tick,
with original per-tick accumulation, decay, paint and displacement. 60Hz displays
up to 60 frames per second while retaining 120 Hz simulation, geometry and effect
integration, exactly like the tested 60 FPS launcher. 120Hz draws each 120 Hz
simulation update. Neither high-rate option uses motion interpolation. Subpixel
geometry and GPU rendering remain available in all three modes.

Existing saved Off=0 and On=1 preferences remain Original and 120Hz respectively;
the saved value 60 selects the new middle choice. The explicit --present-hz
command-line diagnostic override still takes precedence when supplied. The main
launcher sets no presentation override, so menu choices control normal play.

Timing and Framerate edits save immediately but apply only on restart.
The active run owns an immutable profile snapshot. A conditional Restart to
apply timing button appears in TIMING while changes are pending. Restart the
game in GAME or exit and relaunch also applies them. Reverting every edit
removes the pending button.

## Validation

- Four CTest suites pass, including actual overlay navigation through every
  timing field and the framerate selector, immutable active settings, restart
  comparison, invalid-value fallback, and all 15–60 Hz rational schedules.
- Opening title supports non-divisor rates with fractional phase carry;
  27 reference ticks/s takes 560 actual updates at 120 Hz for its 126 ticks.
  Jump physics also handles the numerator when gameplay uses such rates.
- Nine default high-rate game-flow routes pass: levels 1/9/15, analog/mouse,
  two-player, level select, warp game, tools/calibration and password entry.
- Six additional mode/profile routes pass via
  `tools/run_t3k_timing_ux_checks.ps1`: Off default/custom gameplay, On custom
  gameplay, Off warp, Off tools and Off password. Complete timing traces
  verify Off always has numerator=divisions=1 and sample rate=reference rate;
  On always samples at 120 Hz.
- GPU regression: 72 full effect ticks match original direct drawing exactly,
  including after fractional history. Covers brightening and fading zoom,
  rotation, ripple displacement, fresh paint and feedback decay. Existing
  high-rate half-life, moving-trail, paint coupling, web bounds and 1,568
  display-independence checks pass.

This checkpoint preserves the earlier timing-build limitation: native save/load
is still disabled in the 120 Hz research executable, including its new Off
sampling mode. Enabling high-rate continuation states is separate work.
The original 24 Hz diagnostic executable path and its save states are unchanged.
The frozen handoff patch and supplemental web-surface patch are untouched.

A real-time Off run targeting 25 ticks/s measured 25.16 updates/s over
9.936 active seconds, with zero fine updates and a healthy audio queue.
Restart at frame 300 also resumed a fresh complete-tick run successfully.
Evidence: `../tempest-profile/timing-ux-checkpoint.json`.

## Three-way framerate selection validation

All four CTest suites pass. The settings tests cover 60Hz retaining 120 Hz
integration, pending-restart detection when changing display cadence only, legacy
On/Off compatibility and actual overlay navigation. Three game-flow smoke checks
load Original, 60Hz and 120Hz from separate saved configuration files, without a
presentation-rate command-line override; all complete 300 gameplay updates.
Logs: `../tempest-profile/framerate-selector-{0,60,1}.{log,err}`.
