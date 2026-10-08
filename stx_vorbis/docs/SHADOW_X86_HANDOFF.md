# Shadow: native x86 performance handoff

**GIVEN:** Owner requests comparative decode measurements, hot-loop assembly review,
then measured optimizations. Windows work belongs to the Shadow desktop sibling.
Follow `planning/PROGRAMMING_HOUSE_STYLE.md`: explicit types, named behavior, no
lambdas/auto/arrow member access, bounded reusable storage, no blanket fast-math.
Review source semantics as well as running the scanner.

## Ownership and starting point

Provider: https://github.com/falseywinchnet/gui_forms, optimization follow-up to merged PR #6,
branch `codex/stx-vorbis-optimization`. Fetch that branch and record its exact HEAD. Decoder
baseline before optimization is `0015a1c398e0b88462a8c33517c44e26ca583b50`.
The benchmark harness is added after that baseline. A baseline comparison must
use the same harness and compiler with baseline production sources.

Expected desktop checkout: `C:\Users\Shadow\file_manager`; confirm it exists.
GUI.Forms is a separate provider repository/submodule. Inspect Git status and
remotes before editing. Do not update File Manager's dependency pin, modify
PlaySuite, or touch the sibling threading/audio work. Keep outputs under `.build/`.
Coordinate source ownership with the Mac sibling before editing `synthesis.cpp`:
that sibling is measuring ARM64 and removing common transform/bit-reader overhead.
Use a separate provider worktree/branch for x86 changes and send the exact commits
back for integration; never force-push the shared PR branch.

## Reproduce before editing

Use native Windows MSYS2 **CLANG64**, LLVM **22.1.x**, explicitly `clang`/`clang++`.
Record `clang++ --version`, CPU model/features, Windows version, power mode, runner
or desktop, and whether anything else is compiling. Do not call Rosetta/Wine runs
native Windows results. Install CLANG64 CMake, Ninja, pkgconf, libogg and libvorbis
if not present. In a CLANG64 shell at provider root:

```sh
cmake -S stx_vorbis -B .build/stx-perf -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DSTX_VORBIS_REFERENCE=ON -DSTX_VORBIS_BENCHMARKS=ON
cmake --build .build/stx-perf --parallel 4
ctest --test-dir .build/stx-perf --output-on-failure
python tools/check_house_style.py stx_vorbis
python stx_vorbis/tools/differential.py --build .build/stx-perf --work .build/stx-differential
mkdir -p .build/stx-corpus
.build/stx-perf/stx_vorbis_reference encode .build/stx-corpus/stereo-10s.ogg 2 48000 480017 0.5
.build/stx-perf/stx_vorbis_reference encode .build/stx-corpus/surround-10s.ogg 6 48000 480017 0.4
python stx_vorbis/bench/run.py --build .build/stx-perf \
  --input .build/stx-corpus/stereo-10s.ogg .build/stx-corpus/surround-10s.ogg \
  --output .build/stx-performance-before --modes scalar sse2 avx2 --label shadow-before
```

If AVX2 is unavailable, omit it and record that explicitly. `scalar` selects the
portable source kernel; compiler auto-vectorization remains enabled. Fixed source
commits/PCM checks are necessary: generated Ogg byte hashes can differ with the
encoder/platform even when the generator parameters match.

For external stb and Tremor comparisons configure these optional paths:

```sh
cmake -S stx_vorbis -B .build/stx-perf \
  -DSTX_BENCH_STB_SOURCE=/absolute/external/stb_vorbis.c \
  -DSTX_BENCH_TREMOR_ROOT=/absolute/external/tremor
cmake --build .build/stx-perf --parallel 4
```

Sources stay external and never enter the shipping library. Pinned references:

- stb v1.22 from miniaudio commit `f40cf03f80cdb7e741d43e53b7e706e8c1394bcf`,
  `https://raw.githubusercontent.com/mackron/miniaudio/f40cf03f80cdb7e741d43e53b7e706e8c1394bcf/extras/stb_vorbis.c`

- Tremor `https://github.com/xiph/tremor`, commit `820fb3237ea81af44c9cc468c8b4e20128e3e5ad`.
- libvorbis 1.3.7 / libogg 1.3.6 here; record actual Windows package versions.

The native harness measures memory-fed decoding, constructor through EOF,
excluding destruction. First-PCM time includes setup, transport and initial audio
work; remaining time is the rest of that stream, not setup alone. Input loading,
process startup, CSV output and result-vector allocation are outside timing.
It does sparse output probes, not a complete PCM comparison. Correctness comes
from the separate differential tests. Tremor emits int16, the others planar float;
report that conversion/precision difference rather than pretending equal work.
Do not rank unsupported or numerically incorrect oracle cases as performance wins.
`docs/VALIDATION.md` records existing stb/Tremor disagreements.

## Profile, inspect, optimize

Run a long repetition with stdout redirected; use Windows Performance Recorder /
Analyzer CPU sampling or an available native profiler. Preserve the profile and
exact command. Inspect LLVM output from the same Release binary/objects:

```sh
llvm-objdump --disassemble --demangle .build/stx-perf/CMakeFiles/stx_vorbis.dir/src/synthesis.cpp.obj > .build/stx-synthesis-before.asm
llvm-objdump --disassemble --demangle .build/stx-perf/CMakeFiles/stx_vorbis.dir/src/packet.cpp.obj > .build/stx-packet-before.asm
```

Verify the actual Ninja object suffix/path if different. Inspect hot SSE2/AVX2
loops for spills, scalar lanes, divisions, function calls, redundant loads,
`vzeroupper` placement, dependency chains and tails. Twiddles and real/imaginary
arrays are contiguous; preserve that layout and avoid transposes. Check tiny FFT
stages as well as large stages. Do not enable global `-mavx2`: runtime dispatch
must keep the binary runnable on SSE2-only x64. No FMA/reassociation without an
explicit numerical contract and demonstrated benefit. Compare scalar/SSE2/AVX2
on the same input and CPU; AVX2 is not automatically faster.

Each proposed change needs before/after distributions (raw CSV, median/p95),
frame counts, PCM error, setup cost, peak memory, object/code-size change and
assembly explanation. Preserve negative experiments and commands. Do one
meaningful change at a time. Keep native Windows findings distinct from CI.

## Gates and return packet

- All four CTest tests, differential corpus including floor0, residues0/1/2,
  multichannel/chains/format changes, malformed input/resource tests.
- Direct IMDCT definition error <1e-9 absolute; scalar vs SIMD <1e-12 double.
  PCM vs libvorbis <= `2e-6 + 2e-6*abs(reference)`; exact counts required.
- ASan/UBSan + targeted bits/Huffman/setup/packet/decoder fuzzing where supported;
  report Windows sanitizer limitations, and carry the change through Linux CI.
- Source review of every authored implementation/test/tool against house style.
- Return branch, commits, exact source/encoder/compiler revisions, CPU/OS,
  before/after JSON+CSV, assembly excerpts, correctness results, unsupported
  cases and any remaining concern. Do not merge or change consumer pins.

Direct sibling communication is authorized by the owner. The route/thread is
pending; the Mac sibling will coordinate once the owner supplies it.
