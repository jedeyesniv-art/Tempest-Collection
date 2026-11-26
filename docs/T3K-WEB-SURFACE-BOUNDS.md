# Stray purple web panel: geometric surface bounds

Directly selecting Web 9 reproduces a large textured panel detached from the
web. A matched gameplay capture at host frame 3758 shows it before the fix and
not after it. The motion-blur model and all gameplay code are unchanged.

## Cause

The geometry worker writes one quad per geometric segment into the scratch
stream starting at 0x800BACF0. The active web definition's word at +4 stores
the geometric count in its high halfword. For Web 9 it is 0x000F0000: fifteen
surface segments. The logical lane count is a different field and must not
be substituted for the geometric segment count.

The renderer previously tried to read a count through a drawing-MPE local
context. That context is absent from the native renderer's memory view:
0x20100020 reads zero. Consequently it fell back to scanning up to 32 records
until the first all-zero record. The scratch stream is not cleared when a
smaller web replaces a larger one. Selecting an open web after a sixteen-
segment preview therefore leaves nonzero old records beyond its actual end.
The floating panel is one of those stale records, repeatedly drawn as if it
belonged to the current web.

The earlier context-chain count also used logical lane bits at definition +40,
which can differ from geometric segments on open webs. Both issues are fixed.

## Change

Read the geometric count through a valid drawing-MPE context when available;
otherwise use the canonical active definition pointer at 0x800AF700, also used
by level selection and game sessions. In the reproduced native case it points
to 0x800AF788. Accept the producer's supported 1..16 segment range and bound
surface decoding to exactly that count.

Known bounds also allow all-zero degenerate records inside the valid range;
a zero record must not hide later legitimate segments. The old sentinel path
is retained only when neither definition source is available, for incomplete
legacy captures. No game memory is cleared, and no faces are removed by colour,
position or level number. Both CPU-mesh and analytic GPU paths receive the
same correctly bounded geometry.

This is a shared-renderer fix. It does not establish that the original NUON
software rendering has the same bug. The inherited emulator replacement
renderer can exhibit this failure when its view lacks the expected context.

## Verification

- Exact Web 9 level-select/start reproduction: before/after screenshots in
  research/tempest-profile/runs/web9-panel-before and web9-panel-after.
- All fifteen directly selectable starting webs complete bounded native runs.
  Counts include fourteen-segment Web 6 and fifteen-segment Webs 7/8/9/10/11/14,
  as well as the sixteen-segment closed cases.
- A poisoned-tail fixture keeps a stale sixteenth panel in RAM but excludes it
  from a fifteen-segment web. The same panel remains visible when sixteen
  segments are declared. Both GPU and CPU-mesh render paths are checked.
- A degenerate zero-valued first quad does not terminate a known valid range.
  A drawing-MPE context fixture verifies the preferred source and correct field.
- All 32 original-frame GPU/memory replays still match, including rollback,
  framebuffer-independence and read-only decoding checks. Four CTest targets
  and the full renderer suite pass.

## Build and follow-up patch

Play-Tempest-Web-Fix.cmd launches t3k-webfix.exe with the current motion-blur
trial and existing player preferences. The previous trial executable is retained.

A renderer-only follow-up patch is in
`deliverables/tempest-web-surface-fix/tempest-web-surface-bounds.patch`.
Apply it after the frozen native integration patch. It contains no high-rate,
blur or controller research changes. The patch has been checked against the
frozen source and applied/verified in an isolated copy. The original frozen
native patch and ZIP remain unchanged.

The reproduction also passes at original 24 Hz and with the CPU web-mapping
path forced. A longer scripted Web 9 run reaches the original name-entry flow
without runtime errors. These logs and screenshots are under
`runs/web9-panel-reference24`, `runs/web9-panel-cpu-web` and
`runs/web9-panel-long`. Aggregated direct-web results are in
`web-surface-bounds-validation.json`.
