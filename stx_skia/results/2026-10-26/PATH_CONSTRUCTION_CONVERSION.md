# Path storage, construction, iteration and serialization

Converted (relative to `skia/`): `include/core/SkPath.h`, `include/core/SkPathBuilder.h`,
`include/core/SkPathIter.h`, `include/core/SkPathTypes.h`, `src/core/SkPath.cpp`,
`src/core/SkPathBuilder.cpp`, `src/core/SkPathPriv.cpp`, `src/core/SkPathPriv.h`,
`src/core/SkPathData.cpp`, `src/core/SkPathData.h`, `src/core/SkPathRaw.cpp`,
`src/core/SkPathRaw.h`, `src/core/SkPathIter.cpp`, `src/core/SkPath_serial.cpp`,
`src/core/SkPathRawShapes.cpp`, `src/core/SkPathRawShapes.h`, `src/core/SkPathMakers.h`,
`src/core/SkPathEnums.h`, `src/core/SkPath_interpolate.cpp` and `src/core/SkPathDump.cpp`.
The pre-conversion sources are preserved under `baseline/skia/` and selected by the
reference profile. `SkPathBuilder.cpp`'s copy includes the two earlier verified aliasing
corrections (c420bc8, 0d1ed74), so they stay on both sides of the comparison.

## Mechanical step

`this->` was removed, then the GUI.Forms AST tool's pointer-arrow plan (132 rewrites,
restricted to these files) and a fresh `auto` plan (48 resolved sites) were applied. The
`auto` tool rewrote structured bindings into typed declarations, which is not valid C++,
and emitted unqualified nested type names. Those sites became part of the semantic step
below, so this unit is committed as one change.

## Changes that preserve results

- The range iterator dereferences to a named `VerbRecord` (verb, points, weights) instead
  of a `std::tuple`. `SimplifyRRect`, `TransformDirAndStart` and
  `growForVerbsInPath` return named records instead of `std::pair`/`std::tuple`. Every
  structured binding in these files is gone.
- Fluent builder delegations perform the call and then `return *this`. Arc point
  insertion, raw-append helpers, contour clipping and the integer test are named
  functions instead of lambdas. Dumping uses a named flag instead of `std::function`
  lambdas.
- The three `goto`s are structured control flow with the same iteration counts: the
  partial-rectangle close retries the move without consuming it, the contour scan breaks
  on the next move, and the first-direction test uses the cross-product path when no
  horizontal extent is found.
- Reversal walks verbs and points by index. It handles `kMove` before stepping points,
  so it never forms the out-of-range pointer the original computed (and then
  discarded) one element before the start.
- The concavity pre-check visits the closing edge explicitly. The original re-pointed
  its cursor at a stack copy of the first point and compared that address with the end
  of the point array.
- Verb point-count tables become switches with a defined fallback; `SkUNREACHABLE`
  after exhaustive switches becomes a defined return.
- `memcpy`/`memset` over points and verbs become element copies and `std::fill`. The
  dump reads coordinates per point instead of through a punned float array.
- Uninitialized locals, iterator fields and builder fields start initialized.
  `SkPath::Iter`'s `SK_RESTRICT` claims are removed: they were not established.
- Every path, builder, raw view and iterator that borrows storage documents it.
  `SkPath::points()`, `verbs()`, `conicWeights()`, `getBounds()`, `iter()`, the builder's
  array views and `iter()`, `SkPathPriv::Raw`/`GetVerbs`, and both path iterators
  reject temporaries. No library code, test or the GUI.Forms consumer used them on
  temporaries.

## Corrections

- `SkPath::Iter` and `SkPathEdgeIter` started their conic-weight pointer one element
  before the array. They now keep the current weight (`Iter`) or an index (`EdgeIter`);
  results are unchanged.
- `SkPathData::Alloc` returns null for an unrepresentable size, but `MakeTransform`,
  `MakeNoCheck`, `Polygon` and `Oval` dereferenced its result unconditionally. They now
  report failure.
- `SkPath::ReadFromMemory` reads points and weights in place. A buffer that is not
  4-byte aligned is now rejected (`bytesRead` 0) instead of read misaligned.
- `SkPathRawShapes::Rect`, `Oval` and `RRect` view their own storage. Copying them
  would view the original's storage, so copy and assignment are deleted.
- An out-of-range fill type is dumped as `Unknown` instead of indexing past the name
  table. `SkPathBuilder::dump(DumpFormat)` now honors its format argument (it always
  printed decimal).

## Removal scope

`SkPathBuilder::isZeroLengthSincePoint` (private, no caller; the stroker has its own),
`ContourIter`'s conic-weight counter (advanced, never read), the
`SkSafeAccumulator` helper and a no-op failure reporter in `SkPathData.cpp`, an `#if 0`
block, and the unused `SkHalfPlane::operator()`. No public path API is removed.

## Verification

`tests/path_construction.cpp` exercises every factory over six rectangles (sorted,
unsorted, zero-width, huge, NaN, empty) and all nine start indices in both directions,
rounded rectangles of every type, circles, lines, 40 polygons and raw/byte-verb
storage including invalid sequences. It then runs 2,500 random builders, each with up to
twelve operations drawn from all 30 entry points (moves, lines, quads, conics with
degenerate and non-finite weights, cubics, closes, relative forms, polylines, all four arc
forms, every shape adder, offset, transform, inverse toggling, point edits, reserve and
fill type), with special coordinates in a fifth of them. Each builder records storage,
bounds, finite/tight bounds and last point, snapshots with and without a matrix, copies,
moves, construction from a path, comparison, appends in both modes (including its own
snapshot and a rotated and a scaled copy), raw append and detach. Each resulting path
records every query (rectangle/oval/rounded-rectangle/line recognition, convexity,
containment of nine points, conservative rectangle containment), all three iterators in
both close modes, transforms (translation, flip-scale, rotation, perspective,
overflowing scale), offsets, fill-type and volatility variants, swap/reset/equality, the
binary form with every short read, `serialize()`, both dump formats and interpolation.
A last group checks line/quad/cubic degeneracy and conic-to-quad conversion at four
subdivision levels.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 62/62 PASS.
- Reference release: 61/61 PASS.
- Path corpus: 4,905 cases, 22,492,936 bytes, identical hash 11543838873592549500 in both builds.
- Shared raw artifacts: all twenty-two common artifacts match byte for byte except `image-sampling-corrected.raw`, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST-checker violations in the twenty files and zero lexical findings, including the new test.

The first reference run differed from the candidate. The cause was in the test: it read
`SkPath::Iter::isCloseLine()` after non-line verbs, where the original iterator leaves the
flag uninitialized (the converted iterator initializes it). The corpus now records the flag
only after line verbs, where it is defined.

No timing improvement is claimed. The edge iterator's conic index adds one subtraction on
`conicWeight()` and removes a pointer adjustment at construction; this record does not
include a scan-conversion benchmark. Contour measurement, curve geometry (`SkGeometry`),
stroking and dashing remain separate conversion work.
