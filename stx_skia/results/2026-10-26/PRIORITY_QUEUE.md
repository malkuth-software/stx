# CPU priority queue: reduced surface and one-hole sifting

The retained CPU consumer of SkTDPQueue is SkIsSimplePolygon in SkPolyUtils.cpp.
It inserts vertices and repeatedly peeks/pops them for its sweep-line test. The
indexed removal and mutable-priority facilities serve excluded GPU resource
caches. CPU code uses neither sorting nor random access. Those unused methods,
the index callback and the sort dependency are removed in a separate pruning
commit. Their removal is maintenance reduction, not a claimed runtime gain:
uninstantiated template code was already absent from the CPU binary.

The complete original header is preserved under baseline/skia/src/core at the
pinned Skia revision. Relevant original test sources remain available; the new
standalone fixture tests the retained CPU contract. This change does not convert
all of SkPolyUtils.cpp or its active-edge tree.

## Converted algorithm

The queue remains a contiguous binary min-heap using SkTDArray, with one 24-byte
owner in native Release. There is no new persistent metadata, allocation layer,
lock, membership registry or index tracking. A priority comparator only observes
its operands and must provide a strict weak ordering. Peek is a const borrow,
valid until mutation/destruction, and rejects temporary owners. Moving empties the
source through the converted array owner. Pointer-valued elements remain non-owning.

Insertion holds the new entry locally and slides ancestors down until finding its
position. Pop holds the last entry locally and slides the lesser child upward
until finding the replacement's position. Each traversed heap level moves one
entry instead of swapping two; the displaced entry is written once at the end.
Equal children choose the left child and equal parent/child priorities stop,
preserving the original algorithm's tie decisions. The heap is not stable.

The pop loop tests `index < count / 2` before forming a child index. This proves
the child arithmetic is in range even at INT_MAX capacity. The loop accesses one
borrowed contiguous buffer, whose extent is established before traversal, instead
of repeatedly invoking bounds-checked array indexing. Empty pop stops explicitly.
Debug builds validate the full heap at operation boundaries, rather than after
every internal step while an entry is displaced.

## Verification

- Both Release profiles pass all 30 tests. The queue fixture checks 17,742
  permutation/duplicate-priority cases, 64 mixed insert/pop schedules, and 193
  polygon cases. Output identities and payloads hash identically to the original:
  247827689137739466. Candidate-only cases check move/reuse and temporary borrows.
- Polygon fixtures rotate and reverse a concave stair outline, change its starting
  vertex and scale, and reject a crossing outline. The existing 48 triangulation
  fixtures remain part of the broader suite. These finite cases are not universal
  geometric equivalence proof.
- Nine render/mipmap artifacts match byte-for-byte.
- Release and Debug instrumented deterministic suites: PASS. Prior logs were
  preserved; detailed diagnostic output remains for the owner's reviewer.
- Fresh relocated installed consumer, CPU compile/symbol scope, and seven selected
  GUI.Forms integration tests: PASS. The authoritative GUI.Forms checkout is unchanged.
- Converted header/test have zero house-style and lifetime-check findings.

The preceding array head 6ca1845 passed native Linux x64, Linux arm64 and Windows
CI. The queue batch must obtain its own current-head platform results after push.

## Measurements and attribution

M4 macOS arm64, Homebrew Clang 23.1.2, Release. One warmup per executable, then
seven alternating paired trials. Each workload inserts/drains 262,144 entries in
reserved queues of 32, 256 or 2,048 sixteen-byte records. Output hashing is included.
The polygon cases run SkIsSimplePolygon over simple comb outlines, with 262,144
input vertices processed per trial; allocation and the active-edge tree are part
of that measured consumer work. Builds and timing were sequential.

The first two series compare the retained original profile against the full
candidate. They are retained as PRIORITY_QUEUE_PRIOR_MEASUREMENTS.json and
PRIORITY_QUEUE_MEASUREMENTS.json. Small-case measurements vary: the first series
has a 10% equal-32 regression, which reverses in the later series. No observation
is discarded. These broad comparisons alone cannot attribute a consumer gain to
this queue because other converted headers also differ.

PRIORITY_QUEUE_CONTROLLED.json provides the attribution-controlled comparison.
`tools/build_priority_queue_comparison.py` recompiles the current polygon source
and current test using only the original queue header as an include override.
It links the current candidate library and uses the current array implementation.
The complete fixture output matches. Thus the original queue versus the converted
queue is the implementation difference, rather than earlier array/list changes.

| Workload | Old queue median (range), ns/input | New queue median (range), ns/input | New/old |
|---|---:|---:|---:|
| Ascending 32 | 9.47 (8.69–9.91) | 9.72 (9.53–10.91) | 1.026 |
| Descending 32 | 11.61 (10.80–12.06) | 10.21 (9.99–11.14) | 0.879 |
| Equal 32 | 4.15 (4.07–4.30) | 3.81 (3.14–4.47) | 0.920 |
| Permuted 32 | 10.61 (10.25–10.83) | 10.37 (10.10–11.34) | 0.978 |
| Ascending 256 | 12.47 (11.85–12.91) | 12.61 (12.44–13.67) | 1.012 |
| Descending 256 | 14.37 (13.81–14.86) | 13.19 (13.05–14.35) | 0.918 |
| Equal 256 | 4.24 (4.13–4.36) | 4.14 (4.01–4.51) | 0.976 |
| Permuted 256 | 12.86 (12.41–13.25) | 12.45 (12.27–13.27) | 0.968 |
| Ascending 2,048 | 16.87 (16.55–17.82) | 15.87 (15.57–16.40) | 0.940 |
| Descending 2,048 | 19.18 (18.62–19.44) | 16.00 (15.58–16.78) | 0.834 |
| Equal 2,048 | 4.22 (4.14–4.38) | 4.06 (3.90–4.23) | 0.962 |
| Permuted 2,048 | 20.00 (19.64–20.74) | 16.14 (15.12–17.14) | 0.807 |
| Comb polygon 32 | 17.29 (17.17–18.75) | 15.55 (14.92–16.00) | 0.899 |
| Comb polygon 256 | 38.80 (38.16–41.57) | 18.24 (17.66–19.09) | 0.470 |
| Comb polygon 2,048 | 47.18 (45.59–48.51) | 23.50 (23.30–23.92) | 0.498 |

Small ascending queues remain slightly slower. Larger descending/permuted queues
and these comb polygons improve. This is evidence for those workloads, not a
whole-renderer speed claim or a guarantee for all geometries, comparators or CPUs.

`tools/inspect_priority_queue.py` retains native insert/pop probes in
PRIORITY_QUEUE_BASELINE.s and PRIORITY_QUEUE_CANDIDATE.s. Candidate traversed
levels use one 16-byte load/store pair. The original has repeated indexed-access
checks and swap traffic. The candidate pop probe still spills part of the held
entry to the stack: do not claim all state is register-resident. There are no
platform-specific intrinsics, and these native probes do not prove x64 performance.

Reproduce standalone verification with tools/build_native.sh for candidate,
baseline and candidate-sanitize, preserving any existing integrity.log first.
Then run tools/build_priority_queue_comparison.py followed by:

```
python3 -B tools/measure_priority_queue.py --reference-profile priority-queue-prior --output build/priority_queue-controlled.json
```

Run tools/verify_gui.sh with the pinned fixture/toolchain and tools/inspect_priority_queue.py
with the native Release compile database. Build/mirror synchronization and timings
must remain sequential. Existing measurement output is archived before replacement.
