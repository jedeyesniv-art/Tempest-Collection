For the expanded all-scene geometry and optional scene-cadence trial, see [T3K-SCENE-PRECISION.md](T3K-SCENE-PRECISION.md). The notes below describe the earlier web-only checkpoint.

# Web precision checkpoint

The 120 Hz research build preserves web coordinates before world-coordinate
packing and projected-point rounding. Both textured quads and vector edges use
these fractions; converting the already packed integers to floats would not
recover them. This is current simulation geometry, without motion interpolation.

Original native memory writes, collision data, colour evolution and texture
cadence remain unchanged. Render metadata is frame scoped, validated against
its packed native word, invalidated on writes and cleared on rollback/load.
Decoded pictures own their resulting coordinates. Missing metadata falls back
to original decoding. The 24 Hz reference does not enable this path.

The overlay's `120 Hz web precision` option defaults to `Subpixel`; choose
`Original grid` for an immediate comparison. Other actor geometry still has
native coordinate rounding and is outside this change.

Validation: all 32 reference frames retain exact native memory comparisons,
including rollback and framebuffer checks. They generated 1,152 precise web
vertices, with maximum displacement from coarse decoding of 2.959274 game
pixels. A synthetic renderer check verifies fractional movement with unchanged
packed words, alignment of textured and vector endpoints, and invalidation on
same-value rewrites. The existing 32-state feedback independence check passes.
Four CTest targets and four scripted gameplay scenarios also pass.

Death and level transitions still use the older scheduler in this trial. Their
lower cadence can resemble slowdown; CPU/GPU effect cost has not yet been
isolated. This checkpoint makes no performance improvement claim. Profiling
those scenes and adapting their clocks remain separate work.
