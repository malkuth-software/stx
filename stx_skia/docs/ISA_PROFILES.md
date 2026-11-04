# Instruction-set profiles

GUI.Forms ships stx_skia at the compiler's baseline, SSE2 on x86-64 and NEON on arm64, plus one
wider tier on x86-64: AVX2 with FMA, chosen at run time (see "SIMD tiers" below). Skia's own
run-time dispatch is otherwise removed (`cmake/prune.cmake`, `STX_SKIA_SIMD_TIERS`). Each
baseline path that ships is checked here against a scalar build of the same source, first for
its pixels and then for its speed, and the AVX2 tier against the baseline. Wider x86 builds are
measured beside them, so a decision about the baseline can rest on numbers. They are never
shipped.

## The profiles

`STX_SKIA_ISA` (`cmake/isa.cmake`) selects one; `tools/build_native.sh` names each profile.

| Profile | `STX_SKIA_ISA` | What is compiled |
|---|---|---|
| `candidate` | `baseline` | SSE2 or NEON, as GUI.Forms ships |
| `candidate-scalar` | `scalar` | no intrinsics, no vectors, no fused multiply-add |
| `candidate-x86-64-v2` | `x86-64-v2` | SSE4.2 (x86-64 only) |
| `candidate-x86-64-v3` | `x86-64-v3` | AVX2 and FMA (x86-64 only) |

**The scalar reference** uses Skia's own switches and takes vectors away from the compiler too:
- **skvx** stores plain arrays (`SKVX_DISABLE_SIMD`).
- **The raster pipeline** compiles its portable one-pixel stages (`SKRP_CPU_SCALAR`). It then has
  no low-precision stages, so it renders every pipeline at high precision.
- **Hand-written SSE2 blocks** (blitters, row and mask kernels) are not compiled on x86-64:
  `SK_CPU_X64_LEVEL=0` is below every SSE level. Scalar floating point still uses SSE
  registers there, which is not SIMD.
- **NEON blocks** are not compiled on arm64: `-march=armv8.5-a+nosimd` leaves `__ARM_NEON`
  undefined, so `SK_ARM_HAS_NEON` is never defined.
- **The compiler** neither vectorizes nor contracts: `-fno-vectorize -fno-slp-vectorize
  -ffp-contract=off`, for the tests' inline Skia code as well as the library.

The flags apply to every target in the stx_skia directory, so the tests and the library agree
on vector layouts. A profile other than `baseline` is for standalone stx_skia builds only;
CMake refuses it inside GUI.Forms.

## SIMD tiers

`include/stx_skia/simd.hpp` names three levels, after stx_auxlib's (and BFFT's): `scalar`,
`vector128` (SSE2 or NEON, the baseline) and `avx2_fma` (AVX2 and FMA at 256 bits). There is no
AVX-only, SSSE3, SSE4 or AVX-512 tier.
- **What has two tiers.** On x86-64 builds of the `baseline` profile, `cmake/prune.cmake` adds
  stx_skia's own avx2_fma units: the raster pipeline (`skia/src/opts/SkOpts_ml3.cpp`), the row
  blitters and the swizzlers (`SkBlitRow_opts_ml3.cpp`, `SkSwizzler_opts_ml3.cpp`), and defines
  `STX_SKIA_ML3_UNITS`. Each includes every header its kernels need before the target region, so
  only the kernels in namespace `ml3` are compiled for AVX2 (an audit of the objects found no VEX code
  outside it). Memset and the mask blitters keep the baseline only. arm64 builds have one tier.
- **Selection.** `SkGraphics::Init()` selects once per process (GUI.Forms' `SkiaRaster` calls
  it): the highest level the machine runs (SkCpu's AVX2, FMA, BMI1, BMI2 and F16C flags, and
  the operating system's saving of the AVX registers), lowered by `request_level()` or the
  environment variable `STX_SIMD_LEVEL` (`scalar`, `vector128` or `avx2_fma`). A request is
  kept between the build's floor and the machine's ceiling, so `scalar` runs `vector128`.
  Programs that never call `SkGraphics::Init()` run the baseline.
- **Same pixels.** The avx2_fma pipeline rounds as SSE2 does: `mad` and `nmad` round the
  product before the add, and the fast reciprocal is the refined one
  (`STX_SKIA_ML3_VECTOR128_ROUNDING`). With Skia's fused `mad` the two tiers differed on the
  workload's high-precision cases (measured 2026-11-03, Windows x64): `scaled_nearest` 560
  pixels by up to 255 (a sample on a texel boundary takes the neighbouring texel),
  `scaled_linear` 525 pixels by 1, `gradients` and `clipped` one pixel each by 1; the
  low-precision cases matched. Unfused, every case matches, at no measurable cost (below).
  The row blitters and swizzlers are integer code.
- **The check.** `stx_skia_simd_tier_contract` (`cmake/simd_tiers.cmake`) records the workload
  at each tier, through the raster pipeline at both precisions and through Skia's own blitter
  choice, and compares them against `tests/simd_tier_bounds.txt`, which names no case. It is
  skipped on a machine without AVX2. A test program reaches the avx2_fma tier only after
  `SkGraphics::Init()`; running GUI.Forms' raster tests with `STX_SIMD_LEVEL=vector128` and
  `avx2_fma` covers both.
- **Why no run-time scalar tier.** The scalar reference is more than the pipeline's one-pixel
  stages: skvx without vectors (`SKVX_DISABLE_SIMD`), no hand-written SSE2 blocks, and no
  compiler vectorization, across the whole library and its tests. skvx is a header of
  templates used in nearly every unit, so a second, vector-free copy in the same program
  would break the one-definition rule, and the hand-written blocks have no scalar variant to
  dispatch to. A run-time "scalar" could therefore cover the pipeline stages only and would
  not reproduce the reference's pixels. The `scalar` profile stays the reference build.
- **Speed.** `stx_skia_isa_workload_tests --dispatch --benchmark` at 1280×800, seven rotated
  trials, median frame time (Windows x64, 2026-11-03), avx2_fma against vector128: fills,
  gradients, image copies and scaling, and clipped draws 0.51 to 0.60 of the time; rounded
  rectangles 0.63 to 0.70; paths 0.71 to 0.78; glyph masks 0.73 (default) and 0.94 (high
  precision); clears and opaque fills 1.00; strokes 1.05 to 1.08. The unfused form was within
  0.03 of the fused one on every case. In GUI.Forms, the ranges-and-progress page's process CPU
  fell from about 15% to about 10.5% at avx2_fma (private desktop, three rounds).

## The workload

`tests/isa_workload.cpp` draws twelve cases that follow GUI.Forms' paint profile: clears,
opaque and translucent rectangle fills, antialiased rounded rectangles, strokes, gradients,
image copies, linear and nearest image scaling, tinted A8 masks (text-like coverage), clipped
draws and antialiased paths. It draws into BGRA premultiplied surfaces, the format GUI.Forms
renders in.
- **Corpus:** with two file names it draws each case at 320×200, once as the library chooses
  (low precision wherever a pipeline allows it) and once with the pipeline forced to high
  precision, and writes the pixels. It is listed in `tests/corpora.txt`, so the reference and
  candidate profiles must also agree on it byte for byte.
- **Timing:** with `--benchmark` it draws each case at 1280×800 and prints the median frame
  time per case and precision.

## Correctness: `tools/compare_isa.py`

`compare_isa.py <scalar tree> <simd tree>` compares every corpus file of a SIMD profile with
the scalar profile's. Workload cases are compared pixel by pixel; the report gives the pixels
that differ, the largest difference in each channel and a histogram. Every other corpus file is
compared byte for byte.

`tests/isa_bounds.txt` holds what may differ, and why. Anything it does not name must match the
scalar profile exactly. A SIMD profile can differ from the scalar one for these known reasons:
- **Low precision.** The SIMD pipelines run 8- and 16-bit fixed-point stages wherever every
  stage has one. Their rounding differs from the float stages by at most one or two per channel.
- **Fused multiply-add.** NEON's and AVX2's pipelines compute `mad` as one fused operation
  (`vfmaq_f32`, `_mm256_fmadd_ps`). SSE2, SSE4.2 and the scalar reference round the product
  first. Where a sample coordinate lands exactly on a texel boundary, nearest sampling then
  picks the neighbouring texel, so whole pixels differ.
- **Vector approximations.** SIMD stages round halves to even, where scalar rounds them up.
  Their reciprocal is an estimate refined by one Newton step, where scalar divides. NEON and AVX2
  convert to half floats in hardware, rounding where scalar truncates. Each changes a pixel by
  at most one, but one level in a packed 10-bit format moves a byte by 4 or 16.
- **NaN signs and hand-written code.** A NaN that propagates through vector code can keep a
  different operand's sign. The legacy blit rows have their own NEON and SSE2 versions.

The arm64 and x86-64 scalar profiles differ from each other as well: in NaN encoding, in the last
bits of the C library's math functions, and in allocation sizes and `max_align_t`. Compare a
SIMD profile only with the scalar profile of its own architecture. The 2026-10-28 measurements,
and the trace of every difference to its cause, are in the notes repository's gui_forms branch
under `results/2026-10-28-e4-stx-skia-isa`.

## Speed: `tools/measure_isa.py`

`measure_isa.py <tree> [<tree> ...]` runs the workload's `--benchmark` in each tree, rotating
the order over seven trials, and reports the median frame time per case and precision with
each tree's ratio to the first. A ratio within about 0.06 of one is inside the noise of the
hosts measured so far.

## Recorded workloads in CI

`tools/run_recorded_workloads.py` runs, after the reference and candidate profiles are built,
every `measure_*.py` tool that backs a timing claim in `results/`, then `measure_isa.py`. It
writes each tool's output and JSON to one folder with a summary. The stx_skia profiles job
runs it on each CI host (Linux x64, Linux arm64, Windows x64 CLANG64) and uploads the folder, so
claims measured only on the M4 get numbers from the other hosts. Its timings are evidence, not a
gate: the job fails only if a tool cannot run.
