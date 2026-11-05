# Core image-filter removal — 2026-10-25

GUI.Forms owns CSS and backdrop effects. The candidate now removes the remaining
core image-filter graph, cache, backend and paint ownership after the earlier
factory and canvas-layer reductions. Original implementations remain under
baseline/skia exclusively for reference builds.

## Scope

Five compiled implementations are removed: SkImageFilter, SkImageFilterCache,
SkImageFilterTypes, SkBlurEngine and SkLocalMatrixImageFilter. Their associated
headers, SkPaint image-filter field/getters/setter, device filtering methods and
SkImages::MakeWithFilter are removed from the candidate. Cache purges and
branches that tested the deleted paint field are removed as well.

SkBlurEngine was an image-filter backend. Its removal does not remove
SkMaskFilter::MakeBlur or the retained Gaussian mask implementation. Matrix color
filters, shader masks and ordinary device-composed opacity layers remain.

Paint serialization keeps its historical null image-filter slot, allowing
ordinary records to retain their layout. Nonzero image-filter tags invalidate
the reader and produce the existing empty-paint failure state. The library does
not silently accept a record while dropping its effect. Picture serialization
itself remains a separate removal candidate.

These are dependency edits to imported paint/device/recording code, not a claim
that those entire implementations meet house style. Baseline copies isolate the
original implementation and headers from each affected candidate file.

## Evidence

M4 Mac mini, macOS arm64; non-Apple Clang 23.1.2/C++20 for standalone tests;
pinned GUI.Forms LLVM 22.1.8 for the isolated consumer fixture.

- Thirteen deterministic tests pass in both candidate and reference profiles.
- All six complete rendering artifacts compare equal: 24 scenes, 21,420 pixel
  conversions, pixmap/mask output, 288 mask-filter scenes, 576 shader-mask cases
  and 256 layer cases. Hashes remain those recorded in CPU_LAYERS.md.
- New paint tests check compile-time API absence, ordinary paint serialization,
  a historical null effect slot and unsupported image-filter record rejection.
- Whole Release and Debug instrumented suites report PASS. Detailed diagnostic
  files remain unread; previous review logs were preserved before these runs.
- A fresh relocated installation builds and runs its consumer, including its
  API-absence assertion and existing consumer RTTI check.
- Seven selected GUI.Forms renderer tests pass; authoritative GUI.Forms is unchanged.
- Defined-symbol inspection finds no SkImageFilter, SkBlurEngine,
  SkLocalMatrixImageFilter or skif:: implementations in the candidate archive.
- The new/changed first-party tests have zero spelling and lifetime-rule findings.

The compile database has 425 library source units versus 492 in the original
reference. This batch removes five from the previous candidate's 430. SkPaint is
72 bytes versus 80 in the reference on this host. Neither count is a measured
rendering-speed improvement. Cross-platform CI remains a separate gate.

Local records: build/image-core-final.log, build/image-core-instrumented-build.log,
build/image-core-gui.log. Mirror records include build/image-core-symbols.txt
and the fresh build/image-core-consumer installation test. Ordinary output is
retained separately from the instrumented suite's diagnostic files.

SkSL/runtime effects and Skia text/font internals still require dependency work.
Full house-style conversion and the owner's independent review remain outstanding.
