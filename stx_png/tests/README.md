# Owned PNG sample-decoder verification

`stx_png_decoder_tests` (`png_decoder_tests.cpp`) runs 15,300 exact sample cases:
15 legal colour/depth pairs × two interlace modes × seventeen widths × six heights ×
five filters. Each starts from separately constructed source samples and an
independent forward-filter implementation. Additional cases exercise opaque
versus keyed transparency, single-byte IDAT fragments, a dynamic-Huffman image,
truncation, CRC/tail errors, illegal filters, palette indices, resource budgets,
metadata inflater resets, exact storage and overlap rejection. CRC checks use
an independent bit recurrence over every length 0..257 and 16 alignments,
long 65,535-byte slices, and the IEEE `123456789` check value.

`png_dynamic_fixture.hpp` is generated test data from Python zlib 1.2.12 (the
header records the actual generator version), level 6, with the source formula,
shape and raw hash stated in the file. It is not production code. Native tests
need no external PNG or zlib library.

The optional `tools/validate_png.py` experiment (repository root) verifies source hashes against
`libpng_oracle_sha256.json` and the phase-2 upstream zlib manifest, instruments
both oracles and candidate, and compares all matrix samples to libpng's
RGBA16 expansion without colour conversion. `png_oracle.c` owns its row table
in C across setjmp/longjmp; no C++ destructor can be bypassed. The source trees
are caller-provided declared pins, never silently fetched or updated.

The runner generates 150 valid fuzzer seeds and 30 benchmark workloads into
its build output. The PNG fuzzer repairs chunk CRCs after mutation to reach
compressed data and filter processing; it does not repair deflate or Adler.
Input, pixels, dimensions, chunk count, filtered bytes, metadata bytes and work
have finite fuzz limits. A 60-second smoke is not the required sustained fuzz
campaign. ASan/UBSan and leak detection remain enabled; the known LLVM detached
RSS-monitor leak is avoided with `-rss_limit_mb=0 -malloc_limit_mb=0`, as in the
inflate runner, without a leak suppression. Long campaigns need external RSS
supervision and recorded elapsed hours on each architecture.

`tools/benchmark_png.py` measures 256×256 indexed8, RGBA8 and RGBA16 images,
each filter and interlace choice, stored deflate blocks. It reuses the owned
workspace and output buffers; it times a complete CRC-checked decode. The C
libpng oracle creates/destroys its native decoder and row-pointer table per
call, as recorded in its wrapper. Both return lossless RGBA16, with output
sample equality checked outside timing. By default the historical GUI.Forms decoder at
`38390afb` (`tools/codec_bench/png_benchmark_baseline_decoder.cpp.txt`, built
against the name seam in `tools/codec_bench/seam`) is compiled beside the candidate with renamed entry points, sharing
identical CRC and inflate code. `--baseline` selects a recorded trial for a
direct comparison, such as the hinted versus unhinted Sub scan. Each decoder
calibrates batches to at least 50 ms before seven retained rounds, with two
warmup rounds and rotating order. Results include medians/ranges and paired
candidate/baseline ratios; calibration does not guarantee constant later CPU
frequency or scheduling. The no-vector control disables explicit loop hints as
well as automatic loop/SLP vectorization. Throughput is **RGBA16
output MiB/s**, not input bytes/s. Fixtures and allocations for the owned path
are outside timing. These stored-block workloads are useful for isolating PNG
work but do not represent a photographic/dynamic-compression corpus.

The performance oracle enables its native NEON implementation on arm64;
the sanitizer oracle uses portable C. The candidate uses portable C++ only.
The no-vector comparison disables loop and SLP vectorization for all candidate
C++ (including inflate), so its difference is not attributable to one filter.
The x64 output is generic SSE2/no-AVX assembly inspection, not execution.
No result implies an audited replacement or a complete PNG-phase gate.

Text metadata regression coverage checks all 256 iTXt method-byte values with
compression on/off; truncated, overlong, surrogate and out-of-range UTF-8;
embedded NULs; empty text; nonzero Latin-1; and all 1,112,063 nonzero Unicode
scalar values in plain/compressed text and translated keywords. Six small text
seeds supplement the image matrix in the fuzz corpus. These tests validate the
owned strict text policy; libpng remains the sample oracle and is not claimed
to validate those metadata semantics.
