# Points, rectangles and rounded rectangles

Converted: `include/core/SkPoint.h`, `include/core/SkRect.h`,
`include/core/SkRRect.h`, `src/core/SkPoint.cpp`, `src/core/SkRect.cpp`,
`src/core/SkRRect.cpp`, `src/core/SkPointPriv.h`, `src/core/SkRectPriv.h` and
`src/core/SkRRectPriv.h` (paths relative to `skia/`). The originals are preserved
under `baseline/skia/` and selected by the reference profile.

## Changes that preserve results

- Return values are named; pointer member access and `auto` are spelled out;
  local temporaries are initialized. Formulas stay in one expression, so
  floating-point contraction in consumer translation units is unchanged.
- The rectangle intersection macro, which hid a `return`, is two named helpers:
  the overlap of two rectangles and its NaN-rejecting emptiness test. The
  argument order of every `std::max`/`std::min` is preserved, so NaN propagation
  is identical.
- `SkRRect` equality compares the four corner radii element by element instead
  of reading eight floats through the first radius. Its serialized form is
  written and read as the rectangle followed by the radii, field by field,
  instead of copying the object's bytes. The bytes are identical.
- Rounded-rectangle radii are cleared and copied by element instead of
  `memset`/`memcpy`; `setRectRadii` no longer copies overlapping storage when
  passed its own radii.
- `SkPoint::setLength`, `Normalize` and `SkPointPriv::SetLengthFast` share one
  named calculation that returns the vector, the original length and validity.
  The two template instantiations differed only in an assertion.
- `SkRRect::isValid` reaches a default case for type tags outside the
  enumeration, replacing a separate range check before the switch.
- `SkRRect::rect()`, `getBounds()` and `radii()` borrow the object and now reject
  temporaries. No retained caller, test or the GUI.Forms consumer used them on
  temporaries.

## Corrections

- `SkIRect::outset(dx, dy)` negated its arguments and called `inset`; an offset
  of `SK_MinS32` overflowed. It now saturates directly, matching `makeOutset`.
- `SkIPoint::operator-()` saturates: `-SK_MinS32` is `SK_MaxS32`.
- `SkPointPriv::AreFinite(points, 0)` read `points[0]`. It now reads nothing and
  reports an empty list as finite.

Each correction has a candidate-only check. The shared corpus avoids these inputs.

An earlier draft also widened `SkRectPriv::Subtract`'s edge differences to 64
bits. That was unnecessary: a non-empty `SkIRect` already has a width and height
that fit in `int32_t`, and every difference lies inside it. The original
arithmetic is kept, and the bound is documented in place.

## Removal scope

Private helpers with no retained CPU caller were removed. Each was GPU geometry
or debugging support:

- `SkPointPriv`: `SetRectFan`, both `SetRectTriStrip` forms, `AsScalars`,
  `MakeOrthog`, `DistanceToLineBetween`/`Sqd`, `Negate` and the `Side` enumeration
  (used only by Ganesh's convex tessellator).
- `SkRectPriv`: `MakeILargestInverted`, `MakeLargest`, `MakeLargestInverted`,
  `GrowToInclude`, the floating-point and value-returning `Subtract` forms,
  `QuadContainsRect`, `QuadContainsRectMask` and `ClosestDisjointEdge`.
- `SkRRectPriv`: `IsCircle`, `GetSimpleRadii`, `IsSimpleCircular`,
  `IsNearlySimpleCircular`, `EqualRadii`, `GetRadiiArray`, `AllCornersCircular`,
  `AllCornersRelativelyCircular`, `IsRelativelyCircular`, `InnerBounds` and
  `ConservativeIntersect` (the last was the only lambda user in these files).
  Path serialization's `ReadFromBuffer`/`WriteToBuffer` remain.
- The 32-bit vector branch of `SkRect::Bounds`; every supported target is 64-bit,
  which a static assertion now states.
- The Android `SK_SUPPORT_LEGACY_RRECT_TRANSFORM` and Graphite
  `SK_GRAPHITE_USE_LEGACY_RRECT_CLIP_SHADER` branches, neither of which is
  defined by this build.

No public `SkPoint`, `SkRect` or `SkRRect` API was removed. `SkPoint`/`SkIPoint`
remain trivial aggregates: path and vertex storage grows without per-element
construction. Their declarations must initialize coordinates.

## Verification

`tests/geometry_primitives.cpp` builds a deterministic corpus over the extreme
integers, signed zeros, denormals, the float maximum, infinities, NaN and
ordinary fractional coordinates: 4,000 integer rectangle pairs, 4,000 rectangle
differences (half using full-range coordinates), 6,000 float rectangle pairs,
3,000 point lists for bounds, 900 points with every length/normalization/
distance/rotation helper, 221 integer points, 6,000 rounded rectangles across
all five construction routes, with containment, inset/outset in place and
out of place, fourteen transforms (identity, translation, scales and flips, the
three quarter turns, a 30-degree rotation, skew, a zero scale and an overflowing
scale), memory and buffer round trips, short reads and parsing of arbitrary
serialized words, and decimal/hex dumps.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 60/60 PASS.
- Reference release: 59/59 PASS.
- Geometry corpus: 24,217 cases, 9,168,663 bytes, identical hash 12254550361679362040 in both builds.
- Shared raw artifacts: all twenty common artifacts match byte for byte except `image-sampling-corrected.raw`, which records an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House-policy checker over the converted files: zero violations from the AST checker and zero lexical findings (including the new test).

No timing improvement is claimed. The intersection and bounds loops compile to
the same operations; this record does not include an assembly comparison.
`SkMatrix`'s lazily cached type mask, written from `const` accessors, remains a
shared-reader hazard noted for the matrix conversion.
