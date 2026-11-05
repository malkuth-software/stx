# Surface ownership conversion — 2026-10-26

The retained raster, null and base surface implementations now use explicit
local types, named owned images/devices/results and visible transfer and callback
ordering. The change covers SkSurface_Base.cpp/.h, SkSurface_Raster.cpp/.h and
SkSurface_Null.cpp. Public SkSurface.cpp/.h and broader image/device conversion
remain separate work; compatibility GPU query stubs remain unchanged here.

## Ownership and failure behavior

After capture removal, the base surface still stored both an owned canvas and a
second pointer that always observed that same canvas. It now has one unique
canvas owner. Accessors return a borrow for the surface lifetime. Destruction
clears the canvas's observer back link before its notification and destruction.

The base also retained the initial SkPixelStorage solely for the removed capture
interface. On raster copy-on-write, the bitmap/device moved to a replacement
allocation but this extra owner kept the initial pixel ref alive. Removing that
unused field/accessor leaves lifetime with the current bitmap/device and any
snapshot images. Releasing the last old snapshot can now destroy its pixel ref
while the live surface continues using its replacement. The tracked-reference
regression demonstrates that difference without dereferencing freed storage.

Copy-on-write still allocates before replacing the bitmap backend, preserves
pixels in retain mode, and preserves external snapshot pixels in discard mode.
An exclusively cached snapshot is released before restoring backing mutability.
The pixel-ref delegating constructor now transfers its handle explicitly.

Subset snapshots and fallback reads use fallible bitmap allocation rather than
an aborting allocation wrapper. Failure produces a null snapshot or one null
callback result. Subset copying also checks the read result before publication.
This does not inject allocator exhaustion; the ordinary failure test covers a
null surface's unknown-format fallback and verifies one null callback. The
original reference's corresponding path reaches its aborting allocation wrapper,
so that particular test runs only in the candidate. Other read tests run in both.

The base object measures 56 bytes versus 72 in the original on macOS arm64.
This is object layout and ownership evidence, not a rendering-speed measurement.

## Deterministic regression coverage

The new surface contract test covers:

- Owned and wrapped RGBA8888 surfaces with 9×7 pixels and 52-byte rows;
  retain and discard paths; cached snapshot identity and content generation.
- Snapshot preservation across later drawing and after surface destruction;
  clipped subset snapshots and wholly disjoint subset rejection.
- Wrapped-pixel callback lifetime, invalid-input non-ownership, tracked old-ref
  destruction after copy-on-write, and exactly one final release.
- In-place backing reuse when a temporary snapshot has no remaining external owner.
- Null surfaces and synchronous read callbacks for resized uniform pixels,
  invalid source bounds and the candidate's unsupported fallback.

A named callback state outlives every surface using it; borrowed pixel views end
before their owners are replaced. Tests and all five converted implementation/
header files have zero house-style spelling or lifetime-rule findings. Those
checks supplement review of the ownership and failure ordering above.

M4 macOS arm64 with non-Apple Clang 23.1.2: seventeen tests pass for candidate and
original reference. All seven existing rendering artifacts compare byte-for-byte.
Release and Debug instrumented suites report PASS; previous diagnostic logs are
preserved and detailed contents remain unread. A fresh relocated installed
consumer passes. Final cleanup is rechecked in build/surface-conversion-final.log.
Seven selected GUI.Forms tests pass with pinned LLVM 22.1.8, including prepared
text, shadows, damage and drawing API checks. Integration is recorded separately
in build/surface-conversion-gui-final.log.

Original source and headers remain isolated in the reference profile. Original surface implementation and header copies were already retained before
this batch.
Native cross-platform CI on this commit and the owner's independent review remain
separate gates. Full raster-library conversion is still active.

Records: build/surface-conversion-tests-2.log, build/surface-conversion-gates.log,
build/surface-conversion-final.log, build/surface-header-final.log and
build/surface-conversion-gui-final.log.
