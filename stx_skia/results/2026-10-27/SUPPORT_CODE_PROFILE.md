# Where GUI.Forms' Skia rendering spends its time — 2026-10-27

The owner asked for the support code (streams, strings, `TArray`, `TBlockList`, the block
allocator and the arena) to be optimized where that matters more than converting it. This
profile decides which parts matter.

## Method

- **Workload:** GUI.Forms' own renderer benchmark (`benchmarks/renderer_benchmark.cpp`) at the
  pinned fixture (8d1f2be0), built on the M4 with LLVM 22.1.8 against this stx_skia
  (`build/gui-bench`; gallery and benchmarks on, macOS host off). It renders the Gallery
  through the Skia raster adapter, then through CoreGraphics:
  - damage cases at scales 1–3, each 240 frames after 24 warm-ups;
  - a bitmap-band case.

  Every case's checksum matched its reference.
- **Profiler:** macOS `sample`, at 1 ms over a whole run. Inclusive counts come from the call
  tree, restricted to frames under `gui_forms::render::SkiaRaster`: 488 samples.

## Result

| Inclusive under `SkiaRaster` | Share |
|---|---|
| `neon::rect_memset32` (solid rectangle fills, via `SkScan::AntiFillRect` → blitter) | 44.7% |
| `_platform_memmove` (the benchmark's present-copy surrogate) | 17.4% |
| GUI.Forms' own text compositing and clip coverage (not stx_skia) | 15.2% |
| Raster pipeline stages | 4.5% |
| `malloc` / `free` / `new` / `delete` | 1.4% |
| `SkArenaAlloc` | 0.2% |
| `SkTBlockList` / `SkBlockAllocator` / `SkRasterClipStack` | 0.0% |
| `TArray` | 0.0% |
| `SkString` / `SkStream` | 0.0% |

**Negative result.** In this workload the support code is not hot. Even a 2× faster arena,
block list or array would change Skia render time by under 1%. Converting it is the right
treatment; optimizing it is not justified by this evidence. Other workloads may differ: deep
save/restore nesting, many small draws, or complex region clips. Any optimization claim needs
its own measurement.

## The hot spot

Solid rectangle fills. `rect_memsetT` (`src/opts/SkMemset_opts.h`) stores one 128-bit vector
per loop iteration.

**Scratch measurement.** On the M4 (LLVM 22.1.8, `-O3`), each variant fills 32-bit
rectangles, best of five runs:

| Rectangle | Skia loop | 4× unrolled 128-bit | Plain scalar loop (compiler-vectorized) | `memset_pattern4` |
|---|---|---|---|---|
| 100×20 | 51.6 GB/s | 87.6 | 85.9 | 90.0 |
| 1000×500 | 55.4 | 75.0 | 74.0 | 69.8 |
| 1920×1080 | 49.4 | 83.8 | 80.0 | 71.2 |
| 4000×2000 | 61.7 | 81.3 | 80.0 | 91.1 |
| 37×300 | 45.5 | 47.1 | 44.8 | 63.2 |

- **Headroom:** a portable loop the compiler can see reaches 1.3–1.7× Skia's rate on rows of
  100 pixels or more.
- **Narrow rectangles:** no gain; the tail dominates.

At 45% of Skia time, that would cut GUI.Forms' Skia render time by roughly 10–20%. That
estimate is not yet confirmed by the benchmark itself.

**Correction (same day).** The scratch "Skia loop" was not the library's compiled code: the
library's `skvx` loop is already re-vectorized into 64-byte iterations at about 80 GB/s. A
plain-loop replacement then slowed GUI.Forms' full-window repaints, and was not adopted. See
`FILL_KERNEL.md`.
