# Pixel conversion — 2026-10-25

The conversion preserves pixel arithmetic and makes dispatch, extent checks and
row traversal explicit. Original source remains in the baseline profile.

## Gates

- Seven standalone deterministic tests pass against each release profile.
- 21,420 conversion cases produce 5,809,718 identical bytes; arm64 hash
  10683555862440207716. Cases combine every non-unknown color type, three alpha
  types, and untagged/sRGB/linear color spaces, plus awkward widths and strides.
- All 24 rendered cases still match byte for byte.
- Complete Release instrumented suite: PASS.
- Complete Debug instrumented suite: PASS.
- All seven selected GUI.Forms integration tests pass with LLVM 22.1.8.
- A fresh relocated installed-package consumer built and ran, including its own RTTI.
- Four changed production/test files have zero spelling/lifetime checker findings.

Independent expected-value checks cover channel swaps, alpha extraction, copied
pixels and padding. Exact-size allocations exercise last-row handling. Rejection
tests check unchanged destination storage for invalid metadata and strides. The
one-row stride-zero color-array convention is preserved. Floating alpha remains
finite in [0,1]; this batch does not redefine exceptional floating pixel inputs.

## Timings

M4 Mac mini, non-Apple Clang 23, C++20 Release. Buffers are reused, with three
padding pixels per row and fixed input values. Allocation and I/O are excluded.
Three alternating baseline/candidate pairs each run five trials per workload.
The table reports medians in microseconds per conversion; ratios below 1 favor
the candidate. Raw samples and source hashes are in the adjacent JSON record.

| Operation and image | Reference µs | Candidate µs | Candidate/reference |
|---|---:|---:|---:|
| copy:32x32 | 0.065 | 0.070 | 1.083 |
| swap:32x32 | 0.085 | 0.093 | 1.090 |
| premul:32x32 | 0.137 | 0.154 | 1.123 |
| alpha:32x32 | 0.290 | 0.076 | 0.263 |
| linear:32x32 | 3.218 | 3.280 | 1.019 |
| copy:256x256 | 5.527 | 5.181 | 0.937 |
| swap:256x256 | 4.943 | 4.795 | 0.970 |
| premul:256x256 | 8.940 | 9.383 | 1.049 |
| alpha:256x256 | 18.659 | 2.773 | 0.149 |
| linear:256x256 | 204.180 | 207.159 | 1.015 |
| copy:1920x1080 | 173.042 | 177.992 | 1.029 |
| swap:1920x1080 | 149.379 | 142.154 | 0.952 |
| premul:1920x1080 | 297.096 | 275.975 | 0.929 |
| alpha:1920x1080 | 622.942 | 82.692 | 0.133 |
| linear:1920x1080 | 6450.771 | 6136.154 | 0.951 |

Alpha extraction is the clear gain in this workload: roughly 3.8–7.5 times the
reference throughput. Copy/swap/premultiplication at 32×32 gain validation cost
of about 5–17 ns per call and are slower in this run. Other differences are small
or variable; they do not establish a general speedup.

## Generated code

The original RGBA-to-A8 inner loop loads the image width from the SkImageInfo
object after each byte store. Byte stores may alias object representation, so the
compiler cannot treat that width as invariant. Its vectorization remarks report
no vectorization for this loop. The converted loop captures width and height
before processing and computes row pointers from those stable bounds.

Native assembly contains 128-bit `ushr.4s` and `tbl.16b` operations, vector loads
and stores, and scalar tails. Clang reports vectorization for multiple integer
alpha kernels. This uses portable C++ with no added intrinsic fork, restricted
pointer promise, or wider-register design. Several other loops remain scalar;
vectorization is not claimed universally. This is native arm64 evidence only.

Reproduce with tools/build_native.sh baseline/candidate, compare both render.raw
and conversions.raw using cmake -E compare_files, and run
python3 -B tools/measure_conversion.py sequentially after the builds.
python3 -B tools/inspect_conversion.py retains compiler assembly and remarks.
The instrumented and GUI gates use the repository commands in AGENTS.md.

Generated local logs are pixel-conversions-release.log,
pixel-conversions-integrity.log, pixel-conversions-gui.log,
pixel-conversions-installed.log and pixel-conversion-timing.log. Detailed memory
integrity diagnostics were not read. The full retained library conversion and
broader integration/review gates remain unfinished.
