# 3x3 matrices

Converted: `include/core/SkMatrix.h`, `src/core/SkMatrix.cpp`,
`src/core/SkMatrixPriv.h` and `src/core/SkMatrixUtils.h` (paths relative to
`skia/`). The original header and implementation were already preserved under
`baseline/skia/`; the two private headers are now preserved there as well.

## Mechanical step, kept separate from the review

`this->` qualifiers were removed, then the GUI.Forms AST house-policy tool
generated pointer-arrow rewrites from the exact compilation database. Only
records for the four matrix files were applied (63 rewrites). The `auto` plan's
byte offsets went stale once the arrows changed size, so its 13 sites were
spelled by hand from the types the tool reported.

## Changes that preserve results

- Tiny dot/cross-product wrappers (`sdot`, `scross`, `dcross`) are replaced by
  their formulas at each use. Grouping is unchanged, and the library compiles
  with `-ffp-contract=off`, so every rounding step is the same. Wrappers that
  name a precision choice remain: `muladdmul` (double accumulation),
  `scross_dscale` (single-precision cross, double scale), `dcross_dscale` and
  the row-column product `rowcol3`.
- The determinant spells its cofactor expansion as named double-precision minors;
  the evaluation order is the original's.
- `operator==` drops the `SK_RESTRICT` qualifiers on two pointers that may
  address the same matrix, and compares the nine values in a loop.
- `set9` copies elements instead of `memcpy`, so passing a matrix's own values
  is defined.
- `mapHomogeneousPoints` maps directly over its spans. The private stride-based
  routine it called had no other caller and is removed.
- Rectangle mapping reads and writes the four edges by field instead of loading
  four floats from `&fLeft`. Each path reads its source completely before writing
  the destination, so `mapRect(&r)` remains in place.
- The affine point kernel's vector of upper lanes starts at zero rather than
  uninitialized when only one point is mapped. Those lanes are computed and
  discarded; stored results are unchanged.
- `PolyToPoly` selects its polygon procedure in the switch, with a default for
  counts already rejected, instead of indexing a local table after
  `[[fallthrough]]` and ending in `SkUNREACHABLE`.
- Uninitialized locals in rectangle fitting, quadrilateral fitting and scale
  queries now start initialized. Results, type-mask masking (`kORableMasks`
  instead of a literal `0xF`) and the point-kernel table are unchanged.

## Correction

`SkTreatAsSprite` tested linear-filter translations with
`tx != (int)tx`, which is undefined for NaN, infinities and magnitudes beyond
`int`. Such translations are no longer treated as whole pixels, so the sprite
fast path is declined. In-range values give the original answers. A
candidate-only check covers `1e20`, `-3e9`, NaN, infinity, `-2147483520` and a
half-pixel offset.

## Removal scope

No public `SkMatrix` API was removed. Private helpers without a retained CPU
caller were removed:

- `SkMatrixPriv`: `InverseMapRect`, both `MapPointsWithStride` forms,
  `MapHomogeneousPointsWithStride`, `CheapEqual`, `DifferentialAreaScale`,
  `NearlyAffine` and `kMaxFlattenSize`.
- `SkMatrixUtils.h`: `SkDecomposeUpper2x2`.
- An `#if 0` fixed-step block, and stale friend declarations for two classes
  that do not exist.

`SkMatrixPriv::M44ColMajor` borrows its argument and now rejects temporaries.

## Known limitation, not changed

The type mask is cached in a `mutable` field and filled in by `const` queries.
A matrix shared between threads must have its type computed before sharing; the
existing shaders do this. Making the cache eager would conflict with the public
mutable `operator[]`, and making it atomic would change copy semantics. The
header now states this precondition. A decision is left for the canvas/shader
sharing review.

## Verification

`tests/matrix_transforms.cpp` (with the new shared helper
`tests/differential_corpus.h`) covers 30 named matrices (identity, translations,
scales including zero, tiny and overflowing ones, the quarter turns, a pivoted
rotation, skew, perspective, `persp2 != 1`, NaN and infinite entries, a zero
bottom row, sine/cosine and RSXform constructions), 1,200 random matrices across
six shapes, and 64 sprite-oriented scale-translates. Per matrix it records
classification, scale queries, decomposition, inversion through both
interfaces, perspective normalization, point mapping at every count from 0 to 9
(out of place, in place and as vectors), short destinations, homogeneous
mapping in and out of place, five rectangles (including unsorted, NaN and empty),
quadrilateral mapping, the kernel table, concatenation in every form including
self-aliased, all pre/post edits with and without pivots, integer division,
element mutation, serialization with short reads, and the sprite test for three
sizes, three samplings and both antialiasing modes. It adds 1,500 rectangle
pairs over all four fitting modes and polygon fits of every count, including
mismatched counts and the unit square.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 61/61 PASS.
- Reference release: 60/60 PASS.
- Matrix corpus: 2,794 cases, 7,305,342 bytes, identical hash
  12189350721306342545 in both builds.
- Shared raw artifacts: all twenty-one common artifacts match byte for byte
  except `image-sampling-corrected.raw`, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST-checker violations in the four files and zero lexical findings, including the new tests.

No timing improvement is claimed. The point kernels keep their vector form;
this record does not include an assembly comparison.
