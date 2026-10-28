# Raster special-image ownership and subset views

SkSpecialImage and its raster implementation now use named source/destination
rectangles, explicit shader/image owners, initialized bitmap outputs and named
copy/view results. Strict sampling builds a bounded bitmap shader; non-strict
sampling translates content coordinates into the full backing image. Neither
sampling arithmetic nor pixel-copy policy changes.

The bitmap member retains its pixel reference independently of the caller's
bitmap metadata. CopyFromRaster explicitly allocates a new image at the subset's
size and translates its content origin to zero. The full backing-image query
can share immutable pixels or copy mutable pixels, following SkBitmap::asImage.
Metadata references and the borrowed color-space pointer reject temporary
special-image objects. Four compile-time checks enforce that restriction.

The expanded image_subset_contract checks pixel-reference identity for shared
views versus independent copies, mutation visibility versus copy isolation,
nested subset and one-pixel-outset coordinates, full backing dimensions,
retained pixel access after source handles are released, and preservation of an
existing bitmap on a null AsBitmap source. It also adds 48 recorded special-image
shader renders across RGBA/alpha-only sources, all four tile modes, nearest/linear/
cubic filtering, and strict/non-strict bounds. Outside-pixel perturbation leaves
all 24 strict special-image renders unchanged.

## Gates

M4 arm64 with non-Apple LLVM, sequential AGENTS.md build commands:

- Candidate release: 58/58 PASS.
- Reference release: 57/57 PASS.
- All fifteen common raw artifacts match byte for byte.
- Complete deterministic instrumented candidate suite: PASS; diagnostics unread.
- Fresh relocated installed-package consumer builds and executes successfully.
  The relocated prefix is build/special-image-install-relocated and the consumer
  is build/special-image-installed-consumer on the M4 mirror.
- House-style spelling and lifetime rules: zero findings in both converted
  production files and the expanded fixture; semantic review performed as well.

The full image-subsets artifact now contains 408 renders, 339,456 pixels and
1,357,824 bytes. This extends the 360-render record in CPU_SUBSET_PATH.md.
Original source/header snapshots remain unchanged in the reference overlay.
No speed claim is made. Canvas/device, pixel kernels and remaining sampling
implementations continue to require conversion; these gates are scoped evidence.
