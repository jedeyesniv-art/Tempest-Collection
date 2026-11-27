# Shared shader lifetime correction

The native shader pipeline is shared by frame/history wrappers on one SDL
renderer. Each wrapper previously called InitPipeline, which unconditionally
released and recreated those shaders. Existing temporal render states retained
borrowed shader references, and SDL's GPU pipeline cache keys contain shader
pointers. Reused shader addresses could consequently select a stale compiled
pipeline. The visible symptom in the analytic feedback tests was a correct
paint layer but no accumulated paint (or only one final update).

InitPipeline is now idempotent for the same live SDL renderer/device. It keeps
those shared shaders alive until ShutdownPipeline at renderer teardown. The
GPU regression checks that initializing another feedback owner does not change
the shared pipeline generation. Eight consecutive complete renderer suites
passed after the fix; repeated runs before it reproduced the failure.

No shader source/bytecode, blur mathematics, timing, gameplay or aspect rules
change in this fix. Temporary probes and private snapshots are not included.
The separate tempest-native-renderer-lifetime.patch applies to stable revision 1
with or without the aspect-only update. It is already included in the cumulative
revision-2 patches/binary; do not apply it on top of those again.
