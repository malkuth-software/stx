# Mask cache and Gaussian filter helper — 2026-10-27

Converted (relative to `skia/`): `src/core/SkMaskCache.cpp/.h` and `src/core/SkGaussFilter.cpp/.h`.
`src/core/SkMaskBlurFilter.cpp` receives one narrow change: its three `decltype` aliases.
The intake (490c9bd) copies of the four mask-cache and Gaussian files are added under
`baseline/skia/`, and `cmake/reference.cmake` selects them. The candidate copies were
unchanged from intake until this unit. `SkMaskBlurFilter.cpp` already had a reference copy.

## Changes that preserve results

- **Mask cache.** The `CHECK_LOCAL` macro becomes two named functions: `find_in_cache` and
  `add_to_cache`. A null local cache still selects the process-wide cache. Records go to the
  cache through a `std::unique_ptr`, released into `add`/`Add`, which take sole ownership even
  on rejection. `void` functions no longer return a `void` expression. Member arrows become
  `(*p).m`, and the visitors bind the cached data and the lazy result as references. The C-style
  context cast becomes `static_cast`. The global `MaskValue` record moves into the anonymous
  namespace. The rounded-out bounds of the rectangle key are computed directly, not through an
  uninitialized `SkIRect`. The style-to-`int32_t` and integer-to-scalar conversions in the keys
  are explicit. Namespace labels and `fSizes` start initialized.
  - The key layout, key byte counts, hash input, record byte charges and categories are
    unchanged. The header now states the reference handoff, borrow lifetime, unchanged-mask
    failure state and non-null data precondition.
- **Gaussian filter.** The two Bessel-function lambdas become `bessel_i0` and `bessel_i1`, with
  the same expressions, including the integer `k * k` and `k * (k + 1)` divisors. `auto`
  becomes `double` and `int`. `size()` converts explicitly, `width()` names its result, and
  `fBasis`/`fN` start initialized. The header documents the live factor range and the one
  extra factor that stops the recurrence.
- **Blur kernels.** `ToA8`, `BlurX` and `BlurY` are spelled as function types instead of
  `decltype` of a sample kernel. A function that does not match exactly cannot be passed as
  one of these pointer types, so the compiler still checks every kernel against its alias.

## Corrections

- **Rectangle key extent.** The rectangle key reads one or two rectangles. A Debug-only
  assertion becomes a Release check. The only caller, the blur filter's nine-patch path,
  passes one or two.
- **Gaussian recurrence bound.** The recurrence writes `b[n+1]` and `gauss[n+1]` into six-entry
  arrays, and only a Debug assertion limited sigma to below 2. A Release check now stops a
  write past the arrays. For sigma below 2 the loop ends with n at most 5, so the check never
  fires on the admitted range. The blur filter calls this helper only for sigma below 2.

## Verification

`tests/mask_cache.cpp` writes `mask-cache.raw`, which both profiles produce and CI compares.

- **Gaussian filter:** 2,006 sigmas (every 0.001 from 0 to 1.999, plus six edge values up to
  the largest double below 2). Each records size, radius, width and every factor. Both builds
  also require one to five positive, nonincreasing factors.
- **Rounded-rectangle masks:** a local cache holds 48 masks (four rounded rectangles, three
  sigmas, four styles). The test looks each one up, plus misses for a one-ulp sigma change, an
  integer and a fractional translation, and a replacement duplicate. It records byte charges
  before and after purging.
- **Rectangle-set masks:** pairs and single rectangles under every sigma and style. Integer
  translations hit, because the key holds sizes, the pair offset and fractional phase. Fractional
  translation, swapped pairs and a first-rectangle-only query miss.
- **Discarded storage:** a discardable-memory cache relocks surviving storage and returns the
  same pixels. For purged storage the lookup misses and the stale record is removed, with its
  byte charge.
- **Process-wide cache:** one add and lookup through the global cache.
- Every hit checks that the returned mask borrows the returned data's pixels and hashes them.
  Every miss checks that the mask stays unset.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 74/74 PASS. Reference release: 73/73 PASS.
- Mask-cache corpus: 2,290 cases, 106,440 bytes, identical hash 4851679467673625958 in both
  builds.
- Shared raw artifacts: 34 of 35 common artifacts match byte for byte, including mask, mask-filter
  and canvas output; `image-sampling-corrected.raw` differs, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: the AST inventory reports zero findings in the five files, and the lexical
  checker reports none in them or the new test. The whole-library count falls from 2,731 to
  2,680, in 102 files.

No timing claim is made. The Release checks add one comparison per key construction and per
recurrence step.
