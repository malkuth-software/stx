# Synchronization primitives — 2026-10-26

This conversion covers SkSemaphore.h/.cpp, SkMutex.h, SkOnce.h and
SkThreadID.h/.cpp. Their original versions remain byte-for-byte in baseline/skia,
with original implementations selected by reference.cmake. The atomic algorithm
and memory orders remain: acquire permit claims, release permit publication,
acquire once observations, relaxed once claim, and release once completion.
try_wait retains acquire ordering on both successful and failed weak CAS.

## Retained mechanism and lifetime

The mutex remains a non-recursive semaphore with one initial permit. Uncontended
operations use its atomic counter. Contention lazily creates one owned OS semaphore
through SkOnce; a named initialization function replaces the captured lambda.
The opaque raw handle owner remains at this OS boundary to retain constexpr
construction in C++20 without adding a wrapper allocation or requiring C++23
constexpr unique_ptr. Destruction requires all callers, including lazy
initialization, to have finished; the owner then deletes the OS object.

A negative count records unsatisfied wait claims. Signaling releases up to n
waiters even when the resulting count remains negative; the old comments wrongly
described only crossing zero. Waking a waiter does not promise FIFO ordering.
try_wait performs one weak CAS, may fail spuriously or because of contention,
and consumes nothing on failure. It does not spin or block. Initial permits and
signal counts are nonnegative; callers keep permit and waiter totals within int
bounds. Debug assertions express these internal preconditions without adding a
Release validation loop. Negation of a negative counter is widened before computing
the wake count, so the expression itself cannot overflow at minimum int.

SkOnce borrows its named target and arguments for a synchronous call. Targets must
not throw or reenter the same flag. The winner publishes completion; other callers
retain the existing acquire spin until completion. No new queue, backoff policy,
worker pool, callback owner or reference counter is introduced. Mutex guards borrow
their mutex and remain noncopyable/nonmovable. Debug ownership inspection requires
the current owner or quiescent destruction, not arbitrary concurrent inspection.

POSIX thread identity now copies the admitted native pthread representation into
zero-initialized integer storage and bit-casts it to SkThreadID. This avoids the
old pointer-to-signed cast on macOS. A size assertion bounds the representation.
Windows uses the declared DWORD result and explicit conversion. These are live
thread identities for debugging, not permanent identities across thread reuse.

## OS boundary behavior

The platform mechanisms are unchanged: dispatch semaphores on macOS, Win32
semaphores on Windows, and unnamed POSIX semaphores on Linux. API results now use
named variables and declared native result types. Creation, wait, signaling and
destruction failures are checked where those APIs report failure. POSIX wait
retries EINTR; unexpected failures stop with SK_ABORT. The void synchronization
interface cannot safely report apparent success after losing a permit or failing
to acquire a lock. Dispatch signal's nonzero result denotes a woken waiter and is
not treated as an error. The infinite dispatch wait must succeed.

These error checks change exceptional behavior from ignoring OS failures to
stopping explicitly. Normal-operation tests do not inject resource exhaustion or
invalid native handles. macOS executes its native branch here; Linux and Windows
execution of the newly pushed source belongs to the corresponding CI jobs.

## Remove the unused reader/writer lock

SkSharedMutex.cpp/.h have no retained CPU users. SkFlattenable.cpp included the
header but did not use it; only that include is removed from this otherwise
unconverted file. The original reader/writer implementation moves to baseline/skia.
The candidate source list excludes it and the archive scope checker rejects its
symbols. This is a separate pruning commit, 0413528. It removes an unnecessary
compiled implementation rather than replacing the actual cache mutex mechanism.
No reader/writer speed or application latency improvement is claimed.

## Native fast-path evidence

Release sizeof values match the reference on M4 arm64: semaphore 16 bytes, mutex
16 bytes, once flag 1 byte. There are no added persistent fields. The three small
native probes in tools/inspect_synchronization.py produce byte-for-byte identical
assembly for semaphore wait/signal, mutex scope, and a completed once call. The
shared assembly is retained as SYNCHRONIZATION_PROBES.s. This proves equivalence
of those compiled probes, not every instantiation or a timing guarantee.

M4 macOS arm64, non-Apple Clang 23.1.2, Release. One unmeasured run per profile,
then seven alternating paired trials of one million uncontended operations.
Times are ns per operation, median with observed min–max:

| Workload | Original ns | Candidate ns | Candidate/original |
|---|---:|---:|---:|
| Semaphore wait/signal | 3.24 (3.17–3.60) | 3.39 (3.17–3.96) | 1.048 |
| Mutex scope | 3.22 (3.17–3.72) | 3.37 (3.17–3.68) | 1.046 |
| Completed once call | 0.93 (0.91–1.07) | 0.96 (0.91–0.99) | 1.033 |

The candidate medians are slightly worse, with overlapping ranges. Those results
remain in SYNCHRONIZATION_MEASUREMENTS.json; identical probe assembly is not used
to erase the measured difference. Scheduling, frequency and code-layout effects
are possible explanations, not isolated findings. No improvement is claimed.
These workloads do not measure blocking/wakeup latency, fairness, contention
scaling or renderer throughput.

## Verification

The synchronization fixture checks empty and batch permits, maximum positive
count boundaries, bounded retry of weak try_wait, 2,000 payload-publication
round trips, 8,000 claims served by mixed batch sizes, 80,000 mutex-protected
updates across eight threads, publication through once with exactly one target
invocation, and distinct identities of eight simultaneously live workers.
Payload/counter consistency is checked through the synchronization being tested.
Workers are joined before owners are destroyed; publication tests continue their
handshake even if a value check fails, avoiding an unrelated test shutdown hang.

These are deterministic workloads with scheduler-dependent interleaving, not an
exhaustive proof of concurrency or a guarantee of a particular number of OS sleeps.
The explicit contracts and unchanged memory orders were reviewed separately.

All 28 tests pass in both Release profiles. All nine render/mipmap artifacts remain
byte-identical. Release and Debug instrumented suites report PASS; preceding
diagnostics are preserved and detailed logs remain unread. Fresh relocated
installation, candidate source/symbol scope checks and seven selected pinned
GUI.Forms fixture tests pass. GUI verification uses non-Apple LLVM 22.1.8; the
authoritative GUI.Forms checkout is unchanged. The six converted implementation
files/headers and new fixture pass spelling and lifetime checks. SkFlattenable.cpp
is an include-only adaptation and is not claimed as converted.

After the standard Release builds, run tools/measure_synchronization.py and
tools/inspect_synchronization.py sequentially on the M4 mirror. The local complete
gate driver is build/synchronization_gate.sh; ordinary logs are
build/synchronization-gates.log and build/synchronization-gui.log. Native probe
output is under build/synchronization-{baseline,candidate}.s.

The preceding pool head 72b4eaf passed Linux x64/arm64 and Windows CI. Current-head
platform jobs, full library conversion, broader measurements, independent review
and production integration remain separate requirements.
