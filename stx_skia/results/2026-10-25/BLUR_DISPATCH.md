# Blur-filter construction and cache handoff — 2026-10-25

The converted implementation retains sigma transformation/clamping, fractional
rectangle phase, Gaussian behavior, cache keys and all four blur styles. Original
implementation/header copies remain selected by the reference build.

## Ownership and setup

The filter's sigma, style and transform policy are immutable. Mask rasterization
accepts an explicit rounded rectangle or borrowed rectangle span, replacing two
capturing callbacks and a universal callable template. Its local pixel owner
precedes borrowed raster state and publishes storage only after drawing finishes.
Aligned row width and complete mask extent are checked before allocation; local
clip bounds avoid integer negation of the original coordinate.

Nine-patch preparation retains source pixels, uncached blurred pixels and adopted
cache references separately. The source allocation is released after filtering,
before copying output into cache storage. Cache-allocation failure leaves the
uncached owner and mask unchanged. Every filter result is adopted before its
success flag is inspected. Final construction transfers exactly one allocation
or cache reference to NinePatch. Its nonmovable optional is constructed directly.

Unused find/add wrappers were removed; cache calls and ownership transfers are
visible at their use sites. The public factory rejects invalid style values.
Image-filter adaptation remains for this differential step; the owner's later
scope clarification requires its removal with the general image-filter system.

## Verification

M4 Mac mini with standalone non-Apple Clang 23.1.2/C++20. GUI.Forms fixture uses
its pinned LLVM 22.1.8 toolchain.

- Eleven deterministic release tests pass in both profiles.
- All five complete output artifacts match. Mask rendering now covers 288 scenes,
  adding asymmetric per-corner radii; rendering hash 10069483530225865383.
  The appended 576 shader cases retain hash 4976865620939446745.
- Whole Release and Debug instrumented suites report PASS; previous log files
  are preserved and detailed diagnostics remain unread.
- Seven selected GUI.Forms integration tests pass.
- Fresh relocated installed consumer builds and runs with consumer RTTI intact.
- Converted production/test files pass spelling and lifetime checks.

Logs: build/blur-dispatch-release.log, blur-dispatch-verification.log,
blur-dispatch-gui.log and blur-dispatch-installed.log. This batch does not claim
an elapsed-time improvement, exhaustive rendering equivalence or completion of
the full conversion. Direct injection of cache-allocation failure is not covered
by these rendering tests; its fallback ownership is reviewed at the source seam.
