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

The recorded runs used the owner's Apple M4 build host. The portable
commands, from the repository root (`CXX` selects the compiler):

```sh
sh stx_conv/tools/verify.sh            # fetches and verifies the oracle, release suite
sh stx_conv/tools/package_check.sh <new-record-name>
```

The adjacent sanitizer build uses `-DSTX_CONV_SANITIZE=ON` and the same pinned
oracle directory; run it with
`python3 stx_conv/tools/sanitizer_check.py build/stx-conv-sanitize <new-record>`.

A standalone CI workflow was added for macOS, Linux x64/arm64 and Windows using
the repository's existing LLVM-toolchain workflow approach. It runs the core
suite, installation test and supported sanitizers. The authenticated Plan Paint
oracle is an explicit optional local input, not fetched by that workflow.
Those CI jobs have not been run in this local task. The whole GUI.Forms suite
was not run: this change adds an independent library and does not link it into
GUI.Forms or modify renderer/resource behavior.

## Later changes (2026-10-22, intake into GUI.Forms, PR #259)

The sanitizer runner now prints the complete test output as well as PASS or
STOP. The stx-conv workflow fetches the pinned oracle (verified by SHA-256)
and runs the differential tests on every host, and runs the instrumented
suite on Windows too. The owners became move-only with an explicit empty
state; see `HARDENING.md`. None of these changes alters a computed value:
every maximum above is reproduced exactly.

## Speed pass (2026-10-22, GUI.Forms intake, provisional)

Owner decision 1(c): after hardening, a measured pass with 256 to 128 pixel
faces under the 4 ms bake bound as the target. Rule for the pass: no computed
bit may change. Every printed maximum, the face study's digests and the
pass's own bench digests are identical before and after.

**Machine and load.** Windows 11 VM, AMD EPYC 9354 (4 cores, 8 threads),
LLVM 22.1.8 MSYS2 CLANG64, `-O3 -DNDEBUG -ffp-contract=off`, baseline x86-64
(SSE2). Other builds kept the CPU at 23 to 100 % load (recorded per round),
so every figure here is provisional. Comparisons alternate the old and new
code in one session and take each side's minimum, which removes most, not
all, of the load's effect.

**Where the time goes.** One compile of a 256-node, 4-lane line (minimum
cycles per phase over 8,064 compiles of the same line, under load): copy 2.1k,
jets 9.8k, raw currents 6.3k, cell signs 4.4k, lineage 6.6k (75k on noise),
projection 26k (40k on noise). Evaluation is about a tenth of a resize. The
two axes cost the same per line, so memory layout is not the limit. Image
time is the number of lines compiled times this cost: a 256 to 128 reduction
compiles 256 columns and 128 rows.

**Kept (bit-identical).**

1. *Node terms once per node.* The raw currents used `f/5`, `2f/5` and
   `q/20` of both nodes of each cell, so every node's terms were computed
   twice. Jets now store the three terms per node (the workspace holds three
   node arrays instead of the two jets), halving those divisions. Each term
   is the same expression with the same rounding.
2. *Uniform cells.* A cell whose five slots lie between lineage boundaries
   (the common case) has one admitted sign; its unconstrained projection runs
   in registers and is stored only when it keeps that sign. Other cells, and
   uniform cells that fail the test, take the unchanged per-slot path.

**Tried and dropped.** Projecting all lanes of a cell in one vectorized pass
after a separate lineage pass: slower with four lanes (the extra pass and
per-cell overhead outweigh two-wide arithmetic). Branch-free lineage
selects: no gain on smooth faces, about 3 % slower on noise.

**Probes (not adopted).** Same code, same bits, built for x86-64-v3 (AVX2):
8 to 18 % faster; for x86-64-v4 (AVX-512): 14 to 33 % faster. Replacing the
divisions by reciprocal multiplications: 18 % faster on faces, but it changes
bits. A resize composed from the public line operations and split across
threads gives identical bits; its speedup could not be measured at full load.

**Result.**

| Workload (load at or near 100 %) | Before | After | Change |
|---|---:|---:|---:|
| Bench, smooth face 256 to 128, warm minimum | 9.27 ms | 8.46 ms | -9 % |
| Bench, smooth face 256 to 16, warm minimum | 6.59 ms | 5.86 ms | -11 % |
| Bench, smooth face 128 to 64, warm minimum | 2.31 ms | 2.11 ms | -8 % |
| Bench, noise 256 to 128, warm minimum | 22.97 ms | 23.66 ms | +3 % (within run-to-run spread) |
| Face study, CONV64 pixel-area 256 to 128, cold median | 13.3 ms | 11.7 to 11.9 ms | about -10 % |
| Face study, Mitchell-Netravali 256 to 128, cold median | 1.4 to 1.5 ms | (unchanged code) | |

The bench's smooth face is a premultiplied disc and box on a transparent
background; its runs at lower load (23 to 39 %) gave about 6.4 ms before the
pass for 256 to 128, so the single-thread figure on an idle machine after the
pass is near 6 ms, against the 4 ms bound. Raw files:
`../results/2026-10-22-speed-pass/`.

The 4 ms bound is not reached by bit-identical single-thread changes on this
machine; the remaining options (threads at the caller, runtime SIMD dispatch,
a size limit, or keeping Mitchell-Netravali for large faces) are owner
decisions. The workspace now holds one more `length x lanes` array than the
memory table above records (for 2,048 nodes x 4 lanes, 65,536 bytes more).
