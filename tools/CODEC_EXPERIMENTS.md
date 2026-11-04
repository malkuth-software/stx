# Codec experiments

The owned codecs' oracle, fuzz and benchmark experiments: the scripts in this
directory, the pinned-reference manifests (`stx_zlib/tests/oracle_sha256.json`,
`stx_png/tests/libpng_oracle_sha256.json`) and `.github/workflows/codec-differential.yml`
(weekly and on dispatch). They moved here from GUI.Forms (owner ruling); the
records below are GUI.Forms', kept as written, with paths for this repository.
The commands were recorded on the owner's M4 mirror (`m4build`). None of them
fetches a reference: the zlib and libpng trees are caller-provided pins.

`tools/codec_bench` holds the two benchmarks that compare against historical
GUI.Forms decoders (inflate at `123f13cd`, PNG at `38390afb`); they are written
against GUI.Forms' names, which `tools/codec_bench/seam` maps onto stx_zlib and
stx_png. No package includes those headers.

## Records

### Owned deflate: remote phase-2 experiment

From the root of an stx checkout, with upstream zlib at the pin in
`stx_zlib/tests/oracle_sha256.json` already available locally:

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin python3 -B tools/validate_inflate.py --oracle build-zlib-study-sources/upstream-zlib --output build/inflate-study-strict --strict --seconds 60
```

Repeat without `--strict` and use `build/inflate-study-ordinary` for the ordinary
oracle. This is an isolated instrumented experiment, not the full-project or
24-hour acceptance gate. It uses the M4 selector and installs no host software.
The script checks the oracle source digests and compiler version, retains logs
and uses no leak suppressions. See `stx_zlib/tests/README.md` for scope.

Do not resynchronize a worktree while a build is using its M4 mirror. `m4build`
uses `rsync --delete`: remote-only dependency working files outside the excluded
build directories can be removed. Store generated experiment output under
`build/`; serialize mirror sync and full dependency builds.

For the isolated release benchmark, export the baseline (GUI.Forms' owned inflate
before it became stx_zlib) from a GUI.Forms clone before using the M4 mirror:

```sh
mkdir -p build-zlib-study-sources/baseline
git -C <GUI.Forms clone> archive 123f13cd include/gui_forms/compression/inflate.hpp src/core/compression/inflate.cpp | tar -x -C build-zlib-study-sources/baseline
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin python3 -B tools/benchmark_inflate.py --oracle build-zlib-study-sources/upstream-zlib --baseline build-zlib-study-sources/baseline --output build/inflate-benchmark --inspect-x64
```

The runner verifies baseline and oracle hashes. `--inspect-x64` only emits
assembly using the installed macOS SDK, generic SSE2 and no AVX; it does not
execute an x64 test. Decoder SIMD design targets common 128-bit SSE/NEON lane
groups in portable C++, following the owner's BFFT/Airspy direction. Do not
introduce platform-specific intrinsic forks or wider-register algorithms.

For the phase-3 encoder comparison, use the source-pinned oracle and LLVM runtime:

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin GUI_FORMS_LLVM_RUNTIME=/Users/joshuahkuttenkuler/Developer/PlaysuiteDependencies/toolkit-4e126d6/toolchain/llvm-22.1.8-macos14 python3 -B tools/benchmark_deflate.py --oracle build-zlib-study-sources/upstream-zlib --output build/deflate-comparison --png docs/library/captures/text_input_screen.png --png docs/library/captures/picture_box_screen.png --inspect-x64
```

The runner records source/corpus hashes, flags, paired timing spread and encoded
sizes. PNG cases are extracted filtered IDAT bytes, not recompressed PNG files.
Keep negative results. The SSE2 output is inspection evidence, not x64 execution.
For an already configured isolated sanitizer tree, force a clean build after a
mirror sync: source and remote object timestamps can otherwise disagree.

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin cmake --build build/stx-zlib-standalone-llvm22-sanitize --clean-first -j 8
ssh "$(/Users/ultimussecundai/.local/bin/m4host)" 'env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ctest --test-dir /Users/joshuahkuttenkuler/Developer/CodexBuilds/gui_forms-9709c6a35d08/build/stx-zlib-standalone-llvm22-sanitize -V'
```

This focused package check supplements, and does not replace, the sanitized package
jobs and GUI.Forms' `ci_build.py --sanitize` gates.

For an owner-requested automated memory integrity review:

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin GUI_FORMS_LLVM_RUNTIME=/Users/joshuahkuttenkuler/Developer/PlaysuiteDependencies/toolkit-4e126d6/toolchain/llvm-22.1.8-macos14 python3 -B tools/review_deflate_memory.py --oracle build-zlib-study-sources/upstream-zlib --output build/deflate-memory-review --seconds 120
```

The runner prints only `PASS` or `STOP`; do not inspect its detailed diagnostic
files in this chat. On `STOP`, the owner will use a Daybreak sibling. Do not
start further reviews without the owner's request. Preserve each review
directory before changing the harness. Its source snapshots, oracle digests
and logs identify exactly what was reviewed. The
independent installed-consumer fixture is `stx_zlib/tests/installed`; test a
relocated prefix with the same LLVM toolchain and sanitizer mode as its archive.

For the exact match-index algorithm comparison (no memory review is run):

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin GUI_FORMS_LLVM_RUNTIME=/Users/joshuahkuttenkuler/Developer/PlaysuiteDependencies/toolkit-4e126d6/toolchain/llvm-22.1.8-macos14 python3 -B tools/benchmark_match_index.py --output build/match-index-shared --png-offset 1048576 --png docs/library/captures/text_input_screen.png --png docs/library/captures/picture_box_screen.png
```

Use a new output directory for each variant. `--no-sharing` disables carried
match proofs, and `--files-only` omits synthetic cases. Compare identical byte
samples and query schedules. Index timings do not establish whole-encoder
speed or production streaming behavior.

The capped radix experiment uses the same command with `--radix-finger` and a
new output directory, such as `build/match-index-radix-finger`. It runs 1052
deterministic boundary fixtures before measurements. `--verify-only` runs those
algorithm fixtures without timing; omit PNG arguments in that mode.
`--greedy-only` measures only phrase-boundary queries while retaining all
insertion and expiration work. `--radix` selects the eager-maxima comparison.
See `notes:results/2026-10-20-zlib/phase-three/match-index/RADIX.md` for invariants and
the distinction between index memory and the total streaming encoder budget.

The random-maintenance variant additionally selects `--inline-radix
--small-radix --singleton-radix --lazy-radix`. `--random-sweep` adds three
fixed random seeds at 128 KiB and 1 MiB; `--random-only` skips other synthetic
cases. `--assembly` retains compiler assembly, and `--restrict-index` applies
the same disjoint-index contract to both compared kernels. See
`notes:results/2026-10-20-zlib/phase-three/match-index/RANDOM_REFACTOR.md` for results
and limitations. Use `--greedy-only` to reproduce its query schedule.

### Owned PNG: remote phase-5b experiments

The private sample decoder's sanitizer/oracle and benchmark commands are:

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin python3 -B tools/validate_png.py --libpng build-zlib-study-sources/libpng --zlib build-zlib-study-sources/upstream-zlib --output build/png-final --seconds 60
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin python3 -B tools/benchmark_png.py --libpng build-zlib-study-sources/libpng --zlib build-zlib-study-sources/upstream-zlib --workloads build/png-final/workloads --output build/png-benchmark
```

The first command generates the second's workloads. Serialize them and every
other mirror/build operation. Oracle pins and file hashes are checked before
compiling. The sanitizer comparison uses portable-C libpng; the benchmark
uses its NEON implementation on arm64. Neither is a production dependency
change. Full-suite sanitizer verification still uses GUI.Forms' `tools/ci_build.py`. The recorded
helper, `notes:results/2026-10-19-zlib/png/full-suite-driver.sh`, supplies its
logical workspace layout on the hashed M4 mirror. Copy it into `build/` of a GUI.Forms
checkout first; it runs from the checkout's root:

```sh
/Users/ultimussecundai/.local/bin/m4build -- sh build/full-suite-driver.sh
```

### PNG CRC suitability experiment

The standalone comparison is first-party experimental code, not a new runtime
library dependency. It is kept at `notes:experiments/png-crc-suitability/`;
`run.py` finds this checkout two directories above itself, so copy that
directory to `experiments/png-crc-suitability/` in a checkout before running
it. It compiles the existing pinned upstream zlib both natively
and with its ARM CRC opcode path disabled, verifies complete CRCs under
sanitizers, and compares chunk costs and whole PNG decoding. Run it after
`validate_png.py` has generated the workload directory:

```text
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin python3 -B experiments/png-crc-suitability/run.py --zlib build-zlib-study-sources/upstream-zlib --workloads build/png-hinted-check/workloads --output build/png-crc-suitability-final
```

Keep performance trials sequential with builds, fuzzing and suite execution.
Inspect assembly before crediting the degree-261 quartet with register-resident
state; source arrays alone do not establish that allocation. See
`notes:experiments/png-crc-suitability/README.md` for provenance and measurement limits.

Repeat the CRC experiment with `--baseline-isa` and a separate output directory
to measure baseline Armv8-A candidate code without Apple-target three-input XOR
fusion. This option is specific to the arm64 measurement host; the candidate
algorithm itself has no platform intrinsics.
