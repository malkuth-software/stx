# Constructed inline arrays

`skia_private::AutoSTArray` is extracted from the mixed template header into
`include/private/SkAutoSTArray.h` and converted. Retained uses include polygon
triangulation vertices, dash intervals, transformed draw points and shadow-key
storage. The original implementation remains in the preserved reference header.

## Representation and operations

The owner retains its pointer, aligned inline byte array and live element count,
in the same order. Its capacity-rounding formula is unchanged. No capacity field,
registration or shared ownership is added. On M4 macOS arm64, reference and
candidate layouts match at 24, 40 and 152 bytes for inline requests of 1, 4 and
32 uint32 elements respectively. These are measured host layouts.

Reset still destroys old elements in reverse order and default-constructs the
requested number. Scalar storage is not needlessly initialized. Equal positive
live counts reuse the existing allocation; different counts choose inline or
new heap storage, releasing the old heap first. Signed counts and size products
are validated before mutation. Compile-time bounds cover inline capacity and
fundamental heap alignment. Element destructors must not throw.

Trimming destroys only the tail and retains storage, including when the live
count reaches zero. Destruction releases an owned heap allocation independently
of that live count. Explicit reset to zero releases the retained allocation and
returns to null. This corrects the original zero-count path, which could retain
a heap allocation after trimming away its last element.

Inline moves construct destination elements, destroy the moved-from source
elements and leave the source empty. Heap moves transfer the pointer/count
without moving elements, including an allocated block with a zero live count.
Move assignment remains unavailable. Temporary-owner borrows are rejected at
compile time and const observation returns const elements.

For exception-enabled consumers, reset failure leaves the owner empty. A local
unique owner releases pending heap storage while standard uninitialized
construction cleans up a successfully constructed prefix. This adds no persistent
state. A failed inline move cleans up the destination prefix; all source elements
remain live, although earlier ones may be moved-from. The production library is
built without exception handling.

## Verification

Sequential M4 builds use non-Apple LLVM 23.1.2:

- Candidate and reference Release profiles pass all 39 tests.
- The fixture checks 576 scalar reset transitions, same-count reuse, iterators,
  trimming, initialized prefix values and reverse element destruction.
- Move fixtures distinguish inline element moves from heap pointer transfer and
  verify independent source reuse.
- Candidate-only cases cover release after trimming to zero, moving a heap owner
  with zero live elements, failed inline/heap construction, failed replacement,
  reuse after failure and a partially completed throwing inline move. Reference
  cases avoid the original retained-heap cleanup path; they do not claim the
  reference provides the corrected behavior.
- Nine rendering/mipmap artifacts and the serialized artifact match byte-for-byte.
- Release and Debug instrumented deterministic suites report PASS.
- Fresh relocated installed-package consumer and CPU source/symbol checks pass.
- Seven selected GUI.Forms integration tests pass against fixture
  `8d1f2be06a40c5da78bb75e45cbe1fecec2f6b62`, using its LLVM 22.1 toolchain.

Local records are `build/auto-starray-gates.log` and archived
`build/auto_starray_gate.sh`; GUI integration is recorded in
`build/auto-starray-gui.log`. The converted header and fixture have zero house-style
and lifetime spelling findings. The remaining mixed header has eight spelling
findings/review candidates and zero lifetime spelling findings. Instrumented diagnostics remain reserved for the
owner's independent reviewer. No speed improvement is claimed. The general
helpers still in SkTemplates.h and broader renderer code remain unconverted.
