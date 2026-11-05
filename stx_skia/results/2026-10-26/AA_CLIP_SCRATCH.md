# AA clip scratch follows the draw

`SkAAClipBlitter` reserved a four-byte-per-pixel scanline for the entire clip
width even when `blitH` drew a short span or `blitMask` used a small visible
rectangle. Its `int width + 1` calculation could also overflow before the
allocation. Scratch now grows to the active span, mask clip or antialias run
width and is retained for later calls. The size calculation is checked before
the owner receives it. Empty mask clips and empty antialias runs return without
touching scratch or the downstream blitter.

The candidate-only test uses a 50,000-pixel-wide region with a two-pixel gap
and draws four pixels across the gap. Both the solid-span and antialias-run
paths produce `{255, 0, 0, 255}`. Their initial scratch request is 20 bytes
instead of the former 200,004 bytes. This is a workspace calculation, not a
measured allocation peak. `baseline/skia/src/core/SkAAClip.h` retains the
original class layout for the reference implementation.

On the M4 Mini, the candidate Release suite passed 50/50, the reference suite
passed 49/49, and the candidate instrumented suite returned `PASS`. The paired
24-scene render outputs matched byte for byte. `tools/measure.py` reported
median draw times of 0.006830879 seconds for the reference and 0.006862627
seconds for the candidate across nine paired trials. This workload does not
isolate AA clipping; the small timing difference supports no speed claim.
Only macOS arm64 was run for this change. The remaining clip and scan-conversion
implementation has not been fully converted to house style.
