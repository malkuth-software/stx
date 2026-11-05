# Hash storage and probe relocation — 2026-10-26

The retained resource cache and flattenable dictionaries use SkTHash.h.
Subsequent dependency inspection found the generic LRU utility, dynamic-hash
adapter and multimap are unused by the CPU closure; they are now reference-only
(see INTRUSIVE_LIST.md). Its original source remains byte-for-byte in
baseline/skia/src/core/SkTHash.h and is selected first by reference builds.
This work retains descending linear probing and the ordinary three-quarter
growth/one-quarter shrink thresholds. No retained caller supplies the optional
ShouldGrow/ShouldShrink protocol; that member-detection machinery is removed.

## Ownership conversion

The table owns one slot allocation. Each nonzero hash identifies a live value in
that slot's union storage. Construction/destruction use explicit standard lifetime
operations; no additional occupancy flags, counters, handles or registries are
introduced. Slot allocation initializes hashes without clearing inactive payload
bytes. Reset destroys values and releases storage. Moves leave the source empty.
Resize prepares replacement allocation before transferring entries, then swaps
ownership. Copy assignment prepares a replacement before releasing its old values.
Entry/trait/visitor operations must not throw or reenter a mutating table.

Capacity calculations check multiplication and power-of-two bounds. Reserve(0)
is a no-op; manual resize accepts zero only for an empty table. Maximum entry
reservation is 805,306,368 for a capacity of 2^30. Tests exercise that arithmetic
without allocating a huge table. Borrowing entry and iterator APIs reject temporary
owning tables, maps and sets; any mutation may invalidate those borrows. Map's
historical mutable-value return from const find remains for compatibility. Named
stack visitors replace map callback lambdas without allocation or type erasure.

## Separate full-table correction

Manual resize can legally make count equal capacity. The original deletion scans
until an empty slot but does not create one before scanning, so a full table has
no terminator. The bounded original one-slot probe prints its removal marker and
times out after two seconds; the candidate completes. This is an ordinary
deterministic algorithm comparison, not an instrumented review result.

Commit 2d067ef creates an actual hole and clears each moved-from slot. The
subsequent ownership conversion is separate from the final optimization. Automatic
growth already leaves an empty terminator, so the optimized ordinary case retains
live logical holes and assigns values directly between distinct live slots.
Only the full-table case needs to construct into an empty slot and destroy the
moved-from value. A non-inlined helper keeps that rare recovery loop out of the
ordinary deletion body; native assembly confirms the separate call.

## Wrapped-distance relocation

For descending probes, a candidate may move into the hole if its probe path from
the native bucket reaches that hole. With power-of-two capacity C, candidate index
i, hole h and stored hash H, compare:

    ((H - i) & (C - 1)) >= ((h - i) & (C - 1))

Unsigned arithmetic supplies the wrapped distances. This replaces three interval
cases with one comparison, without a table or additional state. The hash's low
bits are its native bucket. Distinct candidate/hole indices are established by
advancing the scan; an actual empty slot terminates first. Exhaustively comparing
the old and new predicates across every distinct hole/candidate and native index
for capacities 1, 2, 4, 8, 16, 32 and 64 gives 294,132 identical decisions.
The native inner loop contains subtract/mask pairs and one unsigned comparison.

## Measurements and rejected intermediate forms

M4 macOS 26.5 arm64, non-Apple Homebrew Clang 23.1.2, Release. Seven paired trials
alternate run order. Tables are populated and reserved before timing. Lookup
alternates deterministic hits/misses. Churn removes one key and inserts a new
key. Values contain two integers; hashes are mixed or deliberately constant.
These timings include hashing/probing/relocation, not allocation/growth,
general owned-value performance, multithreaded contention or frame latency.

Final ns per iteration, median and observed min–max:

| Workload | Live entries | Original ns | Candidate ns | Candidate/original |
|---|---:|---:|---:|---:|
| Mixed lookup | 256 | 1.50 (1.49–1.57) | 1.49 (1.49–1.52) | 0.991 |
| Mixed lookup | 4,096 | 1.71 (1.65–1.76) | 1.69 (1.66–1.71) | 0.989 |
| Constant-hash lookup | 64 | 40.01 (39.77–40.90) | 40.42 (40.10–41.02) | 1.010 |
| Mixed churn | 256 | 31.47 (31.24–31.87) | 31.85 (31.58–31.94) | 1.012 |
| Constant-hash churn | 64 | 130.67 (130.03–132.58) | 121.97 (121.22–122.81) | 0.933 |

Lookup and ordinary churn are near parity with overlapping observed ranges;
the ordinary-churn median remains slightly worse. Collision-heavy churn takes
about 7% less time. An earlier run of the final algorithm measured ratios 1.006
and 0.918 for mixed/collision churn. These observations are not a statistical
confidence interval or a universal speed claim. All output digests agree.

Both implementations' table objects are 16 bytes. Slot storage is unchanged:
6,144 bytes for 256 entries, 98,304 for 4,096, and 1,536 for 64. The conversion
adds no persistent per-entry or per-table bookkeeping. Resize temporarily owns
old and replacement allocations, as did the original implementation.

The initial always-clear deletion measured mixed churn 6.1% slower and collision
churn 3.6% slower. Its summary survives in build/hash-storage-gates.log; its raw
timing JSON was overwritten before preservation. Do not treat it as retained raw
data. Specializing the full-table case alone then measured 4.4% and 10.0% slower;
outlining it measured 4.0% and 3.3% slower; direct live-value assignment measured
2.9% slower and approximately parity. Those raw series are preserved in
HASH_STORAGE_PRIOR_MEASUREMENTS.json. The final complete raw series, compiler
and scope are in HASH_STORAGE_MEASUREMENTS.json. Timings compare the combined
candidate to the original, not an isolated causal estimate for each edit.

## Verification and reproduction

Both release profiles pass all 25 tests. Hash fixtures cover hash-zero remapping,
wraparound, constant collisions, insertion permutations through six entries and
each first deletion, complete cleanup, manual full tables of capacities 1–16,
copy/move/self-assignment, visitors/iterators, pointer-valued lookup, reserve and
empty states. Owned payload counters check destruction and preservation through
relocation, including every victim in the full-table fixtures. Compile-time
checks reject borrowing from temporary owners.

Nine render/mipmap artifacts match byte-for-byte. Release and Debug instrumented
suites report PASS; preceding diagnostic files are preserved and detailed logs
remain unread. A fresh relocated installed consumer and source/symbol scope check
pass. Seven selected tests in the pinned isolated GUI.Forms fixture pass under
non-Apple LLVM 22.1.8. The authoritative GUI.Forms checkout is unchanged. Changed
C++ passes spelling and lifetime checks; ownership and probe invariants were
reviewed separately.

Run the standard candidate and baseline commands in AGENTS.md sequentially on
the M4 mirror, followed by `python3 -B tools/measure_hash_storage.py`.
`python3 -B tools/inspect_hash_storage.py` retains native assembly and missed-inline
remarks. The measurement script preserves preceding JSON before replacement.
The final full gate driver is retained locally at build/hash_storage_final_gate.sh;
its ordinary output is build/hash-storage-final-gates.log. It also compares all
nine artifacts, preserves prior instrumented logs, runs both instrumented suites,
installs to a fresh prefix and tests relocation. GUI evidence is
build/hash-storage-gui.log from the standard tools/verify_gui.sh command.

This completes this hash boundary, not the entire library conversion. Native
platform CI for the newly pushed head, broader workloads, the owner's independent
review and production integration remain separate requirements.
