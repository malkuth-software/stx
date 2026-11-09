# Bitmap and mipmap cache ownership — 2026-10-26

SkBitmapCache.cpp/.h are converted to explicit ownership, lock scopes, immutable
identity and named cache dispatch. The original implementation was already
isolated in the reference build; the original header is now preserved there too.

## Behavior and ownership

A pending bitmap record owns either a malloc-compatible block or discardable
storage. A named unique owner replaces manual malloc release. Record metadata and
pixel identity are immutable; reference/lock state is explicitly mutable under
the record mutex. The finder no longer casts away constness to perform ordinary
cache-state mutation. The remaining const cast supplies the synchronous callback's
untyped context; that callback only edits declared mutable state.

Installation now reserves an external pin while holding the record mutex and
then releases it before installing a temporary bitmap. The returned pixel ref
owns the pin. Failed installation invokes the same release callback to balance
it. Successful publication swaps the temporary with the destination outside the
mutex. Destruction of the destination's former pixel ref can therefore reenter
the record callback. In particular, Find(desc, &bitmap) can replace a bitmap
already using that record. The original implementation held the mutex through
that replacement and recursively reacquired it in ReleaseProc.

Allocation validates description/metadata agreement, formats and byte extent
before acquiring storage. Failure leaves the caller's pixmap unchanged. Callers
fill pending pixels before Add and may not mutate them after publication.
Duplicate admission remains unchanged: a pinned entry wins over a new duplicate;
an unpinned entry may be replaced. Bitmap cache records remain unpurgeable while
external pixel refs exist. Mipmap records use the separate cache-reference
protocol, so removing a record does not invalidate live client references.

The local/global dispatch macro is removed. Mipmap lookup uses a scoped client
owner: reacquisition attempts to lock data, lost data returns null, and failed
lookup balances its reference before the resource cache removes the stale record.
Mipmap record copying is disallowed to prevent duplicating a cache reference
without acquiring another one.

No pixel arithmetic, eviction policy, key composition or cache admission
algorithm is changed. No timing improvement is claimed.

## Deterministic checks

The new bitmap-cache contract test covers cache allocation/publication, immutable
pixel identity, pinned duplicate admission, replacement of unpinned records,
lookup misses preserving output, purge retention while pinned, and stale-ID
removal. Candidate-only cases perform ten same-entry replacements into an
occupied output and reject malformed allocation metadata without changing its
pixmap. The baseline does not execute the recursive-lock case. CTest bounds this
test to 20 seconds so a locking regression cannot wait indefinitely.

Local and global mipmap cases verify that removing cache ownership leaves clients
readable. A local discardable factory physically frees its allocation on unlock;
a later lookup must fail, balance the client reference, and destroy the stale
cache entry. The global bitmap discardable factory is platform-selected, so the
ordinary bitmap tests on this host exercise malloc storage; they do not claim
forced coverage of that global factory configuration or allocator exhaustion.

The initial candidate and first full gates pass all 21 tests. The final gates,
after strengthening the lost-storage fixture to release its block, also pass all
21 tests in both candidate/reference release builds and compare all eight
mip/render artifacts byte-for-byte. Release/Debug instrumented suites report
PASS. The fresh relocated installed consumer passes. These checks use M4 macOS
arm64 with non-Apple Clang 23.1.2. Seven selected tests in the pinned isolated
GUI.Forms fixture pass with non-Apple LLVM 22.1.8. Its authoritative checkout is
unchanged. The three converted source/header/test files pass spelling and
lifetime checks without findings; both original cache files match the import.

Local records: build/bitmap-cache-candidate.log, build/bitmap-cache-gates.log,
build/bitmap-cache-final.log and build/bitmap-cache-gui.log. Sequential drivers
are retained as build/bitmap_cache_gate.sh and build/bitmap_cache_final.sh.
Instrumented runs expose only PASS/STOP, preserve preceding integrity logs and
leave detailed diagnostics unread. Native current-commit platform gates,
independent review and full conversion remain separate requirements.

The optional drawing-based mipmap alternative remains unconverted. Its build
macro is inactive in the current package; that observation alone is not a claim
that its alternate algorithm has been converted or verified.
