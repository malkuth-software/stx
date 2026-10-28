# Block allocator constructor validation

`SkBlockAllocator` previously aligned a caller-supplied block increment before
checking it, narrowed the inline extent into an `int` block size, and kept its
size assertions in Debug only. Zero increment could later become a zero
divisor in growth sizing. A near-`SIZE_MAX` increment could wrap during
alignment, and a large inline extent could narrow to a different block size.
An invalid growth-policy value fell through the exponential branch.

The constructor now validates each input in an initializer helper before its
first unsafe operation. The block increment must be positive and at most the
allocator's 512 MiB per-allocation limit. Inline extent must fit that limit
and the signed block-size representation after base overhead. Growth policy
must be a declared enum value. The valid-input representation and growth
formulas remain unchanged. The original source is preserved at
`baseline/skia/src/core/SkBlockAllocator.cpp` for the reference profile.

Candidate-only Release cases exercise zero and `SIZE_MAX` increments,
`SIZE_MAX` inline extent and a negative policy value. Each is rejected before
block use. Both candidate and baseline continue to run the shared block-list
cases. On the M4 Mac mini, the candidate and baseline Release suites passed
47/47 tests each and their generated rendering/serialization artifacts were
byte-identical. Candidate Release and Debug ASan/UBSan suites reported `PASS`;
their detailed integrity logs remain in the build trees for independent
review. The CPU build-scope check, relocated installed consumer and seven
selected GUI.Forms renderer tests passed. The touched test passed the
house-style spelling and lifetime checks; the imported allocator source
still has pre-existing style findings and is not declared fully converted.

This pass does not establish a speed change or review the entire block
allocator. Its unconverted allocation, reserve and release paths remain in
the conversion ledger.
