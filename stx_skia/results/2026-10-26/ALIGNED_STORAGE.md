# Fixed aligned element storage

SkAlignedStorage.h is converted. Its original is preserved byte-for-byte from
import commit `490c9bd4a97884d268a5b3f9924748ebf31954bb` under baseline/skia;
the reference include priority selects it without changing the production path.

## Representation and contract

The only instance storage remains `alignas(T) std::byte[sizeof(T) * N]`.
There is no live count, allocation, registry, destructor loop or synchronization
in this helper. Its container constructs and destroys elements. The empty
user-provided constructor is retained, so value-initializing this raw-storage
base does not introduce an unnecessary zero-fill pass.

Positive capacity and representable byte extent are now compile-time conditions.
The retained consumer is STArray, whose requested capacity is already positive.
The header does not implement a zero-capacity extension. The existing const and
mutable addresses remain available from live owners; borrowing from a temporary
is rejected at compilation. The constant byte extent is returned directly.

This change does not rewrite TArray's construction, relocation, alias handling or
exception behavior. Those remain separate conversion work. In particular, the
existing distinction between trivial byte relocation and actual move construction
must be assessed against the retained element types before changing it.

## Deterministic fixture

The fixture constructs and destroys 1,152 objects across repeated raw-storage
reuse, including byte/word scalars, 64-byte-aligned objects and nontrivial tracked
objects. Compile-time assertions check exact storage size/alignment, immovability
and the candidate's temporary-borrow rejection.

Another 289 schedules exercise STArray at inline-capacity boundaries and beyond:
copy, move, swap, clear, shrink, reuse of moved-from owners and nontrivial element
destruction. Each schedule checks exact element values and tracked live counts.
The candidate and reference report the same 24/32/272-byte layouts for uint32
STArray capacities 1/4/64 on the M4 release build. These schedules establish this
storage integration boundary, not correctness of every remaining TArray operation.

## Verification

Sequential M4 macOS arm64 builds use non-Apple LLVM 23.1.2:

- Candidate and original-reference Release profiles pass all 43 tests.
- The storage fixture's results and layouts match the reference.
- Nine rendering/mipmap artifacts and the serialized artifact match byte-for-byte.
- Release and Debug instrumented deterministic suites report PASS.
- Fresh relocated installed-package consumer and CPU source/symbol checks pass.
- All seven selected GUI.Forms integration tests pass against fixture
  `8d1f2be06a40c5da78bb75e45cbe1fecec2f6b62`, using its LLVM 22.1 toolchain.
- Changed C++ passes house-style and lifetime spelling checks.

Records: `build/aligned-storage-gates.log`, `build/aligned-storage-gui.log` and
archived `build/aligned_storage_gate.sh`. Instrumented diagnostics remain reserved
for the owner's independent reviewer. There is no measured speed claim. No
runtime state or operation is added by this header conversion.
