# Scan-conversion edges and the edge builder

Converted (relative to `skia/`): `src/core/SkEdge.h`, `src/core/SkEdge.cpp`,
`src/core/SkAnalyticEdge.h`, `src/core/SkAnalyticEdge.cpp`, `src/core/SkEdgeBuilder.h` and
`src/core/SkEdgeBuilder.cpp`. The pre-conversion sources (unchanged since intake 490c9bd)
are preserved under `baseline/skia/` and selected by the reference profile. The
non-anti-aliased path filler (`SkScan_Path`) and the analytic anti-aliased filler
(`SkScan_AAAPath`) consume these edges.

## Changes that preserve results

- Every edge member (links, position, slope, rows, type, winding, segment counts, shifts and
  every forward-differencing coefficient) starts initialized. The scan converters' stack
  sentinels set only some fields; the rest are now defined instead of indeterminate.
- `SkEdge_Compute_DY` and the two `MAX_COEFF_SHIFT` macros become a function and constants.
  Multi-name declarations that were assigned inside preprocessor branches become
  initialized declarations; loop results start defined.
- The builder's three lambdas (the approximate-equality test, the clipped-segment consumer
  and the monotonic-quadratic splitter) become a static function, a private static member
  and a private member. The consumer checks each point for finiteness instead of reading
  `count * 2` floats from `&pts[0].fX`. The edge-list accessors use explicit casts and say
  they borrow the builder's arena; C-style downcasts of list entries become `static_cast`.
- `bool` functions return `false` instead of `0`, success flags are `bool`, `this->`, C-style
  casts and `using std::swap` are gone, and pointer member access is `(*p).`.

## Corrections

- `quick_inverse` indexed its 1,025-entry table on a Debug-only bound. Every caller passes
  a magnitude below the table size; the read now has a Release bound check (it traps, like the
  collection helpers).

## Removal scope

The builder's pure virtual `allocEdges` and `addPolyLine` and their implementations: nothing
in the tree calls either. `buildPoly` no longer stores the edge-count bound it never read,
but it keeps the overflow check that bound performed and still returns no edges when the
product overflows.

## Not changed

The analytic edges convert scaled float coordinates to `int` directly. That is undefined when
the scaled value does not fit; callers clip paths before building edges. A comment now states
the precondition. Making the conversion itself checked would change the hot setup path and
is left for measurement.

## Verification

`tests/scan_edges.cpp` sets 4,000 lines (vertical, horizontal, nearly coincident, on and
between half pixels) as edges without a clip, through three clips and through the null-clip
overload, and as analytic edges with one row step. It then sets the Y-monotonic pieces of
3,000 random quadratics and cubics as edges and analytic edges and steps each through every
segment or update. Finally it builds 2,500 random paths (a third lines only, which use the
polygon path) with both builders, without and through a clip, stepping every built edge.
Edge fields are recorded only after a successful setup: the original leaves them
uninitialized otherwise.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 68/68 PASS.
- Reference release: 67/67 PASS.
- Edge corpus: 9,500 cases, 28,115,015 bytes, identical hash 8794069088836998420 in both builds.
- Shared raw artifacts: all twenty-eight common artifacts match byte for byte, including the render corpus, except `image-sampling-corrected.raw`, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST-checker violations in the six files (remaining findings are in unconverted included headers), zero lexical findings and zero code-integrity candidates, including the new test.

No timing improvement is claimed and no algorithm changed.
