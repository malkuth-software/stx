# First CPU library gate — 2026-10-25

This record compares the retained Skia CPU source closure with the pruned CMake
candidate. It establishes the first working library and isolated GUI.Forms
consumer, not completion of the full house-style conversion.

## Native measurements

M4 Mac mini, arm64, macOS 26.5, non-Apple Homebrew Clang 23.1.2, Release C++20,
`-ffp-contract=off`, no blanket fast-math. The original baseline source closure
includes skcms; both archives also include the new pixel-storage helper for a
consistent comparison. The baseline uses the retained original SkData source.

| Measurement | Baseline | Candidate |
|---|---:|---:|
| Library compilation units | 492 | 447 |
| Static archive | 5,200,264 bytes | 4,885,616 bytes |
| Linked render test | 2,719,688 bytes | 2,586,472 bytes |
| Median time, 24 rendering cases | 8.638 ms | 8.686 ms |
| Range over nine alternating trials | 8.587–8.768 ms | 8.608–8.782 ms |

Archive reduction: 6.05%. Linked test reduction: 4.90%. The candidate median
rendering time is 0.55% higher within overlapping trial ranges; this workload
does not establish a drawing-speed gain. Static/linked file size is not resident
memory. No peak-RSS or application allocation-count measurement was made.

All 24 raw outputs matched byte for byte. The suite draws 173×129 images in both
RGBA and BGRA, with 28 padding bytes per row, and exercises transforms, clips,
paths, strokes, gradients, layers, blur, sampling, A8 masks and selected blends.
It checks row padding and canvas save balance. It is neither an exhaustive
upstream Skia suite nor a replacement for GUI.Forms visual goldens.

Pixel storage trials make 100 buffers per trial, including vector allocation,
fill, SkData construction and destruction; seven alternating trials per size:

| Buffer size | Transfer time / copy time, median |
|---|---:|
| 4 KiB | 0.901 |
| 64 KiB | 0.393 |
| 1 MiB | 0.425 |
| 4 MiB | 0.435 |

This removes a full pixel copy and its simultaneous second pixel allocation.
It still allocates a small vector owner and SkData. These timings exclude image
decoding, application caching and drawing, and do not predict end-to-end speed.
The 4 KiB trial is very short and particularly sensitive to timing noise.

Raw trial arrays, file hashes and environment are in [measurements.json](measurements.json).

## Correctness and integration

- Three standalone tests pass for baseline and candidate: render contract,
  pixel ownership, and SkData subsets/ranges/ownership/streams.
- The complete new standalone suite under ASan/UBSan with leak detection: **PASS**.
  This is a deterministic implementation check. Detailed diagnostics remain in
  the local build record for the owner's independent reviewer.
- Installed consumer builds and runs after moving the installation prefix.
- Compile-graph and archive-symbol policy check: **PASS** for excluded codecs,
  GPU source, platform font backends and wider dispatch objects.
- New/rewritten C++: eight files, zero spelling findings and zero lifetime-rule
  findings in GUI.Forms' two checkers. This does not cover the unconverted import.
- Isolated GUI.Forms at `8d1f2be06a40c5da78bb75e45cbe1fecec2f6b62`, non-Apple
  LLVM 22.1.8 with its pinned libc++ runtime: **7/7 selected tests pass**.

The seven are `gui_forms_skia_smoke`, `gui_drawing_skia_tests`,
`gui_drawing_raster_c_api_tests`, `gui_forms_prepared_text_skia_tests`,
`gui_forms_damage_clip_tests`, `gui_forms_overlay_shadow_tests`, and
`gui_forms_paint_group_tests`. They include the fixture's pixel transfer and
reusable gradient-buffer changes. This was an ordinary release integration
build; it does not claim a full GUI.Forms sanitizer or stage gate.

Source cleanup removed 2,016 excluded files totaling 63,499,658 bytes. That is a
source/maintenance reduction, not a runtime-memory claim. GPU headers needed by
retained CPU declarations remain. Picture/text internals, image filters and SkSL
remain in the compiled dependency closure.

## Reproduction

From the repository root on a host with non-Apple Clang and Ninja:

```sh
sh tools/build_native.sh baseline
sh tools/build_native.sh candidate
sh tools/verify_installed.sh
python3 tools/check_build_scope.py
python3 tools/measure.py
sh tools/build_native.sh candidate-sanitize
```

The installed check requires fresh `build/install-original` and
`build/install-relocated` paths. `llvm-nm` must be on PATH for the scope check.
The measurement script is currently intended for Unix archive/executable names.

For the GUI.Forms experiment, prepare a fresh fixture with
`python3 tools/prepare_gui_fixture.py --help`, fetch its declared text and CNG
build dependencies using the fixture's tools, then run `sh tools/verify_gui.sh`
with LLVM 22.1 on PATH and GUI_FORMS_LLVM_RUNTIME set. The recorded M4 runtime was
`/Users/joshuahkuttenkuler/Developer/PlaysuiteDependencies/toolkit-4e126d6/toolchain/llvm-22.1.8-macos14`.

## Negative results and limits

The first renderer-test compile needed an explicit SkBlurTypes include; fixed.
The first GUI.Forms fixture configure used LLVM 23 and was rejected by its
LLVM-22.1 contract; rerun successfully with the pinned toolchain. These were build
setup failures, not altered rendering expectations.

Native Linux x64/arm64 and Windows CLANG64 CI has been added; results are recorded
when available. macOS evidence above is local M4 evidence. The independent
Daybreak review and authoritative GUI.Forms integration remain outstanding.
