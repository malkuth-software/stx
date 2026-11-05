# Pixel ownership conversion

The next conversion covers SkData's header, SkPixelStorage, SkPixelRef,
SkMallocPixelRef, SkIDChangeListener and their required headers. It preserves
pixel arithmetic, atomic ordering and callback order. Original implementations
and headers are selected in the baseline build from retained source copies.

The new pixel_ref_contract exercises zeroed allocation, explicit padded strides,
short-buffer rejection, retained data, callback release, dropped registrations,
one-shot invalidation, revocation cleanup, reset/destruction and shared identity
reads from eight threads. The latter exercises atomic IDs and counters; it does
not permit or test concurrent writes to the image's pixels.

macOS M4, non-Apple LLVM 23.1.2:

- Four standalone tests pass against the original and converted implementations.
- All 24 rendered outputs remain byte-identical to the original baseline.
- Instrumented complete standalone suite: **PASS**.
- House spelling and lifetime checkers: 19 files, zero findings in each checker.

No new timing advantage is claimed for this conversion. It makes ownership,
callback execution and state initialization explicit; the prior benchmark record
remains evidence for the earlier measured implementation only.

The first CI run independently passed the release suite, pixel comparison and
relocated installation on both Linux arm64 and x64. Linux arm64's instrumented
build then failed to link because client UBSan instrumentation expected RTTI
that the Skia archive does not provide. A derived listener in the new macOS test
reproduced the missing RTTI contract. The first fix exported no-RTTI to consumers, but the GUI.Forms integration build
rejected that: its own adapter uses dynamic_cast. The corrected contract keeps
no-RTTI private to Skia, applies it to the derived-listener test and excludes
vptr instrumentation at this interface. Consumer RTTI remains enabled. This
preserves the library and consumer contracts; RTTI-dependent instrumentation is
explicitly outside this check.

Windows CLANG64 failed compilation because upstream headers use __forceinline.
The existing GUI.Forms GN profile supplies -fms-extensions, and the CMake target
now supplies that same requirement to the library and its consumers. These are
build failures, not runtime review findings. Corrected native CI remains required.

After the corrected RTTI contract, all four reference and candidate tests pass,
the instrumented suite returns PASS, and all seven selected GUI.Forms renderer
tests pass with LLVM 22.1.8. A freshly relocated installed consumer also builds
and runs; it explicitly checks dynamic_cast on application-owned types alongside
the image-storage/drawing calls. Native corrected CI is still pending.
