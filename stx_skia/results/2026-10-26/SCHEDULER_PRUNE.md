# Remove the unused Skia task scheduler — 2026-10-26

SkExecutor.cpp/.h and SkTaskGroup.cpp/.h move to the reference tree, byte-for-byte
at the original import. The two translation units are removed from the candidate
CMake list and retained in the original-reference build. The installed candidate
no longer exposes their headers.

## Dependency evidence and threading direction

A scan of the candidate source inputs finds no remaining SkExecutor/SkTaskGroup
references. The retained CPU source directories have no callers outside the
removed implementations. GUI.Forms' renderer, public headers and tests have no
calls to those APIs. Historical references in excluded codecs, PDF/GPU sources,
upstream tests and upstream build metadata are not candidate consumers.

The candidate recompiles and links all standalone tests after removal, links a
fresh relocated installed consumer, and passes the pinned GUI.Forms renderer
fixture. The build-scope checker rejects the two translation units and their
SkExecutor/SkTaskGroup/SkThreadPool definitions in the candidate archive. An
installed-consumer preprocessing check rejects the removed header paths.

The removed pool stores callable jobs in mutex-protected queues and signals work
per submission. Task-group batches submit one callable and perform one completion
update per item. No retained raster call was dispatching work through it, so this
is a maintenance/build/API reduction, not a measured paint-speed improvement.

The owner's GUI.Forms threadpool_atomic_fast remains the reference for worker
execution: publish a batch, claim chunks atomically, accumulate completion per
worker, reuse caller storage and execute small work directly. No new dependency
on GUI.Forms, duplicated pool, parallel raster implementation or scheduling ABI
is introduced here. Cache synchronization remains necessary and uses Skia's
atomic-backed mutex; the resource-cache record explains the distinction.

## Verification

M4 macOS arm64, non-Apple Clang 23.1.2: 23 tests pass in both candidate/reference
release builds; all nine rendering/mipmap artifacts match; Release and Debug
instrumented suites report PASS with diagnostic files left unread. The fresh
relocated installed consumer and build-scope source/symbol checks pass.

Seven selected tests pass in the pinned GUI.Forms fixture with non-Apple LLVM
22.1.8. Its authoritative source checkout is unchanged. The changed installed
consumer passes spelling and lifetime checks. No timing or memory-speedup claim
is made; platform CI and independent review remain separate gates.

Local logs: build/scheduler-prune-gates.log and build/scheduler-prune-gui.log.
The sequential driver is build/scheduler_prune_gate.sh. Prior instrumented logs
are preserved separately before both new runs.
