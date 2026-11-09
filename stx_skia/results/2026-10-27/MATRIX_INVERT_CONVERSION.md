# 4x4 matrix inversion: SkMatrixInvert — 2026-10-27

Scope (relative to `skia/`): `src/core/SkMatrixInvert.h` and `src/core/SkMatrixInvert.cpp`,
and the one caller, `SkM44::invert`. There are three commits:
- 14708d23: prune, with intake baselines;
- 42cc923b: mechanical conversion and the corpus;
- the fix commit that adds this record.

## Pruned (14708d23)

`SkInvert2x2Matrix` and `SkInvert3x3Matrix` served only the SkSL constant folder, which the
candidate no longer builds. `SkM44::invert` is the only caller of `SkInvert4x4Matrix`. The
reference profile still compiles the original SkSL sources, so the intake `SkMatrixInvert.h` and
`SkMatrixInvert.cpp` now serve the reference build, and `SkMatrixInvert` is listed among the
reference units. That commit builds 77/77 (candidate) and 76/76 (reference).

## Mechanical conversion (42cc923b)

- The sixteen inputs are `const`.
- Each inverse entry's double-to-float narrowing, and the determinant's, is an explicit
  `static_cast`.
- The null test is written as a comparison.

**Object code.** The arm64 Release disassembly of `SkInvert4x4Matrix` has the same 238
instructions as the intake's. One absolute branch address moves, because the pruned inverters
no longer precede the function.

**New corpus.** `tests/matrix_invert.cpp` writes `matrix-invert.raw`, which CI compares. It holds
6,134 matrices inverted out of place and in place: 12,268 inversions, 9,408 of which succeed.
The matrices are:
- general, affine and 2D-perspective;
- signed permutations;
- rank-deficient (one column a combination of two others);
- nearly singular (two columns a few ulps apart);
- small-integer;
- diagonal scales from 2^-10 to `FLT_MAX`, with and without translation;
- matrices with huge, infinite and NaN entries.

Each case records whether the matrix inverts and, if it does, the inverse.

Every entry of a shared matrix is zero, non-finite or at least 2^-10 in magnitude, so it is a
multiple of 2^-33. The double determinant is built from products of four entries, and
roundings at or above that grid, so it is either zero or at least 2^-132. Rounding it to float
cannot reach zero. Matrices with smaller entries go to `matrix-invert-small.raw` (682 cases),
because only those can change under the fix below. At the mechanical commit both files matched
the reference byte for byte. The test builds with `-ffp-contract=off`.

## Defects fixed

1. **Invertibility was decided by a determinant narrowed to float.** `SkInvert4x4Matrix`
   computed the determinant in double, but returned it as a float, and callers treated zero as
   "not invertible". A float rounds to zero at or below 2^-150 (about 7e-46). So a matrix whose
   determinant was nonzero but that small was refused, even though its inverse was finite: for
   example a 2D scale by s with |s| below about 2.6e-23, or a scale by 1e-12 in all four
   coordinates (determinant 1e-48). The
   function now returns whether the matrix is invertible: its double determinant is nonzero and
   every entry of the inverse is finite in float. Of the 1,364 small-entry inversions:
   - 200 that the intake refused now succeed;
   - none that succeeded is refused;
   - the 592 that succeed in both builds give bit-identical inverses.
2. **The checks depended on the caller passing storage.** With a null output, the function
   skipped its finite-inverse check and returned the bare determinant. So a NaN determinant,
   which is not zero, read as invertible. The function now takes both matrices as references to
   arrays of 16, so the compiler rejects a null or shorter output, and the finite check always
   runs. A singular matrix returns before dividing by zero. `SkM44::invert` keeps its scratch
   inverse, so a refused inverse never reaches the caller's matrix, and `invert(nullptr)` still
   tests invertibility.

Through `SkM44::invert`, the canvas's layer setup can now map a layer's bounds back through a
tiny but invertible transform; before, it handled such a transform as singular.

## Verification

**Corpora.**
- `matrix-invert.raw` (6,134 cases) is identical to the mechanical commit's, and it matches
  the reference.
- `matrix-invert-small.raw` now differs from the reference as intended, and CI does not compare
  it.
- In the 4x4 corpus, `SkM44` inversions of matrices with entries below 2^-10 move to
  `m44-corrected.raw` by the same rule. `m44.raw` (3,012 cases) matches the reference.

**Candidate-only checks** (`#ifndef STX_SKIA_REFERENCE`):
- 2D and 4D scales by 1e-12, 1e-20, 1e-23, 2^-60, 1e-30 and 3e-39 invert to the correctly
  rounded reciprocal;
- denormal and 1e-39 scales, whose inverses overflow float, are refused, as are singular, NaN
  and infinite matrices;
- `SkM44::Scale(1e-20f, 1e-20f).invert` succeeds with the exact reciprocal;
- a `static_assert` confirms that the inverter cannot be called with pointers.

**Gates.** All were run sequentially at the fix commit:
- Release candidate: 78/78;
- reference: 77/77;
- all shared artifacts match, except the intentional `image-sampling-corrected.raw`,
  `m44-corrected.raw` and `matrix-invert-small.raw`;
- instrumented suite: PASS.

**Inventory.** The AST inventory found nothing in these files before or after, and the library
still has 1,839 excluded constructs in 91 files. This unit's defects were numerical and in its
interface, not its spelling. The lexical checker reports nothing for the sources or tests.

## Open

- `SkMatrix` computes its own 3x3 inverse and is not affected by this change.
