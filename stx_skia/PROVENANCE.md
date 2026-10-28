# Source provenance

## In GUI.Forms

This directory is a subset of https://github.com/malkuth-software/stx_skia at
`c20afa3c619d736e1588a33d952a26aaa53caced` (its PR #1 merged), taken into
GUI.Forms on 2026-10-27: the 836 files the candidate build, its tests, tools,
records and notices use, every one byte-identical to the pin except as listed
below. Left in that repository, which stays as the read-only record of the
intake: `baseline/` (the reference sources, the GN profiles, the patches and
the source inventory), the GUI.Forms integration fixture and patches
(`integration/`, `tools/prepare_gui_fixture.py`, `tools/verify_gui.sh`), the
copy of the house-style document, its CI workflows and the 8,089 `skia/` files
outside the candidate's closure. The reference profile builds its library from
a checkout of the pin (`STX_SKIA_REFERENCE_ROOT`).

Changed on the way in: `CMakeLists.txt` (subdirectory guards, `stx_skia_` test
names, the reference root), `cmake/reference.cmake` (globs under that root),
`cmake/expect_stop.cmake` (accepts only the Release-check trap),
`tests/mipmap_draw_reference.cpp` (includes the reference original through the
include path), `tools/build_native.sh` (reference root, visible sanitizer
output, corpus list), `.gitignore` (only `build/`), `AGENTS.md`, `README.md`,
`docs/HANDOFF.md` and this section. Added: `tests/corpora.txt`, `tests/intentional_differences.txt`,
`tools/compare_profiles.py` and `house_style_allowance.txt`.
Removed: the four historical gate scripts (`tools/*_gate.sh`), which depended on
the retired PASS/STOP sanitizer wrapper; `results/` records that cite them
describe the pinned repository.

Initial intake: 2026-10-25.

- Upstream: https://github.com/google/skia (official mirror of
  https://skia.googlesource.com/skia.git).
- Exact Skia commit: `2a9b593bab4b2fd019fa494c8d401ff1fab0b883`, GUI.Forms'
  existing chrome/m152 pin. This is deliberately not an upgrade to Skia tip.
- GUI.Forms: https://github.com/falseywinchnet/gui_forms at
  `8d1f2be06a40c5da78bb75e45cbe1fecec2f6b62`.
- Import: `git archive` of that Skia commit into `skia/`, followed by the
  existing `skia_png_only.patch` and `skia_windows_mingw.patch` under `baseline/`.
  Their historical names are retained; the active profile now admits no codec.
- GUI.Forms build profiles, archive check and programming-house-style document
  are copied verbatim. Their original license is retained at the repository root.
- `baseline/inventory.json` records the source hashes, direct consumer includes,
  and imported source identity. Direct include inventory is not transitive closure.

No external dependency checkout, generated executable, downloaded tool, private
asset, credential, local .claude material or GUI.Forms working-tree content is
included. The imported upstream snapshot contains its own data and notices.
Build tools/dependencies referenced by upstream are not automatically fetched.

Skia copyright and license notices, including third-party notices distributed
inside its snapshot, remain in force. Derivation and house-style conversion do
not make that code originally authored here. The separate Git repository starts
with this attributed snapshot; upstream history remains available at the pin.


## First CMake implementation

`cmake/sources.cmake` records the dependency closure of the original macOS CPU
GN target (491 source units including skcms). CMake applies explicit platform
port selection; `cmake/prune.cmake` describes candidate exclusions. The retained
baseline uses `baseline/SkData.cpp`, copied unchanged from the original import.

Changes to imported implementation:

- `skia/src/core/SkData.cpp`: explicit ownership, named release procedures,
  bounds/allocation arithmetic, house-style spelling. Original BSD notice kept.
- `SkOpts.cpp`, `SkBitmapProcState_opts.cpp`, `SkBlitMask_opts.cpp`,
  `SkBlitRow_opts.cpp`, `SkMemset_opts.cpp`, `SkSwizzler_opts.cpp`, under
  `skia/src/core/`: candidate-only conditional to omit higher-ISA dispatch.
  The low-precision raster pipeline remains enabled.
- 2,016 files (63,499,658 source bytes) removed as listed in
  `baseline/pruned-source-files.json`: GPU implementation, unused modules,
  platform demo/tool infrastructure and website. They remain recoverable from
  initial repository commit `490c9bd4a97884d268a5b3f9924748ebf31954bb`.

New `src/`, `include/stx_skia/`, tests and tools are original project additions.
`integration/gui_forms_8d1f2be0.patch` is derived from the pinned GUI.Forms sources
and retains their license. Its generated fixture and dependency checkouts are
excluded from version control. Benchmark archives/binaries are not distributed.


## Pixel ownership conversion

The second batch converts SkData's header and the SkPixelStorage, SkPixelRef,
SkMallocPixelRef and SkIDChangeListener implementations and headers. Original
versions are retained under `baseline/skia/` and used by the reference build.
Callback sequencing, identity arithmetic and atomic ordering are preserved.
The callback-owned, allocated and data-backed pixel references now have named
implementation classes; listener traversal has explicit types and mutation order.
Upstream notices remain on every derivative file. The new pixel_ref test is an
original deterministic contract test, not an imported upstream test suite.


## Reference and view conversion

SkRefCnt.h, SkWeakRefCnt.h and SkSpan.h retain original copies in baseline/skia/.
Their implementation conversion preserves atomic ordering and explicit reference
transfer. SkSpan now refuses temporary owning containers, admits borrowed views,
and handles empty default views without null-pointer arithmetic. Its caller
preconditions and fail-fast invalid-range behavior remain.

SkMatrix.h, SkMatrix.cpp and SkDevice.cpp receive only named corner-array lifetime
adaptations and are separately retained in the baseline; this is not a claim of
complete conversion of those larger files. The new ownership_views.cpp tests
are original project tests. Compatibility declarations remaining in the headers
are recorded in docs/CONVERSION.md.


## Pixel extent conversion

SkAutoPixmapStorage, SkSafeMath, SkReadPixelsRec, SkWritePixelsRec and SkImageInfo
implementations and headers retain original copies under baseline/skia/.
Allocation/move corrections are distinguished from arithmetic-preserving rewrites
in docs/CONVERSION.md and results/2026-10-25/PIXEL_EXTENTS.md. Upstream copyright
notices remain. tests/pixel_extent.cpp is original deterministic contract coverage.


## Pixel conversion

SkConvertPixels.cpp/.h and SkRectMemcpy.h retain original reference copies under
baseline/skia/. The derivative uses explicit dispatch, stable loop bounds and
checked buffer metadata. Original pixel arithmetic remains, including floating
alpha truncation. tests/pixel_conversion.cpp and the conversion measurement and
assembly-inspection tools are original additions. Native CI compares the complete
conversion output between the original and candidate profiles.


## Pixmap conversion

SkPixmap.h, SkPixmap.cpp and SkPixmapDraw.cpp retain original reference copies.
The derivative makes borrow lifetimes, byte offsets, fill dispatch and scaling
ownership explicit. RGBA1010102 float channel order and invalid/empty-view paths
have dedicated candidate-only regression checks. tests/pixmap.cpp is original
coverage; common supported output is compared with the original source.


## Mask and Gaussian blur conversion

SkMask.cpp/.h and SkMaskBlurFilter.cpp/.h retain their original versions under
baseline/skia. The derivative preserves Gaussian arithmetic while making row
traversal, temporary coefficients and borrowed descriptor mutation explicit.
Bounds/allocation rejection and moved-from states have candidate-only checks.
tests/mask.cpp and tools/measure_masks.py are original deterministic coverage and
measurement code; native CI compares complete mask output across both profiles.


## Blur styles and analytic rectangles

SkBlurMask.cpp/.h retain original reference copies. The derivative preserves
common rendering arithmetic, removes unused scanline allocations and expresses
coverage-product bounds in uint16_t. Direct Gaussian reference convolution now
honors source row stride; failure and extent validation are explicit. The unused
BlurRRect declaration had no implementation or source caller and was removed;
rounded-rectangle rendering remains in the retained mask-filter implementation.
Expanded deterministic cases and rectangle timing are original project tests.


## Cached storage conversion

SkCachedData.cpp/.h retain original copies under baseline/skia. The derivative
uses explicitly mutable reference/lock state and named mutex scopes, preserving
cache ownership and callback publication order. SkMipmap.h has a matching const
callback/mutable view adaptation only, with its original header retained for the
reference build. tests/cached_data.cpp is original deterministic transition,
callback and concurrent-reference coverage.


## Mask-filter dispatch conversion

SkMaskFilterBase.cpp/.h and SkMaskFilter.cpp retain original baseline copies; the
already-conforming public SkMaskFilter.h is reviewed and also retained there.
Nine-patch allocations/cache references have named owners, clipped run storage
is bounded, and filter failure results are adopted before checking success.
The named image-filter result requires small signature/consumer adaptations in
SkBlurMaskFilterImpl, SkShaderMaskFilterImpl and SkCanvasPriv; those larger files
are not claimed converted by this batch. The uncalled legacy emboss factory
bridge is removed from SkLightingImageFilter, with its original retained for the
baseline. tests/mask_filter.cpp is original deterministic rendering and ownership
coverage, now included in native differential comparisons.


## Shader-mask conversion

SkShaderMaskFilterImpl.cpp/.h now receive full boundary conversion after the
prior signature adaptation. The public SkShaderMaskFilter.h retains an original
baseline copy as well. Upstream notices remain. The derivative uses SkRectMemcpy,
checked source metadata and an explicit pixel owner through bitmap/canvas use.
The expanded mask_filter test adds original deterministic shader comparisons.


## Blur-filter construction conversion

SkBlurMaskFilterImpl.cpp/.h now receive full implementation conversion; original
copies were already retained for differential testing. The derivative replaces
anonymous drawing callbacks with explicit shape alternatives, scopes temporary
and cached storage, and checks raster extents. tests/mask_filter.cpp adds
asymmetric-corner rendering comparisons and invalid-style rejection checks.


## General image-filter factory removal

The owner's revised scope removes Skia-provided CSS/backdrop effects. All 17
skia/src/effects/imagefilters implementations and SkImageFilters.h now live only
in baseline/skia for the original comparison build. The earlier retained original
SkLightingImageFilter.cpp is used there, preserving its upstream emboss bridge.
SkCanvas.cpp and SkCanvasPriv.h receive original baseline copies before narrow
mask-layer dependency edits. Existing original copies cover other changed files.

Mask-to-image-filter adapters are removed from the candidate. Both render profiles
omit the withdrawn backdrop scene and compare the same remaining behavior. This
is dependency pruning, not a claim that Canvas or core image-filter plumbing has
been fully converted or removed.


## CPU layer reduction

SkCanvas.h, SkRecords.h and the affected picture/record implementations retain
original copies under baseline/skia. Existing baseline copies cover SkCanvas.cpp
and SkCanvasPriv. The candidate removes filter graph/backdrop fields from layer
records and evaluation from canvas drawing, retaining direct device composition.
The remaining serialization code rejects withdrawn layer filter flags. New
original tests/layers.cpp checks complete output against the unchanged reference
implementations and independent group-alpha/empty-layer expectations.


## Core image-filter removal

SkImageFilter, SkImageFilterCache, SkImageFilterTypes, SkBlurEngine and
SkLocalMatrixImageFilter implementations and associated headers now live only in
baseline/skia. Original copies of affected paint, device, image, read-buffer and
utility files preserve the reference profile. The derivative removes the paint
owner/API, device/image filter entry points and cache purges. Historical paint
records retain a zero slot and reject unsupported image-filter entries.
New tests/paint.cpp checks this contract; the installed-consumer test verifies
API absence. These edits do not claim full conversion of the adapted files.


## Runtime/compiler removal and blend-shader conversion

The SkSL source/header trees and runtime effect, shader, color-filter and blender
implementations now live in baseline/skia, preserving notices and the original
reference build. Gainmap shaders, arithmetic-blender factories, programmable
meshes and shader-language capabilities move there as well. Adapted canvas,
device, surface, recording, serialization and raster-pipeline files retain
original baseline copies; those edits are not full-conversion claims.

SkBlendShader.cpp/.h receive a separate house-style conversion within the runtime
removal: named opacity computation, explicit child ownership and pipeline scratch
lifetimes. Original copies preserve the differential oracle. New original
shader_blend tests cover standard blend modes and vertex drawing, and compile-time
checks verify that withdrawn APIs/headers are absent. Compiler trace stages are
removed without replacing them with successful no-op implementations.


## Span constructor participation correction

The converted SkSpan lvalue-container constructor now participates only for
compatible contiguous containers. Original baseline headers are unchanged.
New compile-time checks in ownership_views.cpp cover the libstdc++ variant
failure from db44f01 and incompatible/const element views. This correction
preserves the original conversion's temporary-owner rejection.


## Compiler-only raster stages

SkRasterPipeline.cpp/.h and SkOpts.cpp/.h retain original reference copies;
SkOpts.cpp is taken from the initial import before the candidate SIMD dispatch
condition. Earlier original copies cover the operation list, contexts and vector
implementation. The context packing utility moves to baseline/skia. The candidate
removes compiler operations and their execution state while preserving ordinary
raster arithmetic. New original tests/raster_pipeline.cpp verifies padded rows,
precision selection, extension/reset and compiled callback execution.


## Clip stack removal

SkClipStack.cpp/.h and SkClipStackDevice.cpp/.h now live only in baseline/skia,
unchanged with their notices. They served the PDF, SVG and XPS backends; nothing the
CPU candidate or the reference profile compiles uses them, and the CPU canvas clips
through SkRasterClipStack. tests/canvas_device.cpp checks that the candidate cannot
reach either header. The upstream ClipStackTest.cpp source is preserved and not built.


## Drawable and annotation removal

SkDrawable.h/.cpp leave the candidate; their original copies were already in
baseline/skia, which the reference build compiles. SkAnnotation.h/.cpp,
SkAnnotationKeys.h and the PDF-disabled stub SkDocument_PDF_None.cpp now live only in
baseline/skia, unchanged with their notices. The CPU device ignored annotations, the
stub's PDF entry points were not installed, and no consumer drew drawables. SkCanvas,
SkDevice, the virtual enforcer and the no-draw, N-way and paint-filter canvases lose
their drawable and annotation entry points. tests/canvas_device.cpp and the
installed-consumer test check their absence.


## Wrapper canvas removal

SkNullCanvas, SkCanvasStack, SkCanvasStateUtils and SkAndroidFrameworkUtils (sources and
headers) now live only in baseline/skia, unchanged with their notices. The candidate copies
of SkNWayCanvas and SkPaintFilterCanvas are deleted; their originals were already in
baseline/skia for the reference build. None had a user in the library or GUI.Forms.
tests/canvas_device.cpp and the installed-consumer test check that the headers are absent.

## Shadow drawing removal

SkShadowUtils.h/.cpp, SkShadowTessellator.cpp/.h, SkPolyUtils.h and SkDrawShadowInfo.cpp/.h
now live only in baseline/skia, unchanged with their notices. The candidate copies of
SkPolyUtils.cpp and SkTDPQueue.h, converted earlier, are deleted; their intake originals were
already in baseline/skia. The reference build compiles the baseline shadow sources. Shadow
drawing was reachable only through SkShadowUtils, which neither GUI.Forms nor the library
called; the polygon utilities served only the shadow tessellator, and the priority queue only
the polygon utilities. SkCanvas loses private_draw_shadow_rec and onDrawShadowRec, SkDevice
loses drawShadow, and the virtual enforcer and no-draw canvas lose their overrides. The
upstream ShadowTest.cpp, PolyUtilsTest.cpp and TDPQueueTest.cpp sources are preserved and not
built. tests/canvas_device.cpp and the installed-consumer test check the absence.

## Unused utility removal

SkDocument (with the PDF and XPS document headers in include/docs and
SkMultiPictureDocumentPriv.h), SkCamera, SkJSONWriter, SkDistanceFieldGen, SkParse,
SkParseColor, SkParsePath, SkFloatToDecimal, SkOSPath and the CoreGraphics image bridge
(SkCGUtils.h, SkCreateCGImageRef.cpp, SkCGBase.h, SkCGGeometry.h) now live only in
baseline/skia, unchanged with their notices. A symbol-level check of the candidate archive
found no library object or test referencing them apart from each other, and GUI.Forms calls
none of them. The reference build compiles the baseline sources. SkUniqueCFRef.h stays; a
template-helper test uses it. tests/canvas_device.cpp and the installed-consumer test check
the absence.

## Font-scaler and Windows helper removal

SkMatrix22.cpp/.h and every file in src/utils/win (SkAutoCoInitialize, SkDWriteGeometrySink,
SkDWriteNTDDI_VERSION.h, SkHRESULT, SkIStream, SkObjBase.h, SkTScopedComPtr.h) now live only
in baseline/skia, unchanged with their notices. SkMatrix22 served only the font scaler context,
and the Windows helpers only the WIC codecs, XPS and DirectWrite font backends, none of which
the candidate compiles. The reference build compiles the baseline sources. tests/canvas_device.cpp
checks the absence.

## Uncompiled upstream directory removal

src/gpu, src/codec, src/pathops, src/pdf, src/encode, src/partition_alloc, src/xml, src/xps,
src/svg, src/android and src/capture, and the 58 headers outside them that only they included
(include/gpu, include/codec and include/encode headers, codec metadata headers in
include/private, the Chromium codec headers, SkSVGCanvas.h, SkAnimatedImage.h, and
SkArenaAllocList.h, SkBitmaskEnum.h, SkIPoint16.h, SkRandom.h and SkScopeExit.h in src/core),
now live only in baseline/skia: 952 files, unchanged since intake, with their notices. The
candidate's modified copy of src/image/SkImageGeneratorPriv.h is deleted; its intake original
was already in baseline/skia. The candidate build compiled no source from these directories and
read none of these headers (its Ninja dependency records). The reference build takes its
sources from them in baseline/skia. tests/canvas_device.cpp probes one header of each.

## Stream pruning

The candidate keeps only the abstract SkStream and SkWStream interfaces that SkData::MakeFromStream,
SkPath::dump and the write buffers use (read, skip, isAtEnd, position and length; write, flush,
bytesWritten and writeText) and SkStreamPriv::RemainingLengthIsBelow. The concrete memory, file
and dynamic-memory streams, SkNullWStream, the stream subclass hierarchy, typed readers, text and
packed writers, stream copying, the other SkStreamPriv helpers and SkMD5 had no user in the library
or GUI.Forms; only our own tests used them. baseline/skia holds the intake SkStream.h, SkStream.cpp
and SkStreamPriv.h (taken from 490c9bd4, before the candidate's later stream fixes) and SkMD5,
moved unchanged; the reference build compiles them. The kept parts are carried over verbatim.
