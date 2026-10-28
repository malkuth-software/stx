# Dashing, the dash path effect and path-effect composition

Converted (relative to `skia/`): `src/utils/SkDashPath.cpp`, `src/utils/SkDashPathPriv.h`,
`src/effects/SkDashPathEffect.cpp`, `src/effects/SkDashImpl.h`, `src/core/SkPathEffect.cpp`
and `src/core/SkPathEffectBase.h`. `include/core/SkPathEffect.h` and
`include/effects/SkDashPathEffect.h` needed no change. The dash sources (unchanged since
intake 490c9bd) are preserved under `baseline/skia/`; the reference profile already
selected the original `SkPathEffect.cpp` and header from the earlier flattenable unit.
Dashes are part of the GUI.Forms contract; drawing reaches them through
`skpathutils::FillPathWithPaint` and the dashed-point fast path in `SkDraw`.

## Changes that preserve results

- Axis-aligned line clipping reads and writes the line's axis through named helpers
  instead of indexing `(&pts[0].fX)[axis]` and `(&bounds.fLeft)[axis]` across struct
  fields. Float truth tests become explicit comparisons with zero (NaN behaves as before).
- The rectangle edge loop is a `for` over `std::optional<SkPath::IterRec>`; the last
  point is a named optional. Interval walking uses the span instead of a separate raw
  pointer, with the post-increment split out.
- `SpecialLineRec`, `SkDashImpl` and `DashInfo` members, and every local (line points,
  polygon points, inverse matrix, half extents) start initialized. Integer-to-float
  conversions in the dash-count estimates are explicit; results are discarded explicitly.
- Path-effect composition uses named results, deleted copy operations instead of
  declared-but-undefined ones, and moves its references instead of copying them.
  `computeFastBounds` keeps its short-circuit order.
- The dash interval copy uses `std::copy` instead of `memcpy`.

## Corrections

- `SkPathEffectBase::PointData` owned its points through a raw `new[]`/`delete[]` pointer
  and was implicitly copyable, so a copy would have freed the points twice, and refilling
  a record leaked the earlier points. It now owns them in an `AutoTArray`; it can be moved,
  not copied, and `points()` rejects temporaries. Every consumer used `points()`.
- The dash point fast path wrote the point size through `results` before checking it for
  null, although the interface documents the result as optional. The size is now written
  only into a supplied result. No retained caller passes null; a candidate-only check
  covers it.

## Verification

`tests/dash_effects.cpp` covers dash validation and parameters for 2,000 interval sets
(odd counts, negative and zero intervals, dots, dense patterns, one to three pairs) and nine
phases including negative, large and NaN, with the effect's dash description; dashing of
2,500 random paths (lines, rectangles of both directions and every start, multi-contour
curves with axis-aligned runs, closes, special coordinates) with three stroke records each
(fill, hairline, widths, every cap and join, two miter limits), without and with three
cull rectangles, plus the internal filter without stroke-record application and the
info-based filter; 1,500 dashed-point fast-path queries over horizontal, vertical and
diagonal lines, equal and unequal intervals, integral phases and four matrices including a
rotation; 800 sum and compose effects over valid and invalid dashes with their filtered
output, CTM need, fast bounds and flattened bytes; and two dash-count-limit cases. Each
output records the stroke record and path.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 66/66 PASS.
- Reference release: 65/65 PASS.
- Dash corpus: 6,802 cases, 34,966,182 bytes, identical hash 10034990592051829679 in both builds.
- Shared raw artifacts: all twenty-six common artifacts match byte for byte, including the render corpus, except `image-sampling-corrected.raw`, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST-checker violations in the six files (remaining findings are in unconverted included headers) and zero lexical findings, including the new test.

The first run of this corpus used 0.001-unit dashes on long random paths and produced
10,797,683,752 bytes; both builds matched, but the instrumented suite reported STOP
(diagnostics unread; the dash test under instrumentation is the likely cause through its
180-second limit, which this record does not establish). The dense pattern became
0.25/0.75 and the dash-count limit is exercised by one over-limit line and one 30-unit line
whose point count alone is recorded; the rerun above is the recorded result. The two
oversized artifacts were deleted from the mirror's `build/`.

Decoding is not compared: the reference build restores the original factory registry,
while the candidate keeps rejecting named decoding (see `FACTORY_REGISTRY.md`). No timing
improvement is claimed and no algorithm changed.
