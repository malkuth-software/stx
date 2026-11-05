# Non-anti-aliased scan conversion and hairlines

Converted (relative to `skia/`): `src/core/SkScan.h`, `src/core/SkScan.cpp`,
`src/core/SkScanPriv.h`, `src/core/SkScan_Path.cpp` and `src/core/SkScan_Hairline.cpp`.
The pre-conversion sources (unchanged since intake 490c9bd) are preserved under
`baseline/skia/` and selected by the reference profile. The anti-aliased path fillers
(`SkScan_AntiPath`, `SkScan_AAAPath`) and anti-aliased hairlines (`SkScan_Antihair`) remain
separate units; they include the converted shared headers.

## Changes that preserve results

- The path filler's two `goto`s become structured control flow: inserting a new edge skips
  the relink when the edge is already in place, and an edge that continues into its next
  segment shares the x-reordering step with an edge that just advanced, through a named
  flag.
- The macro-generated direct-blit loops become a function template over the pixel type,
  dispatched on the same pixel sizes; other sizes still draw nothing.
- Hairline subdivision stores curve points with `to_point`, and bounds use `from_point`,
  instead of loading or storing `float2` through `SkPoint` addresses. Cap extension walks the
  points by index with the same iteration counts instead of pre/post-incremented pointers.
- The sentinels' edge-list links, the triangle's edge setup (one loop over the three sides,
  filling the next free edge as before) and the head/tail bracketing are named helpers. The
  `ASSERT_RETURN`, sentinel-row, pre/post and subdivision-level macros become explicit checks
  and constants; release no-op macros become empty functions.
- `InverseBlitter`, `SkScanClipper`, `DrawingParameters` and every local start initialized;
  `auto`, `typedef`, `using std::swap`, C-style casts, braced returns and `->` are gone. The
  scan clipper cannot be copied (it hands out pointers to its own blitters).
- Subdivision levels are clamped where the point buffers are indexed, and the cubic's
  513-point buffer is only zeroed when more than one line is drawn.

## Corrections

- `is_next_contour_closed` dereferenced the next path record without checking it existed.
  The path iterator trims one trailing move, so storage ending in two or more moves left a
  move with nothing after it. A square- or round-capped hairline over such storage read an
  empty optional. It now treats that move as an open contour. No `SkPath` produces this
  sequence; raw path storage can. A candidate-only check covers it.

## Verification

`tests/scan_converters.cpp` records every blitter call (spans, vertical runs, rectangles,
anti-aliased runs with their coverage, anti-aliased rectangles and masks) in order. It
fills 2,500 random paths (every fill rule including inverse, lines, quadratics, conics,
cubics, repeated and axis-aligned points, an eighth with coordinates beyond the fixed-point
limit) with convexity unresolved and resolved, through seven clips: three rectangles, a
complex circle region, an anti-aliased circle clip, a region with a hole and one beyond
the tiling limit. Rectangle-clipped fills also go through the region entry point. It
strokes 2,500 more paths with every butt, square and round hairline procedure, plain and
anti-aliased. Finally it draws 2,000 polylines as hairlines and anti-aliased hairlines, and
also into direct-blit pixel storage of 1, 2, 4 and 8 bytes per pixel (recording the storage
hash). It draws their rectangles as hairline, filled, integer-filled and framed rectangles,
fills their triangles, and records the tiling decision.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 69/69 PASS.
- Reference release: 68/68 PASS.
- Scan corpus: 7,000 cases, 66,072,001 bytes, identical hash 8439081072416237950 in both builds.
- Shared raw artifacts: all twenty-nine common artifacts match byte for byte, including the render corpus, except `image-sampling-corrected.raw`, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST-checker violations in the five files (remaining findings are in unconverted included headers), zero lexical findings and zero code-integrity candidates, including the new test.

The first candidate build failed to compile: a local named `requires` (a C++20 keyword) in
`PathRequiresTiling`. It was renamed, and the results above are from the rerun. The
correction above is established by reading the code; the original's empty-record read was
not reproduced, since it is undefined behavior and probing it would need an instrumented
build of the reference.

No timing improvement is claimed and no algorithm changed.
