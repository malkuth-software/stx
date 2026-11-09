# Discardable-memory pool — 2026-10-26

This boundary covers SkDiscardableMemoryPool.cpp/.h, the SkDiscardableMemory
interface and SkDiscardableMemory_none.cpp. Original files are preserved
byte-for-byte under baseline/skia; reference.cmake selects their implementations
and the reference include path selects their headers. The default global pool
remains process-lifetime storage with the original 128 MiB budget.

## Ownership, mutation and failure

The caller owns each block object. The block owns its byte allocation and retains
its pool. The pool's intrusive list observes resident blocks; it neither owns nor
deletes their interface objects. Discarding an unlocked block releases its byte
allocation, removes it from the list and adjusts usage. Its owner may retain the
empty object, but subsequent lock attempts fail. A live block keeps the pool alive
after the caller drops its separate pool owner.

Each block requires serialized caller access. Successful creation starts locked;
each successful lock must be followed by unlock before destruction. A data pointer
is borrowed only until unlock/destruction. The pool mutex protects membership,
budget, byte accounting, purge decisions and diagnostic counters. It is not a
license for simultaneous callers to mutate the same block or its bytes.

Both the substantial byte allocation and small control-record allocation happen
outside the mutex. The prepared record is empty and unlocked until publication.
This uses existing fields, not a new registration flag: a null allocation has no
list membership, whether newly prepared or previously discarded. Under the mutex,
admission checks byte addition, transfers the allocation, marks the block locked,
links it and charges usage. Only then may it become visible to its caller.

On admission rejection, the guard ends before the prepared owners are destroyed.
The empty record's destructor can therefore acquire the pool mutex without
recursive locking. No accounting or membership was published. Zero-byte requests
return null. Byte/control allocation failure also returns null; the control uses
nonthrowing allocation. Extreme allocation failure and an actual size_t-total
overflow are not forced in the test suite; the arithmetic guard and cleanup order
were inspected directly. Diagnostic counters saturate at INT_MAX rather than
overflowing; tests cover ordinary counts/reset, not billions of increments.

Eviction remains oldest-unlocked-first. Successful reacquisition promotes a block;
locked bytes can exceed the budget and are purged only after unlock. Reacquiring
the current head now skips redundant unlink/relink operations. Native assembly
confirms the head-equality branch bypasses link writes. This optimization does not
change ordering or add a cache, queue, counter or ownership layer.

## Corrections separated from conversion

Commit dcd3cda synchronizes usage/budget/statistics getters and statistics reset
with the existing mutation mutex, rejects zero-byte creation, and bounds diagnostic
increments. The original getters read state concurrently modified by the pool.
The test therefore never calls those original getters concurrently as an oracle.
Each getter is individually synchronized; separate calls do not form one atomic
combined snapshot. Adding synchronized reads has a measured cost below.

Commit 1d6618c converts ownership/admission and interface spelling separately.
The final prepared-record and head-promotion changes follow that conversion.
The first converted version allocated the small control record under the mutex
after admission validation; the final version prepares it outside, preserving the
checked publication boundary without holding the pool lock during allocation.

## Storage and timing

Native Clang record layouts give identical Release sizes and alignment:

| Implementation object | Original | Candidate | Alignment |
|---|---:|---:|---:|
| DiscardableMemoryPool | 64 bytes | 64 bytes | 8 bytes |
| PoolDiscardableMemory | 56 bytes | 56 bytes | 8 bytes |

No persistent field is added. Layout evidence is DISCARDABLE_POOL_LAYOUTS.json;
these are actual private implementation types, not sizeof the abstract interface.
Debug-only membership/counters are a separate layout; no Debug size claim is made.

Final M4 macOS arm64 Release timings use non-Apple Clang 23.1.2. One unmeasured
run per profile precedes seven alternating paired trials, with one million
operations per workload. The pool budget is 4,096 bytes; two initial 64-byte
blocks are created outside timing. Results below are ns per iteration, median
with observed min–max:

| Workload | Original ns | Candidate ns | Candidate/original |
|---|---:|---:|---:|
| Allocate 64 bytes, unlock, destroy | 29.82 (29.17–30.41) | 29.50 (29.24–34.23) | 0.990 |
| Reacquire/unlock the head | 7.21 (7.09–7.26) | 6.85 (6.80–7.06) | 0.950 |
| Reacquire/unlock alternating blocks | 7.26 (7.15–7.40) | 7.33 (7.19–7.40) | 1.010 |
| Read usage and budget | 1.25 (1.20–1.27) | 6.78 (6.71–6.85) | 5.423 |

Allocation and alternating reacquisition are near parity; the head case takes
about 5% less time in this series. The observation pair is about 5.5 ns more
expensive because it now synchronizes. The original reads are cheaper but do not
provide equivalent concurrent behavior. There is no new mutex object, only
acquisition of the existing one. These are single-threaded administration timings
without purge pressure, not application frame latency or contention scaling.

Earlier 100,000-operation trials without warm-up are retained, including a 13.5%
allocation-path regression in the first full gate. Other short trials measured
that path 1–3% slower. The final series changes both code and measurement duration;
it is not an isolated causal estimate of moving allocation outside the lock.
DISCARDABLE_POOL_PRIOR_MEASUREMENTS.json records all four intermediate series;
DISCARDABLE_POOL_MEASUREMENTS.json contains the final raw timings, digests,
toolchain and workload description. No negative result is discarded.

## Verification

All 27 tests pass in both Release profiles. The pool fixture checks zero-budget
locked admission, zero-byte rejection, exact usage, oldest-unlocked eviction,
promotion, retained contents on successful relock, permanent failed relock after
purging, dump with locked survivors, final-unlock purging, diagnostic counts/reset,
and a block outliving its caller's pool owner. A scoped test owner balances lock
state even on failure returns.

Four workers perform 4,000 block lifecycles while another thread changes budgets
and dumps the pool 3,000 times. The candidate additionally observes usage/budget
and diagnostics concurrently. Surviving contents must match, final usage must be
zero, and Debug hit/miss counts total 4,000. This exercises outcomes and validates
the access protocol; it is not exhaustive schedule exploration or a race-detector
claim.

Nine render/mipmap artifacts remain byte-identical. Release and Debug instrumented
suites report PASS; prior diagnostics are preserved and detailed logs remain unread.
A fresh relocated consumer, source/symbol scope checks and seven selected pinned
GUI.Forms fixture tests pass. GUI verification uses non-Apple LLVM 22.1.8; the
authoritative GUI.Forms checkout is unchanged. Five changed C++ files pass spelling
and lifetime checks, with separate review of initialization, rejection cleanup,
publication, lock ordering and eviction.

After the standard Release builds from AGENTS.md, run
`python3 -B tools/measure_discardable_pool.py` and
`python3 -B tools/inspect_discardable_pool.py` sequentially on the M4 mirror.
The latter retains native assembly and full record layouts. The complete local
driver is build/discardable_pool_final_gate.sh; ordinary evidence is in
build/discardable-pool-final-gates.log and build/discardable-pool-gui.log. The driver
also preserves preceding instrumented logs and uses fresh installation paths.

The preceding list-conversion commit 625a459 passed Linux x64/arm64 and Windows CI.
CI for this new pool conversion is a separate gate. Full library conversion,
broader application measurements, independent review and production integration
remain outstanding.
