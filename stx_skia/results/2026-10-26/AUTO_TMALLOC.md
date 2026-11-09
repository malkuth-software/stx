# Typed heap allocation owner

`skia_private::AutoTMalloc` is extracted from the mixed `SkTemplates.h` into
`include/private/SkAutoTMalloc.h` and converted to the house style. The original
complete template header is preserved under `baseline/skia/include/private/` from
import commit `490c9bd4a97884d268a5b3f9924748ebf31954bb`. Reference include priority
selects that original implementation.

## Ownership and work

The owner still contains exactly one owning pointer: eight bytes on M4 arm64 in
both reference and candidate. It stores neither count nor capacity and adds no
registration, counters or shared ownership. Retained uses include blur scratch,
region runs, draw scratch and the storage writer. Callers retain responsibility
for their live element extent and initialize bytes before reading them.

- Reallocation preserves the smaller old/new byte extent. The element-count
  product is checked before multiplication and ownership release, replacing the
  original unchecked multiplication. Zero count releases storage. Failure follows
  the existing terminating allocator policy.
- Reset still allocates replacement storage before freeing the old block, even
  for the same requested count. Reset discards contents; it is not a reserve call.
- Move and release use the existing unique owner. A named typed deleter replaces
  the general overloaded-function adapter for this class only.
- Getters, conversions and indexed borrows reject temporary owners at compile
  time. Const observation returns a const pointer. No runtime borrow state exists.
- Compile-time admission now explicitly requires trivial copying and fundamental
  alignment, as required by byte relocation through the retained malloc family.

No drawing algorithm changes and no speed improvement is claimed. The size-product
check occurs at reallocation, outside element processing. The broader mixed
`SkTemplates.h`, its other owners and its generic adapters remain unconverted.
Its syntax checker still reports 15 existing findings/review candidates; the
extracted header and new test have zero spelling or lifetime findings.

## Verification

Sequential M4 macOS arm64 builds with non-Apple LLVM 23.1.2:

- Both 36-test Release suites pass.
- The new fixture runs 864 grow/shrink transitions for byte, uint32 and double
  storage, checking initialized-prefix preservation. It also checks empty state,
  move, self-move, moved-from reuse, release/adoption, const observation and reset.
- Nine rendering/mipmap artifacts and the 24-case serialization artifact match.
- Release and Debug instrumented deterministic suites report PASS.
- Fresh relocated installed-package consumer and CPU source/symbol scope pass.
- Seven selected GUI.Forms integration tests pass against fixture
  `8d1f2be06a40c5da78bb75e45cbe1fecec2f6b62`, using its LLVM 22.1 toolchain.

Local command records are `build/auto-tmalloc-gates.log` and the archived
`build/auto_tmalloc_gate.sh`; integration is recorded in
`build/auto-tmalloc-gui.log`. Instrumented diagnostics remain reserved for the
owner's independent reviewer. The bounded product is established by code review;
this fixture does not attempt an oversized allocation or deliberately terminate
on the new contract check.
