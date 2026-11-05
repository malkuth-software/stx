# Resource cache ownership and synchronization — 2026-10-26

SkResourceCache.cpp/.h and SkSynchronizedResourceCache.cpp/.h are converted.
Four original files are preserved byte-for-byte at the import pin for the
reference build. The hash table and message-bus implementations remain imported
and are separate conversion work; this record does not claim them converted.

## Ownership and behavior

The cache owns records through its intrusive LRU list; the hash and neighboring
record links only observe. Pending insertion uses a unique owner until admission.
The hash itself has a unique owner. Cache and record copies are deleted. Key,
byte-charge, callback, destruction and concurrent-access contracts are explicit.
The process-global cache deliberately remains alive through process shutdown so
late pixel/ref destruction can post invalidation messages.

Key hashing keeps the original layout and byte sequence. Equality checks count
and hash before comparing initialized object bytes, avoiding uint32_t reads of
pointer fields. Key initialization checks alignment, signed word-count capacity
and byte-extent multiplication before hashing. These are internal key-construction
preconditions; violations terminate rather than creating a partial key.

Insertion checks total charge/count capacity before replacing an unpinned record.
Overflow rejects and destroys the incoming owner without installing it or deleting
the prior entry. Null insertion is ignored. Pinned duplicates still receive the
installation callback and defeat incoming duplicates. Installation precedes budget
purging so it may pin a newly admitted record. Stale visitor results still remove
the entry. All callbacks run synchronously and may not reenter the same cache.

The historical eviction policy is retained: equality with the byte/count limit
triggers purging, and pinned records may keep the cache above budget. Discardable
mode retains its count limit and UINT32_MAX byte threshold; its stored byte-limit
getter/setter and accounting are not falsely documented as always zero/ignored.
The single-allocation limit remains guidance for callers, not an admission test.
Inert transaction tracing and optional hit-rate logging are removed; explicit
memory-accounting visitors remain available to the existing Graphics interface.

## Synchronization correction

Previously the static CheckMessages entry reached the base's private helper
without taking the synchronized cache mutex. Direct purgeSharedID inherited the
unlocked base implementation. The synchronized wrapper now locks both external
entries. Base operations that already hold access authority call the nonvirtual
message helper, which uses qualified base purge dispatch to avoid locking twice.
No change is made to the mutex primitive or cache eviction algorithm.

Four workers each perform 500 independent add/find operations in a local
synchronized cache, then another 500 each through global entry points. Candidate
checks additionally mix direct group purge (including dispatch through a base
reference) and posted-message draining. Installation/destruction totals must both
be 4,000. The original build skips concurrency through the previously unlocked
purge entries. These tests establish exercised outcomes, not a proof of all
thread schedules; no race-detector result or throughput gain is claimed.

## Deterministic verification

Additional fixtures check keys with differing payloads, namespace, group and
extent; exact LRU order; strict-limit eviction; misses preserving context; stale
removal; pinned duplicate installation; replacement; explicit/message group purge;
malloc cached-data acquisition; failed discardable allocation; the 1,024-record
threshold; single-allocation guidance; and candidate-only accounting overflow.
CTest bounds the resource-cache suite to 30 seconds.

On M4 macOS arm64 with non-Apple Clang 23.1.2, all 23 tests pass in both release
profiles. All nine rendering/mipmap artifacts match. Release and Debug
instrumented suites report PASS, preserving old logs and leaving detailed files
unread. A fresh relocated installed consumer passes. Seven selected GUI.Forms
tests pass with the pinned isolated fixture and non-Apple LLVM 22.1.8. The
five source/header/test files pass spelling and lifetime checks. The authoritative
GUI.Forms checkout is unchanged.

Local logs: build/resource-cache-candidate.log (test key-name collision),
build/resource-cache-candidate-2.log, build/resource-cache-gates.log and
build/resource-cache-gui.log. The sequential driver is
build/resource_cache_gate.sh. Current-head platform CI and independent review
remain separate gates.

## Owner's atomic threading reference

Read-only inspection of GUI.Forms' include/gui_forms/threading.hpp and
src/core/threading/atomic_pool/atomic_pool.c confirms the threadpool_atomic_fast
reference: batch publication with release/acquire ordering, atomic chunk claims,
local completion accumulation, reusable caller task storage, sleeping/waking
workers, and synchronous execution for small batches. It uses mutexes and
condition variables at batch/sleep boundaries; it is not a general lock-free
replacement for shared object graphs.

Skia's existing SkMutex uses SkSemaphore, whose uncontended path uses atomic
acquire/release counter operations and whose contended path sleeps/wakes through
an OS semaphore. That primitive is separate from the hash/list/cache ownership
protocol. Merely making cache counters atomic would not synchronize admission,
recency mutation, callback access or destruction. Keep a coordinated cache
boundary while evaluating independent raster batches against the owner's pool.
No claim of universal optimality follows from either source inspection.
