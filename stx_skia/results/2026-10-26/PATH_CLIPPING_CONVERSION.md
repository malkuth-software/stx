# Path clipping ahead of scan conversion

Converted (relative to `skia/`): `src/core/SkLineClipper.cpp`, `src/core/SkEdgeClipper.cpp`
and `src/core/SkEdgeClipper.h`. `src/core/SkLineClipper.h` needed no change. The
pre-conversion sources (unchanged since intake 490c9bd) are preserved under
`baseline/skia/` and selected by the reference profile. Scan conversion
(`SkEdgeBuilder`, hairlines, anti-aliased hairlines) and `SkPath` use these clippers.

## Changes that preserve results

- The edge clipper records and replays segments with integer cursors into its fixed
  point and verb arrays instead of three raw cursor pointers. Every append checks both
  arrays before writing (the original checked only points, and verbs only in some
  callers); the limits are sized for the worst case, so this changes no reachable result.
- `mono_cubic_closestT` takes the four control coordinates on the chopping axis instead
  of reading every other float from `&pts->fY`; the arithmetic is unchanged. The re-chop of
  a still-above cubic copies its four points by name instead of `memcpy` from `&tmp[3].fX`.
- `memcpy` over points becomes element copies; clamp helpers take pointers instead of
  non-const references; `SK_INIT_TO_AVOID_WARNING` and other uninitialized locals start
  initialized; the path walk names `SkPathEdgeIter::Result` and indexes converted quads.
- The line clipper copies the wholly-outside case into its result array instead of
  re-pointing a result pointer at its temporary; every helper returns a named result.

## Removal scope

- `src/core/SkQuadClipper.cpp` and `.h`: nothing in the tree includes the header or
  names the class, including upstream tests; `SkQuadClipper2` was declared without a
  definition. Both are removed from the candidate build; the original files stay under
  `baseline/skia/` and in the reference build.
- The debug-only `sk_assert_monotonic_x`/`_y` helpers and their release macros, which had
  no callers.

## Verification

`tests/path_clipping.cpp` clips 6,000 lines (repeated, axis-aligned, on-edge, tiny and
beyond-float-reliability coordinates) against seven clips (ordinary, symmetric,
zero-width, huge, fractional, unit and left-of-origin) with `IntersectLine` into separate
and the same storage, `ClipLine` and the edge clipper in both culling modes; 6,000
quadratics and cubics through the edge clipper with every replayed segment, followed by a
fresh line recording on the same clipper; and 1,500 multi-contour paths with lines,
quadratics, conics and cubics through `ClipPath`, recording every consumer call.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 67/67 PASS.
- Reference release: 66/66 PASS.
- Clipping corpus: 13,500 cases, 1,393,689 bytes, identical hash 17626081231561725439 in both builds.
- Shared raw artifacts: all twenty-seven common artifacts match byte for byte, including the render corpus, except `image-sampling-corrected.raw`, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST-checker violations in the four files (remaining findings are in unconverted included headers), zero lexical findings and zero code-integrity candidates, including the new test.

No timing improvement is claimed and no algorithm changed.
