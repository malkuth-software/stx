# Pixel extents and temporary ownership — 2026-10-25

This batch converts temporary pixmap allocation, checked size arithmetic, clipped
read/write records and image metadata. It retains the original implementations
and headers in the reference profile. It makes no rendering-speed claim.

## Behavior

Valid arithmetic and transfer behavior stays compatible. Intentional corrections
are occupied-destination moves, self-move, moving borrowed views, and completely
empty state after failed allocation. Metadata may alias the destination during
reallocation. Invalid dimensions and enum values are rejected before allocation.
The alpha-type validator rejects an unknown color enum without changing output.

The common tests run against both profiles. Tests for corrected behavior run only
against the candidate, so known incorrect reference operations are not executed
in an instrumented comparison. Borrowed storage outlives its views; detached
allocations are released explicitly.

## Verification

Host: M4 Mac mini selected by m4host. Standalone: non-Apple Clang 23, C++20,
Release and Debug. GUI.Forms fixture: pinned 8d1f2be0 with LLVM 22.1.8.

- Baseline and candidate: six deterministic tests each passed.
- All 24 rendered cases match byte for byte; arm64 hash 11616627833029224064.
- Complete standalone Release instrumented suite: PASS.
- Complete standalone Debug instrumented suite: PASS.
- Seven GUI.Forms rendering/integration tests: passed.
- Fresh relocated installed consumer: built and ran, including its own RTTI use.
- Eleven converted/test files: zero spelling and lifetime-checker findings;
  manual review remains part of the conversion.

Arithmetic checks cover 194 unsigned boundary values in a pairwise matrix,
signed limits, alignment and sticky failure. Image checks cover every color type,
small/empty dimensions, padded rows, color-space factories and the 2 GiB bound.
Clipping checks compare an independently calculated intersection and unchanged
failure state. Storage tests exercise allocation, detach, callback retention and
move/failure transitions.

Reproduction uses tools/build_native.sh baseline, candidate and candidate-sanitize;
cmake -E compare_files compares their render.raw outputs. The existing Debug
instrumented tree is rebuilt with the same source and executes the complete
suite. tools/verify_gui.sh supplies the isolated consumer gate. Detailed memory
integrity diagnostics are retained for the owner's reviewer and were not read.

Logs in the local build directory: pixel-extents-final.log,
pixel-extents-gui-final.log and pixel-extents-installed.log. They are generated
artifacts, not committed binaries or dependencies.

The preceding reference/view commit 4cd3295 also passed native GitHub Actions on
Linux x64, Linux arm64 and Windows (run 40260304887). That result does not claim
cross-platform verification for this newer batch.
