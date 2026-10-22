# Native binary32 and binary64

Both precisions share the same algorithms through explicitly instantiated C++20
precision templates. There is no runtime precision selector, double staging,
implicit conversion of caller buffers, or platform intrinsic implementation.
The existing names remain binary64 source-compatible aliases. `32` and `64`
aliases cover every public owner/view, with the same `compile`, `evaluate` and
`resize` entry points. Static assertions require IEEE binary32 and binary64.

## Representation and arithmetic

The selected precision governs samples, jets, raw/admitted currents, projection
multipliers, lineage objective costs, quadrature weights and synthesis sums.
Limits and tolerances use that precision's machine epsilon. Float32 admits
finite sample magnitudes through `1e10`; binary64 retains `1e100`. These bounds
leave headroom for two image passes and squared current costs. Neither is a
promise of scale-independent relative accuracy at subnormal amplitudes.

The source-profile sample/current payload is exactly halved for float32.
Workspace lineage signs and indices retain their integer representation, and
plan offsets/indices also remain integers. Consequently total allocated payload
is close to half, rather than exactly half. Allocation occurs during owner
construction; the three processing operations reuse caller/owner storage.

Plan geometry uses exact signed integer numerators and denominators for regular
cell locations and footprint overlaps. With dimensions at most 400,000,000,
these products fit `int64_t`. Only local fractions and weight ratios become
floating point. In particular, a small footprint is not computed by subtracting
two rounded absolute float coordinates. Gaussian quadrature evaluates in the
local cell, and linear moments use overlap weight times the local midpoint.

Arbitrary positions support both absolute values and `SamplePosition32/64`
with a separate cell index and local fraction. The latter preserves subpixel
resolution even when an absolute float cannot represent that fraction. Neither
factory retains the supplied span. Fractions are in `[0, 1)` and the final node
admits fraction zero. Precision mixing fails template deduction at compile time.

## Validation and the important numerical limitation

Recorded on Apple M4, macOS 26.5, LLVM 22.1.8, Release, no fast-math,
`-ffp-contract=off`. See [final output](../results/2026-10-22-float32/benchmark-final.txt).

- 2,857,413 binary32/binary64 line and scale comparisons: maximum absolute error
  `4.35073517e-7`. Inputs are identical representable floats promoted to doubles.
  Includes lengths 1–96; 1/3/4/7 lanes; constant, ramp, step, deterministic hash,
  sinusoidal and alternating patterns; reduction, enlargement, identity,
  arbitrary absolute/local positions, nodal and pixel-area sampling.
- A separate test-only native-float derivation of the pinned Plan Paint profile
  builder retains its original active-face enumeration and full boundary-cost
  recomputation. Maximum output difference from the optimized float profile is
  `4.82797623e-6`. This is a precision-adapted reference, not an upstream float API.
- All 2D float outputs match an explicit composition of public float line
  operations exactly, including short/singleton axes, both axis orders, skipped
  axes, both sampling rules and padded rows.
- The original double oracle still passes: 5,376 line cases, 360 image cases and
  five exact RGBA fixtures; maximum difference `4.6351811278100286e-14`.
- Failure, moved-profile, overlap, padding, finite/range, local-coordinate and
  narrow-footprint checks pass. Relocated installed consumers link and execute
  both precisions. House-style and lifetime checks have no findings.
- Clean adjacent ASan/UBSan build with leak detection: **PASS**. Only the runner's
  indicator was inspected. This is the standalone package's deterministic
  suite, not a claim about the complete GUI.Forms application or remote CI.

**Native float32 does not guarantee a small difference from float64 for every
2D image.** The sign-selection stage is discontinuous near equal neighboring
values. The first image pass may round onto or off a plateau; the second pass
can then choose a different current structure. Among 596,160 stress-battery 2D
samples, 43 differed by more than `2e-5`, with maximum absolute difference
`0.041078123`. The source patterns are bounded roughly to `[-1, 1]`; this is a
material difference, not a last-bit rounding claim. These cross-precision
measurements are diagnostics, distinct from the native-float oracle and exact
axis-composition correctness checks. They do not estimate a real-image rate.

The first implementation's geometry produced a worse stress maximum of
`0.110030668`. That negative result remains in
[the initial record](../results/2026-10-22-float32/benchmark.txt). The exact-ratio
footprint rewrite reduced it but did not remove the underlying discontinuity.
No epsilon-based flattening, double fallback or changed tie policy was added.
Use binary64 when retaining its structural decisions matters; choosing a common
precision-independent plateau rule would be an algorithm change for both paths.

## Error bounds and range

The figures above are measured maxima over the stated batteries, not proven
bounds. What is bounded by construction:

- **Lineage ties.** The incremental boundary objective drifts from the
  reference summation by at most about 30 eps E over a knot's ten slots
  (E: the slots' current energy). Contenders within 128 eps E are recomputed
  in the reference order, so every boundary decision equals the reference's.
- **Projection.** Each cell keeps at least one slot of its own sign, so the
  sign-constrained projection always has a solution; `numerical_failure` is
  only the fallback for a multiplier that misses every piece by more than the
  tolerance, and no sweep or fuzz input has reached it. The accepted
  multiplier may lie outside its piece by 64 eps s (s: the largest raw-current
  or mass magnitude in the cell), so the admitted currents can miss the cell's
  mass by at most 5 x 64 eps s. Node values are exact (a node is evaluated at
  local fraction zero); the curve approaching a node from the left can differ
  from the node by that amount.
- **Range.** The output is not bounded by the input range. Near a sign
  transition an admitted current of the opposite sign can carry the curve past
  the neighbouring nodes. On premultiplied RGBA faces of 16 to 256 pixels the
  largest excursion measured was 0.40 of full scale (a 24-pixel checkerboard
  enlarged) and 0.10 to 0.12 on icons and edges
  (`tools/resampler_face_study`, 2026-10-22). Image callers clamp.

## Measurements

Single thread, 512×384, four equal channels per pixel, with prepared plans,
workspaces and input/output buffers. Timed work includes profile construction
and both axis passes. Allocation, color conversion and plan construction are
excluded. Eleven measured pairs follow one warmup pair; order alternates.
Medians below; full min/max ranges are in the raw record. The shared host showed
substantial variation, so small median advantages are not conclusive speedups.

| Pattern | Target | Float32 ms | Float64 ms | Float32 / float64 |
|---|---:|---:|---:|---:|
| Deterministic hash noise | 237×181 | 43.563 | 47.010 | 0.927 |
| Deterministic hash noise | 769×577 | 81.532 | 85.037 | 0.959 |
| Horizontal ramp | 237×181 | 11.334 | 16.732 | 0.677 |
| Horizontal ramp | 769×577 | 19.307 | 23.899 | 0.808 |
| Alternating columns | 237×181 | 28.922 | 35.907 | 0.805 |
| Alternating columns | 769×577 | 66.804 | 74.738 | 0.894 |

For these six benchmark cases, maximum cross-precision output error ranged
from `1.16e-7` to `1.12e-5`. That does not replace the adverse plateau results
above. The workload generator and timing loop live in `tests/precision.cpp`.

| Allocated plan + workspace payload | Float32 bytes | Float64 bytes |
|---|---:|---:|
| 512×384 → 237×181, four channels | 1,748,000 | 3,456,928 |
| 512×384 → 769×577, four channels | 4,984,368 | 9,927,456 |

Caller buffers are additional and exactly halve with float storage. These
counts are allocated payload capacities, not process RSS or peak allocator use.

## Common-width vectorization

LLVM 22.1.8 emits `fmul.4s`/`fadd.4s` in native ARM synthesis and `mulps`/`addps`
with generic x86-64 SSE2 (`-msse2 -mno-avx`). The inspected float synthesis
function contains no double-vector operations, float/double conversions, or
YMM/ZMM registers. The x86 output is assembly inspection, not an executed test.
[Assembly hashes and counts](../results/2026-10-22-float32/assembly.json) identify
the inspected files. Array alignment is not overclaimed; private restrict
contracts apply only to disjoint owned arrays. Scalar lineage/projection work
still limits whole-operation speed on high-contrast inputs.

## Reproduction

The record above was made on the owner's Apple M4 build host. From the
repository root, with `CXX` naming the compiler if needed:

```sh
sh stx_conv/tools/verify_precision.sh <new-record-name>
build/stx-conv-release/stx_conv_precision --benchmark
```

Choose an unused record name. Run the benchmark on an otherwise idle machine,
after the builds. The sanitizer source manifest is retained with the results.
