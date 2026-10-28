# Block resize sanitizer boundary

`SkBlockAllocator::Block::resize` shrinks the live tail of a reservation. The
previous implementation began ASan poisoning at `nextCursor + 1`; that left
the first released byte outside the requested poisoned region. The candidate
starts at `nextCursor`, matching the allocator's exclusive-end cursor rule.
The original header is retained at
`baseline/skia/src/core/SkBlockAllocator.h` for the reference build.

The candidate-only deterministic check allocates a 64-byte, eight-byte-aligned
reservation and shrinks it by 32 bytes. Under ASan it checks that the last
retained byte remains accessible and the first released byte is poisoned.
The boundary lands on an eight-byte shadow unit, so the check does not depend
on partial-granule poisoning behavior. No out-of-bounds memory access is made
by the check.

On the M4 Mac mini, the focused candidate sanitizer check reported `PASS`.
The full candidate and baseline Release suites passed 47/47 tests each;
generated rendering and serialization artifacts matched byte for byte.
Candidate Release and Debug ASan/UBSan suites reported `PASS`. The CPU build
scope, relocated installed consumer and seven selected GUI.Forms tests passed.
Detailed integrity logs remain in their build trees for independent review.
The touched test file passed house-style spelling, and the touched header and
test passed lifetime checks. The imported allocator header remains scheduled
for full style and ownership review.
