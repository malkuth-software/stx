# Mask descriptors and Gaussian blur — 2026-10-25

This batch converts SkMask.cpp/.h and SkMaskBlurFilter.cpp/.h. Original versions
are retained under baseline/skia and selected by the reference profile. It does
not convert SkBlurMask, filter dispatch or all raster mask generation.

## Representation and traversal

A mask is a borrowed descriptor. Its builder does not acquire ownership merely
because it holds writable pixels. Allocations remain separately owned, including
when replacing a populated destination. Moves now empty the source descriptor;
self-move preserves it. The metadata fields are genuinely mutable, replacing
writes through casts of const fields. Writable image access retains a qualification
cast at the existing builder boundary, whose caller must supply writable storage.
Reference-returning metadata access rejects temporary builders.

Image-size calculations check the signed 32-bit extent limit before multiplication.
Destination preparation checks nonnegative padding, dimensions, padded coordinates
and products. Invalid requests return an empty A8 descriptor. Allocation alignment
cannot wrap. Ordinary valid masks retain their original layout and output.

The Gaussian arithmetic, rounding, border convention and eight-lane uint16_t
kernels remain unchanged. Coefficient arrays initialize all their capacity before
vector arguments load them. Strided scans use output counts rather than logical
end pointers beyond the allocation. Source rows and vertical tail writes form
only live addresses; no final-row padding is needed in the candidate tests.
Internal unsupported-radius dispatch has an explicit fail-fast fallback.

The filter remains a private caller-contract boundary. The production caller
supplies equal sigmas and checks hasNoBlur before invoking it. Pixel buffers must
have valid extents, strides, alignment and supported formats. No new anisotropic
or zero-axis capability is claimed. The original sigma limit is retained.

## Verification

M4 Mac mini selected by m4host; standalone non-Apple Clang 23.1.2, C++20.
GUI.Forms fixture 8d1f2be0 uses its required LLVM 22.1.8 toolchain.

- Nine deterministic tests pass in each release profile.
- 300 direct Gaussian cases cover BW, A8, ARGB32 and LCD16; widths 1, 7, 8,
  17 and 33; heights 1, 5 and 19; sigma 0.5, 1.5, 2, 4 and 8.
- Four additional BoxBlur cases cover normal, solid, outer and inner styles.
- Complete mask output is byte-identical: 289,308 bytes, FNV hash
  17789011958592203429. Candidate source storage ends at the last live pixel;
  the original profile retains an extra row for its final iterator advancement.
- Existing render.raw, conversions.raw and pixmap.raw remain byte-identical.
- Size/padding, addresses, bit/alpha iteration, allocation and descriptor moves
  have deterministic assertions. Invalid metadata and move-empty behavior have
  separate candidate-only checks.
- Whole Release instrumented suite: PASS. Whole Debug instrumented suite: PASS.
  Detailed diagnostics remain unread and retained; preceding logs were preserved.
- Seven selected GUI.Forms rendering/integration tests pass.
- Fresh relocated installed consumer builds and runs, preserving consumer RTTI.
- Five changed production/test files pass spelling and lifetime checks.

Reproduce with tools/build_native.sh baseline/candidate/candidate-sanitize and
compare all four raw outputs. The native CI workflow includes mask.raw. Local
build logs are build/mask-verification.log, mask-debug-measurement.log,
mask-gui.log and mask-installed.log.

## Timing

Run tools/measure_masks.py after both release builds. The retained JSON contains
all nine alternating trials, compiler and host information. Source preparation is
excluded; each timed operation includes destination/temporary allocation, blur
and cleanup. Input is a deterministic coverage pattern with padded row strides.

| Format | Shape | Sigma 1.5 candidate/reference time | Sigma 8 candidate/reference time |
|---|---:|---:|---:|
| BW | 33 × 19 | 0.964 | 0.980 |
| BW | 257 × 127 | 0.998 | 1.059 |
| A8 | 33 × 19 | 1.023 | 1.033 |
| A8 | 257 × 127 | 1.016 | 1.003 |
| ARGB32 | 33 × 19 | 0.970 | 0.974 |
| ARGB32 | 257 × 127 | 0.927 | 0.957 |
| LCD16 | 33 × 19 | 0.966 | 0.994 |
| LCD16 | 257 × 127 | 0.971 | 0.967 |

Ratios use median elapsed time; lower is faster. Trial ranges overlap in every
case. These measurements do not establish a general speed improvement or a
whole-GUI result. The BW large-sigma median regression is retained. The practical
result of this batch is explicit storage/traversal with byte-exact rendering.
