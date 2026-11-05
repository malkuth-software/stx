# Strict CPU subsets and unused GPU dispatch

The public CPU image-rectangle path already implements strict subsets through
bounded bitmap views. SkBitmapDevice rounds the source rectangle outward,
extracts that pixel view and discards the original mip chain. The raster special
image similarly builds its strict shader from an extracted bitmap subset.
Fractional source rectangles admit pixels whose squares intersect the rectangle;
the contract is not a fractional mask over individual source pixels.

The separate SkImageShader subset factory was documented GPU-only. Its canvas
callers were gated by useDrawCoverageMaskForMaskFilters, which every retained
CPU device inherited as false. Only excluded Graphite supplied true. This change
removes those canvas branches, the unused device dispatch methods, the GPU-only
image-shader subset field/factories and the unused special-image base shader
implementation. The retained raster override becomes an explicitly required
implementation. The original header is preserved in the reference overlay.

CPU drawBitmapAsMask and drawRRectNinePatch remain. Archive symbol inspection
confirms both in candidate and reference, while MakeSubset, MakeForDrawRect,
SkBitmapDevice::drawCoverageMask, SkBitmapDevice::drawBlurredRRect and
SkCanvas::attemptBlurredRRectDraw are absent from candidate and present in reference.
This removes unused interfaces and state; it does not claim a measured speedup.

## Perturbation and comparison

The new image_subset_contract uses 180 combinations: integer, fractional and
partially out-of-bounds source rectangles; RGBA and alpha-only images; nearest,
linear, cubic, mipmapped and anisotropic sampling; identity, affine and perspective
transforms; ordinary and blurred paint. For each, a second source changes only
pixels outside the rounded source rectangle. All 180 strict draws remain identical.
The same perturbation changes 144 fast draws, establishing that the fixture can
observe outside samples when permitted. Both profiles pass the same assertions.
Their 360 recorded original-source draws match byte for byte (1,198,080 bytes).
The fixture also rejects a silently empty render and compile-checks that the
candidate no longer exposes the two removed dispatch/factory entry points.

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 58/58 PASS.
- Reference release: 57/57 PASS.
- All fifteen common raw output artifacts match byte for byte.
- Complete instrumented deterministic suite: PASS; diagnostics left unread.

The new test initially failed to compile because it lacked SkBlurTypes.h; the
explicit include fixes that dependency. No rendering failure was observed.
The image shader/header and new test have zero spelling/lifetime findings.
Whole-file checks across the partially converted canvas/device/special-image
files decrease from 693 to 643 spelling findings; lifetime checks report zero.
These inherited files are not claimed converted by this pruning change.
Logs of that comparison are in build/subset-pruning-style/.

This resolves the strict-subset follow-up in IMAGE_SAMPLING_CONVERSION.md.
It does not establish all possible source-coordinate behavior or cross-platform
execution. Geometry, canvas/device conversion and raster kernels remain active.
