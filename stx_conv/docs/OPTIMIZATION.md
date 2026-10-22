# Initial implementation and measurements — 2026-10-22

## Contract and reference

This implements Plan Paint's separable, ordered-current CONV* resampler at
`3edc452068d646b1ba68d7ae853abb1aa26ff6de`. The mathematical version is the
six-source/five-current quintic construction, with Plan Paint's short-line
closure, endpoint handling, nodal basins and pixel-area footprints. Axis order
is preserved. Eikonal interpolation and alternative fixed orders are outside
this operator. See `PROVENANCE.md` and `tests/oracle.json`.

Binary64 is used throughout. LLVM builds disable floating-point contraction;
there is no fast-math flag. Numerical agreement is tested before image
quantization. Caller color/alpha conversion is deliberately outside the numeric
library; applying the pinned Plan Paint conversion reproduces its five retained
RGBA fixtures exactly.

## Changes that reduce work

1. **Reuse representations.** A LinePlan owns position/integration coefficients,
   a Profile owns anchors and admitted currents, and LineWorkspace owns temporary
   jets and lineage storage. Repeated evaluation needs no source analysis.
   ImageWorkspace reuses two line profiles and one intermediate image. There is
   no per-line allocation, implicit pool, or global mutable state.
2. **Incremental ordered lineage.** The boundary objective advances using
   `C(b+1)=C(b)-s*a[b]*abs(a[b])`, replacing repeated sums over overlapping
   neighborhoods. Roundoff-close candidates are recomputed in reference order,
   retaining the earliest-index tie convention. An already exact best cost is
   cached rather than recomputed at every tie.
3. **Finite multiplier projection.** First test the all-active affine projection.
   Otherwise, a nine-comparison sorting network orders the five breakpoints.
   At most six multiplier intervals are visited, using the unique convex
   projection's affine mass equation. This removes candidate error calculations
   over the reference's enumerated active faces. Interval tolerances scale with
   current magnitude, retaining the small-amplitude scaling law.
4. **Compiler-visible synthesis.** The plan decides whether a segment needs
   currents before the lane loop. Disjoint source/current/output arrays enter
   restricted private kernels. Five weights are loaded once; each lane keeps
   the original five-term accumulation order. The first segment writes directly
   and subsequent segments accumulate, removing output zero-fill. There are no
   platform intrinsics. The boundary/lineage dependencies remain scalar.

The first implementation regressed on alternating input: 1.2625 times the
reference's line runtime. Its conservative tie handling redundantly recomputed
both candidate and incumbent costs. This negative result motivated change 2;
it is not omitted from the record. That first run's other line ratios were
0.6824 random, 0.4285 ramp, and 0.4494 sinusoidal.

## Machine and measurement scope

Apple M4, macOS 26.5 (25F71), Homebrew LLVM 22.1.8, C++20, Release `-O3 -DNDEBUG`,
`-ffp-contract=off`. Builds ran on the selected M4 host from the authoritative
GUI.Forms checkout. x86 SSE2 was cross-compiled to assembly, not executed.

Both benchmark sides run **one worker**, reuse prepared plans/workspaces and
preallocated output, and include source profile construction plus synthesis.
The candidate also performs its input validation and snapshot copy. The reference
calls its pinned LineWorkspace resampler directly. The 2D harness composes those
same reference line routines serially in Plan Paint's axis order. This isolates
algorithmic work; it is not a comparison against Plan Paint's convenience API
using up to eight workers, RGBA conversion, and per-call allocation.

Each timed pair alternates execution order. The first pair is warm-up. Lines
retain 32 repetitions and report the upper median; images retain 11 repetitions
and report the median. These are two synthetic workloads sizes and a finite
pattern battery, not a universal performance claim or a natural-image corpus.
Submillisecond line timings varied appreciably between runs; the retained image
runs give the more useful initial estimate. No confidence interval is claimed.

## Final confirmation: 2D

Input is 512 x 384 with four interleaved double channels. Reduction is 237 x 181;
enlargement is 769 x 577. Noise uses deterministic mt19937 seed 101010 and uniform
[0,1] samples; gradients are affine ramps; checkerboards alternate every pixel.
Source generation and allocation are outside timing. Output is checked against
the reference after every pair.

| Input | Operation | stx_conv ms | Reference ms | Reference / stx_conv |
|---|---|---:|---:|---:|
| Noise | Reduce | 53.744 | 172.002 | 3.20x |
| Noise | Enlarge | 90.446 | 276.494 | 3.06x |
| Gradient | Reduce | 18.088 | 74.950 | 4.14x |
| Gradient | Enlarge | 29.661 | 110.706 | 3.73x |
| Checkerboard | Reduce | 89.348 | 172.505 | 1.93x |
| Checkerboard | Enlarge | 150.056 | 271.395 | 1.81x |

The previous synthesis run ranged from 1.75x to 4.21x over the same six cases.
Absolute times shifted between runs; both are retained. Earlier results before
vectorized synthesis are retained separately as well.

Final 1D comparison: 2,048 nodes, four lanes, 3,071 outputs. Candidate/reference
upper-median time ratios were 0.3868 noise, 0.2121 ramp, 0.6211 alternating,
0.2253 sinusoidal. For the reasons above these very short timings are supporting
evidence, not the headline speed estimate.

Raw files are in `../results/2026-10-22/`: `benchmark-v2.txt`,
`benchmark-before-synthesis.txt`, `benchmark-synthesis.txt`, and
`benchmark-confirmation.txt`. The confirmation's constructor-only row-product
validation also protects 32-bit size arithmetic; no 32-bit execution is claimed.

## Memory

Processing makes no allocations. Constructors prepare the storage. These are
allocated payload capacities, not measured process RSS; object/allocator metadata
and caller source/destination arrays are excluded from workspace figures.

| Workload | Profile payload | Scratch/workspace payload | Plan payload |
|---|---:|---:|---:|
| 1D, 2048 nodes x 4 lanes | 393,056 B | 479,072 B | 221,120 B |
| 2D reduction above | Included in workspace | 3,346,560 B | 110,368 B |
| 2D enlargement above | Included in workspace | 9,830,528 B | 96,928 B |

For the image cases, caller input is 6,291,456 bytes; caller output is 1,372,704
bytes for reduction and 14,198,816 bytes for enlargement. Thus library workspace
plus plan is about 3.30 MiB / 9.47 MiB, and including both caller images gives
about 10.61 MiB / 29.01 MiB. This excludes the benchmark's separate oracle buffers.

Profiles retain the source to support independent repeated evaluation. Scratch
contains jets/raw currents and per-lane lineage arrays. 2D intermediate storage
scales with the selected pass dimensions, not with an entire dense 2D quintic
coefficient field. Separate concurrent resizes require separate workspaces.

## SIMD evidence

Before synthesis refactoring, its function contained no packed double multiply
or add instructions in either inspected target. Afterwards, the compiled function
contains 20 `fmul.2d` and 15 `fadd.2d` instructions on ARM64, and 20 `mulpd` and
13 `addpd` instructions on SSE2. These are static instruction counts across
compiler-generated paths, not operations per pixel. No YMM/ZMM register appears
in the SSE2 assembly. Jets already vectorized before this change.

`assembly.json` records assembly SHA-256 values and counts. Commands:

```sh
clang++ -std=c++20 -O3 -ffp-contract=off -Istx_conv/include \
  -S stx_conv/src/profile.cpp -o profile-arm64.s
clang++ -std=c++20 -O3 -ffp-contract=off -Istx_conv/include \
  -target x86_64-apple-macos14 -msse2 -mno-avx \
  -isysroot "$(xcrun --show-sdk-path)" \
  -S stx_conv/src/profile.cpp -o profile-sse2.s
```

Full assembly remains in the task's local `build/` directory and the M4 build
record. Source SIMD behavior remains portable; x86 speed has not been measured.

## Validation and reproduction

- 5,376 line case executions: source lengths 1–96, four target lengths, nodal
  and pixel-area sampling, seven patterns including quantized/random/step data.
- 360 image case executions, covering singleton/short/general axes, unchanged
  axes, reduction, enlargement and mixed-axis changes under both sampling rules.
- Five pinned RGBA fixture outputs match byte for byte after caller conversion.
- Maximum absolute double difference across the differential battery:
  `2.7977620220553945e-14`; comparison tolerance is
  `2e-11 * max(1, abs(reference))` per sample.
- Core invariants: constant/affine reproduction, nodal cardinality, integrated
  ramp means, snapshot lifetime, failure preservation, overlap rejection,
  padded strides, lane counts 1/2/3/7, and amplitude scales 1e-80/1/1e80.
- Deterministic ASan/UBSan/LSan suite: PASS. Only PASS/STOP is exposed by its
  runner. Records use separate directories and source digests.
- Relocated installed consumer: PASS.
- House spelling and lifetime checks: zero findings. Ownership, byte arithmetic,
  bounds, numerical guards and private restricted-pointer contracts reviewed.

Exact ordinary build/test command from the repository root:

```sh
python3 stx_conv/tools/fetch_oracle.py build-stx-conv-sources
/Users/ultimussecundai/.local/bin/m4build -- sh stx_conv/tools/verify.sh
```

The adjacent sanitizer build uses `-DSTX_CONV_SANITIZE=ON`, the same pinned
oracle directory and LLVM compiler. After mirror synchronization, build it with
`cmake --build build/stx-conv-sanitize --clean-first --parallel 4`, then run
`python3 stx_conv/tools/sanitizer_check.py build/stx-conv-sanitize <new-record>`.
The final clean-run record is `build/stx-conv-integrity-04-clean` on the M4 mirror.
For release package relocation use
`sh stx_conv/tools/package_check.sh <new-record-name>`.

A standalone CI workflow was added for macOS, Linux x64/arm64 and Windows using
the repository's existing LLVM-toolchain workflow approach. It runs the core
suite, installation test and supported sanitizers. The authenticated Plan Paint
oracle is an explicit optional local input, not fetched by that workflow.
Those CI jobs have not been run in this local task. The whole GUI.Forms suite
was not run: this change adds an independent library and does not link it into
GUI.Forms or modify renderer/resource behavior.
