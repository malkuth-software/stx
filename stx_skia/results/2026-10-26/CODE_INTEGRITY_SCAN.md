# Code-integrity scan of stx_skia

The GUI.Forms `code_integrity_checker` (commit 7790b881 in the code-integrity checkout;
its 20 self-tests pass) was run at the owner's request over `skia/include`, `skia/src`,
`src` and `tests`. It is a source-shape scanner: it does not compile, execute or prove
anything, and a missing finding certifies nothing. It skips `build/` and `baseline/`.
Reports: `build/code-integrity/stx_skia-full.json` (before) and
`build/code-integrity/stx_skia-after.json` (after); `build/` is generated output.

## Counts

Before: 1,725 files, 106 review candidates. 41 were in sources already converted to house
style or in tests; 65 were in imported Skia not yet converted (built and unbuilt alike).
After: 1,726 files (one new test header), 66 candidates: the 65 imported ones and one
reviewed test candidate.

## Resolved in converted sources

| Location | Rule | Change |
| --- | --- | --- |
| `SkMatrix::get`, `set` | I03 | Release check on the index instead of a debug assertion. |
| `SkPath::getPoint` | I05 | Compares a non-negative index with the `size_t` count instead of truncating both to `unsigned`. |
| `SkPath::Iter::setPath`, `ContourIter` | I04 | An empty verb span leaves the cursors null; endpoints come from the last element. |
| `spans_overlap` (path builder) | I04 | Separate empty exits; endpoints from the last element. |
| `SkPathBuilder::rMoveTo` | I03 | Reads the last move point only when its index is in range. |
| `SkPath_PointIterator::current` | I03 | Indexes modulo the extent instead of asserting it. |
| Path extrema helpers (quad, conic, cubic) | I03 | Clamp an impossible extremum count to zero in Release. |
| `SkPixmap` erase | I03 | Rejects an unsupported pixel shift before indexing the fill table. |
| `SkRectMemcpy` | I06 | Row offsets advance by addition inside the already validated last-row extent. |
| `SkColorMatrix::setSaturation` | I04 | Rows are addressed by index into the fixed array. |
| Gradient position and legacy color accessors | I03 | Release check on the stop index. |
| Perlin noise permutation | I03 | Unsigned remainder keeps the index in range by construction. |
| Stroker `cubic_in_line`, `quad_in_line` | I03 | Return "not in line" when no outer pair is found. |
| Contour measurement | I03, I05 | Clamp the searched segment, check a segment's points fit before reading them, reject an inverted segment range, and reject an empty contour. |
| Tests (seven files) | I04, I05 | A checked `test_sizes::to_int` helper and empty-safe sort endpoints. |

One of these was a latent out-of-range read: when every pairwise difference in
`quad_in_line` is NaN, no outer pair is chosen, both indices stay 0 and the original read
`quad[3]` of a three-point array. The stroker's degenerate-vector checks reject such input
first, so no retained caller reaches it; the function now answers "not in line", the
value its comparison would give.

The remaining in-scope candidate, `tests/mipmap.cpp:425` (I01), is a valid contract:
`attachToCacheAndRef()` keeps a reference while the client pointer is reset, which is what
the test exercises. It is left as is.

## Correction: bounds helpers in Release

An earlier version of this section said `sk_collection_check_bounds` and
`sk_collection_check_length` make an out-of-range index undefined in Release. That was
wrong. Their Release failure path is `SkUNREACHABLE`, which this tree defines as
`__builtin_trap()` (Clang/GCC) or `__fastfail` (MSVC), so such an access terminates. A
probe confirmed SIGTRAP for out-of-range `SkTDArray` and `SkSpan` reads. The I03 candidates
resolved above still benefit from a local, evident bound. Some of them index raw arrays or
`std::array` without these helpers. The measured cost of the alternatives is in
`BOUNDS_FAILURE_MEASUREMENT.md`.

## Verification

Sequential AGENTS.md commands on the M4 Mini:

- Candidate release: 66/66 PASS.
- Reference release: 65/65 PASS.
- Shared raw artifacts: all twenty-six common artifacts match byte for byte, except `image-sampling-corrected.raw`, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- Lexical house-style check: zero findings in changed files except two existing arrow
  uses at `SkPerlinNoiseShaderImpl.h:70` and `:72`, outside the changed lines.

These are source-boundary changes; results for valid inputs are unchanged, and no timing
claim is made.
