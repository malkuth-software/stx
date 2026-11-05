# 4x4 matrices: SkM44 — 2026-10-27

Scope (relative to `skia/`): `include/core/SkM44.h` and `src/core/SkM44.cpp`, which hold `SkV3`,
`SkV4`, `SkM44` and `SkMatrixPriv::MapRect(const SkM44&, ...)`. There are three commits:
- 539fe598: prune, with intake baselines;
- 0feedd7e: mechanical conversion and the corpus;
- the fix commit that adds this record.

GUI.Forms never names these types. The library uses them for the canvas and device transforms,
for `SkCamera`, and to read matrices from buffers.

## Pruned (539fe598)

Nothing compiled in the library, its tests or GUI.Forms used these:
- `SkV2`;
- `SkM44::LookAt`, `Perspective`, `RectToRect`, `Rows`, `Cols` and `RowMajor`;
- `row`, `col`, `setRow`, `setCol`, `getRowMajor`, `transpose` and `dump`;
- the NaN constructor.

The candidate build proves it. Only uncompiled Graphite and Ganesh sources and the preserved
upstream `tests/M44Test.cpp` still mention them.

The reference profile's original `SkBlurEngine` and `SkImageFilterTypes` still use `SkV2` and
`RectToRect`. So the same commit adds the intake `SkM44.h` and `SkM44.cpp` as baselines and lists
`SkM44` among the reference units. That commit builds 76/76 (candidate) and 75/75 (reference).

## Mechanical conversion (0feedd7e)

- The `setConcat` column, the `preConcat(SkMatrix)` column and the perspective rect-mapping
  `project` and `clip` lambdas become the named functions `concat_column`, `concat_3x3_column`,
  `project_corner` and `clip_edge_to_w0_plane`.
- Locals spell `skvx::float4`, `skvx::float2` and `skvx::int4`.
- Arrows and `this->` are gone.
- `SkV3` and `SkV4` declare and initialize one member per line.
- The scratch inverse, the mapped vector and the mapped rects start initialized.
- The reciprocal's narrowing to `float` in `normalizePerspective` is now an explicit
  `static_cast`.

**Object code.** The arm64 Release object was compared function by function with the intake one:
- `setConcat`, the translate and scale forms, `map`, equality and rotation are
  instruction-identical;
- `MapRect` differs only in a constant-pool label number;
- `preConcat(SkMatrix)` loads the same scalars differently, with the same multiplies and adds in
  the same order;
- `invert` gains the 64-byte zeroing of its scratch array.

**New corpus.** `tests/m44.cpp` writes `m44.raw`, which CI compares. At the mechanical commit it
held 3,284 cases (567,911 bytes) and matched the intake reference byte for byte. It covers:
- construction from SkMatrix and back, and element access and assignment;
- equality under sign flips, NaN and one-ulp changes;
- every concatenation, translation and scaling form, with aliased operands;
- rotation about 18 axes at 10 angles;
- perspective normalization over 16 bottom-right entries;
- inversion in place and out of place, including singular and tiny-scale matrices;
- `map` and both vector products;
- `SkV3` and `SkV4` arithmetic and indexing;
- 1,200 rect mappings with and without perspective, some with corners behind the w = 0 plane.

Inputs include signed zeros, NaN, infinities, denormals and the float limits. Every NaN is
recorded as one canonical value, because NaN payloads follow operand order, which a respelling
may change. The test builds with `-ffp-contract=off`.

## Defects fixed

1. **Perspective normalization rounded twice and could create NaN.** `normalizePerspective`
   divides a matrix whose bottom row is [0, 0, 0, X] by X. Its comment said it worked in double
   precision. It actually rounded 1/X to float and then rounded each product, so:
   - For a sample of 220,000 entries over eleven values of X, 55,966 results (25%) were one
     rounding away from the correctly rounded quotient.
   - The same 2D matrix normalized differently as an SkMatrix, which does scale in double.
   - For |X| below about 2.9e-39, the float reciprocal overflowed, so every zero entry became
     NaN, and a finite matrix stopped being finite.
   - An infinite X made every finite entry zero, so a non-finite matrix became a finite,
     degenerate one.

   Each entry is now scaled by 1/X in double precision and rounded once, exactly as SkMatrix
   does; the sample has no misrounded entries. If X is not finite, or a scaled entry overflows,
   the matrix is left unchanged; it still maps points correctly as a perspective matrix. Affine
   matrices, X = 1, never take this path, so GUI.Forms' transforms are unaffected.
2. **Rotation axes of extreme length became the identity or lost precision.** `setRotate` is
   documented to normalize any axis. It took `sqrt(x² + y² + z²)` in float, so:
   - an axis such as (1e20, 1e20, 0), whose squared length overflows, gave the identity;
   - so did an axis whose squared length underflows to zero, such as (2⁻⁸⁰, 2⁻⁷⁹, 0);
   - between those cases, a denormal squared length (for example (1e-20, 0, 0)) produced a
     length accurate to only a few digits, so the axis was not unit length and the rotation
     matrix was not orthonormal.

   Axes whose squared length is a normal float keep the old computation exactly. Others are
   divided by their largest component first, which puts the length between 1 and √3; they now
   rotate exactly as their direction does. Zero and non-finite axes still give the identity.
   The header comment named a function that does not exist (`setRotateAboutUnitRadians`); it
   now names `setRotateUnit`.
3. **Equality depended on object identity.** `operator==` returned true early when both operands
   were the same object. So a matrix holding NaN equalled itself but not its copy. Equality is now
   always elementwise IEEE comparison.
4. **Indices were Debug-checked, and vector indexing went past its member.** `rc`, `setRC` and
   `SkV4::operator[]` checked their indices only in Debug builds; in Release an out-of-range
   index read or wrote outside the matrix. They are now Release checks. `SkV4::operator[]`
   indexed `&x` as an array, which is undefined beyond `x`. It now selects the named member.
5. **A constructor left the matrix uninitialized, and `invert` required storage.**
   - `SkM44(kUninitialized_Constructor)` produced a matrix whose every read was undefined; its
     only compiled user was `Rotate`. The constructor and its enumeration are removed.
   - The matrix storage now starts as the identity, so the concatenating constructor also starts
     initialized.
   - `invert(nullptr)` dereferenced null. It now tests invertibility only, as SkMatrix allows. It
     still computes the inverse, because `SkInvert4x4Matrix` checks that the inverse is finite
     only when it has storage for it.

## Verification

**Shared corpus.** Shared inputs whose results these fixes change move to `m44-corrected.raw`.
Both builds write it, and it is meant to differ. CI does not compare it, as with
`image-sampling-corrected.raw`. That leaves `m44.raw` with:
- rotations about the 11 axes that rotate as before, plus the unit-axis forms for every axis;
- normalization with X in {1, 0, -0, 2, -4, 0.25} and ordinary entries.

`m44.raw` now holds 3,044 cases (543,591 bytes), and it matches the reference byte for byte.
`m44-corrected.raw` holds 382 cases:
- `Rotate` and `setRotate` about the seven axes whose squared length is not a normal float
  (70 cases);
- normalization with 12 other values of X, including tiny, denormal, huge, infinite and NaN, and
  with X = 2 over extreme and non-finite entries (312 cases).

**Candidate-only checks** (`#ifndef STX_SKIA_REFERENCE`):
- a NaN matrix equals neither itself nor its copy;
- 512 2D matrices, over eight values of X, scale their six entries to SkMatrix's bits exactly.
  The bottom rows agree by value: SkM44 scales its bottom row too, so for negative X it leaves
  -0 where SkMatrix keeps +0, as the intake did;
- tiny and denormal X stay finite, with zeros kept;
- overflowing, infinite and NaN X leave the matrix unchanged;
- six extreme axes rotate exactly as their directions do, and zero and infinite axes give the
  identity;
- `invert(nullptr)` agrees with `invert(&m)` on 260 matrices.

**Gates.** All were run sequentially at the fix commit:
- Release candidate: 77/77;
- reference: 76/76;
- all shared artifacts match, except the intentional `image-sampling-corrected.raw` and
  `m44-corrected.raw`;
- instrumented suite: PASS.

The first run of the fix stopped at two test mistakes:
- The 3x3 check compared the bottom rows by bits, which caught the -0 above.
- The rotation corpus put the changed forms in the shared file.

Both were corrected in the test, without changing the library.

**Inventory.** The whole-library AST inventory finds no excluded constructs in either file; the
library total falls from 1,910 in 93 files to 1,839 in 91. The lexical checker reports nothing
for the two sources or the test.

## Open

- Resolved in MATRIX_INVERT_CONVERSION.md: `SkInvert4x4Matrix` returned the determinant as a
  float, so a matrix whose double-precision determinant is at most about 7e-46 had a finite
  inverse but was reported as non-invertible (for a 2D scale s, |s| below about 2.6e-23).
  `SkInvert3x3Matrix`, which had the same narrowing, is removed.
- `SkMatrix::normalizePerspective` still turns a matrix with an infinite bottom-right entry
  into a finite one; that belongs to the 3x3 matrix unit.
- `SkV3::Normalize` and `SkV4::Normalize` return NaN for a zero vector, as upstream does. No
  compiled caller passes one.
- The Release checks have no death tests.
- `SkV3` remains in `SkCamera`, which is not yet converted.
