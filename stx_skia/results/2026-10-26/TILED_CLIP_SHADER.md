# Clip shaders across CPU tile boundaries

`SkBitmapDevice` draws an extent past 8,191 pixels through `SkDrawTiler`.
The tiler translates its geometric raster clip to each tile. Previously,
`SkRasterClip::translate` did not copy the clip shader. A draw beginning in
a later tile therefore omitted the shader entirely. The pre-fix focused
case produced alpha **255** where an untiled draw with the same half-alpha
clip produced **128**. A full-width draw did not reveal the defect: its
first tile began at origin zero and copied the shader, leaving that pointer
in the reused tile clip.

Copying the pointer alone is insufficient for a spatial shader. The clip
shader retains the device CTM present when it was specified, while the tile
draw supplies tile-local pixel coordinates. For a tile translation `d`, the
original device position is `tile_position - d`. The candidate wraps the
translated clip shader with that offset. Its raster stage record carries the
offset into `MatrixRec::apply`, which composes it after the inverse saved CTM
and before sampling. The offset propagates through a blend of multiple clip
shaders; ordinary untiled stage records have zero offset. Translation from a
clip without a shader also clears any shader left in the destination clip.

The original versions of `SkRasterClip.cpp`, `SkEffectPriv.h`,
`SkLocalMatrixShader.cpp/.h`, and `SkShaderBase.cpp` are preserved under
`baseline/skia/`. The reference profile selects those originals. The focused
candidate regression compares an untiled half-alpha clip with a narrow draw
whose first tile has a nonzero origin, then compares a two-shader spatial
gradient against an equivalent untiled view with translated canvas geometry.
It also checks destination reuse after translating a clip without a shader.

On the M4 Mac mini, candidate and baseline Release suites passed 49/49 tests
each and generated rendering/serialization artifacts matched byte for byte.
Candidate Release and Debug ASan/UBSan suites reported `PASS`; detailed
integrity logs remain in the build trees for independent review. The CPU
build-scope check, relocated installed consumer and seven selected GUI.Forms
renderer tests passed. The new test passed house-style spelling and lifetime
checks; the retained Skia shader and clip sources still need full conversion.

The focused image is 9,000×2 pixels. This establishes its clip behavior and
the existing renderer comparison scope; it does not establish a speed change
or exhaustive behavior for every shader implementation.
