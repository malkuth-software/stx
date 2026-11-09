# House-policy inventory and blur-filter dispatch status — 2026-10-27

## Blur-filter dispatch

The requested blur-filter dispatch unit is already converted, so this record changes no
source. `SkMaskFilterBase.cpp/.h` and `SkMaskFilter.cpp` were converted in e15c498. The public
`SkMaskFilter.h` was reviewed in that commit and needed no change. `SkBlurMaskFilterImpl.cpp/.h`
were converted in a09364d. The records are `../2026-10-25/MASK_FILTER_DISPATCH.md` and
`../2026-10-25/BLUR_DISPATCH.md`. Later edits in 2b9b437 and bc87d43 only removed the
image-filter adapter and the effect registry.

The ledger's blur-style notes (c046c51) still called blur-filter dispatch unconverted. They
were written before those two commits; this record corrects them. All six files have no
findings in the inventory below, and the lexical checker reports none either. Their
differential coverage is unchanged: the mask-filter corpus, plus the canvas corpus that again
draws blurred paths and points since 2333dc6.

## Inventory

The GUI.Forms AST house-policy tool ran in inventory mode, with production source scope,
over every library translation unit in the arm64 candidate build's compile commands
(`build/candidate`, at 2333dc6). The tool also inventories admitted constructs (`if
constexpr`, designated initializers, `std::span`, `has_single_bit`); they are excluded from
the counts below.

| Date | Excluded constructs | Files |
|---|---:|---:|
| 2026-10-26 (before the geometry, scan-conversion, region, blitter and canvas units) | 6,196 | — |
| 2026-10-27 | 2,731 | 106 |

Of the 2,731: 2,284 member arrows, 362 `auto`, 53 lambdas, 24 `decltype` expressions,
6 trailing returns and 2 structured bindings.

**Limits.** The tool sees only code this configuration compiles. SSE and AVX paths, Linux and
Windows ports, and headers no translation unit includes are not counted. Macros, `goto`,
`typedef` and casts belong to the lexical checker. Zero findings is not evidence of semantic
conversion.

| Findings | File | Constructs |
|---:|---|---|
| 409 | `src/opts/SkRasterPipeline_opts.h` | pointer_member_arrow 225, auto_type 173, lambda 11 |
| 295 | `src/utils/SkPolyUtils.cpp` | pointer_member_arrow 290, auto_type 5 |
| 192 | `src/core/SkRasterPipeline.cpp` | pointer_member_arrow 182, auto_type 7, lambda 3 |
| 131 | `src/core/SkStream.cpp` | pointer_member_arrow 128, auto_type 3 |
| 113 | `src/core/SkClipStack.cpp` | pointer_member_arrow 112, auto_type 1 |
| 83 | `src/utils/SkJSONWriter.h` | pointer_member_arrow 83 |
| 83 | `src/utils/SkShadowTessellator.cpp` | pointer_member_arrow 69, auto_type 14 |
| 76 | `src/core/SkDistanceFieldGen.cpp` | pointer_member_arrow 73, auto_type 3 |
| 69 | `src/core/SkString.cpp` | pointer_member_arrow 69 |
| 61 | `src/core/SkM44.cpp` | auto_type 54, lambda 4, pointer_member_arrow 3 |
| 50 | `src/core/SkTBlockList.h` | pointer_member_arrow 48, auto_type 2 |
| 50 | `src/core/SkVertices.cpp` | pointer_member_arrow 43, auto_type 5, lambda 2 |
| 48 | `modules/skcms/src/Transform_inl.h` | pointer_member_arrow 40, auto_type 4, lambda 4 |
| 48 | `src/core/SkBlockAllocator.cpp` | pointer_member_arrow 46, auto_type 1, lambda 1 |
| 48 | `src/utils/SkCanvasStateUtils.cpp` | pointer_member_arrow 45, auto_type 3 |
| 44 | `src/utils/SkNWayCanvas.cpp` | pointer_member_arrow 43, auto_type 1 |
| 43 | `src/core/SkVertState.cpp` | pointer_member_arrow 43 |
| 42 | `src/utils/SkPaintFilterCanvas.cpp` | pointer_member_arrow 42 |
| 41 | `include/core/SkString.h` | pointer_member_arrow 40, decltype_expression 1 |
| 41 | `src/utils/SkCamera.cpp` | pointer_member_arrow 37, auto_type 2, structured_binding 2 |
| 40 | `include/private/SkTArray.h` | pointer_member_arrow 40 |
| 40 | `src/utils/SkShadowUtils.cpp` | pointer_member_arrow 37, auto_type 2, lambda 1 |
| 33 | `src/core/SkMaskCache.cpp` | pointer_member_arrow 33 |
| 32 | `src/core/SkVx.h` | pointer_member_arrow 18, auto_type 10, lambda 2, trailing_return 1, decltype_expression 1 |
| 30 | `src/core/SkBlendMode.cpp` | pointer_member_arrow 29, auto_type 1 |
| 28 | `include/core/SkStream.h` | pointer_member_arrow 27, auto_type 1 |
| 28 | `src/core/SkPaintPriv.cpp` | pointer_member_arrow 25, auto_type 3 |
| 27 | `src/core/SkColorSpace.cpp` | pointer_member_arrow 21, auto_type 5, lambda 1 |
| 26 | `src/core/SkClipStack.h` | pointer_member_arrow 26 |
| 24 | `src/core/SkBlockAllocator.h` | pointer_member_arrow 24 |
| 24 | `src/core/SkPaint.cpp` | pointer_member_arrow 23, auto_type 1 |
| 23 | `src/core/SkClipStackDevice.cpp` | pointer_member_arrow 22, auto_type 1 |
| 23 | `src/shaders/SkPerlinNoiseShaderImpl.cpp` | pointer_member_arrow 21, lambda 1, auto_type 1 |
| 20 | `src/utils/SkCanvasStack.cpp` | pointer_member_arrow 20 |
| 19 | `src/core/SkArenaAlloc.h` | pointer_member_arrow 14, decltype_expression 2, lambda 2, trailing_return 1 |
| 19 | `src/opts/SkSwizzler_opts.inc` | auto_type 12, lambda 7 |
| 16 | `src/core/SkMD5.cpp` | pointer_member_arrow 16 |
| 16 | `src/utils/mac/SkCreateCGImageRef.cpp` | pointer_member_arrow 14, lambda 2 |
| 14 | `src/core/SkGaussFilter.cpp` | auto_type 12, lambda 2 |
| 14 | `src/core/SkRandom.h` | pointer_member_arrow 14 |
| 13 | `src/core/SkBitmapProcState.cpp` | pointer_member_arrow 8, auto_type 5 |
| 12 | `src/core/SkDrawShadowInfo.cpp` | pointer_member_arrow 8, auto_type 3, lambda 1 |
| 12 | `src/core/SkZip.h` | pointer_member_arrow 7, auto_type 3, decltype_expression 1, trailing_return 1 |
| 11 | `include/core/SkPaint.h` | decltype_expression 7, pointer_member_arrow 4 |
| 11 | `src/core/SkArenaAlloc.cpp` | pointer_member_arrow 10, auto_type 1 |
| 11 | `src/core/SkBuffer.h` | pointer_member_arrow 11 |
| 10 | `include/core/SkM44.h` | pointer_member_arrow 9, auto_type 1 |
| 10 | `src/core/SkVerticesPriv.h` | pointer_member_arrow 10 |
| 10 | `src/utils/SkParsePath.cpp` | pointer_member_arrow 5, auto_type 4, lambda 1 |
| 9 | `src/core/SkDocument.cpp` | pointer_member_arrow 9 |
| 9 | `src/core/SkDrawable.cpp` | pointer_member_arrow 9 |
| 8 | `src/image/SkTiledImageUtils.cpp` | pointer_member_arrow 8 |
| 7 | `include/core/SkSpan.h` | decltype_expression 5, trailing_return 2 |
| 7 | `modules/skcms/src/skcms_public.h` | pointer_member_arrow 7 |
| 7 | `src/ports/SkOSFile_posix.cpp` | pointer_member_arrow 7 |
| 7 | `src/utils/SkJSONWriter.cpp` | pointer_member_arrow 7 |
| 7 | `src/utils/SkPatchUtils.cpp` | pointer_member_arrow 5, auto_type 2 |
| 6 | `include/core/SkTiledImageUtils.h` | pointer_member_arrow 6 |
| 6 | `src/core/SkStreamPriv.h` | pointer_member_arrow 6 |
| 6 | `src/shaders/SkBitmapProcShader.cpp` | pointer_member_arrow 5, auto_type 1 |
| 5 | `src/core/SkPathUtils.cpp` | pointer_member_arrow 5 |
| 4 | `src/core/SkBuffer.cpp` | pointer_member_arrow 4 |
| 4 | `src/core/SkColor.cpp` | pointer_member_arrow 3, lambda 1 |
| 4 | `src/shaders/SkTriColorShader.cpp` | pointer_member_arrow 3, auto_type 1 |
| 3 | `src/core/SkAnnotation.cpp` | pointer_member_arrow 3 |
| 3 | `src/core/SkBitmapProcState.h` | pointer_member_arrow 3 |
| 3 | `src/core/SkDrawShadowInfo.h` | auto_type 3 |
| 3 | `src/core/SkLatticeIter.cpp` | pointer_member_arrow 3 |
| 3 | `src/core/SkMaskBlurFilter.cpp` | decltype_expression 3 |
| 3 | `src/core/SkRasterPipeline.h` | pointer_member_arrow 3 |
| 3 | `src/core/SkStringUtils.cpp` | pointer_member_arrow 3 |
| 3 | `src/opts/SkBitmapProcState_opts.h` | auto_type 3 |
| 3 | `src/utils/SkCanvasStack.h` | decltype_expression 3 |
| 3 | `src/utils/SkEventTracer.cpp` | lambda 1, pointer_member_arrow 1, auto_type 1 |
| 2 | `include/core/SkDataTable.h` | pointer_member_arrow 2 |
| 2 | `include/private/SkAssert.h` | lambda 2 |
| 2 | `include/utils/SkNoDrawCanvas.h` | pointer_member_arrow 2 |
| 2 | `include/utils/SkPaintFilterCanvas.h` | pointer_member_arrow 2 |
| 2 | `src/core/SkBlendModeBlender.cpp` | auto_type 1, pointer_member_arrow 1 |
| 2 | `src/core/SkPoint3.cpp` | pointer_member_arrow 2 |
| 2 | `src/core/SkTraceEvent.h` | pointer_member_arrow 2 |
| 2 | `src/opts/SkBlitRow_opts.h` | auto_type 1, lambda 1 |
| 2 | `src/shaders/SkPerlinNoiseShaderImpl.h` | pointer_member_arrow 2 |
| 2 | `src/utils/SkParseColor.cpp` | auto_type 1, lambda 1 |
| 1 | `include/core/SkCPURecorder.h` | pointer_member_arrow 1 |
| 1 | `include/core/SkDrawable.h` | pointer_member_arrow 1 |
| 1 | `include/core/SkRSXform.h` | pointer_member_arrow 1 |
| 1 | `include/core/SkRefCnt.h` | trailing_return 1 |
| 1 | `include/core/SkSamplingOptions.h` | pointer_member_arrow 1 |
| 1 | `include/utils/SkCamera.h` | pointer_member_arrow 1 |
| 1 | `include/utils/SkParsePath.h` | auto_type 1 |
| 1 | `src/core/Sk4px.h` | pointer_member_arrow 1 |
| 1 | `src/core/SkATrace.cpp` | lambda 1 |
| 1 | `src/core/SkBitmapProcState_matrixProcs.cpp` | auto_type 1 |
| 1 | `src/core/SkBlenderBase.h` | pointer_member_arrow 1 |
| 1 | `src/core/SkCPURecorder.cpp` | pointer_member_arrow 1 |
| 1 | `src/core/SkCpu.cpp` | lambda 1 |
| 1 | `src/core/SkGaussFilter.h` | pointer_member_arrow 1 |
| 1 | `src/core/SkLatticeIter.h` | pointer_member_arrow 1 |
| 1 | `src/core/SkMD5.h` | pointer_member_arrow 1 |
| 1 | `src/core/SkSamplingPriv.h` | auto_type 1 |
| 1 | `src/core/SkSpinlock.h` | pointer_member_arrow 1 |
| 1 | `src/core/SkTime.cpp` | auto_type 1 |
| 1 | `src/opts/SkOpts_SetTarget.h` | decltype_expression 1 |
| 1 | `src/pdf/SkDocument_PDF_None.cpp` | pointer_member_arrow 1 |
| 1 | `src/utils/SkMatrix22.cpp` | pointer_member_arrow 1 |
