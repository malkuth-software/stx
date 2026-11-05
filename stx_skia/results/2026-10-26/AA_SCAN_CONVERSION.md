# Anti-aliased scan conversion

Converted (relative to `skia/`): `src/core/SkScan_Antihair.cpp`, `src/core/SkScan_AntiPath.cpp`
and `src/core/SkScan_AAAPath.cpp`. The pre-conversion sources (unchanged since intake
490c9bd) are preserved under `baseline/skia/` and selected by the reference profile. The
analytic filler draws every anti-aliased path fill and builds anti-aliased clips.

## Changes that preserve results

- The analytic filler's three `goto`s become structured control flow (the convex walk's two
  exits are returns; new-edge insertion skips the relink through a named flag). The 217
  pointer-arrow rewrites came from the GUI.Forms AST tool; the 29 inside macro arguments and
  debug-only code were done by hand.
- Run accumulation clips its coverage input with an offset instead of moving the caller's
  pointer, and splits runs through one shared helper. The run buffer is a byte pointer into
  the real blitter's allocation, with sizes in explicit `size_t`.
- The hairline blitters, run accumulators and mask accumulator start fully initialized and
  cannot be copied; `typedef`, `using std::swap`, macros, `#if 0` alternatives, `auto`,
  chained assignments and C-style downcasts are gone, with each downcast's invariant stated.
- Named results throughout; integer-to-alpha narrowing is explicit.

## Corrections

- **Mask rows.** The mask accumulator addressed a row as `image + rowOffset - left` and
  indexed it with absolute x. Whenever the mask's left edge exceeded the row offset (every
  mask away from the origin), that formed a pointer outside its buffer. Rows are now a
  handle holding the row start and its left edge, so every access forms an address inside
  the storage, including the one column of slack on either side. The rectangle and
  coverage entry points (used for the rectangle pieces of convex fills) check that their
  columns and rows fit the mask in Release; the per-pixel path is unchanged.
- **Trapezoid buffers.** A row's coverage, temporary coverage and runs shared one `char`
  buffer (or one `new[]` of `SkAlpha`) read back as `int16_t`. They are now typed arrays,
  on the stack up to 31 pixels and owned heap arrays beyond.
- **Integer NaN test.** `any_bad_ints` looks for `0x80000000` by computing `x & -x`; negating
  that value as an `int` overflows. It now does the same test on the unsigned
  representation.
- **Frame coordinates.** `SkScalarToFDot8` converted `x * 256` to `int` directly, which is
  undefined when the value does not fit (huge or non-finite frames). It now saturates;
  in-range values are unchanged.

## Removal scope

The disabled `TEST_GAMMA` hook (it called an undefined function), the unused `SkBITCOUNT`
and `SkAlphaMulRound` macros, the `#if 0` alternatives of `any_bad_ints`, partial-pixel
coverage and triangle alpha, and the mask accumulator's redundant `memset` (its storage is
now zero-initialized).

## Verification

`tests/aa_scan_converters.cpp` records every blitter call in order, including every coverage
byte of blitted masks. It fills 3,000 random paths with convexity unresolved and resolved:
rectangles (the fat-rectangle path), ovals and one- or two-contour curves; four size classes
that select the mask, run-based and clamped run-based accumulators; every fill rule
including inverse. It fills through seven clips: rectangles, a complex region, two
anti-aliased clips (one with a hole) and one beyond the coordinate limit. It builds 600
anti-aliased clips from random paths and fills through them. It draws 2,500 anti-aliased
rectangle fills, hairline rectangles, frames with six stroke sizes, fixed-point rectangle
fills, and anti-aliased polylines (vertical, horizontal and long subdivided lines). The
non-anti-aliased scan corpus also strokes paths with every anti-aliased hairline procedure.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 70/70 PASS.
- Reference release: 69/69 PASS.
- Anti-aliased scan corpus: 6,100 cases, 36,586,904 bytes, identical hash 8888844876026413812 in both builds; no Release mask check tripped.
- Shared raw artifacts: all thirty common artifacts match byte for byte, including the render and non-anti-aliased scan corpora, except `image-sampling-corrected.raw`, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST-checker violations in the three files (remaining findings are in unconverted included headers), zero lexical findings and zero code-integrity candidates, including the new test.

No timing improvement is claimed and no algorithm changed. The trapezoid row now zeroes
its 96-byte stack buffers and the Release mask checks add comparisons on the rectangle
paths; neither cost was measured.
