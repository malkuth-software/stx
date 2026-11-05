# CPU layers without image-filter evaluation — 2026-10-25

The owner's contract retains group-opacity layers while excluding general image
and backdrop effects. This batch separates those requirements in the actual
canvas path instead of keeping a filter graph for ordinary layers.

## Implementation

SaveLayerRec now carries bounds, restore paint, color space and layer flags.
Backdrop filters, filter arrays, backdrop scaling and tile modes are removed from
that public record and its helper factories. Layer storage no longer retains
filter arrays, coverage-layer state or padding state. Recording storage follows
the same reduced shape; serialized layer records with withdrawn filter flags are
explicitly rejected instead of silently dropping their effects.

Ordinary layers use the parent device's pixel coordinate system. Bounds intersect
the device clip; optional local bounds map through the existing matrix. Restore
paint that affects transparent black, or modifies copied prior content, retains
the original full-clip behavior. Low-alpha source formats retain the existing
N32 upgrade, and explicitly requested F16 layers remain supported. The new device
inherits the parent coordinate system with its integer bounds offset. Restore and
initialization from prior pixels use device composition directly.

The full mapping/filter-evaluation helper, optimized image-filter draw path and
automatic image-filter layer guard are removed from SkCanvas. PreparedDraw is
only an owned paint value after destination preparation; it does not create or
restore hidden layers. CPU mask filters remain on the paint.

The remaining SkPaint image-filter field/serialization interface is still being
removed. The reduced canvas explicitly rejects that unsupported state with a
release assertion; it does not pretend to execute the effect. Core SkImageFilter,
its cache/types, device filtering interfaces and SkSL still compile elsewhere.
This is NOT a claim that those subsystems or the full canvas are converted.

## Rounding evidence and retained negative result

The first reduction used strict roundOut for mapped bounds. Four of 256 cases
with skew and a partial layer differed (139/126/143/128 differing bytes; maximum
channel differences 38/36/38/36). The original layer code allows a 0.001-pixel
inset before outward rounding so floating transform error does not introduce an
extra row. Restoring that exact rule makes the entire battery byte-identical.
The first result remains in build/layers-reference.log; the correction is in
build/layers-rounding-check.log. Reversed bounds also remain empty under skew,
verified by an independent all-black expectation.

## Verification

M4 Mac mini; standalone non-Apple Clang 23.1.2/C++20; pinned GUI.Forms LLVM 22.1.8.

The new layer test covers 256 combinations of four transforms, prior-content/F16
flags, opacity/source/multiply/color-matrix restores and four bound choices. Each
scene includes nested opacity and antialiased clipping. An independent pixel
expectation checks that two overlapping opaque children receive group alpha only
once. The layer output contains 7,475,200 bytes with FNV hash
8297667486674300511. Both profiles run the same tests and compare complete bytes.

The 24 existing scenes, conversion/pixmap/mask output, 288 mask-filter scenes and
576 shader-mask cases keep their hashes. Native CI compares layers.raw as a sixth
artifact. Imported canvas and recording files receive scoped dependency edits;
only the rewritten layer path and new tests are claimed as converted here.

Final checks: twelve deterministic tests pass in both profiles, all six complete
artifacts match, Release and Debug instrumented suites report PASS, the freshly
relocated installed consumer builds/runs and seven selected GUI.Forms tests pass.
The final verification record is build/layers-final.log. Detailed instrumented
suite diagnostics remain unread and prior files are preserved. Cross-platform
CI, independent Daybreak review and full conversion remain separate gates.
