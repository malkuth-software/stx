# Regions and raster clips

Converted (relative to `skia/`): `include/core/SkRegion.h`, `src/core/SkRegion.cpp`,
`src/core/SkRegionPriv.h`, `src/core/SkRegion_path.cpp`, `src/core/SkAAClip.h`,
`src/core/SkAAClip.cpp`, `src/core/SkRasterClip.h`, `src/core/SkRasterClip.cpp` and
`src/core/SkRasterClipStack.h`. Region, antialiased-clip and raster-clip sources were already
retained under `baseline/skia/` because of earlier boundary corrections; this unit adds the
intake (490c9bd) copies of `SkRegion.h`, `SkRegionPriv.h`, `SkRegion_path.cpp` and
`SkRasterClip.h`, and the reference profile now also selects the original `SkRegion_path.cpp`.
The earlier corrections (coordinate-limit translation, serialized-region validation, the
clipping blitter's run and mask bounds, clip-shader handling and stack resizing) are kept.

## Changes that preserve results

- The AST tool applied 507 pointer-arrow rewrites; the arrows inside assertion and debug-only
  code were converted by hand.
- The region and antialiased-clip run heads are now constructed in place in their
  allocations. Both hold a `std::atomic` reference count and were used as if `sk_malloc`
  storage already contained one.
- The region builder's working scanlines are positions in its run storage, with named field
  offsets, instead of a `Scanline` struct cast over that storage. Translation, run bounds,
  serialization validation, span visits, row expansion, trimming and run merging index their
  runs instead of post-incrementing pointers.
- The antialiased clip builder's rows own their run bytes through `std::unique_ptr` in a
  `std::vector` instead of raw `new`/`delete` pointers in an `SkTDArray`, and the current row
  is an index.
- Six lambdas (two iterator-advance helpers, the two alpha operators, the row expander and
  the run appender) become named functions. The `SkRegion_gEmptyRunHeadPtr`,
  `SkRegion_gRectRunHeadPtr`, `QUICK_EXIT_TRUE_COUNT`, `assert_valid_pair`,
  `AUTO_AACLIP_VALIDATE` and `AUTO_RASTERCLIP_VALIDATE` macros become constants, functions and
  always-declared validators whose checks are empty in Release. `typedef`, `using std::swap`,
  `auto`, `SK_INIT_TO_AVOID_WARNING`, `SkDEBUGCODE`, C-style casts, chained assignments and
  multi-name declarations are gone; every member and local starts initialized.
- The verb tables of the region path filler become exhaustive switches. Named results
  throughout.
- Raster-clip, wrapper and clip-stack accessors that return borrows of the object refuse
  temporaries.
- The region's span visitor keeps its `std::function` parameter: its one caller is the
  unconverted blitter, and changing the signature belongs with that unit.

## Corrections

- **Mask source rows.** The antialiased clip's mask blitter advanced its source pointer one
  row past the clip's last row, which forms an address outside the mask when the clip ends at
  the mask's bottom away from its left edge. Each row's address now comes from the mask.
- **Unsupported mask formats.** A partly covered 32-bit mask reached a null merge function
  and called it. It is now ignored, like other unsupported inputs of that blitter. A
  candidate-only check covers it.
- **Raster clip assignment.** Assigning an integer clip over an antialiased one kept the old
  antialiased runs alongside the new region, against the class's own invariant (validated in
  debug builds), and converting to antialiased kept the old region. Both now release the
  representation that is no longer active. A candidate-only check covers assignment.
- **Release bounds.** The region operator's span writes, its run array's index operator, the
  region builder's span writes, the boundary-path edge searches and the antialiased clip's
  row search now stop in Release (or, for the row search, stay on the last row) instead of
  relying on Debug assertions. Valid regions and clips never reach these checks.

## Removal scope

The Android-framework-only `SkRegion::toString`, the debug-only `SkAAClip::debug` and its row
dumper, the unused builder `dump` functions, the `#if 0` interval counter, the fuzzer-only
row limit in the clip operator, MSVC warning pragmas, and the `small_memcpy`/`small_bzero`
wrappers.

## Not changed

The region operator's 256-entry stack array is still left uninitialized: every element is
written before it is read, and upstream measured the saving. The restricted-pointer
declarations in the clip blitter's merge kernels are upstream's and describe distinct
scratch, mask and clip storage.

## Verification

`tests/region_clips.cpp` records regions through their serialized runs and every query, and
antialiased clips through their bounds and a hash of their full coverage mask. It builds
2,000 region pairs (empty, rectangles, unions of up to twelve rectangles, and paths) and
applies all six operators, rectangle operators in both orders and in place (checking the
shared original stays unchanged), point, rectangle and region containment and intersection
tests, iteration, clipped iteration, span iteration, span visits, boundary paths, nonzero
translations in place and into another region, serialization round trips, assignment and
swap. It fills 2,000 random paths (every fill rule) into regions through rectangular,
complex and tiled clips. It builds 1,500 antialiased clips from rectangles, regions and paths
(antialiased and not), applies both clip operators with clips, integer rectangles and
fractional rectangles, translates them, and draws spans, runs, columns, rectangles and A8,
BW, LCD16 and 3D masks through the clipping blitter. It applies 1,500 sequences of rectangle,
rounded-rectangle, path, region and integer-rectangle operations under five transforms to raster
clips, translating, copying and assigning between both kinds. Finally it drives 400 clip
stacks through saves, restores, clips and replacements. The shared corpus avoids the inputs
where earlier corrections already differ from the reference: coordinate-limit translations,
invalid serialized regions, runs or masks outside the clip, clip shaders and stack resizing.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 71/71 PASS.
- Reference release: 70/70 PASS.
- Region and clip corpus: 7,400 cases, 35,360,783 bytes, identical hash 14511883882676225664 in both builds; no Release check tripped.
- Shared raw artifacts: 31 of 32 common artifacts match byte for byte, including the render and both scan-converter corpora; `image-sampling-corrected.raw` differs, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST-checker violations in the unit's files, zero lexical findings and zero code-integrity candidates, including the new test.

No timing improvement is claimed and no algorithm changed. In-place construction, the
Release bounds, the rows' separate ownership and the earlier release of inactive clip
representations were not measured.
