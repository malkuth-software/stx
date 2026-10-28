# Inline scratch arrays

`skia_private::AutoSTMalloc` is extracted from `SkTemplates.h` into
`include/private/SkAutoSTMalloc.h` and converted. Retained callers include gradient
stop storage, raster-pipeline programs and memory-context patches, polygon work,
shadow tessellation, curve points and blitter runs. The original class remains in
the preserved reference template header.

## Storage and behavior

The original union used a one-element `T` array as a view over a larger allocation.
The converted owner uses aligned byte storage for implicit-lifetime trivial
elements. It retains the four-byte capacity padding, T's required alignment and
one pointer. No length, capacity field, registry or shared owner is added.
Compile-time constraints validate the inline size product, trivial copying and
fundamental heap alignment; no new aligned allocator is introduced.

The caller still tracks the initialized extent. Default construction exposes inline
capacity; explicit zero construction is empty. Reset discards contents, releasing
old heap storage before replacement. Reallocation preserves the smaller old/new
extent. Growing inline storage beyond capacity copies that capacity into a heap
allocation; shrinking a heap allocation keeps it on the heap. Growing an empty
owner also allocates on the heap. Reset selects inline storage again when it fits.
Count-to-byte multiplication is checked before allocation or release.

Moving inline storage copies its full capacity as bytes, as the original did;
only initialized elements may be observed afterward. Heap moves transfer the
allocation without copying. Both paths now leave the source pointer null. Move
assignment remains unavailable. Const observation and temporary-owner borrowing
are constrained at compile time, without runtime borrow state.

Measured M4 macOS arm64 Release layouts match the reference:

| Instantiation | Original bytes | Candidate bytes |
|---|---:|---:|
| `<1, uint8_t>` | 16 | 16 |
| `<3, uint32_t>` | 24 | 24 |
| `<4, double>` | 40 | 40 |
| `<2, AlignedRecord>` | 40 | 40 |

The test's AlignedRecord contains two uint64 fields and requests the platform's
`std::max_align_t` alignment. These are host measurements, not universal ABI sizes.

## Verification

Non-Apple LLVM 23.1.2 on M4, sequential builds:

- Both candidate and reference Release profiles pass all 38 tests.
- The fixture checks 1,024 reset/reallocation transitions over four element types,
  including inline shrink/growth, inline-to-heap growth, heap shrink, empty growth,
  alignment, initialized-prefix preservation and reset back to inline storage.
- Move fixtures cover empty, fully initialized inline and heap storage, independent
  source reuse, and partially initialized inline storage. Source-empty assertions
  on inline moves are candidate-only because that is a deliberate behavior change.
- Nine rendering/mipmap artifacts and the serialized artifact match byte-for-byte.
- Release and Debug instrumented deterministic suites report PASS.
- A fresh relocated installed-package consumer and CPU source/symbol checks pass.
- Seven selected GUI.Forms integration tests pass against fixture
  `8d1f2be06a40c5da78bb75e45cbe1fecec2f6b62`, using its LLVM 22.1 toolchain.

The initial test sequence was refined to exercise inline growth before emptying
storage. The corrected fixture and complete verification were rerun; final records
are `build/auto-stmalloc-verified-gates.log` and archived
`build/auto_stmalloc_verified_gate.sh`. Earlier verification is retained in
`build/auto-stmalloc-gates.log`; integration is recorded in
`build/auto-stmalloc-gui.log`. The converted header and fixture have zero house-style
or lifetime spelling findings. The remaining mixed header has 12 spelling
findings/review candidates and zero lifetime spelling findings. Instrumented diagnostics remain reserved for the
owner's independent reviewer. No speed improvement is claimed. The constructed
inline array owner and other mixed-template helpers still require conversion.
