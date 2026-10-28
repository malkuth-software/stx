# Pixmap views and scaling — 2026-10-25

This batch converts SkPixmap.h, SkPixmap.cpp and SkPixmapDraw.cpp. Original
versions remain under baseline/skia and are selected by the reference build.
The view still borrows pixels and retains its color-space metadata by reference
count. No pixel ownership or asynchronous work is added.

## Changes

- Explicit byte offsets replace signed offset shifts and compound pointer forms.
- Metadata references are lvalue-only; temporary views cannot return a dangling
  reference to their owned SkImageInfo.
- Named color conversion and typed fill kernels replace captured/anonymous code.
- Fills select their format once and retain row stride, column offset and count
  before writing. Floating opacity scans form each live row address, without
  advancing past a minimally allocated last row.
- Scaling names its bitmap, image, shader, surface and borrowed canvas lifetimes.
  Its existing unpremultiplied color and cubic-clamping convention is preserved.

The constructor/reset remains a low-level caller-contract boundary: it does not
acquire storage or prove an allocation's actual capacity. Typed getters require
valid coordinates, format and aligned live pixels. Read/fill operations validate
metadata and stride before accessing storage; opaque-only formats need no pixel
inspection. No new speed claim is made in this batch.

## Intentional corrections

RGBA1010102's float-color getter previously interpreted the packed channels as
BGRA, reversing red and blue. It now honors the declared channel order. Dedicated
red/blue fixtures check RGBA and BGRA independently. Only the corrected RGBA
float getter is excluded from the common-output comparison; its integer getter
and all other supported getters remain in the reference comparison.

Generic addresses on missing/unknown storage return null. Nonempty read/fill
operations reject missing storage and malformed metadata. Empty fills fail;
empty opacity scans are true for known formats, since they contain no nonopaque
pixels. Unknown formats return false. Null subset-result pointers fail. Subset
byte offset overflow fails without publishing the result.

The upstream XR color-getter omissions are retained and excluded from color
getter calls. Their alpha getter, fills and opacity behavior remain exercised.
This does not claim implementation of an absent upstream HDR accessor.

## Verification

M4 Mac mini selected by m4host. Standalone: non-Apple Clang 23, C++20,
Release and Debug. GUI.Forms fixture: 8d1f2be0 with LLVM 22.1.8.

- Eight deterministic release tests pass against each profile.
- Pixmap common output: 410,459 bytes match; hash 5122214914345034535.
- Existing 21,420 pixel conversions still match byte for byte.
- All 24 rendered cases still match; hash 11616627833029224064.
- Complete Release instrumented suite: PASS.
- Complete Debug instrumented suite: PASS.
- Seven selected GUI.Forms rendering/integration tests: passed.
- Fresh relocated installed consumer: built and ran, including consumer-owned RTTI.
- Four changed production/test files: zero spelling and lifetime-checker findings.

Pixmap coverage includes 84 format/alpha combinations, padded and minimally
sized storage, pixel getters and alpha/opacity, clipped fills, metadata-only and
in-place subsets, and 192 independently checked clipped-read positions. Thirty
scaling cases cover premultiplied and unpremultiplied inputs, identity/up/down
scaling, nearest, bilinear, Mitchell, Catmull-Rom and linear mipmap sampling.
Candidate-only assertions cover the stated behavior corrections and compile-time
borrow lifetime restriction.

Reproduce using tools/build_native.sh baseline and candidate, then compare
render.raw, conversions.raw and pixmap.raw with cmake -E compare_files. Native
CI now performs all three comparisons. Instrumented and GUI commands remain in
AGENTS.md. Detailed memory integrity diagnostic files were retained, not read.

Generated local logs: pixmap-verification.log, pixmap-alignment-verification.log,
pixmap-final-verification.log, pixmap-gui.log and pixmap-installed.log.
Full library conversion, cross-platform gates for this revision, and the owner's
independent review remain separate work.
