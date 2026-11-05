# Contour measurement and the path-measure facade

Converted (relative to `skia/`): `include/core/SkContourMeasure.h`,
`src/core/SkContourMeasure.cpp`, `src/core/SkPathMeasure.cpp` and
`src/core/SkPathMeasurePriv.h`. `include/core/SkPathMeasure.h` needed no change. The
pre-conversion sources (unchanged since intake 490c9bd) are preserved under
`baseline/skia/` and selected by the reference profile. Dashing (`SkDashPath`) is the
retained library caller; GUI.Forms does not call these classes directly.

## Changes that preserve results

- Segment search, interpolation and extraction work on indices into the segment array.
  The templated pointer search, `seg[-1]` and the pointer-walking `Segment::Next` are gone;
  the walk to the next verb's first segment is bounded by the stop segment. The search,
  its tie handling and every formula are unchanged.
- Segments are appended as named, fully initialized records; points with `push_back`.
  The structured binding over path records becomes `SkPathPriv::VerbRecord`.
- `kMaxTValue` and the curvature limit are constants instead of macros; switch fallbacks
  are defined (`SkDEBUGFAIL` instead of `SK_ABORT`; the two-bit type field cannot hold
  another value). The zero-length dash line only reads the builder's last point when it
  exists.
- `VerbMeasure` and `Segment` members start initialized; `getMatrix` and
  `getSegment` locals start initialized. The iterator's implementation owns its path copy
  and cannot be copied. Contour iteration returns `sk_sp` directly instead of a raw
  `new` pointer. `VerbMeasure` and the verb iterator document that they borrow the
  contour; the contour's destructor is private, so no temporary can be iterated.
- Locals no longer shadow `length()`; `SkPathMeasure` uses named results and explicit
  casts, and `setPath(nullptr)` no longer copies an empty path through a conditional
  expression.

## Removal scope

`SkPathMeasurePriv.h` declared `SkPathMeasure_segTo(..., SkPath*)`, which had no
definition anywhere; it is removed. The defined `SkContourMeasure_segTo` had no
declaration and is now internal to its file. `SkPathMeasurePriv::CountSegments` and the
verb iterator stay: the upstream `PathMeasureTest` uses both.

## Verification

`tests/contour_measure.cpp` builds 3,000 random multi-contour paths (lines, quadratics,
conics with weights from 1e-6 to 1e6 and 0, cubics, repeated points, single and double
closes, trailing moves, a quarter with large or tiny coordinates) and measures 1,800 of
them with and without forced closing at seven resolutions, including 0, NaN and 1e-30.
Every contour records length, closure, position and tangent (each alone and together)
at eleven distances including negative, beyond the end and non-finite, matrices for all
three flag sets and with no output, six segment extractions per contour (pinned, empty,
NaN, appended to an existing builder) with and without a leading move, and every
per-verb measure. The other 1,200 paths go through `SkPathMeasure`: every contour's
queries, segment counts, move construction and assignment, `setPath` with a path and
with null, and the default and moved contour iterators.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 64/64 PASS.
- Reference release: 63/63 PASS.
- Contour corpus: 3,001 cases, 4,132,596 bytes, identical hash 15192583342709228512 in both builds.
- Shared raw artifacts: all twenty-four common artifacts match byte for byte, including the render corpus that draws dashed strokes, except `image-sampling-corrected.raw`, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST-checker violations in the five files (remaining findings are in unconverted included headers) and zero lexical findings, including the new test.

No timing improvement is claimed and no algorithm changed. Stroking and dashing remain.
