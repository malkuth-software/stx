# Color matrix and lookup-table conversion

The matrix value type, lighting factory, matrix filter, color-table owner and
table filter now use explicit initialization, named results and ownership
transfers. Matrix concatenation retains its exact alias scratch path and
arithmetic order. Lookup-table construction chooses copying or identity filling
once per channel. Four immutable 256-byte rows belong to the table; the filter
retains that owner while a raster pipeline borrows the rows. Coefficient and
table views reject temporary owners.

Pre-conversion sources and headers are retained under baseline/skia. Existing
reference copies, including original registration, were not overwritten.

The color_adjustment_contract fixture checks concatenation aliasing, finite
coefficient admission, RGBA/HSLA and clamped/unclamped filtering, both lighting
factory paths, all sixteen nullable-channel combinations, copied table storage,
retention after the original handle is released, and table serialization with
truncated-input rejection. Its output covers 1,024 matrix colors, two lighting
colors and 3,840 table colors including partial alpha.

M4 Mini, non-Apple LLVM, Release: candidate 52/52 and reference 51/51 tests pass.
The new 93,996-byte color-adjustments.raw artifact matches byte for byte. Eight
existing shader, layer, render, pixel conversion, pixmap, mask, mask-filter and
flattenable artifacts also match byte for byte. The whole candidate instrumented
suite returned PASS. House spelling and lifetime checks report zero findings
across the nine production files and new test. This is conversion correctness
evidence, not a measured speed claim or completion of the remaining color pipeline.

Commands: the candidate, baseline and candidate-sanitize invocations of
`tools/build_native.sh` through m4build in AGENTS.md, followed by direct byte
comparison of the nine artifacts. Detailed instrumented diagnostics were not
read in this chat.
