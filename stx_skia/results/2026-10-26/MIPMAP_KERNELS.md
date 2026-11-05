# HQ mipmap kernels — 2026-10-26

## Conversion and intentional corrections

SkMipmapHQDownSampler.cpp is converted to explicit packed/expanded types and
named arithmetic intermediates. Packed expansion and contraction retain their
channel order; half-float filters retain binary32 operation order. The eight
footprints use box weights for even dimensions, [1,2,1] weights for odd dimensions,
and identity on a singleton axis. Three-column filters retain shared edge sums.

Source indices and row addresses are formed only for live samples. This replaces
pointer increments that continued beyond the final row or singleton sample.
Kernel selection precedes row traversal. A static immutable 3x3 procedure table
per format replaces eight separately assigned procedure fields and nested shape
selection. Each sampler borrows its static table; it does not copy that table.
No platform intrinsic fork, wide-register requirement or blanket fast-math is added.

Two corrections are separate commits from conversion:

- `403d46e`: RG16 unorm formerly widened after a 32-bit shift and returned a
  16-bit packed value. Green was lost. Expansion now widens first, and contraction
  returns both 16-bit channels in 32 bits.
- `53db238`: RGB10A2's alpha was stored at bit 60 in the weighted accumulator.
  The eight- and sixteen-weight filters could overflow its four available bits.
  Four 16-bit slots now accommodate all weighted channels in the same 64-bit
  scalar: ten-bit channels need at most 16 × 1023 = 16368, and alpha at most 48.

Both changes are checked against independent weighted sums across all eight
footprints, including single-axis cases, widths/heights 1–17, and output padding
for RG16. There are 288 RG16 cases and 576 cases across RGBA10A2/BGRA10A2. The
reference assertions record the old defects explicitly; the candidate assertions
require correct channels. These cases are not claimed byte-identical to upstream.

## Verification

The common artifact now covers 2,432 chains and 134,752 bytes with hash
823643337167435916 in both profiles. It includes 16 supported formats, varying
packed integer patterns, finite half values, odd dimensions, padded source rows,
source preservation and complete mip chains. The three corrected format variants
have the independent oracles above instead of an equality requirement.

On M4 macOS arm64, non-Apple Clang 23.1.2, candidate and original-reference release
builds pass all 20 tests. All eight mip/render artifacts match. Release and Debug
instrumented suites report PASS; previous integrity logs are preserved and no
detailed diagnostic file was read. A fresh relocated installed consumer passes.
The pinned isolated GUI.Forms fixture passes all seven selected tests with
non-Apple LLVM 22.1.8. The authoritative GUI.Forms checkout is unchanged.

The converted kernel and test pass spelling and lifetime checks without findings.
The original HQ implementation is preserved byte-for-byte at the initial import.
The optional drawing downsampler and surrounding cache/shader code are not marked
converted by this result.

## Paired measurement

Seven alternating candidate/reference trials used the same padded source and
destination buffers, one warm-up, and repeated construction of one mip level.
Timing excludes allocation and factory construction. Values below are ratios of
median candidate time to median reference time; below one is less time. Raw
samples, ranges, compiler and hashes are in MIPMAP_KERNEL_MEASUREMENTS.json.

| Format | Source size | Calls per trial | Candidate/reference time |
|---|---|---|---|
| RGBA8 | 8 × 8 | 100000 | 0.946 |
| RGBA8 | 127 × 129 | 500 | 0.992 |
| RGBA8 | 128 × 128 | 500 | 0.967 |
| RGBA8 | 1023 × 1025 | 20 | 1.004 |
| RGBA8 | 1024 × 1024 | 20 | 1.007 |
| RGBA F16 | 8 × 8 | 100000 | 0.876 |
| RGBA F16 | 127 × 129 | 500 | 0.969 |
| RGBA F16 | 128 × 128 | 500 | 0.955 |
| RGBA F16 | 1023 × 1025 | 20 | 0.959 |
| RGBA F16 | 1024 × 1024 | 20 | 1.001 |

Large cases are close to parity within the observed trial spread. Some small
cases improve, but the measurements are short and are not a general renderer
speed claim. The corrected packed formats are not timed against their incorrect
reference arithmetic. Peak process memory and complete-pyramid latency are not
measured here.

## Assembly and records

Native RGBA8 2x2/3x3 kernels use vector widening, addition, shifts and narrowing.
The inspected 3x3 kernel carries its shared column sum in a vector register,
without a stack temporary in the pixel loop. The level dispatcher selects its
procedure before looping over rows. SSE2 assembly is inspection only, not x64
execution. The x64 command explicitly selects x86-64 baseline and disables SSE3,
SSSE3, SSE4 and AVX rather than relying on the macOS target's CPU default.

The first cross-compile missed the SDK path; the first successful x64 attempt
still used the platform's SSE4.1 default. Both are retained as diagnostic evidence;
the corrected command supplies an SDK and baseline CPU options. The paired
performance samples were not rerun for these assembly-command corrections.

Local logs: build/mipmap-rg16-candidate.log, build/mipmap-rg16-reference.log,
build/mipmap-kernels-candidate.log, build/mipmap-kernels-candidate-2.log,
build/mipmap-kernels-reference.log, build/mipmap-kernels-gates.log,
build/mipmap-kernels-measure.log, build/mipmap-kernels-assembly.log,
build/mipmap-kernels-baseline-assembly.log and build/mipmap-kernels-gui.log.
Assembly is retained under build/mipmap-kernels-{arm64,sse2}.s; the earlier default
x64 output is build/mipmap-kernels-x86-default.s. The gate driver is retained as
build/mipmap_kernels_gate.sh; tools/measure_mipmap_kernels.py reproduces the paired
measurement and assembly, or only assembly with --assembly-only.

Current-commit native Linux/Windows gates, full conversion and the owner's
independent review remain open.
