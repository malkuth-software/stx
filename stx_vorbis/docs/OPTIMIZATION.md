# Comparative decoder optimization, 2026-10-07

**GIVEN:** Measure complete work against other decoders, profile the hot paths,
inspect SIMD assembly, remove avoidable work, retain numerical/resource gates.
Windows x86 optimization belongs to the owner's Shadow desktop sibling. This
record measures Apple M4 only; Windows CI correctness is not a desktop benchmark.

## Workload and method

**MEASURED:** Apple M4 Mac mini, macOS 26.5 (25F71), arm64, LLVM 22.1.8.
Production flags: `-O3 -DNDEBUG -std=c++20 -arch arm64
-mmacosx-version-min=14.0 -fPIC -ffp-contract=off`, with strict warnings.
No LTO, `-march=native`, global fast-math or FMA permission was added.

Two original deterministic signals, 480,017 frames each at 48 kHz: stereo q0.5
and six-channel q0.4. The exact Ogg bytes, generator provenance and SHA-256 are in
[bench/corpus](../bench/corpus/README.md). Their duration is 10.000354 seconds.
This is a bounded synthetic workload, not a representative music-corpus claim.

`bench/decode.cpp` builds separate executables for stx, libvorbis, Tremor and stb.
Input is read into memory before the clock. Every iteration creates a new decoder,
decodes through EOF and probes output. Timings exclude file I/O, process startup,
result-vector allocation, CSV writing and decoder destruction. First-PCM time
includes setup, transport validation and initial audio work. Remaining time is
all work after the first returned PCM block. This does **not** isolate the setup
parser alone. Processes, allocator and CPU caches are warm after three warmups.

Three rounds of 21 measured iterations per implementation, with implementation
order rotated between rounds. Tables use the combined 63 observations. Before and
after are separate sessions; there is no CPU pinning or frequency lock. Control
median drift was about 0.5–3%, smaller than the observed stx improvement. No other
build, fuzz campaign or sampling profiler was running during final timed rounds.
Raw distributions are preserved; no unsupported confidence interval is claimed.

- Baseline production: `0015a1c398e0b88462a8c33517c44e26ca583b50`, using the new
  benchmark harness (before the production optimizations).
- Optimized source: `cb997fd` (production changes in `c1ca69d`).
- libvorbis 1.3.7 / libogg 1.3.6: Homebrew prebuilt libraries. Their compiler/build
  flags are not asserted equal to this local Clang build. They require macOS 15,
  only in the test executable; they are not shipping dependencies.
- Tremor: `820fb3237ea81af44c9cc468c8b4e20128e3e5ad`, built here with Clang 22.1.8
  and CMake Release defaults, system libogg. It emits interleaved **int16**.
- stb v1.22: miniaudio `f40cf03f80cdb7e741d43e53b7e706e8c1394bcf`, built here with
  Clang 22.1.8 and CMake Release defaults. It emits planar float32, as do stx and
  libvorbis. Reference code is external, unchanged, and never linked into stx.

The baseline executable was retained before modifying production sources. JSON
records the binary hashes as well as compiler, source identity, inputs and raw
CSV. It identifies baseline production separately from the newer harness revision.
The external source warnings are preserved in the validation record; no warning
suppression was added to production to accommodate them.

## Results

Total memory-decode time in milliseconds, median of 63 runs:

| Implementation | Stereo before | Stereo after | Six-channel before | Six-channel after |
|---|---:|---:|---:|---:|
| stx portable scalar source | 17.372 | 15.373 | 48.044 | 42.134 |
| stx NEON | 15.192 | **13.421** | 41.708 | **36.925** |
| libvorbis control | 10.578 | 10.807 | 27.148 | 27.332 |
| Tremor int16 control | 11.156 | 11.271 | 28.375 | 28.834 |
| stb float control | 5.847 | 6.019 | **Invalid PCM** | **Invalid PCM** |

NEON total time fell **11.66% stereo** and **11.47% six-channel**. stx remains
**24% slower than libvorbis on stereo, 35% slower on six-channel**, and about
**2.23 times stb's stereo time**. The remaining gap is substantial. `scalar`
selects portable source, not a promise that compiler auto-vectorization is off.

After-change NEON timing detail:

| Workload | First PCM median | Remaining decode median | Total median | Total p95 |
|---|---:|---:|---:|---:|
| Stereo | 0.274 ms | 13.145 ms | 13.421 ms | 13.751 ms |
| Six-channel | 0.439 ms | 36.482 ms | 36.925 ms | 37.971 ms |

Medians of subintervals need not sum exactly to the median of the total.
[Before JSON](../bench/results/2026-10-07-m4/before/summary.json),
[after JSON](../bench/results/2026-10-07-m4/after/summary.json), and all sibling CSV
files retain individual observations. Preliminary single-round results (15.94 to
13.16 ms stereo) were directionally consistent; use the repeated table above for
reported speedup. Do not combine process-level timings in VALIDATION.md with these
in-process measurements.

## PCM and resource gates

**MEASURED:** On both exact benchmark inputs, stx matches libvorbis frame counts
and has maximum float absolute error **2.384185791015625e-7**, with zero samples
outside `2e-6 + 2e-6*abs(reference)`. Tremor differs by at most **one int16 unit**
after reference rounding/saturation. Stereo stb matches within tolerance.

Six-channel stb returns the expected count but **2,880,065 of 2,880,102 values**
exceed that tolerance; maximum error is **27.011178**. Its raw timing is retained,
but excluded from correct-output ranking. Counts alone are not a correctness test.
See [PCM results](../bench/results/2026-10-07-m4/pcm-correctness.json). Broader
synthetic floor0/Tremor and stb reference disagreements remain in VALIDATION.md.

Local Release: **6/6 tests** including both benchmark smoke tests. ASan/UBSan:
**4/4 codec tests**. The ordinary/floor0/floor1/residue0/1/2/chained differential
corpus passes. Direct cosine-sum IMDCT checks cover block sizes 64–8192; accelerated
output retains the `<1e-12` double agreement gate. A new independent per-bit oracle
checks every offset and width 0–33 over packet lengths 0–32, including unaligned
loads, every short tail, failed-read output preservation and zero-bit EOF reads.

Additional 31-second ASan/UBSan fuzz runs after these changes: bits **10,672,889**,
packet **126,788**, whole decoder **706** executions; no finding. These bounded
runs are evidence, not a proof of memory safety. Existing resource checks and
whole-packet validation remain in force. No new transform buffers or tables are
allocated, and the workspace shape is unchanged; a fresh peak-allocation benchmark
was not performed.

Focused native workflow at `cb997fd`: **all four platforms passed** (macos-15
arm64, ubuntu-24.04 x64, ubuntu-24.04-arm, windows-2022 CLANG64):
https://github.com/falseywinchnet/gui_forms/actions/runs/37719941968
These are build/correctness results, not Windows performance measurements.

## Profile and assembly findings

**MEASURED:** `sample <pid> 4 1 -file <output>` while repeatedly decoding the
stereo input. Before: 3,101 samples, approximately 42% inclusive inverse-transform
work and 18% bit-reader peeks. CRC was about 3%, so it was not optimized first.
After: 3,103 samples, bit-reader peeks about 5%. Shares are sampled attribution,
not independently timed kernels. The after profile still spends heavily in
synthesis and packet/residue decoding.

[Before](../bench/results/2026-10-07-m4/profile-before.txt) and
[after](../bench/results/2026-10-07-m4/profile-after.txt) flat profiles are retained.
Assembly was inspected with LLVM 22.1.8 `llvm-objdump --disassemble --demangle` on
the exact Release objects; relevant
[before](../bench/results/2026-10-07-m4/assembly-before.txt) and
[after](../bench/results/2026-10-07-m4/assembly-after.txt) functions are retained.

1. **Per-sample division removed.** `(index + block/4) % size` emitted `udiv`
   in the output-modulation loop. Two disjoint contiguous index ranges now express
   the same wrap. The new loop emits vector `fmul.2d`/`fsub.2d` and no `udiv`.
2. **Dispatch moved out of group traversal.** Previously `blr` ran once per FFT
   group: N−1 indirect calls per N-point transform. Now one selected kernel handles
   the whole stage: log2(N) indirect calls, 11 instead of 2,047 for N=2,048.
   Tiny-stage fallback is selected outside the group loop. Arithmetic order and
   twiddle values are unchanged.
3. **Bounded bit loads.** The common little-endian path checks that eight bytes
   remain and uses standard `memcpy` into uint64. Clang emits one unaligned `ldr`,
   shift and mask; short tails read only the required bytes. No padding or aliasing
   assumption was introduced. [Reader assembly](../bench/results/2026-10-07-m4/bit-reader-after.txt).
4. **SIMD loop inspection.** NEON uses contiguous 128-bit loads/stores, four
   `fmul.2d` operations and explicit add/subtract per two complex butterflies.
   No lane transpose, spill, call, FMA or division occurs in its inner loop.
   Mode selection is outside that loop. Good local assembly does not erase the
   broader algorithmic cost of the current N-point complex FFT.

Executable size, same benchmark target/toolchain: 128,152 to **128,168 bytes**
(+16 bytes). Mach-O `__text`: 63,388 to **63,220 bytes** (−168 bytes); page-aligned
`__TEXT` stayed 81,920 bytes. No assertion of an application-package size change.

**CANDIDATE, not measured/selected:** a more specialized IMDCT factorization,
tiny-stage fusion, and residue scatter/Huffman overhead are the next useful
comparisons. Preserve the scalar mathematical reference and current implementation
as controls. Do not assume AVX2 beats SSE2 or that NEON arithmetic is the only
bottleneck. Native x86 assembly and timings await Shadow.

## Reproduce and review

Use the commands in [Shadow handoff](SHADOW_X86_HANDOFF.md), replacing modes with
`scalar neon` on arm64. For a baseline checkout use the same benchmark harness and
baseline production sources, and pass `--decoder-revision <baseline-sha>`. Keep the
same compiler/flags/inputs; run correctness before collecting uncontended timings.

House-style source review covers `src/bit_reader.cpp`, `src/synthesis.cpp`,
`src/synthesis.hpp`, `tests/core.cpp`, `bench/decode.cpp`, `bench/run.py`, benchmark
CMake and workflow edits: explicit types, named retained callback state, disjoint
buffer contracts, bounds before loads, unchanged operation order, no per-element
allocation/dispatch, failure-state preservation and named timing boundaries.
The scanner reports **26 files, zero spelling findings** for the whole collection.
External oracles were not rewritten or claimed house-style compliant. LLVM's
assembly dump is tool output, not first-party implementation source.
