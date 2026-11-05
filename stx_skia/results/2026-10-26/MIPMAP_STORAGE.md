# Mipmap storage and construction — 2026-10-26

SkMipmap.cpp/.h and SkMipmapBuilder.cpp/.h are converted to explicit owners,
named dimensions/extents, checked layout arithmetic and documented lifetime
contracts. The original source/header versions remain in the reference build.

## Layout and ownership

The size pass walks successively halved dimensions once, replacing repeated
ComputeLevelSize/ComputeLevelCount calls. Checked products and sums establish
the complete allocation extent before invoking a storage factory. The original
signed-32-bit allocation limit and extra descriptor-sized tail allowance remain.
No smaller allocation or timing gain is claimed in this batch.

The returned client owner holds one SkCachedData reference; all constructed
level descriptors borrow its block. Descriptors are constructed as complete
Level records and have null color spaces. The enclosing object owns the color
space and adds it to returned descriptor copies. This matters because a
discardable block may disappear without running descriptor destructors.
Initial owner fields are explicitly initialized. Cache/client transitions still
control locking and the published descriptor pointer through onDataChange.

Format/alpha enums, source metadata, row stride and computed source extent are
validated before reading pixels. An unsupported downsampler is rejected before
allocating the mip block. Metadata-only construction does not require readable
source storage and leaves every level's pixels for the caller to initialize.
Null builder attachment now returns null. Borrowed builder views reject temporary
builders. Failure and fallback behavior are documented separately from success:
incompatible levels return the original image, and an empty builder can trigger
default mip generation through withMipmaps, as before.

The half-level bias, nearest rounding, level dimensions, scalar precision,
downsample procedures, color conversion and pixel contents are unchanged.
extractLevel still clamps to the last level; direct getLevel rejects invalid
indices. No new cache policy or custom sampling algorithm is introduced.

## Verification

The new mipmap contract test builds 1,152 nontrivial chains: every width and height
from 1 through 17, in A8, RGB565, RGBA8 and RGBA F16, excluding the four 1x1 cases.
It checks level dimensions, compact rows, scales, color-space retention, source
preservation, index rejection and terminal extraction. The generated mip pixels
are compared against the original reference, including odd and one-axis chains.
A separate RGBA fixture checks known 2x2 averages independently. Builder tests
fill custom levels and retain them through image attachment and builder release.
Discardable-storage fixtures check release/relock, lost storage and final unlock.
Allocator failure is explicit; malformed and excessive input extents are
candidate-only rejection tests and never invoke the allocation callback.

Both release builds pass all 20 tests. The new mip artifact contains 103,800 bytes
with hash 13229200515257983872 in each profile; it and the seven existing rendering
artifacts compare byte-for-byte. Release and Debug instrumented suites report
PASS, with previous integrity logs preserved and details unread. The fresh
relocated installed consumer passes. These gates run on M4 macOS arm64 with
non-Apple Clang 23.1.2.

The pinned isolated GUI.Forms fixture passes all seven selected tests using
non-Apple LLVM 22.1.8. The authoritative GUI.Forms checkout is unchanged. Five
converted production/test files pass spelling and lifetime checks without findings.
All four preserved mipmap/builder source/header files match the initial import.

Local records are build/mipmap-storage-candidate.log,
build/mipmap-storage-gates.log and build/mipmap-storage-gui.log. The driver is
retained as build/mipmap_storage_gate.sh. Native Linux/Windows verification on
this commit, full conversion and the owner's independent review remain separate.
The imported HQ/drawing downsamplers are still separate conversion work.
