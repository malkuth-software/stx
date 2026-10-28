# CPU image facade — 2026-10-26

SkImage.cpp and the public SkImage.h are converted to explicit types, named
owners/results and CPU-specific contracts. Documentation now describes actual
buffer ownership, borrowed views, shader retention, synchronous read callbacks,
scaling and deferred materialization instead of advertising removed codecs,
pictures and GPU images.

## Backend interfaces removed

Texture backing/size queries, protected-content queries, non-texture conversion,
unsupported YUV/YUVA callbacks, Graphite read stubs, image-context lookup and the
unused generating-surface notification are removed. Image kinds now admit raster
and deferred CPU images only. The generator's protected-content query is removed
with the image query; no retained caller uses it.

Canonical pixel reads and raster materialization take CPU arguments directly.
Existing public calls with a leading nullptr remain source-compatible through
std::nullptr_t overloads. A typed GPU context pointer cannot call those overloads,
and the image API no longer declares a GPU context type. The private virtual
reads and their bitmap-cache/device/mipmap callers omit the argument entirely.
This keeps a bounded public compatibility seam without carrying a fake backend
through the implementation.

Shader overloads converge on the explicit tile-mode/matrix-pointer implementation,
which retains one owned image handle. Scaling overloads converge on one CPU
implementation. The legacy makeScaled property parameter remains ignored, as in
the original; the header now says so. Likewise, the current color-conversion paths
do not promise to construct mipmaps from RequiredProperties. Neither limitation
is disguised as an implemented capability by the new documentation.

Pixel conversions, sample positions, precision, cache admission, mipmap fallback
and identity-preserving fast paths are unchanged. No speed or object-size
improvement is claimed for this facade change.

## Verification

The expanded raster-image test checks:

- Compile-time absence of texture/protected/YUV interfaces and rejection of a
  typed GPU context pointer, contrasted with the original reference.
- Bounds/dimensions and alpha queries; identity retention for unchanged scaling
  and already materialized raster images; invalid scaling dimensions.
- All three scaling overloads on a patterned 9×7 RGBA image reduced to 3×7,
  with independently specified nearest-neighbor samples. Direct scalePixels
  also checks row padding and the equal-size copy path.
- All eight normal/raw shader overloads, translated by one pixel, on clamp and
  repeat sampling with independently calculated expected source coordinates.
  Raw cubic sampling must reject the request.

The other existing tests retain image generation/cache/failure, ownership,
callbacks, subsets and mipmap coverage. The initial candidate build found an
implicit SkPaint declaration dependency in SkImageShader.h. Its header now names
that dependency explicitly; the failed compiler record is preserved in
build/image-facade-candidate.log. The corrected candidate passes in
build/image-facade-candidate-2.log.

Ten changed image implementation/header/test files pass the spelling and
lifetime checks without findings. Five adapted imported dependency files retain
128 spelling findings; they are not claimed fully converted. Their lifetime
check has zero findings. Original SkImage, mipmap accessor, special-image and
bitmap-cache source copies are isolated in the reference build at the initial
import. Existing original image headers were already preserved.

The sequential drivers are retained locally as build/image_facade_gate.sh and
build/image_facade_final.sh. The final record is build/image-facade-final.log;
the earlier gate is build/image-facade-gates.log. Instrumented suites report only
PASS/STOP, preserve preceding logs, and leave detailed diagnostics unread.

M4 macOS arm64 with non-Apple Clang 23.1.2: candidate and original reference
pass all 19 tests and compare all seven rendering artifacts byte-for-byte.
Final Release/Debug instrumented suites report PASS; the fresh relocated
installed consumer passes. Seven selected GUI.Forms tests pass with pinned
non-Apple LLVM 22.1.8, including prepared text, shadows, damage and drawing APIs.
The authoritative GUI.Forms checkout is unchanged; see build/image-facade-gui.log.

Archive inspection confirms removal of the image protected/non-texture/YUV
operations and GPU-context read signatures; see build/image-facade-symbols.log.
All four newly preserved reference implementations match the original import.

Full conversion, native cross-platform verification on this commit and the
owner's independent review remain separate gates.
