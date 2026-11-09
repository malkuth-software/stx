# Curve geometry, solvers, cubic clipping and cubic maps

Converted (relative to `skia/`): `src/core/SkGeometry.cpp`, `src/core/SkGeometry.h`,
`src/core/SkBezierCurves.cpp`, `src/core/SkBezierCurves.h`, `src/core/SkQuads.cpp`,
`src/core/SkCubics.cpp`, `src/core/SkCubics.h`, `src/core/SkCubicClipper.cpp`,
`src/core/SkCubicClipper.h`, `src/core/SkCubicMap.cpp` and `include/core/SkCubicMap.h`.
`src/core/SkQuads.h` needed no change. The pre-conversion sources (unchanged since intake
490c9bd) are preserved under `baseline/skia/` and selected by the reference profile.

## Changes that preserve results

- Every formula, operand order and precision is unchanged. Point and coefficient vectors
  are built from and stored to named fields instead of `sk_bit_cast`, `float2::Load` and
  `store` through `SkPoint` addresses; lanes are bitwise identical.
- Helpers that walked an `SkPoint` array as a strided float array
  (`flatten_double_*_extrema`, `formulate_F1DotF2`, `conic_deriv_coeff`, `p3d_interp`) take
  the coordinates by name. `SkConic::chopAt` checks the two result conics field by field
  instead of reading fourteen floats from the first conic's address.
- Lambdas (Bezier coordinate accessors, the cubic-map initial guess and range check, the
  debug range check in `SkChopCubicAt`), the variadic `eval_poly` template, structured
  bindings, `if`-initializers, the `AS_QUAD_ERROR_SETUP` macro and the
  `chopIntoQuadsPOW2` `goto` become named locals, overloads and structured control flow.
- `SkFindUnitQuadRoots`, `SkCubics::RootsReal` and the solver loops count results by index
  instead of advancing output pointers. `return_check_zero` (a breakpoint hook that returned
  its argument) is gone.
- `SkConic` is default-constructible as a unit-weight conic at the origin; its array setter
  copies the points before assigning, so it accepts its own `fPts`. Coefficient records,
  `SkAutoConicToQuads`, `SkCubicMap` and `SkCubicClipper` members start initialized; every
  uninitialized local starts initialized. `SkConic::chop`'s `SK_RESTRICT` claim becomes a
  documented no-overlap precondition. `SkBezierCubic::Subdivide` documents the same.
- `SkCubics::EvalAt(coefficients, t)` accepts const coefficients.

## Corrections

- `SkChopCubicAt(src, dst, t0, t1)` with `t1 == 1` copied `src[3]` into the last three
  points after its first chop had already overwritten `dst[3]`. When `src` is `dst`, which
  the multi-parameter chop passes from its second pair onward, the result ended at the
  chop point instead of the curve's end. The end point is now saved first. No retained
  library caller passes a parameter list that reaches this (extrema and curvature chops
  pass parameters strictly inside (0, 1)), so the shared corpus avoids it and a
  candidate-only check covers it. A probe linked against both release libraries chopped
  (0,0) (10,20) (30,20) (40,0) in place at 0.5 and 1: the original's last point was
  (20, 15), the converted library's is (40, 0).

## Removal scope

Declarations and definitions with no caller in the pinned GUI.Forms CPU closure and whose
upstream users are excluded subsystems (GPU tessellation, PDF, path ops, viewer tools,
benchmarks) or upstream test files:

| Removed | Upstream users outside this library |
| --- | --- |
| `SkMeasureAngleBetweenVectors`, `SkFindBisector`, `SkMeasureQuadRotation`, `SkFindQuadMidTangent`, `SkChopQuadAtMidTangent`, `SkFindCubicMidTangent`, `SkChopCubicAtMidTangent`, `SkConic::findMidTangent` | GPU stroke tessellation |
| `SkChopQuadAtMaxCurvature` | path ops edge builder, GM, `GeometryTest` |
| `SkConvertQuadToCubic` | PDF, `GeometryTest` |
| `SkMeasureNonInflectCubicRotation` | GPU, `GeometryTest`, `FindCubicConvex180ChopsTest` |
| `SkChopCubicAtInflections` | SVG test typeface tool, `GeometryTest` |
| `SkCubicType`, `SkCubicIsDegenerate`, `SkCubicTypeName`, `SkClassifyCubic` | GPU, path ops, viewer, `GeometryTest` |
| `SkConic::computeAsQuadError`, `SkConic::asQuadTol` | path benchmark |
| `SkConic::chopAtXExtrema` | none |
| `SkConic::findMaxCurvature` | declared only (its definition was `#if 0`) |

Also removed: the `SK_SUPPORT_LEGACY_CONIC_CHOP` variant of `SkConic::chop`, the disabled
`if ((false))` logarithmic branch of `computeQuadPOW2`, the debug-only
`test_collaps_duplicates`, and `SkCubicClipper`'s never-compiled `NEWTON_RAPHSON`
variant. Path ops is not compiled here and was not part of the pinned closure; restoring it
would also need `SkClassifyCubic` and `SkCubicType` from `baseline/skia/`. Upstream test
sources are kept unchanged. `SkBezierCubic::EvalAt` and the horizontal-line intersections,
`SkBezierQuad`, all of `SkQuads` and `SkCubics`, `SkCubicClipper::clipCubic` and the public
`SkCubicMap` have no retained library caller either, but they are small, dedicated
upstream tests cover them, and the public header stays installed, so they are kept and
converted rather than removed.

## Verification

`tests/curve_geometry.cpp` covers every retained entry point: unit quadratic roots over all
512 combinations of eight special coefficients plus 3,000 random triples; 2,500 quadratics
(evaluation and tangents at six parameters in all three forms, chops, extrema and monotonic
chops on both axes, maximum curvature); 2,500 cubics including a family with exact cusps
(evaluation with every output combination, extrema, single, double and multi-parameter
chops, monotonic chops with and without output, inflections, maximum curvature with and
without parameter output, cusps, monotonic chops at four intercepts on both axes); 2,500
conics over thirteen weights including non-finite ones (evaluation, tangents, single and
range chops including both ends, halving, quad counts at seven tolerances, explicit
subdivision at every level, extrema, tight and fast bounds, and `SkAutoConicToQuads` with
non-positive raw weights); weight transformation under six matrices including perspective;
unit arcs from 24 start angles, fifteen sweeps, both directions and three user matrices; the
double-precision Bezier, quadratic and cubic solvers including near-degenerate and
non-finite coefficients; the cubic clipper on monotonic and unsorted cubics against four
clips; and the cubic map at 37 inputs for 1,500 control-point pairs. A fifth of the curves
use special coordinates.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 63/63 PASS.
- Reference release: 62/62 PASS.
- Curve corpus: 20,472 cases, 11,923,596 bytes, identical hash 4373027617523386501 in both builds.
- Shared raw artifacts: all twenty-three common artifacts match byte for byte except `image-sampling-corrected.raw`, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST-checker violations in the twelve files (remaining findings are in the unconverted `SkSpan.h`, `SkVx.h` and `SkAssert.h` they include) and zero lexical findings, including the new test.

No timing improvement is claimed; this unit does not change any algorithm and includes no
benchmark. Contour measurement, stroking and dashing remain separate conversion work.
