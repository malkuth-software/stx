# Rectangle fill kernel: a measured negative result — 2026-10-27

**Outcome.** A portable plain-loop replacement for `SkOpts::memset*` / `rect_memset*`
(`src/opts/SkMemset_opts.h`) was implemented, verified byte-identical, and measured. It was
not adopted, because it made GUI.Forms' full-window repaints slower. The kernel stays as
imported. This unit keeps:
- the corpus;
- the timing tools;
- the CI timing step;
- these notes.

## Why the fill was examined

In GUI.Forms' renderer benchmark, solid rectangle fills (`neon::rect_memset32`) took 44.7% of
the Skia renderer's samples (`SUPPORT_CODE_PROFILE.md`). A scratch benchmark suggested 1.3–1.7×
headroom. That suggestion was wrong; see below.

## Evidence kept

- **Corpus.** `tests/memset.cpp` writes `memset.raw`: 2,004 cases covering
  `memset16/32/64` and `rect_memset16/32/64`. They span every count from 0 to 80 and longer rows,
  start offsets, padded strides and several heights, each between sentinel guards. Both builds
  write identical bytes.
- **Reference copy.** The intake `SkMemset_opts.h` is copied to `baseline/skia`, so the reference
  keeps the original kernel whatever the candidate does.
- **Benchmark.** `tests/memset.cpp --benchmark` times `rect_memset32`, both as compiled and after
  run-time dispatch, plus canvas solid fills. `tools/measure_fill.py` alternates the reference
  and candidate seven times.
- **CI.** CI runs the timing on Linux x64/arm64 and Windows CLANG64. The candidate's kernel is
  again the original, so on x86 those rows compare the reference's run-time-dispatched AVX/ERMS
  variants with the candidate's baseline-SIMD policy, which disables dispatch.

## What was tried

`memsetT` as a plain `for` loop for the compiler to vectorize and unroll, and `rect_memsetT`
forming each row from the base pointer, with no pointer stepped past the last row.
- **Correctness:** `memset.raw` stayed identical.
- **Code:** on arm64 the loop compiled to two `stp q0, q0` per 64-byte iteration, then a
  4-pixel vector loop and a scalar tail, with no library calls.

## Measurements

**Kernel, in-library, on the M4 (seven alternating runs, median candidate/reference).**
- **Narrow rows:** 0.70 at 37×300.
- **Medium and large rectangles:** about 1.0 for 100×20 through 4000×2000. The noise floor is
  about ±6%: in the candidate, the "default" and "dispatched" rows are the same function, yet
  differ by up to 6%.
- **Canvas solid fills:** 0.98 (antialiased) and 0.99 (aliased).

**GUI.Forms' renderer benchmark on the M4.** Before and after builds against identical fixture
sources, alternating runs, median of per-case `render_ns.mean`, after/before:

| Case | Run 1 (5+5) | Run 2 (9+9) |
|---|---|---|
| damage, scale 1, 1% | 0.960 | 1.143 |
| damage, scale 1, 10% | 0.909 | 1.045 |
| damage, scale 1, 100% | 0.929 | 0.977 |
| damage, scale 2, 1% | 1.026 | 0.976 |
| damage, scale 2, 10% | 1.059 | 1.020 |
| **damage, scale 2, 100%** | **1.347** | **1.250** |
| damage, scale 3, 1% | 0.922 | 0.914 |
| damage, scale 3, 10% | 0.936 | 0.898 |
| **damage, scale 3, 100%** | **1.076** | **1.143** |
| bitmap band, scale 2 | 1.053 | 0.971 |

- **Load:** run 1 began at load average 7.1 and run 2 at 3.5. A background updater was taking
  about one core.
- **Consistent result:** both runs agree on the full-window repaints at scales 2 and 3, slower by
  1.25–1.35× and 1.08–1.14×, outside the before-run ranges.
- **The rest:** the small cases move both ways and are within noise.
- **Raw data:** `fill-renderer-ab.json` on the build host, run 2.

## What was learned

1. **The scratch estimate was wrong.** The scratch "Skia loop" used NEON intrinsics with one
   store per iteration. The real `skvx` loop is visible to the compiler, which already turns it
   into 64-byte iterations, so the library fills a 1920×1080 rectangle at about 80 GB/s.
   Benchmark the compiled library, or confirm the scratch code compiles to the same
   instructions.
2. **The kernel is near the store bandwidth already.** Its only measurable weakness is short
   rows and tails.
3. **A kernel microbenchmark does not predict the workload.** The plain loop matched the
   original on every large rectangle in isolation, yet slowed full-window repaints. The cause
   was not established. Code alignment or layout around the pointer-dispatched call, or the
   tail path on the workload's row widths, are candidates.
4. **The real lever is pixels filled, not fill speed:** overdraw in GUI.Forms' paint order.
