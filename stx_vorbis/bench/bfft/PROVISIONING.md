# BODFT allocation boundary and provision

**GIVEN, owner direction 2026-10-07:** bound and provision the BFFT adaptation
holistically. All transform storage must participate in the codec resource gates.

**IMPLEMENTED candidate:** BFFT provider revision
`0f75ca79fbdc9176af729fc67afed59dca436ff1`,
[provider PR 90](https://github.com/falseywinchnet/bfft/pull/90), plus this optional
decoder adapter. This is a complete resource-controlled decoder experiment;
shipping `stx_vorbis` and File Manager dependency pins are unchanged.

## Ownership and accounting

| Storage | Owner and lifetime | Provision |
|---|---|---|
| BODFT coefficients, permutation and metadata | Immutable Setup plan, stream lifetime | One queried aligned block per size, double precision only |
| DCT-IV rotations | Same plan | PMR vector with N/4 complex entries |
| BODFT execution scratch | Decoder Workspace | One queried block per distinct block size, reused across channels/packets |
| Packed spectrum, inverse output, DCT-IV output | Decoder Workspace | Three PMR vectors sized to the largest block; active prefixes for small blocks |
| Overlap window | Existing Setup Transform | Original coefficients retained; unused original FFT arrays stay empty |

Every allocation goes through the decoder's `detail::Memory` and its supplied
`memory_resource`. Setup construction charges plans before publishing a const
Setup owner. Workspace byte reports include all BODFT and conversion buffers;
current/peak reports include the exact upstream requests, excluding upstream
allocator bookkeeping. No accounting-only reservation or global allocator hook
is used. The resource outlives both Setup and Workspace.

BFFT queries bytes and alignment without allocation. It constructs coefficients
and scratch in supplied storage and retains neither allocator nor input/output.
Independent workspaces may share an immutable plan. Scratch does not borrow its
creation plan; the compatible plan is supplied for each execution, so resetting
or replacing a stream cannot leave a dangling plan pointer in retained scratch.
No lazy plan cache or first-audio allocation is introduced.

Replacement storage still counts toward the peak. Setup is built before it is
published; failed setup is released. A failed workspace preparation invalidates
scratch and follows the decoder's sticky failure/reset protocol. It does not
promise preservation of the preceding stream's execution state. Equal block
sizes reuse one scratch block; each Setup currently retains two coefficient
plans even when their sizes match. That bounded duplication is explicit.

The worst supported mixed block pair (64,8192) provisions **248,704 bytes** in
nine requests for this adapter's plans, rotations and scratch on Windows x64.
The equal (8192,8192) pair uses **329,424 bytes**, eight requests. These are
transform-only totals, not complete decoder limits or portable ABI constants.

## Measured correctness and failure gates

Windows x64, Clang 22.1.8, 2026-10-07:

- BFFT full library suite: **10/10**. Prepared storage suite also passes ASan/UBSan.
- Adapter release suite: **5/5**; adapter plus BFFT ASan/UBSan suite: **5/5**.
- Tests prove exact-byte-budget success, one-byte-short failure, cleanup after
  each injected transform-allocation failure, and no allocation requests during
  alternating small/large transforms with allocation forbidden.
- Existing hostile decoder allocation-failure sweep passes with the candidate.
  Additional whole-decoder tests compare every current/peak report with the
  tracking upstream allocator, including reset, changing-size chained streams,
  and exact versus one-byte-short limits. Destruction balances size/alignment
  for every allocation, including partial construction.
- All **17** fixture/generated/chained/benchmark PCM comparisons are byte
  identical to current production. Every sample is finite and passes the
  libvorbis differential bound. Kernel tests cover N=64..8192, five patterns,
  the existing transform and the independent phase-reduced cosine oracle.
- The unchanged shipping codec suite passes **7/7**.

Transform execution is allocation-free. This does not claim all demuxing/input
growth or stream replacement is allocation-free. Those operations retain their
existing bounded resource behavior.

## Measurements

Same Shadow Windows 11 guest/toolchain and workload as the initial BFFT record:
two 480,017-frame 48 kHz inputs; memory-fed construction through EOF, destruction
excluded; three warmups plus 21 samples per round, three rounds per observation.
No owned build, sanitizer or profiling process overlaps timing. The second
observation reverses candidate/control order. Both runs are retained, including
the first run's visibly slower libvorbis control.

| Run/input | Current median / p95 ms | Provisioned BODFT median / p95 ms | libvorbis median, current-run / candidate-run ms |
|---|---:|---:|---:|
| First, stereo | 18.8512 / 22.3774 | 14.4311 / 17.4595 | 17.2365 / 17.6349 |
| First, six-channel | 50.9258 / 53.1763 | 45.0903 / 51.1377 | 42.8359 / 47.8080 |
| Repeat, stereo | 18.5963 / 19.4479 | 13.9347 / 14.8229 | 17.6885 / 17.1920 |
| Repeat, six-channel | 50.5979 / 52.3032 | 37.6734 / 39.3254 | 42.9234 / 43.0067 |

**MEASURED:** repeat median decode time falls 25.1% stereo and 25.5% six-channel
against the current FFT decoder. First PCM medians in that repeat are
0.8840→0.8131 ms and 1.3989→1.3153 ms. The earlier prototype's extra legacy plan
construction is no longer present. This guest evidence is not a cross-platform
speed claim or proof that every input beats libvorbis.

Reported complete-decoder resident requests at stream begin/end decrease from
1,480,656 to **1,414,832 bytes** (stereo), and 1,884,944 to **1,819,120 bytes**
(six-channel): **65,824 bytes saved** in each. These are comparable complete
counts; the old unbounded prototype's incomplete reported count is not used.

Raw CSVs, binary hashes, summaries, PCM comparisons and test logs live in
[`../results/2026-10-07-bodft-provisioning/`](../results/2026-10-07-bodft-provisioning/).
The recorded candidate timings precede only an invalid-order guard, explicit
deleted copy declarations and build-dependency/test-reporting edits; no numerical
or execution-kernel change followed. Correctness/resource/sanitizer suites were
rerun on the final source.

## Remaining release work and source review

The BFFT provider's [CI run 37738642195](https://github.com/falseywinchnet/bfft/actions/runs/37738642195)
passes all four jobs: Linux Make, plus CMake on Linux, macOS and Windows.
The three CMake jobs include the new provider storage tests.

GUI.Forms CI now runs the provider storage tests and provisioned adapter on Windows x64,
macOS arm64, Linux x64 and Linux arm64. Pending runs are not claimed as passed.
Before default integration, define how BODFT's build-selected backend coexists
with the public `Synthesis` selector, retain the original transform control,
and choose the installed dependency/export contract. This experiment intentionally
uses the BODFT path regardless of the old butterfly selector and benchmarks only
`automatic`; it must not be presented as a new runtime AVX2 implementation.

House-style semantic review covers `bounded_imdct.hpp`, `provisioning.cpp`, the
projection edits, kernel harness edits, PCM reporting, CMake and CI integration:
explicit types, named behavior, initialized active regions, owner/resource
lifetimes, shape/precision/conversion boundaries, destruction after partial
construction, and setup versus repeated processing. The spelling scan covers
33 codec files plus 13 projected source files with zero findings. Unchanged
external legacy BFFT implementation is not certified by that scan; the provider's
own scoped review is in `documentation/bodft-storage.md`.
