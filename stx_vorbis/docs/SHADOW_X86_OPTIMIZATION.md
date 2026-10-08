# Native Windows x86 experiment, 2026-10-07

**GIVEN:** Follow `SHADOW_X86_HANDOFF.md`; compare native scalar/SSE2/AVX2,
inspect hot assembly, preserve PCM/resource gates and negative results.

**OBSERVED:** PR #7 merged as `8cd6905d972ebe0e568afa6d6d407adac04896b9`
at 2026-10-08 04:19:25 UTC. That revision includes PR #8's common transform and
bit-reader improvements. This work started after that gate. Provider checkout:
`C:/Users/Shadow/gui_forms`, branch `codex/stx-shadow-x86`. File Manager remains
at `964261f33031db3cec48fd4cacd84f78f3843548`, with GUI.Forms pinned to
`c90eacf36652e2c30a94201e6621f91b2026a865`; no consumer pins changed.

**CANDIDATE:** Pack four independent first-stage FFT butterfly groups into AVX2
registers. Numerical and allocation checks pass locally. Timing evidence is
inconclusive on this shared virtual host. Keep this PR draft; do not promote the
candidate as an accepted speedup. SSE2, scalar, NEON and common codec algorithms
are unchanged by the final candidate.

## Environment and workload

- Native Windows 11 Home 10.0.22621; Shadow virtual desktop, Blade/ShadowComputer,
  hypervisor present. CPU reports AMD EPYC 9354, four guest cores/eight logical
  processors, maximum reported clock 3250 MHz. Balanced power plan.
- MSYS2 CLANG64 Clang 22.1.8, package 22.1.8-3, compiler revision
  `d34feaaa1d0aff624c870910b6f2589aa40d20de`; CMake 4.4.4, Ninja 1.13.2.
- Release `-O3 -DNDEBUG`, C++20, existing `-ffp-contract=off`; no global AVX2,
  FMA, fast-math, LTO or native-CPU flags. SSE2 remains the x64 fallback.
- libvorbis 1.3.7-3, libogg 1.3.6-1. External stb and Tremor use the exact
  commits in the handoff; their source stays outside the repository.
- The checked-in `bench/corpus` stereo and six-channel 48 kHz inputs each emit
  480017 frames. Input hashes and generating encoder are in that corpus README
  and every benchmark summary. All runs use identical compressed bytes.
- Existing benchmark harness from merged `8cd6905d`: warm memory-fed decoding,
  constructor through EOF, excluding destruction, input loading, CSV output and
  result allocation. First-PCM time includes setup, transport and first audio;
  it is not an isolated setup measurement. Sparse probes are not PCM validation.
- No concurrent local compile during timed runs. Shared-host load is outside our
  control. Unchanged libvorbis controls show substantial scheduling variation.

Evidence is under [`bench/results/2026-10-07-shadow`](../bench/results/2026-10-07-shadow/).
Each run directory contains the original summary and a combined `samples.csv`;
the extra `source_csv` column preserves each original filename, round and mode.
Original CSV bytes remain in local `.build/` directories. Summary binary hashes
identify the actual measured executable; Git working-tree status alone does not.

## Baselines

`full-before` is the unmodified merged `8cd6905d` executable, 3 warmups and
3 rounds of 21 measured iterations per mode/case (63 observations).
Times are milliseconds for the complete approximately ten-second input.

| Mode | Stereo median / p95 | Six-channel median / p95 |
|---|---:|---:|
| stx scalar | 28.655 / 29.534 | 81.580 / 85.512 |
| stx SSE2 | 23.256 / 24.766 | 65.898 / 68.817 |
| stx AVX2 | 21.285 / 22.999 | 60.571 / 62.501 |
| libvorbis | 17.387 / 18.436 | 43.425 / 44.577 |
| stb float | 10.491 / 10.846 | Excluded: incorrect PCM |
| Tremor int16 | 21.793 / 22.390 | 56.870 / 58.078 |

Tremor timings are observations only: it emits int16 and full PCM agreement has
not been revalidated locally. Do not treat these as a correctness-qualified
ranking against float output. stb stereo passed a separate full-PCM comparison.

The earlier decoder `0015a1c398e0b88462a8c33517c44e26ca583b50` was also exported
and built with the **same merged harness** and compiler (`original-results`).
Its six CTests passed. Median scalar/SSE2/AVX2 times were 49.332/40.067/37.380 ms
for stereo and 134.546/115.308/106.766 ms for six channels. This later run's
libvorbis controls also slowed to 22.613/54.138 ms. These are separate-session
observations, not a controlled percentage estimate of the sibling's improvement.
Do not attribute the original-to-merged difference to this new AVX2 candidate.

## Profiling and assembly

`wpr -status` reported no active recording. `wpr -start CPU -filemode` failed:
"Failed to enable the policy to profile system performance", profile
`CPU.Verbose.File`, error `0xc5585011`. No WPR CPU profile was obtained.

A small diagnostic tool instead launched an owned benchmark child and sampled
its main-thread instruction pointer using SuspendThread/GetThreadContext/
ResumeThread at approximately 1 ms waits. It recorded 1382 addresses, zero
sampling failures and child exit 0 during 1000 stereo AVX2 decodes. Its source,
raw addresses, symbol listing and function counts are preserved. Sampling
suspends the program and perturbs execution; these are flat wall-clock
instruction observations, not an ETW CPU-time distribution or inclusive stacks.
The sampled run is excluded from all throughput comparisons.

Observed addresses included 626 in `decode_packet`, 315 in `avx2_butterfly`,
130 in `inverse_mdct` and 120 in `Codebook::decode`. The original analysis rebased
addresses using the PE image base and mapped to nearest preceding function
symbols; an approximate image bound classified external addresses. Treat small
counts and boundary attribution as approximate.

The merged AVX2 butterfly handles `half == 1` through scalar arithmetic.
Approximately 66 of its 315 sampled addresses fell in that first-stage region,
204 in the wide loop, 41 in the half-two path and four at boundaries. This
motivated the candidate; it does not establish a whole-decoder gain.

The original wide loop has contiguous four-double loads, multiply/add/subtract,
no loop division, calls, FMA or per-iteration spills. Nonvolatile XMM registers
are saved at entry; `vzeroupper` is outside the interior loop. Assembly files
preserve both original and final Release objects. The candidate packs adjacent
groups with unpack instructions, keeps their arithmetic order, and interleaves
stores back to the original layout. This trades scalar work for shuffles; that
trade must be measured. Short even tails retain the existing SSE2/scalar path.

## Experiments and negative results

1. `sse2-trial`: vectorize two first-stage groups through SSE2. SSE2 medians
   improved approximately 1.3–1.8% in one session; AVX2 did not materially improve.
   **REJECTED for this candidate:** insufficient benefit to retain the extra
   SSE2 helper. Its exact patch and raw measurements remain available.
2. `wide-trial`: both SSE2 and four-group AVX2 helpers. AVX2 medians were
   21.004/58.405 ms, but six-channel p95 worsened to 66.183 ms.
   **REJECTED as evidence of a reliable improvement:** one-session medians were
   not stable enough. The combined patch is retained, not applied.
3. Final candidate: AVX2 helper only. Two A/B/B/A sequences compare the
   preserved merged executable (A) with the candidate (B). Both use the same
   external oracle binaries. Three warmups precede each invocation.

| Sequence/run | Variant | Stereo median / p95 | Six-channel median / p95 | libvorbis median stereo / six-channel |
|---|---|---:|---:|---:|
| Unrestricted 0 | A | 21.759 / 28.233 | 59.675 / 72.835 | 17.282 / 43.459 |
| Unrestricted 1 | B | 20.797 / 21.934 | 60.539 / 93.653 | 17.294 / 43.536 |
| Unrestricted 2 | B | 21.076 / 22.057 | 58.667 / 66.117 | 17.423 / 43.275 |
| Unrestricted 3 | A | 21.922 / 29.870 | 58.697 / 61.129 | 17.184 / 42.714 |
| Affinity 0 | A | 23.233 / 49.844 | 60.737 / 67.881 | 17.142 / 43.022 |
| Affinity 1 | B | 20.914 / 24.110 | 58.091 / 66.486 | 18.533 / 42.930 |
| Affinity 2 | B | 23.968 / 30.177 | 60.020 / 68.165 | 23.841 / 43.067 |
| Affinity 3 | A | 21.142 / 21.670 | 60.884 / 63.328 | 17.132 / 42.619 |

Unrestricted runs use 31 observations each; affinity runs use 41, with the
benchmark shell and children pinned to logical processor mask 4. No machine
power configuration changed. **MEASURED:** control and candidate distributions
vary substantially. **HYPOTHESIS:** the first-stage packing helps some workloads.
**Not established:** a repeatable whole-decode speedup across both workloads.

## Correctness, resources and code size

- Final candidate: six Release CTests pass, including direct IMDCT definition
  and SIMD agreement over sizes 64 through 8192; the existing thresholds remain
  absolute 1e-9 and 1e-12 respectively.
- New exact-bit first-stage checks exercise SSE2/AVX2 when available, even
  sizes 2 through 18, vector boundaries/tails, signed zero, large/small finite
  values and both normal and nontrivial coefficients. Fixed arrays are reused.
- Differential corpus passes, including malformed/resource cases and the
  existing floor/residue/chaining coverage. Largest observed synthetic absolute
  float difference is 1.9073486328125e-6, within the relative-plus-absolute gate.
- Full shared-input candidate float comparison against libvorbis: exact
  960034/2880102 sample counts, maximum absolute errors
  2.384185791015625e-7 / 2.682209014892578e-7; zero out-of-tolerance samples.
  The checker rejects nonfinite output and uses `2e-6 + 2e-6*abs(reference)`.
- stb stereo maximum error is 1.4901161193847656e-7. stb six-channel maximum
  error is 27.011178076267242, with 2880065 samples outside tolerance. Preserve
  this failure; exclude its apparent speed from accepted comparisons.
- Native Windows ASan/UBSan: four CTests pass. libFuzzer build fails because
  this target rejects `-fsanitize=fuzzer-no-link`; no Windows fuzz success is
  claimed. Existing Linux CI owns the six fuzz-stage runs.
- Untimed `memory_report.cpp` queries decoder-owned allocation accounting after
  each advance. Original, merged and candidate all report peaks of 1480656
  bytes stereo / 1884944 bytes six-channel. Setup is 1080208/1287888 bytes;
  workspace 131072/327680 bytes. These are library-reported allocations, not
  process RSS or stack usage. The candidate adds no persistent allocation.
- `llvm-size` baseline/candidate: text 85798/86950 bytes, data 23169/23197 bytes,
  BSS zero. Increase: 1152 text and 28 data bytes. PE file 143872/144896 bytes.
  First-PCM distributions remain in each summary; no isolated setup-only timing
  was added because setup code is unchanged by the candidate.

## Source review and remaining gates

Reviewed scope: added AVX2 first-stage helper and dispatch; new first-stage test;
`bench/tools/memory_report.cpp`; the two diagnostic tools stored with evidence.
Review covers explicit types, named behavior, ownership/borrow lifetimes,
initialization, conversion bounds, unchanged arithmetic order, read-before-write,
disjoint real/imaginary arrays, unaligned vector accesses, even tails, pre-loop
storage and runtime AVX2 selection. No callbacks, FMA or retained state added.
The scanner supplements this review; it does not certify legacy source.

Remaining gates: repeatable performance distributions on a less variable native
x86 host, final cross-platform CI including Linux fuzzing, and review of the
code-size tradeoff. No broad legacy style audit, ETW CPU profile, process-RSS
measurement, Windows Tremor PCM certification or isolated setup benchmark is
claimed. Reversal is removal of the helper and its `half == 1` dispatch.

## Reproduction

Use the commands and pinned external sources in `SHADOW_X86_HANDOFF.md`. On
Shadow prepend `C:/Users/Shadow/file_manager/.build/toolchain/msys64/clang64/bin`
to PATH. Exact compiler/configuration output is retained with evidence.

```powershell
cmake -S stx_vorbis -B .build/stx-perf -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DSTX_VORBIS_REFERENCE=ON -DSTX_VORBIS_BENCHMARKS=ON
cmake --build .build/stx-perf --parallel 2
ctest --test-dir .build/stx-perf --output-on-failure
python stx_vorbis/tools/differential.py --build .build/stx-perf --work .build/stx-differential-final
python stx_vorbis/bench/run.py --build .build/stx-perf --input stx_vorbis/bench/corpus/stereo-10s.ogg stx_vorbis/bench/corpus/surround-10s.ogg --output .build/stx-repeat --modes scalar sse2 avx2 --label shadow-repeat
clang++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wconversion -Wshadow -Istx_vorbis/include stx_vorbis/bench/tools/memory_report.cpp .build/stx-perf/libstx_vorbis.a -o .build/stx-shadow/memory-perf.exe
.build/stx-shadow/memory-perf.exe stx_vorbis/bench/corpus/stereo-10s.ogg
```

To reproduce the original baseline without changing any checkout, export
`0015a1c398e0b88462a8c33517c44e26ca583b50:stx_vorbis` under `.build/`, overlay
only `stx_vorbis/CMakeLists.txt`, `bench/CMakeLists.txt`, `bench/decode.cpp` and
`bench/corpus` from `8cd6905d`, then configure a separate build. Pass
`--decoder-revision 0015a1c398e0b88462a8c33517c44e26ca583b50` to `bench/run.py`.
For A/B runs preserve separate executables, use `--rounds 1 --iterations 31
--modes avx2`, and invoke A/B/B/A. For the affinity sequence set
`(Get-Process -Id $PID).ProcessorAffinity = [IntPtr]4` in the benchmark shell
and use 41 iterations. Record unchanged controls and all runs, not only minima.

The PCM diagnostic expects the existing `.build/stx-perf` reference/decode
executables and `.build/stx-shadow/stb_reference.exe`; build that external oracle
with `clang -O3 -Istx_vorbis/include stx_vorbis/tools/stb_reference.c
.build/stx-oracles/stb_vorbis.c -o .build/stx-shadow/stb_reference.exe`.
Run `python stx_vorbis/bench/results/2026-10-07-shadow/tools/check_benchmark_pcm.py`
from provider root. Raw float files remain ignored.

The diagnostic sampler compiles with `clang++ -std=c++20 -O2 -Wall -Wextra
-Wconversion -Wshadow -municode sample_owned_process.cpp -o sample_owned_process.exe`.
Its five arguments are absolute benchmark path, absolute input path, `avx2`,
child CSV output path, and sample CSV output path. It owns only that child;
never attach it to an interactive application. Do not time a sampled run.

The archived sampler source adds output-error checks after the recorded capture; its sampling loop is unchanged. The spelling scanner reports 28 C++ files and zero findings across stx_vorbis; semantic review remains limited to the authored scope above.
Manifest source hashes describe the captured Windows working-copy bytes; Git line-ending normalization may change text-file hashes on another checkout. Binary hashes are unaffected. Archived logs have trailing whitespace normalized.
