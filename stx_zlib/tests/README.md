# Inflate verification

`inflate_tests.cpp` (`stx_zlib_inflate_tests`) runs in the package suite without an
external codec; `stx_zlib_inflate_fuzz_tests` replays the fuzz harness's
standalone input.
It covers stored/fixed/dynamic blocks, one-byte buffers, exact tails, truncated
input, malformed alphabets/repeats/symbols/distances, wrapper checks, dictionaries,
quotas, history wrap, overlap rejection and reset/failure behavior.
`inflate_fixture.hpp` was generated with Python zlib 1.2.12 from 40000 bytes
whose byte at index i is i modulo 13. It is generated fixture data, not codec
source. GUI.Forms' checked-in 1x1 red RGBA PNG (`assets/fixtures/compression`) was
constructed from PNG fields with the same Python zlib producer. The fuzz corpus
(`fuzz/corpus/inflate`) contains its IDAT, raw stored/fixed/dynamic streams, dictionary and reserved-block
cases. Corpus byte zero selects raw (even) or zlib (odd) framing.

`tools/validate_inflate.py` (repository root) builds an isolated, instrumented oracle experiment
with LLVM 22.1.8, ASan/UBSan/LSan, explicit source digests and no suppressions:

```sh
python3 -B tools/validate_inflate.py --oracle build-zlib-study-sources/upstream-zlib --output build/inflate-study-strict --strict --seconds 60
python3 -B tools/validate_inflate.py --oracle build-zlib-study-sources/upstream-zlib --output build/inflate-study-ordinary --seconds 60
```

Provide upstream zlib at `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`; the script
never fetches it. M4 execution uses `m4build` with LLVM 22.1.8 on PATH. The oracle
adds 700 positive streaming cases: 10 levels, 7 windows (9–15), 5 strategies and
2 wrapper modes, each with both one-byte input/output and whole-buffer decoding. Window 8 is covered by the
native contract tests; upstream deflate's window-8 behavior is not relabelled
as a window-8 oracle. The fuzz harness compares whole-buffer and byte-split
execution, then checks acceptance, exact output and stream boundary against
the selected oracle. Resource-limit results are separated from format errors.

Use `--seconds 86400` for each accepted 24-hour campaign on each architecture.
Short smoke campaigns do not pass that gate. Fuzzer coverage counters are not
source-region or branch coverage; collect and inspect the latter separately.
The candidate decoder and oracle are instrumented in the experiment; this does
not substitute for the sanitized package and GUI.Forms suites on Linux, macOS
and Windows.

The M4 smoke run exposed a shutdown leak in LLVM libFuzzer's detached RSS
monitor. The validation runner disables that thread with `-rss_limit_mb=0`
(and the malloc limit with `-malloc_limit_mb=0`), while retaining ASan/UBSan,
LSan and libFuzzer leak detection without suppressions. The fuzzer therefore
has no process-RSS watchdog; the candidate's finite per-stream budgets remain
active. Preserve the original failure and use external process supervision for
long campaigns. LLVM 22.1.8 `FuzzerDriver.cpp:StartRssThread` documents this
zero-limit path. This is not a source-level fix to libFuzzer.

Bulk-kernel tests also exercise 19 match distances against 19 output chunk
sizes, history wrap, exact trailing bytes, 300 chunk sizes on the dynamic
fixture, and maximal stored blocks of 0xff around Adler's 4096-byte batch
boundary. Two additional seeds contain 16384 bytes of i modulo 13, encoded
with Python zlib 1.2.12 in raw/zlib framing, within the fuzzer's output cap.

`tools/benchmark_inflate.py` builds the original owned implementation, current
implementation and a compiler-vectorization-disabled ablation with the same
release flags, each also timing pinned upstream zlib. See the
[measurement record](https://github.com/malkuth-software/notes/blob/gui_forms/results/2026-10-19-zlib/phase-two/optimization/README.md)
for limitations. The baseline is GUI.Forms' owned inflate at `123f13cd`, before it became
stx_zlib: pass `--gui-forms <GUI.Forms clone>` to read it from that clone's Git,
or `--baseline` pointing to an export of its header and source; the runner
verifies their hashes.
