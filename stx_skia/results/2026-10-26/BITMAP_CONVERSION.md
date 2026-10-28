# Bitmap ownership and metadata conversion

`SkBitmap.cpp` and its public header now use explicit types, named intermediate
values and ownership transfers throughout. The pixel-reference owner precedes
its borrowed pixmap. Metadata accessors reject temporary bitmaps; copying or
moving a bitmap keeps the established shared-pixel and empty-source behavior.
Callbacks remain synchronous, and their contract excludes reentering mutation
of the bitmap being released. Setting alpha or color-space metadata changes
only that bitmap; the inherited comments incorrectly claimed a shared change.

Alpha extraction now has separate filtered and unfiltered procedures. A filter
can produce an extracted mask, request unfiltered fallback, or fail because of
invalid output or allocation. Returned storage is owned on every path. The
filtered procedure's owners end before fallback allocation begins, and output
is committed only after successful construction. The missing-pixel alpha fill
addresses each live row instead of advancing beyond the final row's extent.
The filter mathematics and pixel conversion rounding are unchanged.

The pre-conversion source and header are preserved under `baseline/skia/` and
selected by the reference build. They include the previously committed bounds
repairs; this comparison isolates the conversion from those earlier fixes.

The new `bitmap_contract` fixture checks copy/move/reset ownership, release
counts, metadata borrowed during reset, color metadata isolation, failed
installation, shared and self-replacing subsets, pixel transfers, alpha values,
self-extraction and unchanged output after allocator rejection. Compile-time
checks reject metadata borrows from temporary bitmaps.

M4 Mini, non-Apple LLVM, Release: candidate 51/51 tests; reference 50/50 tests.
The rendering, mask, pixel-conversion and serialization artifact hashes match.
This is correctness evidence for the conversion; no performance gain is claimed.

The complete candidate instrumented suite returned `PASS`. A fresh installed
prefix was relocated, and the independent consumer compiled, linked and ran.
Eight candidate/reference artifact files also matched byte for byte. The house
spelling and lifetime checkers report zero findings in the converted source,
header and new fixture.
