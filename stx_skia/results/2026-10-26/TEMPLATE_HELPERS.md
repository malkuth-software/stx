# Remaining template helpers and raster address offsets

## Pruning and ownership

The CPU compile dependencies and retained source uses were inspected before
removing the generic function-object adapters, cleanup-callback owner, index-based
array factory and typed-after helper from SkTemplates.h. The unused SkBitSet
header is now reference-only. The macOS compile closure does use SkUniqueCFRef;
its cleanup therefore remains, with a named Core Foundation deleter replacing
the generic callable adapter. UniqueVoidPtr similarly uses a named sk_free deleter.
Its measured layout remains eight bytes in candidate and reference on M4.

The original SkBitSet and SkUniqueCFRef headers are preserved byte-for-byte from
import commit `490c9bd4a97884d268a5b3f9924748ebf31954bb`. The original SkTemplates
header was already preserved. Candidate source-scope checks reject reintroduction
of the removed generic helpers and the unused bitset header.

The retained scalar helper preserves negative zero and NaN behavior rather than
substituting a different absolute-value operation. Integer magnitude must be
representable. The ignore-unused helper still evaluates its argument normally.
The byte-offset helper preserves cv qualification and admits accesses within the
caller's allocation, including a zero offset from null. It adds no bounds metadata.
The extracted array-owner headers remain included for existing consumers.

## Raster-tail address calculation

Raster tails temporarily redirect a context to scratch storage while keeping
original image coordinates. The previous implementation formed a pointer before
the scratch allocation and later added image coordinates to it using typed pointer
arithmetic. The conversion instead represents the temporary bias and its
cancellation with uintptr_t arithmetic on the supported native CPU platforms.
A stage forms its actual access pointer after adding the byte displacement.

This changes patch_memory_contexts, restore_memory_contexts and ptr_at_xy together.
It adds no context fields, allocation, callbacks or per-element validation. Load
and store byte counts and context restoration remain unchanged. This is a native
flat-address contract, not a portability claim for capability-pointer machines.
The surrounding large raster-pipeline header remains unconverted.

## Verification

Sequential M4 macOS arm64 builds use non-Apple LLVM 23.1.2:

- Both candidate and reference Release profiles pass all 40 tests.
- The helper fixture checks scalar bit patterns, positive/negative in-allocation
  offsets, null/zero offset, ownership transfer and named cleanup. On macOS, a
  Core Foundation array's release callback confirms its owner releases it once.
- The expanded raster fixture checks 4,224 padded-row cases: widths 0 through 65,
  heights 0 through 3, x origins 1/3/67/129, direct and compiled execution, and both
  precision modes. It checks exact pixels, untouched surrounding storage and
  restored context pointers after execution. Larger origins exercise nontrivial
  scratch address biases. Broader format cases remain in the conversion/render
  suites; this specific fixture uses RGBA8.
- Nine rendering/mipmap artifacts and the serialized artifact match byte-for-byte.
- Release and Debug instrumented deterministic suites report PASS.
- Fresh relocated installed-package consumer and CPU source/symbol checks pass.
- Seven selected GUI.Forms integration tests pass against fixture
  `8d1f2be06a40c5da78bb75e45cbe1fecec2f6b62`, using its LLVM 22.1 toolchain.

Local evidence: `build/template-helper-dependencies.txt`,
`build/template-helpers-gates.log`, `build/template-helpers-gui.log`, and archived
`build/template_helpers_gate.sh`. Converted headers/tests and the three extracted
raster functions pass spelling/lifetime checks. The entire raster-pipeline header
is not claimed converted. Instrumented diagnostics remain reserved for the owner's
independent reviewer. No timing improvement or general rendering equivalence
beyond the recorded corpus is claimed.
