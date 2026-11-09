# Cached mask and pixel storage — 2026-10-25

This batch converts SkCachedData.cpp/.h and adapts its sole production notification
override in SkMipmap.h. Original versions of all three files remain in baseline/skia.
The rest of SkMipmap remains unconverted.

## Ownership and synchronization

Construction adopts either a malloc-compatible allocation or an already locked
SkDiscardableMemory object. Storage kind, address owner and extent are immutable.
Reference count, cache membership, lock state and the published data pointer are
explicitly mutable. This supports the existing const ref/unref interface without
casting away the object's const qualification.

All reference transitions run under the object's mutex. Releasing the last client
while the cache remains unlocks discardable storage and clears the published
pointer. Acquiring a client from that state attempts to lock again. Failure leaves
the pointer null; it does not call the underlying unlock later for a lock that
never succeeded. Final object deletion occurs after the mutex scope ends.

The count includes one cache reference when attached; it is not a tagged low bit.
Invalid attachment/release or count overflow now fails explicitly. These are
internal protocol errors, not alternative successful states.

A data-change callback runs before the new pointer is published and under the
mutex. It may update its borrowed views but cannot reenter reference/cache methods
or throw. SkMipmap's level pointer follows that contract through a const callback
and explicitly mutable borrow. Diagnostic getters require externally quiescent
reference transitions. A live client reference stabilizes the data address;
writable pixel access still requires external exclusive ownership.

## Verification

M4 Mac mini selected by m4host; non-Apple Clang 23.1.2, C++20. No timing speedup
is claimed: this is an ownership and state-machine conversion.

- Ten release tests pass for both original and candidate profiles.
- New contract tests cover malloc/discardable storage, cache-last and client-last
  destruction, successful reacquisition, purge failure and repeated failed locks.
- Notifications are checked against the old still-published pointer, and the
  underlying lock/unlock/destructor counts are checked independently.
- Four threads perform 80,000 reference pairs while a root client retains stable
  immutable data. This exercises concurrent sharing, not concurrent pixel writes
  or unsupported cache-only reads.
- A candidate-only const heap instance checks the mutable-state contract.
- All existing render, conversion, pixmap and mask bytes match the reference.
- Whole Release and Debug instrumented suites: PASS. Detailed diagnostics remain
  unread; the prior files were preserved before running.
- Seven selected GUI.Forms rendering/integration tests pass with LLVM 22.1.8.
- Fresh relocated installed consumer builds and runs with consumer RTTI preserved.
- Converted implementation/header and new test pass spelling/lifetime checks.

The original upstream CachedDataTest.cpp remains available. The new standalone
fixture extends its state cases and does not claim to execute the entire upstream
suite. There is no broad cache performance or concurrent mutation claim.

Reproduce with tools/build_native.sh baseline/candidate/candidate-sanitize.
Generated logs are build/cached-data-release.log, cached-data-instrumented.log,
cached-data-gui.log and cached-data-installed.log.
