# Allocation and byte primitives

SkMalloc.h, SkMalloc.cpp and the native SkMemory_malloc.cpp port are converted.
Their original implementations are preserved byte-for-byte from import commit
`490c9bd4a97884d268a5b3f9924748ebf31954bb`; the reference source list selects both
original implementation units and its include priority selects the original header.

## Policy and representation

The native implementation remains a thin interface to malloc, calloc, realloc,
free and the existing platform allocation-size queries. There are no new owners,
registries, locks, allocation headers or persistent counters. The null-free fast
path remains. Zero-byte allocation follows libc and can return null or a freeable
allocation; no bytes may be accessed. Reallocation to zero frees and returns null.
Normal reallocation preserves the smaller extent without initializing growth.

The two flag values remain 1 and 2, with unsigned flag inputs and ignored unknown
bits as before. The historical THROW spelling denotes termination on nonzero
allocation failure in this port, not C++ exception throwing. Failure procedures
are named and the public fatal entry is declared non-returning. Debug diagnostics
remain failure-only; ordinary allocation does not log.

Count products are checked using the existing SkSafeMath implementation before
allocator calls. A fallible product overflow now returns null immediately; fatal
product overflow terminates immediately. Previously overflow became SIZE_MAX and
was passed to the allocator. This avoids submitting a fabricated request and
keeps failure policy at the API boundary. No oversized allocation is needed to
exercise the new fallible cases.

Native allocation-size queries remain unchanged: malloc_size with the existing
requested-size lower bound on macOS, malloc_usable_size on Linux, and _msize on
Windows. The unrelated Android allocator controls and special review-build
allocation-limit/exit overrides are removed from the candidate; originals remain
in the reference. The admitted targets are Windows, macOS and Linux.

Zero-length byte helpers admit null. Nonempty memcpy ranges must be disjoint;
memmove admits overlap. Compare preserves the libc ordering convention. The
implementation still dispatches directly to the standard byte primitives and
performs no range tracking.

## Verification

Sequential M4 macOS arm64 builds use non-Apple LLVM 23.1.2:

- Both candidate and reference Release profiles pass all 41 tests.
- The new fixture covers 209 allocation/reallocation cases: zero and nonzero
  sizes, byte products, zero initialization, fundamental alignment, allocation-size
  queries, retained prefixes across growth/shrink, and release on zero resize.
- Three candidate-only overflowing products return null through the fallible API.
  Reference tests do not submit enormous requests or claim the old implementation
  short-circuits them. Fatal termination paths are established by source review,
  not deliberately executed by this fixture.
- 5,577 byte-operation cases compare overlap moves and disjoint copies against
  an independently assembled expected buffer, including untouched surrounding
  bytes, clearing, ordering and null/zero-length calls.
- Nine rendering/mipmap artifacts and the serialized artifact match byte-for-byte.
- Release and Debug instrumented deterministic suites report PASS.
- Fresh relocated installed-package consumer and CPU source/symbol checks pass.
- Seven selected GUI.Forms integration tests pass against fixture
  `8d1f2be06a40c5da78bb75e45cbe1fecec2f6b62`, using its LLVM 22.1 toolchain.

Local records are `build/allocation-primitives-gates.log` and archived
`build/allocation_primitives_gate.sh`; integration is recorded in
`build/allocation-primitives-gui.log`. Instrumented diagnostics remain reserved
for the owner's independent reviewer. All converted sources and the fixture pass
house-style and lifetime spelling checks. No speed improvement is claimed. The
alignment helpers and remaining container/raster implementation still require
conversion; this is not completion of the full library.
