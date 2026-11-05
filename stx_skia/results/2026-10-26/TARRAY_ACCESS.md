# TArray access and alias append

This follows TARRAY_CONSTRUCTION.md and TARRAY_RELOCATION.md. The imported
reference remains under `baseline/skia/`.

`fromBack` now validates its index before subtracting it from the live count.
Previously an extreme negative argument could overflow signed `int` before
`operator[]` received the result. Both overloads use a Release assertion and
then form an in-range forward index.

Methods returning an element reference or pointer now require an lvalue array.
This covers `data`, `begin`, `end`, indexed/first/last/reverse access, and append
methods that return the inserted address. Rvalue overloads are deleted so a
temporary owning array cannot hand out a borrow that outlives its full
expression. Compile-time checks reject temporary `data` and `back` calls. The
library and its existing consumer compile with this contract.

Repeated-value and range append still snapshot a live source before growth can
invalidate it. If the current capacity already holds the appended range, the
source stays live and disjoint from the new slots, so no snapshot is needed.
Candidate tests check the exact four element copies in both in-capacity alias
cases, alongside the growing alias cases and construction interruptions.

The shared heap allocator guarantees fundamental alignment. TArray now rejects
an element type with stricter alignment at instantiation, matching the raw
SkTDArray storage contract. A compile-only negative fixture confirms an
`alignas(64)` element is rejected with that diagnostic. This prevents growth
from replacing properly aligned inline storage with under-aligned heap storage.

The candidate/reference focused fixture passes 425 ordinary schedules. The
candidate passes 264 interrupted construction, 24 interrupted relocation, 16
append and 2,112 alias-reset cases. The complete Release suites each pass 45
tests. Candidate Release and Debug instrumented suites report PASS. All ten
rendering and serialization artifacts were regenerated and match reference
byte-for-byte. The CPU pixel-only scope check passes, a consumer builds and
runs against a relocated candidate installation, and the seven selected
GUI.Forms renderer integration tests pass with the pinned LLVM 22.1 toolchain.
The spelling check on the new test code and the lifetime-rule check on both
touched C++ files report zero findings. The imported header's remaining style
conversion is still open.

The alignment assertion changes only type admission. The exact source after
that assertion passes the 45-test candidate Release suite and the complete
candidate Release/Debug instrumented suites. Its relocated installed consumer,
CPU scope check and all seven selected GUI.Forms tests also pass.
