# Heap and inline allocation owners

`SkAutoMalloc.h` is converted to the house style. The original header is preserved
byte-for-byte from import commit `490c9bd4a97884d268a5b3f9924748ebf31954bb` under
`baseline/skia/src/core/`; the reference build gives that directory include priority.

## Behavior and storage

The heap owner retains its pointer and capacity. Explicit move operations transfer
the allocation and clear the source's existing capacity field. Previously the
implicit move transferred the pointer but copied capacity: resetting the moved-from
owner to its former size could return null without allocating. Candidate fixtures
exercise reuse after both move construction and assignment, including reuse policy.

The inline owner retains its pointer, capacity and inline allocation. It remains
nonmovable. Inline capacity now uses aligned byte storage; all read bytes must be
initialized by the caller. Const observation returns a const pointer. Borrowing
get/reset calls reject temporary owners at compile time. Ownership release still
transfers the existing allocation and its `sk_free` obligation.

Existing allocation policies remain: heap replacement prepares the replacement
before releasing the previous block; inline/heap replacement frees a previous heap
block first. Reuse preserves contents and capacity; replacement discards contents.
Allocation failure follows the library's terminating policy. Size alignment is
bounded at compile time and invalid shrink choices reach a release assertion.
No counters, registration, shared ownership or additional fields are introduced.

On M4 macOS arm64 Release, original and candidate sizes are identical:

| Type | Original bytes | Candidate bytes |
|---|---:|---:|
| `SkAutoMalloc` | 16 | 16 |
| `SkAutoSMalloc<64>` | 80 | 80 |
| `SkAutoSMalloc<65>` | 88 | 88 |

## Verification

Non-Apple LLVM 23.1.2, sequential M4 builds using the repository's `m4build`
contract. The allocation fixture checks release, retained content, move, self-move,
moved-from reuse and 120 transitions across four inline capacities and both shrink
policies. Candidate-only assertions cover strengthened borrow and moved-from
contracts; reference fixtures do not claim that the original implements the fix.

- All 35 standalone tests pass in both candidate and reference Release builds.
- Nine rendering/mipmap artifacts and the 24-case serialized artifact match.
- Release and Debug instrumented deterministic suites report PASS.
- A fresh relocated installed-package consumer passes.
- CPU source/symbol scope checks pass.
- Seven selected GUI.Forms integration tests pass against fixture
  `8d1f2be06a40c5da78bb75e45cbe1fecec2f6b62`, using its LLVM 22.1 toolchain.
- House-style and lifetime spelling checks pass for the converted header and fixture.

Local command records are `build/auto-malloc-gates.log`,
`build/auto-malloc-gui.log` and the archived `build/auto_malloc_gate.sh`.
Instrumented diagnostics remain reserved for the owner's independent reviewer.
No speed improvement is claimed. These checks cover the allocation boundary and
existing rendering corpus, not the full remaining conversion or independent review.
