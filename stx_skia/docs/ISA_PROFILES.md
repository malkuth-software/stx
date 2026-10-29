# Instruction-set profiles

GUI.Forms ships stx_skia at the compiler's baseline: SSE2 on x86-64 and NEON on arm64, with
Skia's run-time dispatch to wider code removed (`cmake/prune.cmake`, `STX_SKIA_BASELINE_SIMD`).
Every SIMD path that ships is therefore one of two, and each is checked here against a scalar
build of the same source, first for its pixels and then for its speed. Wider x86 builds are
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
