# Clip translation at signed coordinate limits

An antialiased clip stores coverage runs whose dimensions must agree with its
integer bounds. `SkAAClip::translate` moved those bounds with independent
saturating additions. At a large positive or negative requested offset, one
edge could saturate while the other still moved, changing the width or height
without changing the runs. A deterministic boundary case failed before the
fix. `SkRegion::translate` tried to prevent this, but its lower limit was
`INT_MIN`; `SkIRect::offset` actually saturates at `SK_MinS32`, which is one
greater. That discrepancy shrank a negative-edge integer clip by one pixel.

Both clip representations now limit the requested offset to the range that
preserves the complete span inside `SK_MinS32` through `SK_MaxS32`.
`SkRasterClip::translate` derives the applied offset from the translated
bounds before wrapping a clip shader, so shader sampling follows the same
translation when a request is clamped. An empty clip returns before any shader
wrapper is allocated. The original `SkAAClip.cpp` and `SkRegion.cpp` are
preserved and selected by the reference profile alongside the already
preserved raster clip source.

The focused test covers antialiased translation toward the positive and
negative coordinate limits, an in-place antialiased translation, and an
integer region translated toward the negative limit. Before their respective
changes, the antialiased and integer boundary cases failed. Ordinary tiled
shader and clip replacement tests continue to pass.

On the M4 Mac mini, candidate and reference Release suites pass 49/49 tests
each. Candidate Release and Debug ASan/UBSan suites report `PASS`, with the
detailed logs retained for independent review. Eight generated rendering and
serialization artifacts compared here remain byte identical. The CPU build
scope check, a fresh relocated installed consumer and all seven selected
GUI.Forms renderer tests pass. The
new test has zero house-style spelling findings; the three touched imported
production sources have the same 467 pre-existing findings as their preserved
reference copies. The lifetime-rule check finds zero findings in the four
touched C++ files. This boundary correction does not complete the wider scan
conversion or source-style programme.
