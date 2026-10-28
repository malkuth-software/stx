# Owned cache invalidation — 2026-10-26

The retained compile closure uses the generic SkMessageBus only for broadcast
resource-cache shared-ID invalidation. Its routing identity, arbitrary message
type, reference-counted transfer and move-only delivery branches have no retained
consumer. The original header moves byte-for-byte to baseline/skia, where original
resource caches and reference tests continue to use it. The candidate has a
small first-party SkResourceCacheInvalidationInbox implementation instead.

## Lifetime and synchronization

A process-lifetime registry observes live inboxes. Each inbox owns its pending
IDs. Constructor registration follows member initialization. Destruction removes
the observer under the registry mutex before destroying the inbox's storage.
Posting holds the registry mutex while delivering under each inbox mutex, so an
inbox cannot disappear during delivery. Polling takes only the inbox mutex; its
owner must finish its own poll calls before destroying it. No client callback
runs in the registry. Lock ordering and process-shutdown lifetime match the
existing broadcast bus rather than introducing a new ownership graph.

Posts have one serialized order. Every registered inbox receives each nonzero
ID once, duplicates remain distinct, and new inboxes receive no replay. Registry
removal may swap with the last entry; recipient enumeration order is not an API
guarantee. No new reference counter, task scheduler, per-pixel state, subscription
token system or atomic queue is introduced.

The resource cache retains a drain vector. Poll swaps the pending and drain
allocations, then record purging runs outside the inbox lock under the cache's
existing access authority. The prior implementation created a fresh zero-capacity
TArray at every check, transferred pending storage to it and freed that storage
on return. The two owned buffers now retain capacity for later batches.

## Administration cost and memory trade

On M4 macOS arm64 Release, sizeof(SkResourceCache) is 136 bytes versus the pinned
reference's 112 bytes: **24 more fixed bytes per cache**. In addition, both buffers
retain their peak capacities until cache destruction. Empty caches allocate no
message storage. A large burst can therefore leave more idle retained capacity
than the original temporary-buffer path; this is not a memory-reduction claim.
No shrinking policy or extra bookkeeping was added to conceal that tradeoff.

The test checks two distinct allocation addresses alternating across twelve
64-ID batches after warm-up, showing storage reuse. The timing trial includes
posting, locking, allocation/storage, draining and missing-key lookup against
empty local caches. It excludes construction, record destruction and rendering.
Each iteration posts the listed number of IDs, then performs one lookup/drain per
cache. Seven paired trials alternate run order; each case warms for 20 iterations.
The reference is the pinned original resource cache/message bus, so timings compare
the combined implementation, not a causally isolated buffer-reuse change.

Times below are ns per whole iteration, median with observed min–max:

| Caches | Posted IDs per iteration | Reference ns | Candidate ns | Candidate/reference |
|---:|---:|---:|---:|---:|
| 1 | 0 | 3.94 (3.85–4.09) | 3.96 (3.89–4.36) | 1.007 |
| 1 | 1 | 26.49 (23.47–27.86) | 8.14 (7.31–8.70) | 0.307 |
| 1 | 16 | 115.05 (114.09–117.51) | 70.26 (68.57–74.39) | 0.611 |
| 1 | 256 | 1582.79 (1568.04–1723.54) | 1183.08 (1109.96–1298.00) | 0.747 |
| 4 | 1 | 100.95 (89.98–124.44) | 23.00 (21.86–23.26) | 0.228 |
| 4 | 16 | 407.40 (389.00–503.82) | 169.02 (151.60–176.78) | 0.415 |

Empty checks remain within observed spread. The measured nonempty cases reduce
administration time. These are single-producer microbenchmarks, not contention
scaling, application frame latency, overall rendering speed or global optimality.
Raw measurements and toolchain are in CACHE_INVALIDATION_MEASUREMENTS.json;
tools/measure_cache_invalidation.py reproduces the trial after release builds.

## Verification

The new contract suite checks empty replacement, late registration without
history replay, duplicate preservation, zero-ID rejection, pending inbox
destruction and continued delivery, noncopyable/nonmovable identity, and buffer
reuse. Four concurrent producers post 4,000 IDs while another thread constructs,
polls and destroys 300 transient inboxes. Two persistent inboxes must receive
identical order; each producer's sequence must appear exactly once in order.
This exercises concurrency outcomes without claiming exhaustive schedule or
race-detector coverage. The original bus runs the common semantic cases through
a test adapter; candidate-only assertions check the new storage policy.

All 24 tests pass in candidate/reference release builds on M4 macOS arm64 with
non-Apple Clang 23.1.2. All nine rendering/mipmap artifacts match byte-for-byte.
Release and Debug instrumented suites report PASS; preceding diagnostics are
preserved and detailed files remain unread. The relocated installed consumer
passes and rejects the removed generic message-bus header. Build-scope checks
pass. Seven selected tests in the pinned isolated GUI.Forms fixture pass with
non-Apple LLVM 22.1.8. The authoritative GUI.Forms checkout is unchanged.

Seven changed C++ files pass spelling and lifetime checks. Semantic review covers
registry borrowing, destructor order, swapped allocation ownership and lock order;
syntax checks alone are not evidence for those properties. Full conversion,
current-head native platform CI and independent review remain separate gates.

Local records: build/cache-invalidation-candidate.log,
build/cache-invalidation-gates.log, build/cache-invalidation-gui.log and
build/cache-invalidation-measurements.json. The sequential driver is
build/cache_invalidation_gate.sh.
