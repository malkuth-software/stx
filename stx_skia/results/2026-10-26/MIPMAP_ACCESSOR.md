# CPU mipmap selection — 2026-10-26

SkMipmapAccessor.cpp/.h now name acquisition, selection and scale computation
without capturing lambdas or implicit local types. Bitmap and mipmap owners
precede borrowed level views; copied views remain valid until the owning arena
is reset or destroyed. Access through a temporary accessor is rejected. The
image and input matrix are needed only during construction.

Image-provided levels, cache lookup, and mipmap construction retain their order.
Nearest selection retains rounding; linear selection retains floor and the
fractional blend weight. The existing half-level sharpening bias is unchanged.
A missing upper level falls back to the source image; a missing lower level
leaves the selected upper level with zero lower weight. An unreadable source
returns null from Make. Partial acquisition is released by arena destruction.
No sampling arithmetic, cache admission or pixel-generation algorithm changes.
No throughput or memory-size improvement is claimed.

The original accessor header joins its already preserved implementation in the
reference build. It is copied byte-for-byte from the initial import. Existing
sampling callers retain the paired pixel-view/matrix interface. Their broader
shader and pipeline conversion remains separate work.

## Deterministic coverage

The raster-image contract test adds 13 selection cases with both attached and
cache-generated mipmaps (26 cases), including odd dimensions, base selection,
nearest and linear levels, unequal axis scales, singular scale, and requests
past the last level. Expected level dimensions, weights and scale matrices are
specified without calling the selection algorithm. Repeated acquisition must
reuse the same upper pixel storage. Pixel reads remain valid after source image
release and resource-cache purge. A one-pixel image checks failure to generate
mipmaps; three failed-generator cases check null results in every sampling mode.
These are deterministic ownership/selection tests, not an allocation-exhaustion
or cache-contention study.

The three converted source/header/test files pass spelling and lifetime checks
without findings. Mipmap construction, bitmap caching and the image shader's
remaining imported implementation are not marked converted by this change.

## Verification

M4 macOS arm64 with non-Apple Clang 23.1.2: candidate and original reference
pass all 19 tests; all seven rendering artifacts compare byte-for-byte. Release
and Debug instrumented suites report PASS. Their previous integrity logs were
preserved; detailed diagnostics were not read. A fresh relocated installed
consumer also passes. The pinned isolated GUI.Forms fixture passes seven selected
tests using non-Apple LLVM 22.1.8. The authoritative GUI.Forms checkout is unchanged.

Local records: build/mipmap-accessor-candidate.log,
build/mipmap-accessor-gates.log and build/mipmap-accessor-gui.log. The sequential
driver is retained as build/mipmap_accessor_gate.sh. Native Linux/Windows checks
on this commit and the owner's independent review remain separate gates.
