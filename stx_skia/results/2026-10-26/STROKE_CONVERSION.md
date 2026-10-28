# Stroking, caps and joins, and stroke records

Converted (relative to `skia/`): `src/core/SkStroke.cpp`, `src/core/SkStroke.h`,
`src/core/SkStrokerPriv.cpp`, `src/core/SkStrokerPriv.h`, `src/core/SkStrokeRec.cpp` and
`include/core/SkStrokeRec.h`. The pre-conversion sources (unchanged since intake 490c9bd)
are preserved under `baseline/skia/` and selected by the reference profile. Stroke records
are reached from drawing (`skpathutils::FillPathWithPaint`) and from dashing.

## Changes that preserve results

- The three `goto`s become structured control flow with the same outcomes: the cubic end
  normal skips the second-point fallback when both end tangents are degenerate; the miter
  join's right-angle, too-sharp and nearly-180-degree exits share a named blunt ending
  that receives the same (possibly swapped and negated) normal as before; a close after a
  lone move or zero-length verbs records a zero-length line through a named flag.
- Path records come from `std::optional<SkPath::IterRec>` in `for` loops, so the
  look-ahead's `continue` still advances. The stroke sink, iterator records, optional last
  points and raw path are named types instead of `auto`. The outer/inner sink choice is a
  reference-returning helper.
- `SkQuadConstruct`, `SkPathStroker`, `SkStroke` and `SkStrokeRec` members start
  initialized; every uninitialized local (normals, reductions, roots, chops, ray points,
  rectangles, bevel points, `SK_INIT_TO_AVOID_WARNING` indices) is initialized. Stroke
  constructors use member initializers.
- Cap and join factories are switches with a defined fallback instead of indexed tables;
  the direction reversal is a comparison instead of a two-entry table. Bitwise `&`, `|` and
  `^` on booleans become logical operators; the degenerate-edge count adds explicit
  integers. Function-pointer typedefs become aliases, the macro constants become
  `constexpr`, and every ignored result is discarded explicitly.

## Removal scope

Debug-only instrumentation, none of it compiled in this library's builds: the
`DEBUG_QUAD_STROKER` decision trace and its `STROKER_RESULT`/`STROKER_DEBUG_PARAMS`
macros, the `DEBUG_CUBIC_RECURSION_DEPTHS` histogram, the
`QUAD_STROKE_APPROX_EXTENDED_DEBUGGING` recursion maxima (`gMaxRecursion`), and the
`SK_DEBUG` viewer override of the stroke resolution (`gDebugStrokerErrorSet`,
`gDebugStrokerError`). Unused `CWX`/`CWY`/`CCWX`/`CCWY`/`CUBIC_ARC_FACTOR` macros and two
`#if 0` blocks are also gone. Upstream `StrokerTest.cpp` references `gMaxRecursion` and is
kept unchanged; it is not built here.

## Verification

`tests/stroke_geometry.cpp` strokes nineteen hand-built shapes (zero-length lines, a lone
move and close, collapsed quadratics and cubics, control points on the chord inside and
outside the ends, a cusp, a loop, a known over-curved quadratic, reversing and sharp joins,
a closed convex polygon, rectangles in both directions with a start index, an oval, and
contours of both orientations) with every cap and join at three widths, with and without
fill. It then strokes 2,500 random multi-contour paths (lines, quadratics, conics with
weights from 1e-6 to 1e6 and 0, cubics, repeated and nearly repeated points, closes,
inverse fill, special coordinates in a fifth) with three random parameter sets each: nine
widths up to 1e5, six miter limits, five resolutions, every cap and join, and fill. Every
output records fill type, verbs, points and weights. Rectangle strokes cover six
rectangles (sorted, reversed, mirrored, zero-width, empty, huge), every join, three miter
limits, four widths, fill and both directions. The stroke-record group covers both initial
styles, 600 records from paints with every style and override, inflation radii, every
setter, paint application, path application and 3,600 equality comparisons.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 65/65 PASS.
- Reference release: 64/64 PASS.
- Stroke corpus: 4,991 cases, 10,172,717 bytes, identical hash 13220916730655064189 in both builds.
- Shared raw artifacts: all twenty-five common artifacts match byte for byte, including the render corpus, except `image-sampling-corrected.raw`, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST-checker violations in the six files (remaining findings are in unconverted included headers) and zero lexical findings, including the new test.

No timing improvement is claimed and no algorithm changed. Dashing and path-effect
composition remain.
