# stx_zlib

`stx_zlib` is the independently buildable and installable owned compression
library. Its public surface includes bounded, allocation-free streaming
inflate and deflate for RFC 1950 zlib streams and RFC 1951 raw deflate. The caller owns
the input/output spans and one mutable stream. It sets finite input, output
and work limits before decoding; a successful stream end authenticates only
the zlib format checksum, not the origin of the bytes. Output emitted before
stream end is unverified and must not be published on failure. The implementation
retains no borrow of caller storage.

The encoder is implemented; the phase-3 acceptance gate remains open pending
the full platform and comparison measurements. Consolidated phase-4 checksums
and the zlib-compatible C ABI are not yet implemented. This is not yet a
complete drop-in zlib replacement.
GUI.Forms' `gui_forms/compression/inflate.hpp` is a source-compatibility seam
for existing callers; new independent consumers include `stx_zlib/inflate.hpp`
and link `stx_zlib::stx_zlib`.

Encoding uses `stx_zlib/deflate.hpp`. Set finite `input_limit`, `output_limit`
and `work_limit`, call `reset`, then feed disjoint input/output spans to
`process`. Input and output may be arbitrarily segmented. Advance each by the
returned consumed/produced count. Use `none` while accumulating input, `sync`
to expose a complete decoded prefix, and `finish` to close the stream. Repeat
the flush with any unconsumed input, then use empty input until the boundary
returns `flush_complete` or `stream_end`. These statuses acknowledge the
boundary; issuing another sync afterwards requests another empty marker.
No input/output pointer survives a call. Resetting is the only way to recover
from an error; discard an emitted prefix on failure. Invalid reset preserves
the previous stream. Allocate the stream object once, preferably off-stack.

Levels `0` through `9` retain the usual speed/compression intent; `-1` selects
`6`. Level `0` stores bytes. Levels 1–3 use greedy LZ77; levels 4–9 compare a
candidate at the following position before committing a match. Search limits
are 4, 8, 32, 16, 32, 128, 256, 1024 and 4096 candidates respectively; early
match lengths are 8, 16, 32, 16, 32, 128, 128, 258 and 258. These are the pinned
reference's level parameters. Lazy matching stops at lengths 4, 16, 16, 32,
128 and 258 for levels 4–9. A lookahead whose existing match reaches 4, 8, 8,
8, 32 or 32 bytes respectively uses one quarter of the chain budget. A higher level
need not produce fewer bytes for every input. Strategies are normal, filtered,
Huffman-only, distance-one RLE and fixed-Huffman. Window bits are 8–15; zlib
format promotes 8 to 9, as upstream zlib does. Raw format can use 8 directly.
Memory levels 1–9 cap the hash table at 256–65536 buckets. Short streams use a
smaller live table, selected from the canonical buffered extent; it grows when
needed. Workspace capacity remains fixed. There are no preset dictionaries or
gzip framing. Speed levels 1–3 skip interior hash insertions for matches longer
than `level + 3`; lazy levels retain all positions. These choices trade search
work against compressed size and are reported together in benchmarks.

The initial deterministic encoding profile uses 32768-byte blocks, a previous
window of at most 32768 bytes, byte-defined hashing, newest-first match ties,
and Huffman leaves ordered by weight then symbol. Sorted leaves and parent
nodes form two monotone queues: equal weights prefer a leaf, then queue order.
One reverse traversal computes depths. Length-limited canonical trees
use at most 15 bits (7 for the code-length alphabet). Stored, fixed and dynamic
blocks compete on encoded bit cost, including headers and stored alignment;
final blocks include final padding. Stored wins a tie; fixed wins a tie with
dynamic. Flush positions are part of the profile. Buffer segmentation does
not select block boundaries. This profile is not a promise of byte identity
with upstream zlib, nor a claim of globally optimal compression. Changes that
alter its bytes require identity review.

The work limit uses conservative implementation-specific effort units, not
retired instructions or CPU cycles. Token append and index-insertion bounds
are charged before their loops. Token emission validates the full bit extent,
packs each token once and publishes its byte cursor once. Match searches
prove whether the whole bounded query fits the remaining allowance; smaller
budgets use an exactly checked kernel. Both accumulate the same effort locally.
Winning lookahead searches are reused, and prefix tags establish the first
three equal bytes without rereading them. History movement and index rebasing
share one pass. An encoding error leaves a frozen
stream and a prefix to discard. No speed comparison disables these limits.

The isolated comparison runner is `tools/benchmark_deflate.py`. It verifies
the pinned reference digests, snapshots source, records commands/LLVM version,
checks each encoder's bytes with upstream inflate outside the timed region,
and reports compressed size alongside throughput. Alternating paired samples
reduce bias from changing host load. `--png PATH` adds the actual filtered
IDAT bytes from a repository PNG, with source and extracted-byte hashes.
`--inspect-x64` emits generic SSE2 assembly without executing it. Synthetic
results and selected captures are not the full PNG or Silesia acceptance matrix.

`tools/benchmark_match_index.py` separately evaluates an exhaustive three-byte
chain and a recency-priority lexicographic tree under identical exact query
schedules. Both can reuse certified prefixes from previous matches. It records
index maintenance and comparison work as well as timing; `--no-sharing`
isolates that optimization. These are experimental indexes over fully
available bytes, not replacements for the encoder's bounded level policies.
See `results/2026-10-20-zlib/phase-three/match-index/README.md` for results and
streaming integration constraints.

The newer exact critical-bit matcher is in `bench/radix_index.hpp`, exercised
by `bench/match_index.cpp`. It shares adjacent-key traversal, defers common-path
maximum updates, and uses lazy expiration for small groups. Select it with
`--radix-finger --inline-radix --small-radix --singleton-radix --lazy-radix`.
The [random-data refactor report](../results/2026-10-20-zlib/phase-three/match-index/RANDOM_REFACTOR.md)
records the current results and negative cases. It is included for integration
but is not wired into `DeflateStream`: incremental lookahead, rebasing, level
policy, work accounting, and the combined workspace budget must be resolved
at that boundary. The streaming encoder above remains usable independently.

`stx_zlib_deflate_tests` checks round trips, buffer-schedule identity, sync
boundaries, length limiting and quota/failure contracts. If Python is present,
`stx_zlib_deflate_oracle` also decodes the emitted records using its independent
zlib runtime. This supplemental runtime check is distinct from the pinned
upstream differential acceptance campaign.

`tools/review_deflate_memory.py` performs an automated memory integrity review
of the encoder and source-pinned upstream decoder with LLVM 22.1.8. It checks
arbitrary valid settings, whole versus fragmented schedules, explicit sync
prefixes, compressed-byte/work-count identity, and short input/output/work
quotas followed by frozen-state checks. The runner retains source snapshots,
commands and detailed artifacts for a separately authorized investigation.
Its console interface reports only `PASS` or `STOP`. On `STOP`, leave detailed
diagnostics for the owner's Daybreak sibling. Do not run this review unless
requested by the owner. A pass applies only to the bounded review performed.

Standalone build and install:

```text
cmake -S stx_zlib -B build/stx_zlib -DCMAKE_INSTALL_PREFIX=<prefix>
cmake --build build/stx_zlib
ctest --test-dir build/stx_zlib --output-on-failure
cmake --install build/stx_zlib
```

The independent fixture in `stx_zlib/tests/installed` uses only
`find_package(stx_zlib CONFIG)` and the exported target. Configure it with
`-DCMAKE_PREFIX_PATH=<relocated-prefix>` after moving an installation; build
and run CTest there to verify the installed encoder and decoder together.
Use the repository LLVM toolchain and, for a sanitizer installation, the
same sanitizer flags for the consumer.

The installed package exports `stx_zlib::stx_zlib`. The altered-source notice
and origin are documented in [PROVENANCE.md](PROVENANCE.md) and
[LICENSE-puff.txt](LICENSE-puff.txt).
