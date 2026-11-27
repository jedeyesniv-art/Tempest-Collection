# Aspect/HUD update over the previous stable handoff

Apply `tempest-native-aspect-over-stable-r1.patch` to the exact previous stable
native handoff (2026-11-26 revision 1). Its cumulative source patch SHA256 is
`dcbf79103195158d50f96be6bbe7d53d3ffb3d71a9a05a2d546fc1c6f26146bd`.
That handoff was based on collection commit
`47bdc7a0e3c12d71ff99258b0e96b55875ed8fe0`.

From that patched repository: `git apply --check <patch>`, then `git apply <patch>`.
Do not also apply the cumulative revision-2 or earlier-native follow-up patches:
they overlap. This patch is for the LAST STABLE handoff, not the older initial
native-integration-only patch.

The update contains only aspect/HUD presentation, source provenance needed to
identify HUD groups, menu wording, related tests and this note. No gameplay,
timing, control, particle spawning/lifetime or other-game changes are included.
No private profiling/snapshot instrumentation is included. The normal build
commands and framerate options are unchanged.

Picture > Aspect ratio: 4:3 (original), 16:9, 21:9. The selected view fits inside
the window with bars as needed and keeps its proportions. Legacy 4:3 choices
remain 4:3; legacy wide choices default to 16:9 until an explicit new choice is
saved. Corner HUD groups retain their sizes/insets in the selected viewport.
HUD layout > Original allows comparison. Menu/title layouts remain centered.
Particle coverage was already present and was verified, not altered.

Keep the prior handoff's known limitations, including unavailable save/load in
scene-timing mode. The aspect patch is separate from any unrelated feedback
renderer correction in the refreshed full package.
