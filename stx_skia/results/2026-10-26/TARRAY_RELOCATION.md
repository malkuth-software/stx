# TArray relocation and append ownership

This continues the construction work recorded in TARRAY_CONSTRUCTION.md. The
reference header remains in `baseline/skia/`. These changes correct failure
states in the owned array; they do not establish a complete conversion of the
container or the surrounding imported library.

## Changed paths

Move construction now uses the same transfer path as move assignment. A heap
source transfers its allocation and capacity; an inline source constructs its
destination elements before publishing their count. Reserve, shrink and append
growth guard a newly allocated buffer until relocation succeeds. Nontrivial
relocation destroys the old elements only after the new range is complete.
Bit-relocatable elements retain the original byte relocation contract and do
not run source destructors. An interrupted move leaves the source array live;
its elements may have moved-from values, as in the standard basic guarantee.

Bulk copy, fill and move append construct into unused capacity before changing
the live count. When the copied source is a live element or subrange of the
destination, it is snapshotted before growth can invalidate it. The move-array
append publishes its count only after the entire destination range exists.
`removeShuffle` requires either bit relocation or a nonthrowing move because it
ends the removed element before replacing it.

Unused capacity is temporarily unpoisoned during construction and re-poisoned
if construction throws. The local guard adds no array field. Heap swap also
transfers the associated sanitizer poison state with each allocation. The
ordinary heap and four-element inline layouts remain 16 and 32 bytes.

## Verification and performance

The focused candidate/reference fixture passes 425 ordinary schedules. The
candidate additionally passes 264 interrupted construction cases, 24
interrupted relocations, 14 bulk-append cases and 2,112 alias resets. The
interrupted paths check live counts and, in the instrumented build, unused
capacity poisoning. Ordinary successful paths cover nontrivial and bitwise
relocation, inline and heap moves, `move_back` and `removeShuffle`.

An attempted forced-inline growth path was measured and reverted because it
increased the small-array copy/reset costs. A direct capacity check in the
copy/reset hot path retained the simpler call structure. In nine paired M4
trials against the preserved reference, the current trial's candidate/reference
medians were 1.148 and 1.116 at 16 elements, 0.988 and 1.022 at 256, and
1.107 and 1.070 at 4096, for copy assignment and disjoint span reset
respectively. Trial-to-trial spread and the large-array regressions preclude a
speed claim. The change is justified by corrected ownership and failure states.

The complete candidate/reference Release builds pass 45 tests each. Candidate
Release and Debug instrumented suites report PASS. Ten rendering and
serialization artifacts match byte-for-byte. A consumer builds and runs against
a relocated fresh candidate installation; the CPU pixel-only build-scope check
passes. The seven selected GUI.Forms renderer integration tests pass with the
pinned LLVM 22.1 toolchain. The spelling check on the new test file and the
lifetime-rule check on both touched C++ files find zero issues; the imported
header's other style conversion remains open.

## Remaining container scope

The array still carries imported spelling and compatibility interfaces.
Reference-returning accessors need complete rvalue-borrow qualification, and
the inline `swap` path has only a basic failure guarantee. Work on those
interfaces and the surrounding renderer remains open in `docs/CONVERSION.md`.

## Additional inline swap check

The candidate regression now exchanges two inline arrays and a heap array
with an inline array, then checks element values, live object counts,
self-swap, and subsequent append. Candidate Release passes 49/49 tests; the
ASan/UBSan runner reports `PASS`. This verifies successful swaps for those
storage combinations. It does not strengthen the documented failure guarantee
when an element move throws.
