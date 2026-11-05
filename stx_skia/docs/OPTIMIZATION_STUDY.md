# stx_skia optimization study notes

Living notes for everyone optimizing stx_skia during the port, including GUI.Forms sibling
sessions. Record every measurement here, losses included, with a dated record in `results/`.
Read this before starting optimization work. The owner's direction: some of the support code
is more important to optimize than to convert, so profile the real workload before choosing.

## Rules carried from the project contract

- **No blanket changes:** no fast-math, no guessed `restrict` contracts, and no unmeasured SIMD
  speed claims.
- **SIMD width:** new SIMD work targets the common 128-bit SSE/NEON width, as portable,
  compiler-visible kernels. Imported upstream optimized code stays as the baseline.
- **Targets:** keep the wasm build (SSE4.1 emulated over SIMD128) and the planned Android NDK
  build (NEON) compiling.
- **Proof of output:** every optimization keeps the differential corpora byte-identical, or
  records an intentional difference with its reason.

## Where the gains usually are (owner's guidance)

Gains in these kernels usually come from giving the compiler what it needs, not from rewriting
the arithmetic:
- **Aliasing.** Use `restrict`, but only where the callers' contract establishes no overlap. The
  house style allows restricted pointers in kernels whose callers satisfy the access rules,
  never as a guess. The swap kernel gets the same effect without the keyword: it checks overlap
  first, then calls a loop over separate buffers.
- **Branches inside loops.** Hoist per-element conditions out of inner loops: format, alpha,
  clipping and edge handling. Use separate interior and edge kernels instead of a branch per
  pixel (PLAN step 4).
- **Pipeline management and batching.** Amortize per-call costs over enough work: per-span
  virtual blitter calls, per-row dispatch through function pointers, and per-draw setup.
  Batch spans or rows so the inner loop runs long and stays branch-free.

To check a hot loop for these, read its compiled code (`llvm-objdump -d`): look for runtime
alias checks, a vector loop's width and unrolling, and calls or branches inside the loop.
Then confirm with the renderer A/B.

## How to measure the real workload

GUI.Forms' renderer benchmark (`benchmarks/renderer_benchmark.cpp`) renders the Gallery through
the Skia raster adapter and through CoreGraphics. Its damage cases run at scales 1–3, each 240
frames after 24 warm-ups, and every frame is checksummed against a reference. It needs macOS,
the Skia adapter, CoreGraphics and the Gallery model, so it cannot run on Linux or Windows.

```sh
# On the M4 build host, from the stx_skia mirror, with the pinned fixture in build/gui-fixture:
PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin \
GUI_FORMS_LLVM_RUNTIME=<pinned LLVM 22.1.8 macOS 14 runtime> \
cmake -S build/gui-fixture -B build/gui-bench -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/build/gui-fixture/cmake/llvm22.cmake" -DSTX_SKIA_SOURCE_ROOT="$PWD" \
  -DGUI_FORMS_BUILD_GALLERY=ON -DGUI_FORMS_BUILD_BENCHMARKS=ON -DGUI_FORMS_ENABLE_MACOS_HOST=OFF \
  -DGUI_FORMS_BUILD_TESTS=OFF -DGUI_FORMS_BUILD_PREPARED_TEXT=ON
cmake --build build/gui-bench --target gui_forms_renderer_benchmark
build/gui-bench/gui_forms_renderer_benchmark build/bench-out   # writes renderer-results.jsonl
```

- **Profile:** start the benchmark, then `sample <pid> 8 1 -mayDie -file run.sample`. Then
  `python3 tools/sample_tree.py run.sample SkiaRaster --exclusive 20` gives self time, and
  `python3 tools/sample_tree.py run.sample SkiaRaster <regex>...` gives inclusive time for
  named functions.
- **Compare before and after:** build a second benchmark against a copy of the earlier stx_skia
  tree (`STX_SKIA_SOURCE_ROOT` pointing at it). Alternate runs of the two, and compare medians
  of each case's `render_ns.mean`. Record machine load (`uptime`) with every run.
- **Kernels and CI:** `tools/measure_fill.py <reference build> <candidate build>` times the fill
  kernel and canvas solid fills in both profiles. CI runs it on Linux x64/arm64 and Windows
  CLANG64.
- **Pruning:** `tools/link_closure.py` lists the symbol-level users of library objects, for
  prune decisions.

## Findings

### 2026-10-27 — where GUI.Forms' Skia rendering spends its time

Measured over 488 samples under `SkiaRaster`, at the pinned fixture on the M4:
- **Fills:** `neon::rect_memset32` takes 44.7%, for solid rectangle fills via
  `SkScan::AntiFillRect`.
- **Copies:** the benchmark's present-copy surrogate (`memmove`) takes 17.4%.
- **GUI.Forms text:** GUI.Forms' own text compositing takes about 15%.
- **Pipeline:** raster pipeline stages take 4.5%.
- **Allocation:** `malloc`/`free` takes 1.4%.
- **Support code:** `SkArenaAlloc` 0.2%; `SkTBlockList`, `SkBlockAllocator`, `TArray`,
  `SkString` and `SkStream` 0%.

**Negative result:** the support code is not hot in this workload. Convert it; don't tune it
without a workload that shows otherwise. See `results/2026-10-27/SUPPORT_CODE_PROFILE.md`.

### 2026-10-27 — the rectangle fill kernel

- **A scratch reproduction misled.** A scratch benchmark of "Skia's loop", written with NEON
  intrinsics and one store per iteration, filled at 45–62 GB/s, against 74–86 GB/s for a plain
  compiler-vectorized loop. But the real kernel (`memsetT` over `skvx::Vec`) is visible to the
  compiler. The library build already re-vectorizes it into 64-byte iterations
  (`stp q0, q0` twice) and fills a 1920×1080 rectangle at about 80 GB/s.
- **Lesson:** benchmark the library's own compiled code, or check that the scratch code
  compiles to the same instructions, before estimating gains.
- **Where the original really loses:** short rows and tails, with one 16-byte store per
  iteration for 4–59 pixels and then a scalar loop for the remainder.
- **The plain-loop kernel**, in-library on the M4, candidate time over reference:

  | Rectangle | Ratio |
  |---|---|
  | 37×300 | 0.70 |
  | 100×20 | 1.00 |
  | 1000×500 to 4000×2000 | about 1.0 (±6% noise) |
  | Canvas fills, 64 solid rectangles on 1920×1080 | 0.98–0.99 |

- **End-to-end:** in two A/B runs of the renderer benchmark (5+5 and 9+9), full-window repaints
  were slower: scale 2 by 1.25–1.35×, scale 3 by 1.08–1.14×. The small cases were within noise.
- **Not adopted; the kernel stays as imported.** Lesson: a kernel that matches in isolation
  can still lose in the workload. Always run the renderer A/B before claiming a kernel gain.
- **Record:** `results/2026-10-27/FILL_KERNEL.md`.

## Open leads

- **Fills are bandwidth-bound.** At about 80 GB/s, the remaining fill time is memory traffic,
  so the real lever is fewer filled pixels: overdraw. GUI.Forms fills backgrounds and then the
  controls over them. Measure how many pixels each frame fills more than once.
- **The present copy** (17% of the benchmark's Skia half) is the benchmark's surrogate for
  presenting. Check what the hosts actually copy.
- **Text compositing** (about 15%) is GUI.Forms' own code, not stx_skia.
- **x86:** CI prints fill timings on x64 Linux and Windows. With the candidate's kernel back to
  the original, the dispatched rows show what the reference's AVX/ERMS run-time variants would
  give, which the candidate's baseline-SIMD policy disables. Copy the numbers here when read.
- **The rectangle loop:** `rect_memsetT` steps its row pointer one row past the last, which is
  technically undefined at the end of an allocation. A fix must not change the kernel's code
  generation, and needs the renderer A/B.
- **Narrow and AA-edge fills:** the 30% gain on narrow rows suggests that control borders,
  1-pixel lines and the sides of antialiased edges are worth a dedicated look.
