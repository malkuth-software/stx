# Owned raster surface stride boundary

`SkSurfaces::Raster` and `skcpu::Recorder::makeBitmapSurface` previously checked
only the image format before allocating pixels. A caller could supply a stride
larger than the bitmap's signed 32-bit limit for a one-row image. The allocator
needed only the final row's bytes and could succeed; the surface constructor's
`SkBitmap::setInfo` then rejected the stride, but the factory still returned a
surface. The private `SIZE_MAX` ignored-stride marker had the same issue.

Both factories now validate the effective stride before allocation and reject
the private marker at their public entry points. Zero remains the documented
request for the minimum stride. The candidate-only surface test covers both
factories with a one-row image and both invalid strides. This does not add a
second pixel allocation or any work inside a drawing loop.

Verification on the nearby M4 Mac mini with Clang 23.1.2: candidate Release
49/49 tests passed; candidate ASan/UBSan returned `PASS`. The sanitizer runner
reported only that indicator; detailed diagnostics were not inspected here.
The prior reference and GUI.Forms gates are recorded in
[RASTER_REGION_INPUT_BOUNDS.md](RASTER_REGION_INPUT_BOUNDS.md); they do not cover
this new negative case. This correction changes only invalid input behavior.
