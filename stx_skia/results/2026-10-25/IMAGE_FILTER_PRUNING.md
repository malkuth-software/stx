# General image-filter factory removal — 2026-10-25

The owner's clarification withdraws Skia backdrop and CSS image effects. GUI.Forms
owns those effects on its own raster. Blur masks, shader masks, matrix color
filters and ordinary group-opacity layers remain required.

## Removed production scope

All 17 image-filter factory implementations are removed from the candidate source
tree and build: blend/arithmetic, blur, color-filter wrapper, composition, crop,
displacement, drop shadow, image source, lighting, magnifier, matrix convolution,
matrix transform, merge, morphology, picture source, runtime shader and shader
source. The public SkImageFilters.h factory header is removed from installation.
Original sources/header remain under baseline/skia for reference comparisons.

The mask-filter-to-image-filter adapters and ImageFilterResult are removed from
the base mask filter and both retained mask-filter implementations. Canvas no
longer creates an image-filter layer to implement a mask filter. The CPU bitmap
device uses direct coverage-mask filtering. The fallback after an unsuccessful
analytic rounded-rectangle draw retains that existing mask filter on the paint.

These are narrow edits to SkCanvas/SkCanvasPriv, not full conversion of those
files. The complete core image-filter system has NOT yet been removed:
SkImageFilter, its cache/types, layer filtering and SkSL still compile. Skia text
internals also remain next work. No no-op implementation substitutes for an
admitted effect; withdrawn factory declarations and definitions are absent.

## Verification

M4 Mac mini, non-Apple Clang 23.1.2/C++20; GUI.Forms fixture LLVM 22.1.8.

- Candidate compilation units fall from 447 to 430; original reference remains
  492 on this host. This is a build/source count, not runtime memory or speed.
- Eleven deterministic release tests pass in both profiles. Five complete raw
  output artifacts match byte for byte.
- Both profiles omit the withdrawn backdrop scene. The 24-scene render hash is
  now 4948189495826467330. This differs intentionally from earlier test versions;
  elapsed times across the old/new workload must not be compared as a speedup.
- The 288 mask scenes and 576 shader cases preserve their previous hashes.
  Matrix color filters, opacity layers and external A8 composition remain in
  the retained scene and isolated consumer comparisons.
- Whole Release and Debug instrumented suites report PASS. Prior logs are
  retained; detailed diagnostics remain unread.
- Seven selected GUI.Forms integration tests pass.
- A fresh relocated installed consumer builds and runs. Its installed include
  tree contains no SkImageFilters.h; consumer RTTI is preserved.
- llvm-nm's defined-symbol output contains no SkImageFilters:: factory symbols
  in the candidate archive.
- Eight converted production/test files pass spelling and lifetime checks.
  The narrowly adapted canvas files remain explicitly unconverted.

Logs: build/image-factory-prune-release.log, image-factory-prune-instrumented-build.log,
image-factory-prune-gui.log and image-factory-prune-final.log. Symbol evidence is
build/image-factory-symbols.txt. Cross-platform CI and the owner's independent
review remain separate gates. The authoritative GUI.Forms checkout is unchanged.
