# Constructed heap arrays

`skia_private::AutoTArray` is extracted from `SkTemplates.h` into
`include/private/SkAutoTArray.h` and converted. Its retained production use is the
dash effect's interval storage. The original implementation remains in the
preserved `baseline/skia/include/private/SkTemplates.h` and is selected by the
reference profile's include priority.

## Preserved representation and behavior

The owner still contains a unique array pointer and element count, measuring
16 bytes in both M4 Release profiles. Elements are default-initialized by `new
T[count]`; scalar arrays are not needlessly zero-filled. Reset constructs a new
array before replacing the old one, even when the requested count is unchanged.
Moves transfer the allocation and clear the source count; self-move leaves the
array intact. No registration, additional metadata or shared ownership is added.

The conversion validates signed counts and the count-to-byte product with Release
contract checks, before allocation. Const observation returns const elements and
pointers. Getters, iterators and indexed borrows reject temporary owners at compile
time. Empty iterator endpoints explicitly return null. Existing collection bounds
checking on element access is retained.

For exception-enabled consumers, failed allocation or element construction during
reset leaves the original array unchanged. Standard array construction cleans up
the successfully constructed prefix before propagating an element exception. The
production library itself is built without exception handling; no new recovery
mechanism is introduced there.

## Evidence

Sequential M4 macOS arm64 builds use non-Apple LLVM 23.1.2:

- Both candidate and reference Release profiles pass all 37 tests.
- The new fixture checks 192 scalar reset transitions, size/byte counts, const
  access, iterator extents, move construction/assignment, self-move and reuse.
- Constructed-element tests verify destruction counts, reset at the same size,
  construction of a replacement before destruction of old elements, and preservation
  of the original array when the fourth replacement constructor throws. Partial
  replacement elements are destroyed and all live elements are eventually released.
- Both profiles measure the owner at 16 bytes.
- Nine render/mipmap artifacts and the serialized artifact match byte-for-byte.
- Release and Debug instrumented deterministic suites report PASS.
- A fresh relocated installed-package consumer and CPU source/symbol checks pass.
- Seven selected GUI.Forms integration tests pass against fixture
  `8d1f2be06a40c5da78bb75e45cbe1fecec2f6b62`, using its LLVM 22.1 toolchain.

Local records: `build/auto-tarray-gates.log` and archived
`build/auto_tarray_gate.sh`; GUI integration is recorded in
`build/auto-tarray-gui.log`. Instrumented diagnostics remain reserved for the
owner's independent reviewer. The new header and fixture pass the house-style and
lifetime spelling checks. Other classes remaining in the mixed template header
are not claimed converted: that remaining header still has 13 spelling findings/
review candidates and zero lifetime spelling findings. No speed improvement is
claimed by this batch.
