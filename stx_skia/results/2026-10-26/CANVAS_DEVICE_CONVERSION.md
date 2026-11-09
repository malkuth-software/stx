# Canvas, device and CPU draw

Converted (relative to `skia/`): `include/core/SkCanvas.h`, `src/core/SkCanvas.cpp`,
`src/core/SkCanvas_Raster.cpp`, `src/core/SkCanvasPriv.h`, `src/core/SkCanvasPriv.cpp`,
`src/core/SkDevice.h`, `src/core/SkDevice.cpp`, `src/core/SkBitmapDevice.h` (reviewed; no change
needed), `src/core/SkBitmapDevice.cpp`, `src/core/SkDraw.h`, `src/core/SkDraw.cpp`,
`src/core/SkDraw_atlas.cpp`, `src/core/SkDraw_vertices.cpp` and `src/core/SkDrawProcs.h`
(reviewed; no change needed). The canvas, device and bitmap-device sources already differed
from their reference copies through earlier pruning (text, pictures, image filters, runtime
effects), and the reference profile already selected the originals. This unit adds the intake
(490c9bd) copies of `SkDraw.cpp`, `SkDraw_atlas.cpp`, `SkDraw_vertices.cpp` and `SkDrawProcs.h`
and selects them in the reference profile.

## Changes that preserve results

- The AST tool applied 724 pointer-arrow rewrites; 36 more inside assertions, macros and
  debug-only code were converted by a checked pass.
- The `RETURN_ON_NULL`/`RETURN_ON_FALSE`, `DIRECT_BLIT_LOOP`, `MAX_DEV_PTS` and `LOOP_TILER`
  macros become explicit early returns, a pixel-type template, a constant and explicit tiler
  loops. Five lambdas in the vertex drawer (crossing parameter, interpolation, plane clip,
  triangle clip and shader-color blend) become named functions.
- The four `goto`s become structured control flow: the rounded-rectangle draw computes whether
  it needs the path fallback, and the bitmap device's image-rectangle draw decides once whether
  it may draw through `drawBitmap`, with the original branch order.
- The rectangle draw maps the rectangle's two corners as points instead of reinterpreting the
  `SkRect` as two `SkPoint`s, and finiteness checks test each point instead of reading point
  arrays as float arrays. The atlas builder writes each quad at its index instead of advancing
  three pointers.
- `Bounder` no longer converts implicitly to a pointer at its own member; its bounds accessor
  refuses temporaries, and the tiler, whose draw points into itself, cannot be copied.
- `typedef`, `auto`, C-style casts (including the canvas save-record casts), chained
  assignments, multi-name declarations, nested ternaries and `SkDEBUGCODE` are gone; members
  and locals start initialized, including the canvas's record pointer, save count, surface and
  quick-reject bounds.

## Corrections

- **Path procedure.** The device-path draw selected its scan procedure through switches over
  the stroke cap and called an uninitialized function pointer when the cap matched none. A cap
  outside the enumeration now draws nothing.
- **Release bounds.** The point procedure tables and the perspective triangle clipper's
  four-point buffer are indexed under Release checks instead of Debug assertions.

## Removal scope

Configuration that this library never defines: `SK_BUILD_FOR_ANDROID_FRAMEWORK` (the
`ColorBehavior` canvas constructor, the vertices-without-texture-coordinates shader quirk and a
trace guard), `SK_SUPPORT_LEGACY_ALPHA_BITMAP_AS_COVERAGE`, `SK_DRAWBITMAPRECT_FAST_OFFSET`,
`SK_LEGACY_IGNORE_DRAW_VERTICES_BLEND_WITH_NO_SHADER`, `SK_LEGACY_INITWITHPREV_LAYER_SIZING`
(its non-legacy code is kept) and the fuzzer-only point limit; and an `#if 0` alternative alpha
computation.

## Not changed

The canvas and device keep their virtual interfaces, including the Android and GPU hooks
declared on `SkDevice`, because other retained sources implement or call them. The consumer
configuration macros `SK_SUPPORT_LEGACY_GETTOTALMATRIX` and
`SK_CANVAS_SAVE_RESTORE_PREALLOC_COUNT` remain. Plain forwarding returns of a single call are
left as they are; compound expressions are computed into named results.

## Verification

`tests/canvas_device.cpp` hashes the destination after each case. It draws 1,200 sequences of
eight operations on N32, RGB 565 and F16 canvases: every primitive (rectangles, integer
rectangles, ovals, rounded and nested rounded rectangles, circles, arcs with and without
centre, rounded rectangles by radius, paths with every fill rule, regions, paint and color fills)
and all three point modes, with fill, stroke, hairline and stroke-and-fill paints, every cap and
join, two miter limits, five blend modes, gradients, blur, dash, a color matrix and dithering,
under identity, integer and fractional translation, scale, rotation, skew and perspective, with
rectangle, rounded-rectangle, path and region clips in both operations, saves and layers;
two-point dashed lines exercise the dashed-line fast path. It draws 800 image sequences: sprites
at integer offsets, A8 images, source-rectangle subsets with both constraints, integer subsets,
nine-patches, lattices with fixed-color and transparent cells, and image shaders, under four
sampling modes, with and without blur. It draws 800 meshes: triangle and strip vertices with
and without texture coordinates and colors in five blend modes, patches and atlases with and
without colors and culling. It draws 40 cases on an 8,300-pixel-wide device, which the bitmap
device draws in tiles. Finally it records 1,000 sets of canvas queries (save count, total
matrix, local and device clip bounds, clip state, quick rejection of rectangles and paths) and
pixel writes and reads through the canvas.

**Pre-existing divergence.** With blurred paths and blurred point draws included, 25 of the
1,200 primitive cases differed from the reference. The committed candidate before this unit,
built separately with the same test, produced exactly the converted candidate's bytes, so this
unit did not introduce the difference: blurred path coverage already differs from the
reference. The shared corpus therefore draws paths and points unblurred (blurred rectangles,
rounded rectangles, arcs, regions and images remain), and the candidate before and after this
unit agree on that corpus too. The difference was later traced to two unintended rejections in
c046c51. Both were fixed on 2026-10-27, and the corpus again draws blurred paths and points (see
../2026-10-27/BLURRED_PATH_DIVERGENCE.md). The corpus figures below are from this unit's own
gates; the current corpus has 3,856 cases.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 73/73 PASS.
- Reference release: 72/72 PASS.
- Canvas and device corpus: 3,840 cases, 279,720 bytes, identical hash 16050249481463111025 in both builds and in the candidate before this unit.
- Shared raw artifacts: 33 of 34 common artifacts match byte for byte, including render, layers, mask-filter, blitter and region corpora; `image-sampling-corrected.raw` differs, an earlier intentional correction.
- Complete deterministic instrumented suite: PASS; diagnostics unread.
- House policy: zero AST-checker violations in the unit's files, zero lexical findings and zero code-integrity candidates, including the new test.

No timing improvement is claimed and no algorithm changed. The point draw now zeroes its
32-point stack buffer and the Release checks add comparisons; neither cost was measured.
