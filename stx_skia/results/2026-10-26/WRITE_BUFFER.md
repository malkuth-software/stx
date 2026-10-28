# Convert the binary writer and make reset independent

SkWriteBuffer.h and SkWriteBuffer.cpp now follow house style. Procedure records,
caller storage, snapshots, type names and short-lived mipmap image views have
explicit roles. The measured M4 Release writer remains 80 bytes. No fields,
registries, reference counters or synchronization mechanisms are added.

## Retained behavior and corrections

Ordinary primitive, array, string, paint, image and effect serialization preserves
its existing byte layout. Repeated effect names still use the existing dictionary;
its keys observe the process-lifetime names supplied by SkFlattenable. Image
serialization still uses the supplied synchronous callback or existing encoded
data. Mipmap callbacks run while the owning image/mipmap remains alive. No codec
is added. Returned snapshots own independent copies of written bytes.

Reset now clears the existing name dictionary as it resets byte storage. A new
stream therefore starts with an inline name, rather than an index referring to a
name in the previous stream. This is a deliberate correction to reuse behavior.
The dictionary releases its slots on reset; the underlying SkWriter32 retains its
internal allocation under its existing policy. Nothing tracks names beyond the
stream that needs them.

An empty snapshot now uses SkData::MakeEmpty. Zero-byte raw writes and empty
writeToMemory calls avoid the lower-level null-storage path. Supplied initial
storage must be aligned and non-null for a positive capacity. Bulk record counts,
byte-length conversions, string-size arithmetic, type-name indices and effect
payload lengths receive explicit checks before the corresponding narrowing/write.
The existing void API treats invalid programmer-supplied extents as fatal contract
violations, consistent with its allocation failure policy; no new per-writer
error state is introduced. This does not claim conversion of the underlying
SkWriter32 allocator, capacity growth or raw storage implementation.

The unused virtual writeStream input operation and its concrete implementation
were removed in separate commit e628519. No retained source calls that operation;
remaining similarly named calls are stream-to-stream methods or excluded tools/PDF.
The original interface remains in the reference profile. This private-interface
removal intentionally changes its vtable; binary compatibility with excluded
writer subclasses is not promised. Output through writeToStream remains.

## Verification

M4 macOS arm64 with non-Apple Clang 23.1.2 Release; the isolated GUI.Forms fixture
uses its pinned LLVM 22.1 toolchain. The authoritative GUI.Forms checkout is unchanged.

- Both Release profiles pass all 33 tests.
- The 24-case effect-stream artifact remains 3,344 identical bytes,
  hash 16215645112662696255. Nine render/mipmap artifacts remain identical.
- New candidate reuse cases use 0-, 4-, 16- and 4,096-byte supplied capacities,
  repeated effect names, fallback growth, surrounding guards and output streams.
  Every reset output matches a separately constructed fresh writer. Owned snapshots
  remain unchanged after reset. Empty snapshots and zero-byte null-buffer calls
  are checked. The compile fixture rejects the removed input method.
- The reader fixture now also serializes and reconstructs an image and two mipmap
  levels through first-party test callbacks. Both profiles observe the three encode
  and three decode calls, expected dimensions and attached mipmaps.
- Release and Debug instrumented deterministic suites: PASS. Previous logs were
  preserved, diagnostics were not inspected, and no new randomized review ran.
- Fresh relocated installed-package consumer and seven selected GUI.Forms tests: PASS.
- Changed C++ passes house-style and lifetime checks. Source/archive checks reject
  the removed writer stream-input symbol.

The preceding 5b9965e head passes Linux x64, Linux arm64 and Windows CI. This batch
needs its own current-head native checks after publication. The original writer
header and implementation remain byte-for-byte equal to the initial Skia import
in baseline/skia. No serialization-throughput or rendering-speed gain is claimed;
this batch establishes conversion, correct reuse and unchanged ordinary output.

Reproduce with tools/build_native.sh for candidate/baseline/candidate-sanitize,
the ordinary artifact comparisons, relocated consumer and tools/verify_gui.sh.
The lower-level SkWriter32 and remaining private reconstruction methods are next
conversion work, alongside the larger unfinished raster/geometry implementation.
