# Replacing a raster clip clears its shader

The raster device implements canvas clip reset through
`SkRasterClipStack::replaceClip`. That path calls `SkRasterClip::setRect` or
`setEmpty`. Both replaced the geometric clip while retaining its shader. A
prior `clipShader` could therefore continue attenuating later draws after the
canvas clip was reset. The sibling `SkClipStack` replacement path drops prior
clip elements, including shader elements.

The candidate clears the shader in both raster clip replacement operations.
Changing the root extent also calls `setRect`, so it now starts with a clean
shader clip. Translating an empty clip returns before allocating a shader
wrapper; the destination is reset to the empty clip with no shader. Ordinary
nonempty translated clips still retain the shader and its device-coordinate
offset from the preceding tile fix.

The candidate-only regression checks `replaceClip`, root resizing, and a
white draw after `SkCanvasPriv::ResetClip` following a half-alpha shader
clip. Before the fix, the focused candidate test failed. After the fix, it and
the adjacent tiled shader test pass. The preserved original
`SkRasterClip.cpp` remains the reference source.

On the M4 Mac mini, candidate and reference Release suites pass 49/49 tests
each. Candidate Release and Debug ASan/UBSan suites report `PASS`; their
detailed logs remain in the build trees for independent review. Eight
generated rendering and serialization artifacts compared here match the
reference byte for byte. The CPU build-scope check, a fresh relocated
installed consumer, and seven selected GUI.Forms renderer tests pass. The
new test passes the house-style spelling check, and the lifetime-rule check
finds no issue in the touched C++ files. Wider clip and scan conversion
source remains in the conversion programme.
