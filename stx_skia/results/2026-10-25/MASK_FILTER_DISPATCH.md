# Mask-filter dispatch and nine-patch ownership — 2026-10-25

This batch converts SkMaskFilterBase.cpp/.h and SkMaskFilter.cpp, and reviews the
already-conforming public SkMaskFilter.h. The original files remain in baseline/skia.

## Ownership and failure behavior

NinePatch now adopts exactly one cache client reference or one raw mask allocation
through named owning members, declared before its borrowed descriptor. Cache
identity is checked before destruction; the owners release after the descriptor's
lifetime. Cache references are adopted, not incremented again.

The generic filter path retains source and destination allocations before checking
success. A failed filter can return allocated, unusable pixels; the caller now
releases that storage. Metadata-only fast-bound queries also retain any allocation
returned by an implementation. Filter contracts require an initially empty output
and distinct source/destination storage.

The owning-pointer overload of as_MFB rejects temporaries. Raw-pointer casts retain
the existing internal requirement that a mask filter derives from SkMaskFilterBase.
Deserialization names the intermediate flattenable owner and its ownership transfer.

## Nine-patch traversal

Corners and side strips retain their original ordering and mask interpretation.
Horizontal run storage is only constructed if a top or bottom strip intersects
the clip. Its capacity follows the clipped width and is capped at 32,768 entries
for each of the int16_t run and byte-alpha arrays: a maximum 98,304-byte payload.
This is a storage bound, not a timing measurement. The inline scratch object's
stack footprint and allocator bookkeeping are additional to that payload.

Spans longer than 32,767 split into valid signed-16-bit runs. Each run initializes
its live count, terminator and alpha value; skipped sparse-array entries are not
read by the blitter contract. Bottom strips preserve bottom-to-top publication.
Source offsets use size_t; the zero-origin mask/center/outer-bound preconditions
are explicit. Side-only clips skip horizontal scratch construction entirely.

## Interface adaptation and pruning

ImageFilterResult names its filter and appliesShading fields, replacing the
positional pair. SkBlurMaskFilterImpl, SkShaderMaskFilterImpl and SkCanvasPriv
receive signature/consumer adaptations only, not a claim of complete conversion.
The canvas transfers the returned filter to its restore paint and retains the
same coverage/shading choice.

SkLightingImageFilter's private legacy emboss factory bridge and its include are
removed from the candidate. Its sole source caller is the already-excluded
SkEmbossMaskFilter implementation. Other lighting factories remain available. Archive symbol inspection confirms
SkEmbossMaskFilter::LegacySpecular is present in the reference and absent in the
candidate.
The original lighting source is selected in the baseline, where emboss remains
part of the original closure. This is not a wholesale lighting-filter conversion.

## Verification

M4 Mac mini selected by m4host; standalone non-Apple Clang 23.1.2/C++20.
GUI.Forms fixture uses its pinned LLVM 22.1.8 toolchain.

- Eleven deterministic release tests pass in both profiles.
- New common rendering output covers 240 scenes: four blur styles, three sigmas,
  five geometries and four clipping/transform arrangements. Repeated draws
  exercise first creation and retained cache reuse.
- The complete new output matches: 32,254,080 bytes, FNV hash
  7996610373406714751. Native CI now compares mask-filter.raw as well.
- Existing render, conversion, pixmap and mask outputs remain byte-identical.
- Independent pixel expectations check a known 3×3 nine-patch, including
  side-only clipping and widths 32,766, 32,767, 32,768 and 50,000 in the candidate.
- Both cached and uncached nine-patch ownership paths are exercised. A cache
  allocation's destructor verifies final release. A candidate-only test filter
  deliberately allocates then fails, exercising caller cleanup.
- Compile-time checks reject borrowing from a temporary owning filter pointer.
- Whole Release and Debug instrumented suites report PASS. Detailed diagnostics
  remain unread and preceding log files are preserved.
- Seven selected GUI.Forms rendering/integration tests pass.
- Fresh relocated installed consumer builds and runs with its RTTI preserved.
- Five fully converted/reviewed production/test files pass spelling and lifetime
  checks. Adapted unconverted files retain their separate scope in the ledger.

Reproduce with tools/build_native.sh baseline/candidate/candidate-sanitize and
compare all five raw output artifacts. Logs are build/mask-dispatch-release.log,
mask-dispatch-instrumented.log, mask-dispatch-final.log, mask-dispatch-finish.log
mask-dispatch-gui.log and mask-dispatch-gui-final.log. No new runtime speed or broad platform completion claim
is made by this record.
