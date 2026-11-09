# Raster clip resize policy

`SkRasterClipStack` chooses whether to disable antialiased clipping when it
is constructed, based on the root bounds and the scan converter's tiling
limit. Its `setNewSize()` method previously changed the root bounds but left
that decision unchanged. Growing across the limit could retain antialiasing
where the scanner had required tiling; shrinking back could unnecessarily
disable it.

The candidate computes the policy from the new bounds before publishing them.
It rejects negative dimensions and a resize while a saved clip state is
pending, before changing the root. The original header is preserved at
`baseline/skia/src/core/SkRasterClipStack.h` for differential builds.

The shared test confirms an ordinary fractional clip is antialiased for a
16×16 root and hard-edged for a 20,000×16 root. Candidate-only cases resize
across that threshold in both directions and check the resulting clip mode.
Two focused Release invocations verify invalid resize inputs are rejected.

On the M4 Mac mini, candidate and baseline Release suites each passed 48/48
tests and generated rendering and serialization artifacts matched byte for
byte. Candidate Release and Debug ASan/UBSan suites reported `PASS`; detailed
integrity logs remain in their build trees. The CPU build-scope check, a
relocated installed consumer, and seven selected GUI.Forms renderer tests
passed. The new test file passed house-style spelling and lifetime checks;
the imported clip-stack header still needs full conversion.

This validates the resize policy and its state boundary. It does not establish
a performance change or complete review of clipping and scan conversion.
