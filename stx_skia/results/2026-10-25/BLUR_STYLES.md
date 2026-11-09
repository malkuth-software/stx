# Blur styles and analytic rectangle kernels — 2026-10-25

This batch converts SkBlurMask.cpp/.h. Original versions remain under
baseline/skia. Normal, solid, outer and inner style composition remains available;
rounded-rectangle filter implementation is retained elsewhere. The removed
BlurRRect declaration had neither an implementation nor a source caller.

## Changes

- Style-composition kernels derive live row addresses and only advance source
  iterators when another row remains. Inner masks have named, separately retained
  allocation owners before their borrowed descriptors.
- Analytic scanlines select the wide-profile or narrow-profile formula before
  traversal. Two unused temporary scanline allocations per rectangle disappear.
- Profile lookup centers a valid coordinate before doubling it. This retains
  signed 32-bit arithmetic without overflow at the documented coordinate limits.
- Rectangle preparation checks finite inputs, enum choices, padded coordinates,
  extents and scratch sizes. Pixel storage is published only on success.
- Fractional inner rectangles whose rounded extent disagrees with the existing
  floor-sized scanline convention fail explicitly. This analytic inner shortcut
  is not used by the current nine-patch rectangle caller, which already excludes
  inner/outer styles; the general mask path remains available.
- Direct Gaussian reference convolution respects source row stride, initializes
  padding, checks temporary products, and retains allocations through failures.
  Metadata-only calls no longer construct an unused Gaussian window.

BoxBlur retains its no-effective-blur convention: false for the original-mask
styles, true with an empty result for outer style. Private descriptor/storage
preconditions are documented in the header; this is not a new public renderer API.

## Exact bounded coverage arithmetic

The rectangle reconstruction multiplies horizontal and vertical byte coverage.
Its existing rounded divide by 255 is:

    rounded = horizontal * vertical + 128
    result = (rounded + (rounded >> 8)) >> 8

The product is at most 65,025; rounded is at most 65,153; their final pre-shift
sum is at most 65,407. All intermediate stages therefore fit uint16_t exactly.
Spelling those limits in the types lets the compiler use eight 16-bit lanes in
128-bit registers. There are no platform intrinsics, restricted-pointer guesses,
fast-math flags or rounding changes. The retained arm64 assembly excerpt shows
umlal.8h/umlal2.8h and 16-bit shifts, replacing the earlier 32-bit intermediates.
The compiler unrolls multiple common-width groups; no wider register ISA is used.

## Measurements, including the rejected form

M4 Mac mini, selected by m4host; non-Apple Clang 23.1.2 Release/C++20. Nine
alternating trials per profile; input preparation excluded; allocation, profiles,
reconstruction and cleanup included. Widths 33 and 257 use heights 19 and 127,
with 2,000 and 80 iterations respectively. The JSON files retain every trial.

The initial conversion regressed on large rectangles: several median ratios
were 1.15–1.31. That negative record is rect-measurements-initial.json. Centering
profile coordinates alone did not remove the regression. Assembly then showed
32-bit multiply/accumulate lanes in reconstruction. Explicit proven 16-bit
intermediates produced the final result below.

| Style | Shape | Sigma 1.5 time/reference | Sigma 8 time/reference |
|---|---:|---:|---:|
| Normal | 33 × 19 | 0.730 | 0.817 |
| Normal | 257 × 127 | 0.544 | 0.521 |
| Solid | 33 × 19 | 0.759 | 0.779 |
| Solid | 257 × 127 | 0.648 | 0.583 |
| Outer | 33 × 19 | 0.741 | 0.728 |
| Outer | 257 × 127 | 0.578 | 0.569 |
| Inner | 33 × 19 | 0.802 | 0.771 |
| Inner | 257 × 127 | 0.581 | 0.574 |

These are ratios of median elapsed times, not a whole-GUI speed claim. Reproduce
with both release builds, then `python3 tools/measure_masks.py --rect`. Native
SSE2 execution, broader image/path distributions and whole-application allocation
measurements remain separate gates.

## Verification

- Nine release tests pass for each profile.
- Mask common output: 1,267,548 bytes match exactly; FNV hash 5441704247411333558.
- Coverage includes the previous 304 Gaussian/style cases, 1,200 additional
  format/style combinations, 192 analytic rectangles plus metadata companions,
  and 72 direct Gaussian reference cases.
- Separate candidate checks compare padded and tight Gaussian sources and cover
  invalid style/geometry, empty input and centered coordinate limits.
- Existing 24 render cases, 21,420 pixel conversions and pixmap output remain
  byte-identical to their original reference profiles.
- Whole Release instrumented suite: PASS. Whole Debug instrumented suite: PASS.
  Prior logs are preserved and detailed diagnostics remain unread.
- Seven selected GUI.Forms tests pass with its pinned LLVM 22.1.8 toolchain.
- A fresh relocated installed consumer builds and runs, preserving its RTTI.
- Three changed production/test files have no spelling or lifetime findings.

Generated logs: blur-style-release.log, blur-style-verification.log,
blur-style-centered.log, blur-style-lanes.log, blur-style-final-gates.log and
blur-style-gui.log under build/. Full conversion and independent review remain
ongoing; this batch does not complete filter dispatch or mask rasterization.
