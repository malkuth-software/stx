# BFFT-backed inverse MDCT experiment

**GIVEN, owner direction 2026-10-07:** adapt the owner's BFFT library for the
Vorbis decoder. This explicitly opens this transform experiment beyond the older
research-only BFFT references. The shipping decoder remains the control.

**MEASURED:** A working complete decoder using BFFT's BODFT inverse takes roughly
23–25% less time on the two shared Windows benchmark inputs. This directory is
an opt-in experiment, not an installed codec configuration or a completed release
integration. It deliberately cannot replace the ordinary `stx_vorbis` target.

## Dependency and implementation

BFFT source: https://github.com/falseywinchnet/bfft

Reviewed revision: `6e64386f2da658a90493733c08b01dea6425ecdd`, MIT license.
Only its public BODFT API and `src/bodft.cpp` with its two private kernel/backend
headers are compiled. No BFFT viewer, FCT, vision subsystem, Python runtime or
other transform is linked. The checkout is external and remains unchanged.
CMake requires the exact revision and rejects modifications to the consumed
files. CI fetches that source explicitly using a sparse checkout.

`../bfft_imdct.hpp` owns one BODFT plan per block size per experimental decoder.
The plan has mutable scratch and must not be shared by concurrent transforms.
All adapter buffers and plans are prepared before packet processing; execution
does not allocate. Storage is double precision. The measured Windows build uses
BFFT's SSE2-capable baseline without global AVX2/FMA flags. The compact path uses
the inverse kernel, not its explicitly vectorized forward kernel. Architecture
selection within BFFT is a build choice, not our existing per-call synthesis
selector; the experiment measures `automatic` only.

`project_decoder.py` copies private codec sources into the CMake binary directory
and replaces exactly checked anchors for workspace ownership and the transform
call. It leaves the shipping source files unchanged. The projected decoder still
performs the original parsing, coupling, floor application, windowing, overlap,
PCM conversion and original resource checks. Its additional BFFT allocations are
outside those checks, so its memory report is incomplete (see release gates).

## Transform identity

Let the Vorbis output block size be `N`, and `M=N/2`. The old implementation uses
an N-point complex FFT. The candidate computes an M-point **real inverse BODFT**
and reconstructs the inverse MDCT through DCT-IV symmetry:

```text
phi[k] = pi*(k+1/2)/(2*M),                         0 <= k < M/2
H[k]   = (X[k] - i*X[M-1-k]) * exp(i*phi[k])
z      = inverse_BODFT_M(H)                       (normalized by 2/M)
y[2*j]       =  (M/2)*z[j]
y[M-1-2*j]   = -(M/2)*z[M/2+j],                  0 <= j < M/2
```

Here `y` is the unnormalized M-point DCT-IV. The N-sample IMDCT consists of
`y[M/2..M)`, then the negated reverse of all `y`, then `-y[0..M/2)`.
The adapter expresses these as three bounded loops. For a 2,048-sample block,
the transform becomes a 1,024-point real BODFT instead of a 2,048-point complex
FFT. It uses the library's paired radix-4 odd-frequency factorization.

The earlier padded candidate uses an N-point forward BODFT on `[X, zeros]` and
rotates its output into DCT-IV coordinates. It remains available through the
adapter's `compact=false` constructor for comparison; the decoder projection
selects the smaller inverse formulation. Its source and initial measurements
are retained, rather than presenting the padded version as the final result.

## Measurements

**MEASURED:** Shadow Windows 11 guest, AMD EPYC 9354, four presented cores/eight
logical processors, Balanced power plan, MSYS2 CLANG64 LLVM 22.1.8. Release
`-O3 -DNDEBUG`, contraction off, no global AVX2/FMA or LTO. The retained baseline
also has debug line tables; these are not enabled in the candidate. Baseline
production revision is `481f885362e2f21be8311f848800834dbe2704e3`, identical in
source to merged main `593b1cadf920e0463f9702ac320f5483dc93ba97`.

Each invocation has three warmups and 21 measured decodes; three rotating-order
rounds give 63 observations per decoder/input. The entire sequence is repeated.
Both original inputs have 480,017 frames at 48 kHz. Timings include construction
through EOF, with memory-fed compressed data, and exclude destruction. No owned
build, sanitizer run or profiler overlaps a timing run. The shared guest remains
subject to scheduling noise. libvorbis is the same-machine control; external
stb/Tremor results from the baseline harness are retained but not used here.

| Run and input | Current median / p95 ms | BODFT median / p95 ms | libvorbis median, baseline / candidate ms |
|---|---:|---:|---:|
| First, stereo | 18.7143 / 19.9410 | 14.0763 / 14.9245 | 17.1927 / 17.1372 |
| First, six-channel | 50.6655 / 53.8094 | 37.9194 / 39.9265 | 42.9117 / 42.9440 |
| Repeat, stereo | 18.5848 / 19.9267 | 14.3632 / 16.2908 | 17.1455 / 17.9688 |
| Repeat, six-channel | 50.8299 / 58.2836 | 38.3380 / 42.2013 | 42.9151 / 43.0701 |

The candidate beats the libvorbis control on these two Windows workloads. This
is not a claim about every file or other machines. The repeated stereo control
also slowed, showing guest variability. First-PCM latency in the first comparison
increases from 0.8981 to 0.9478 ms stereo and 1.3004 to 1.3484 ms six-channel.
The experimental projection retains unused old transform plans/scratch, and the
public BODFT plan prepares both float and double state, so setup/memory need their
own integration work. No allocation reduction is claimed.

Initial kernel measurements at N=8,192 were about 46 us current versus 17 us
compact BODFT. Whole-decoder results above are the acceptance evidence; the
microbenchmark is an attribution aid, not an application speedup claim.

## Correctness and limits

**MEASURED:** Release and ASan/UBSan experiment CTests both pass (2/2 each).
All block sizes 64–8,192 pass zero, impulse, constant, alternating and sinusoidal
guards. Every output sample is compared with the existing transform at absolute
error below `1e-9`; maximum observed error is about `3e-12`. A separate direct
cosine-sum oracle checks up to 128 positions per pattern/block. Its rational
phase is reduced modulo 2*pi in integer arithmetic before evaluating cosine.
The initial unreduced oracle failed the large constant case against **both**
implementations, whose output agreed; that diagnostic is retained. The acceptance
bound was not loosened. Higher precision is confined to this independent oracle.

All 17 existing/generated/chained/benchmark inputs produce **byte-identical PCM**
to the retained baseline. The existing libvorbis differential suite passes, and
complete benchmark PCM is finite and within `2e-6 + 2e-6*abs(reference)`. Maximum
benchmark errors are 2.384185791015625e-7 stereo and 2.682209014892578e-7 six-channel.
`check_pcm.py` compares every float and explicitly rejects nonfinite values.
Byte identity is evidence for this corpus, not a mathematical guarantee for all
inputs after changing factorization.

**OBSERVED release gates, not waived by benchmark success:**

- BODFT's internal `heap_array::resize` failures are not checked by its plan
  constructor. The public `new(nothrow)` checks only the outer allocation; it
  does not establish successful table/scratch construction. Allocation failure
  must become an explicit, recoverable failure before default use.
- BODFT allocates outside the codec's supplied `memory_resource`. Its plan and
  scratch need an allocator-aware or caller-owned storage interface so the
  decoder can enforce/report its budget and exercise every allocation failure.
- BODFT's `const` plan owns mutable scratch. Ownership must stay per decoder,
  or an explicit workspace must separate immutable plan data from execution.
- Default integration should remove redundant old plans/buffers, avoid unused
  float planning, define synthesis-selection semantics, and rerun memory,
  failure, numerical, native-platform and complete-decode gates. The original
  transform remains a reversal path and independent control.

This experiment does not silently waive those contracts or install a new default.
Its purpose is to demonstrate the adaptation with actual library calls and quantify
the benefit before changing the provider's allocation API. CI runs the adapter on
macOS arm64, Windows x64, Linux x64 and Linux arm64; pending runs are not local
evidence. Existing shipping-code fuzz stages remain enabled.

## Reproduce

From GUI.Forms root, with the toolchain and libvorbis oracle dependencies in
`docs/SHADOW_X86_HANDOFF.md` available:

```text
cmake -S stx_vorbis/bench/bfft -B .build/stx-bfft -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DBFFT_SOURCE=/absolute/path/to/pinned/bfft
cmake --build .build/stx-bfft --parallel 4
ctest --test-dir .build/stx-bfft --output-on-failure
python stx_vorbis/tools/differential.py --build .build/stx-bfft --work .build/stx-bfft-differential
python stx_vorbis/bench/run.py --build .build/stx-bfft --input stx_vorbis/bench/corpus/stereo-10s.ogg stx_vorbis/bench/corpus/surround-10s.ogg --output .build/stx-bfft-timings --modes automatic --iterations 21 --warmups 3 --rounds 3 --compiler clang++ --label bodft
```

Add `-DSTX_BFFT_SANITIZERS=ON` in a separate build for instrumentation; never time
that build. `check_pcm.py --help` documents the baseline/full-PCM comparison.
Raw results are under `../results/2026-10-07-bfft/`. Neither consumer pins nor
BFFT sources were modified.

House-style semantic review covers the adapter, kernel check/benchmark, projection
tool, PCM check, CMake and workflow edits: explicit types and initialized storage,
named executable behavior, mode decisions outside element loops, numerical order,
bounded borrows, per-decoder mutable state and failure boundaries. The scanner
reports 31 C++ files with zero findings. BFFT is an external reviewed dependency;
this does not certify its legacy source as compliant with GUI.Forms' house style.
