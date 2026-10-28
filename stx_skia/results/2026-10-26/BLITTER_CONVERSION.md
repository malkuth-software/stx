# Blitters

Converted (relative to `skia/`): `src/core/SkBlitter.h`, `src/core/SkBlitter.cpp`,
`src/core/SkCoreBlitters.h`, `src/core/SkBlitter_A8.cpp`,
`src/core/SkBlitter_ARGB32.cpp` (outside its LCD kernels), `src/core/SkRasterPipelineBlitter.cpp`,
`src/core/SkBlitter_Sprite.cpp`, `src/core/SkSpriteBlitter.h`,
`src/core/SkSpriteBlitter_ARGB32.cpp` and `src/core/SkAutoBlitterChoose.h`; the region span
visitor in `src/core/SkRegionPriv.h` and `src/core/SkRegion.cpp`. `src/core/SkBlitBWMaskTemplate.h`
is replaced and removed from the candidate. `src/core/SkBlitter_A8.h` was reviewed and needed
no change. The intake (490c9bd) copies are retained under
`baseline/skia/` and the reference profile selects the original sources; `SkBlitter_Sprite.cpp`
was already retained there.

## Changes that preserve results

- The AST tool applied 205 pointer-arrow rewrites; the arrows inside assertions, debug-only
  code and macros were converted by hand.
- The region span visitor takes a named, non-owning `SkRegionSpanVisitor` interface instead
  of a `const std::function&`. Region blitting implements it in a small named class instead of
  a capturing lambda.
- The BW-mask include template, configured by five macros and two pixel macros, becomes a
  function template over named solid and blend functors.
- The A8 blitter's `WRAP_BLIT` macro, which expanded to two lambdas per blend mode, becomes
  row templates instantiated on the blend function.
- Thirteen lambdas in total become named functions or types (the chooser's raster-pipeline
  fallback, the four memset kernels, the mask-plane setup, the pipeline dump, the coverage
  kernel driver and its two A8 kernels, the two LCD channel helpers and the region visitor).
  `goto`s, the `SK_INIT_TO_AVOID_WARNING` and MSVC warning pragmas, `auto`, C-style casts,
  multi-name declarations and `SkDEBUGCODE` are gone; members and locals start initialized.
- The shader blitter's row buffer is owned by `AutoTMalloc` instead of `sk_malloc`/`sk_free`.
  The clipper, clip blitters and sprite blitters state what they borrow; the clipper cannot be
  copied; single-argument blitter constructors are explicit; `SkAutoBlitterChoose` drops its
  unused arrow operator.
- The contract that `blitAntiH` run and coverage arrays are writable, despite their `const`
  parameter types, is now stated on `SkBlitter`: the clipping blitters split runs in place.
- The direct-blit value is read from aligned converted bytes with `memcpy` instead of through
  an inactive union member.

## Corrections

- **Row addresses.** The BW and A8 mask paths of the base blitter, the A8 blitters, the
  32-bit solid, black, shader and LCD paths, the 32-bit mask copy, and the memcpy and 32-bit
  sprites stepped their row pointers once more after the last row. When the drawn rectangle
  ends at the bottom of its pixmap or mask away from the left edge, that forms an address
  outside the allocation. Every row's address now comes from its pixmap or mask.
- **BW mask template.** The template backed the device pointer up to the mask byte boundary,
  before the row's first pixel when the clip starts at the left edge, and advanced it eight
  pixels per byte past the clip. Each byte's pixels are now indexed from the clip's left edge,
  and only set bits form addresses. When the clip covered the whole mask it also drew every
  byte of the row without the right-edge mask, so padding bits beyond the mask width were
  drawn. They are now masked. A candidate-only check covers the padding.
- **32-bit mask columns.** The 32-bit mask copy addressed the mask with the byte accessor, so a
  mask clipped from its left edge was read `3 * (clip.left - mask.left)` bytes too early,
  shifting its pixels. It now uses the 32-bit accessor. A candidate-only check covers it.
- **Sprite source origin.** The raster-pipeline sprite formed the source's backed-up origin
  with pointer arithmetic, which leaves the allocation whenever the source is offset; it is now
  computed in integers, like the mask planes.
- **Coverage conversion.** Fat-rectangle edge coverage converted `a * 255` to `SkAlpha`
  directly, which is undefined outside the byte range and for NaN; it now saturates.
- **Clip tests.** The rectangle clip blitter's row and column tests subtracted signed
  coordinates; they now use unsigned differences, exact for every input.

## Removal scope

`SkBlitBWMaskTemplate.h` (replaced by the function template), the unreachable assertion after
the coverage blitter's run loop, and the MSVC warning pragmas.

## Not changed

The LCD row kernels of `SkBlitter_ARGB32.cpp` (SSE2, NEON, LASX, LSX and the portable
fallback, 1,250 lines) are imported upstream optimized code and remain unchanged, as do the
`SkBlitRow` and `SkBlitMask` kernel sources. The raster pipeline blitter keeps its compiled
pipelines in `std::function`, which `SkRasterPipeline::compile()` returns; changing that
belongs with the raster pipeline. The mask-plane origin it gives the pipeline is still an
integer-computed address outside the mask, as the pipeline's addressing requires. Legacy
shader contexts are compiled out, so `SkBlitter::Choose` never builds the 32-bit shader blitter;
the corpus constructs it directly.

## Verification

`tests/blitters.cpp` hashes the destination pixels after each case. It chooses 2,400 blitters
for N32, RGBA 8888, BGRA 8888, RGB 565, F16 and ARGB 4444 destinations with six colors (black,
white, opaque, translucent, transparent and nearly transparent), six blend modes, dithering,
gradients, a color matrix filter and clip shaders, and drives each through spans, runs,
columns, rectangles, antialiased rectangles, pairs, fat rectangles and A8, BW, LCD16, 32-bit
and 3D masks, whole and clipped, recording the direct-blit value. It drives 1,500 rectangle
clip, region clip and clipper cases with calls extending past the clip on every side, records
every forwarded call, and exercises region, rectangle-region and mask-region blits and the base
class's default operations. It runs 800 A8 cases (blend and coverage blitters), 800 sprite cases
(memcpy, 32-bit and raster-pipeline sprites across four destination and four source color
types) and 600 32-bit shader blitter cases with opaque and translucent test contexts. The shared
corpus keeps 32-bit masks unclipped on the left, where the reference differs. The region
corpus calls the span visitor through each build's interface.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 72/72 PASS, including both candidate-only checks.
- Reference release: 71/71 PASS.
- Blitter corpus: 6,100 cases, 8,072,880 bytes, identical hash 8918093221168513735 in both builds; the region corpus is unchanged (hash 14511883882676225664).
- Shared raw artifacts: 32 of 33 common artifacts match byte for byte, including the render, scan-converter and region corpora; `image-sampling-corrected.raw` differs, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST-checker violations in the unit's files, zero lexical findings and zero code-integrity candidates, including the new test.

No timing improvement is claimed and no algorithm changed. Row loops now compute each row's
address from its coordinates instead of adding the stride, the BW template tests each bit
instead of using an unrolled macro, and the span visitor is a virtual call where the
`std::function` was an indirect one; none of these costs was measured.
