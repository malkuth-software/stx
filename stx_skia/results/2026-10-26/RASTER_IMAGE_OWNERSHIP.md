# Raster image ownership and conversion — 2026-10-26

SkImage_Base.cpp/.h, SkImage_Raster.cpp/.h and SkImage_RasterFactories.cpp now
use explicit local types, named owners/results, visible callback transfers and
checked failure paths. SkImage_Lazy.cpp receives only a constructor-signature
adaptation; its broader conversion and the public SkImage facade remain work.
The base's GPU-era compatibility stubs also remain pending that facade cleanup.

## Removed work and ownership

Capture removal left SkImage_Base with a small array of pixel-storage owners and
an unused accessor. The raster bitmap already owned that same pixel reference.
No retained caller reads the array. Removing it eliminates the second reference
and its array storage. Bitmap and mipmap ownership are explicit; the data-backed
constructor transfers one data reference to the bitmap's release callback.

On M4/macOS arm64 the base object is 48 bytes versus 80 in the reference; the
raster image is 104 bytes versus 136. The deterministic test checks that the
candidate data-backed image's pixel ref has one owner, whereas the original
capture bookkeeping supplies another. This is structural work and object-layout
evidence, not an allocation-throughput or drawing-speed benchmark.

Raster onReadPixels previously copied its SkBitmap before invoking a const read.
It now reads directly from the existing bitmap, removing that temporary owner
and its retain/release operations. Read clipping, conversion and stride behavior
are unchanged. Passing shared storage does not establish a supported overlapping
copy; the tests supply disjoint destinations.

Subset identity remains distinct from the underlying full pixel-ref identity.
Mipmapped images still receive independent copied pixels/new identities, avoiding
cache-key aliasing and interference with surface copy-on-write. Existing mipmap
levels are copied only after checking each level and copy result; failures use
the existing null/regeneration path. Color conversion now checks the pixel-copy
result before publishing an immutable image. Fallback callback reads use
fallible allocation and one null-result callback on failure. No allocation-failure
injection was performed; generator success/failure and invalid conversion tests
exercise ordinary failure paths. Numerical kernels and precision are unchanged.

The as_IB helpers reject temporary owning handles, preventing a returned borrow
from immediately outliving its owner. Existing retained callers compile with the
borrow restriction.

## Remaining compressed-texture decoder removed

The ordinary codec removal had not removed RasterFromCompressedTextureData and
SkCompressedDataUtils, which contain CPU ETC/BC1 decoding. Their only retained
production caller was that factory. GUI.Forms supplies decoded pixels and does
not use this path. The factory declaration/definition, compression-type header
and decoder implementation/header are now reference-only. This closes that
remaining decoder path; it corrects the earlier overly broad codec-exclusion
summary. Candidate compilation units fall from 253 to 252 on macOS arm64;
reference compilation remains unchanged.

## Verification

A new raster-image contract test covers:

- 9×7 RGBA8888 images with 52-byte rows and exactly the required last-row extent;
  mutable-input copies, immutable sharing, explicit release callbacks, rejected
  strides and undersized data, and data lifetime after the caller drops its owner.
- Full-image versus subset identities, subset origins, immutable legacy bitmap
  views and continued image access after source bitmap handles are reset.
- Generated mipmaps, independent full mipmap copies, byte equality of every
  copied mip level, partial subsets with/without mipmaps and invalid bounds.
- RGBA-to-BGRA conversion, invalid destination format, color-space
  reinterpretation preserving raw bytes, and negative-offset reads preserving
  destination borders and padding.
- Deferred CPU generators that succeed or fail, exercising the base callback
  fallback and checking exactly one result callback with the expected pixels.

M4 macOS arm64, non-Apple Clang 23.1.2: candidate and original reference pass all
18 tests. Seven existing rendering artifacts compare byte-for-byte. Release and
Debug instrumented suites report PASS; prior logs are preserved and their detailed
contents remain unread. A fresh relocated installed consumer passes. Seven
selected GUI.Forms tests pass with pinned non-Apple LLVM 22.1.8, including external
text, shadows, damage and drawing APIs. The authoritative GUI.Forms checkout is
unchanged. Candidate archive inspection finds no definitions for the removed
compressed-texture decoder/factory. Six newly
converted implementation/header/test files have zero spelling/lifetime findings.
The two adapted imported files retain 58 spelling findings and are not claimed
fully converted; their lifetime check has zero findings. Original copies of all
newly isolated sources/headers match the initial import, with upstream notices.

The sequential gate driver is retained at build/raster_image_gate.sh. Its log is
build/raster-image-gates.log; the initial candidate record is
build/raster-image-candidate.log. Integration and symbol records are
build/raster-image-gui.log and build/raster-image-symbols.log. Native cross-platform verification on this
commit, the independent review and full library conversion remain separate gates.
