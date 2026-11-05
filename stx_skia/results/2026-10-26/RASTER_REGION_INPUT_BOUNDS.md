# Wrapped pixels and serialized regions: input bounds

This pass fixes two public input boundaries in the retained CPU library. The
reference profile keeps its original implementations. Candidate-only negative
cases express the corrected behavior without changing the reference workload.

## Failure and correction

`SkSurfaces::WrapPixels` accepted `SIZE_MAX` as a private marker meaning that
row bytes could be ignored. `SkBitmap::setInfo` rejected the same value, invoked
the caller's release callback, and left an empty bitmap. The surface constructor
ignored that failure, and the factory returned a nonnull surface. A very large
nonsentinel stride could also enter signed multiplication in the validator
before the bitmap rejected it. The factory now rejects the marker; the shared
validator rejects unrepresentable strides and computes the extent in unsigned
arithmetic. Invalid calls return null without invoking the callback.

`SkRegion::readFromMemory` cast a caller-supplied byte address to `int32_t*`
when validating complex runs. That imposed an undocumented alignment and object
lifetime requirement. The validator now reads serialized words through a
four-byte copy, while internally owned run arrays use direct typed reads. It
checks `count` against available bytes before multiplying, avoiding a 32-bit
size-product wrap. A serialized rectangle ending at the reserved run sentinel
is rejected, matching `setRect`. Failed reads preserve the destination region.

The run parser still performs a single validation pass before allocating its
owned run storage. No temporary full-size copy was added. The normal rendering
path and serialized bytes are unchanged; parser throughput was not measured.

## Verification

On the nearby M4 Mac mini, with Clang 23.1.2 for the native library:

- Candidate Release: 49/49 tests passed, including the new stride, unaligned
  region, malformed count, sentinel rectangle, and unchanged-region checks.
- Reference Release: 49/49 tests passed.
- Candidate Release with ASan and UBSan: `PASS`. Detailed diagnostics remain
  in the private build log and were not inspected here.
- Eight generated render and serialization artifacts match the reference byte
  for byte.
- CPU-only source/symbol scope: `PASS`.
- Relocated installed consumer: built and ran successfully.
- Seven selected GUI.Forms render and GUI.Drawing fixture tests: 7/7 passed
  with Clang 22.1.8 and its pinned macOS 14 runtime. The first configure
  attempt used Clang 23.1.2 and stopped at GUI.Forms' required LLVM 22 gate;
  it was discarded before the successful LLVM 22 build.
- The house-style spelling checker reports 137 existing findings over the four
  touched source/test files, equal to the prior revision; the lifetime checker
  reports zero. `git diff --check` passes.

The available native gates exercise macOS arm64. The count guard is established
by arithmetic review and the malformed-count test, but no 32-bit runtime was
executed. The broader source conversion and independent security review remain
open.
