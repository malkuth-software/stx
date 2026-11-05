# Blurred path and point divergence — 2026-10-27

The canvas and device unit (0453cb1, `../2026-10-26/CANVAS_DEVICE_CONVERSION.md`) found that
blurred paths and blurred point draws differ from the reference, and that the difference
predates that unit. This record finds where it came from.

## Verdict

The difference is **not an intended correction**. Commit c046c51 ("Convert blur composition and
optimize exact coverage products", 2026-10-25) added two input rejections to `SkBlurMask`. In
both cases the mask filter reports failure, and the draw falls back to the **unblurred**
geometry. The blur is silently lost. Neither rejection is described in
`../2026-10-25/BLUR_STYLES.md`, which states that mask output is byte-identical, and its corpus
contains neither input. The original code is memory-safe on both inputs (see below), so the
rejections remove no defect.

## Bisection

Two temporary probes were built against the library at each commit, in a separate worktree
with its own mirror. Each probe prints one hash per drawn bitmap.

- **Path probe:** 288 blurred path draws, covering the fill rules (including inverse fills),
  styles and anti-aliasing.
- **Point probe:** 216 blurred `drawPoints` cases: three point modes, stroke widths 0, 0.5, 2.5
  and 6, every cap, both anti-aliasing settings and three styles. The blur is normal style with
  sigma 1.5.

| Commit | Path cases differing | Point cases differing |
|---|---:|---:|
| reference profile | 0 | 0 |
| 0b9f2be (parent) | 0 | 0 |
| c046c51 | 144 (all inverse fills) | 12 |

The 12 point cases are all point mode at stroke width 0.5 with butt or square caps. Reference
hashes in those cases do not depend on anti-aliasing. Candidate hashes split by anti-aliasing
setting, which is what unblurred fallback drawing would produce. The probes were temporary and
are not committed.

## Mechanisms

The mechanisms come from reading the code at c046c51 and following its failure returns. They
match the probe pattern above. The fix below confirms them: removing exactly these two
rejections makes the restored canvas corpus byte-identical to the reference.

1. **Inverse fills: the BoxBlur extent check.** `SkBlurMask::BoxBlur` now begins with:

       if (width <= 0 || height <= 0 || width > INT32_MAX || height > INT32_MAX) return false;

   For an inverse fill, `SkDraw`'s mask path asks the filter for its margin with a
   metadata-only mask (`fImage == nullptr`). That mask's bounds are the saturated infinite
   rectangle, so its 64-bit width is about 2^32 and the check fails. `compute_mask_bounds`
   returns false, `DrawToMask` returns false, and the filter's `filterPath` fails. The draw then
   continues unblurred. The original had no such check. Its metadata path only computes the
   border; the real mask is later sized to the clip-intersected bounds. Every blurred
   inverse-filled path is affected.

2. **Narrow rectangles: the BlurRect profile check.** `SkBlurMask::BlurRect` now rejects:

       if (width < profileSize || height < profileSize) return false;

   Here `width` and `height` are the padded mask extents. A 0.5 × 0.5 point square with sigma 1.5
   pads to 8 or 9 pixels, but the profile size is ceil(6 × 1.5) = 9. `filterRectMask` fails,
   `filterRectsToNine` returns `kFalse`, `filterRects` passes it through, and `filterPath`
   returns false. Rectangle-shaped paths therefore fall back unblurred. Round caps produce a
   non-rectangular path and are unaffected. The original accepts the case: in
   `ComputeBlurredScanline`, `sw = width - profile_size` wraps as unsigned, so the
   `profile_size <= sw` branch selects `ProfileLookup` with a negative sharp width.

## Why the original is safe on these inputs

- **Inverse fills.** The metadata-only query allocates nothing and returns the border from the
  blur filter's own destination preparation. Pixel allocation happens later, for the
  clip-bounded mask, and keeps its own size checks.
- **Narrow rectangles.** Let W < P be the padded width and the profile size, and let
  c = (P & ~1) − 1 ≤ P − 1. Then the sharp width is w = (W − P) − c. In `ProfileLookup`,
  |2x + 1 − W| ≤ W − 1, so dx ≤ P − 1 + c ≤ 2P − 2 and the index ox = max(dx >> 1, 0) ≤ P − 1.
  This stays inside the P-entry profile. Both lines of that function stay far inside `int`
  range at the extents BlurRect already admits.

## Effect on records

- The canvas corpus's exclusion of blurred paths and points (`tests/canvas_device.cpp`) is
  caused by this regression. The fix below lifts it.
- The c046c51 rejections that are documented remain separate decisions: invalid style and
  enumerations, non-finite inputs, padded coordinate limits, and the inconsistent-rounding
  rejection for fractional inner rectangles.

## Fix

- **BoxBlur:** the non-positive extent rejection stays; the empty-input case is a documented
  c046c51 decision that drawing never reaches. The INT32_MAX limit now applies only when the
  source has pixels. The metadata-only margin query keeps the original border computation;
  the blur filter's own destination preparation already returns an empty descriptor for those
  bounds without allocating, and `SkDraw` uses only the margin.
- **BlurRect:** the profile-size rejection is removed. `ComputeBlurredScanline` handles a width
  below the profile size explicitly. It computes the original lookup offset,
  (|2x + 1 − W| − ((W − P) − c)) / 2, in signed 64-bit arithmetic, where the original relied on
  unsigned wraparound. The index is checked against the profile size in Release; the bound
  proven above makes the check a guard, not a behavior. The scanline precondition and header
  documentation now admit any positive width up to INT32_MAX.

## Regression checks

- **Mask corpus (`tests/mask.cpp`, shared):** 144 analytic rectangles with padded extents
  below the profile size. Widths are 0.25, 0.5, 0.75 and 7, heights 0.5 and 1, at two origins,
  with sigma 1.5, 4 and 9 in normal, solid and outer styles. Each has its metadata companion.
  Both builds also check 16 metadata-only `BoxBlur` margin queries with saturated
  `INT32_MIN`/`INT32_MAX` bounds: each must succeed, allocate nothing, and return the margin
  of a bounded query with the same sigma and style.
- **Canvas corpus (`tests/canvas_device.cpp`):** the primitive corpus draws its paths and points
  with the paint's blur again. Sixteen explicit draws must differ from the same draw without its
  blur, which is how the lost blur showed. They cover inverse winding and inverse even-odd
  fills, and half-pixel butt- and square-capped points, each at sigma 1.5 and 4, with and
  without anti-aliasing. The blurred pixels are also hashed into the corpus.

## Verification

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 73/73 PASS. Reference release: 72/72 PASS.
- Canvas and device corpus: 3,856 cases, 279,848 bytes, identical hash 618473508008738493 in
  both builds. The regression draws pass in both.
- Mask corpus: 1,455,390 bytes, identical hash 4667190342981395946 in both builds (previously
  1,267,548 bytes).
- Shared raw artifacts: 33 of 34 common artifacts match byte for byte; `image-sampling-corrected.raw`
  differs, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House style: the lexical checker reports zero findings in the four changed files.

No timing claim is made. The narrow-width loop runs only for padded extents below the profile
size, and the wide and intermediate scanline paths are unchanged.
