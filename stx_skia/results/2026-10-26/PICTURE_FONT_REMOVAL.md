# Picture and font dependency removal — 2026-10-26

GUI.Forms supplies decoded images and finished glyph coverage. It has no picture
recording/playback or Skia font engine consumer. Removing only canvas text drawing
left picture serialization, drawable snapshots and global caches retaining the
font implementation. This change cuts that remaining dependency chain.

## Candidate scope

Removed from candidate compilation and headers:

- Picture recording, playback and serialization; record storage/optimization,
  bounding-box hierarchy/R-tree, multi-picture document output and capture.
- Picture-backed deferred images, image generators and shaders. Canvas picture
  entry points and drawable picture snapshots are removed, not empty drawing stubs.
- Font/typeface/glyph/text-blob/strike/scaler implementations and cache controls,
  OpenType table helpers, font gamma tables, CoreText/DirectWrite helpers and
  uncompiled platform font backends, including Fontations sources.
- Picture/typeface/slug serialization callbacks, typeface reader tables and writer
  ownership. The retained image callbacks remain explicitly synchronous.

Surface canvas construction no longer tries to wrap the owned canvas in capture
state. Surface image snapshots and copy-on-write remain. Tiled-image cache keys
retain their existing raster layout and fallback identity; only the deleted
picture-backed subtype branch is removed. The cache slot layout is unchanged.
SkGraphics initializes the same CPU dispatch and preserves resource-cache
controls; its purge/dump operations no longer visit font caches.

SkPicturePriv retains historical version identifiers used by ordinary paint,
shader and vertex deserialization, but no picture API or implementation. Generic
flattenable/image serialization and other unused compatibility interfaces remain
separate cleanup work. SkDrawable's direct drawing remains at this boundary.

SkGraphics.cpp now uses explicit intermediate results and execution order.
SkSerialProcs.h states the image callback lifetime contract. The other adapted
imported files are dependency edits, not full house-style conversions.

## Reference integrity and evidence

Original copies are reference-only under baseline/skia with upstream notices.
The original source selection remains platform-aware, including macOS-only font
helpers. All 234 newly retained reference
files match initial import 490c9bd4a97884d268a5b3f9924748ebf31954bb byte-for-byte.

M4 Mac mini, macOS arm64, non-Apple Clang 23.1.2/C++20: the candidate compiles
253 units versus 305 before this change and 492 in the original reference.
The candidate archive has zero defined class symbols matching SkFont, SkTypeface,
SkGlyph, SkStrike, SkTextBlob, SkScalerContext, SkPicture, SkCapture, GlyphRun or Slug.
This checks the actual archive in addition to the source-list removal.

All sixteen deterministic tests pass in candidate and reference profiles. All
seven complete output artifacts compare byte-for-byte, including retained blur
masks, shader masks, gradients, sampling, layers and pixel conversion. The external
A8 coverage test adds compile-time checks for picture-drawing and font-cache API
absence in the candidate and presence in the reference. It initializes dispatch
twice and purges retained caches after the independent coverage checks.

Release and Debug instrumented suites report PASS; their diagnostics remain unread
and earlier logs were preserved. A fresh relocated installed consumer builds and
runs. Seven selected GUI.Forms tests pass with pinned LLVM 22.1.8, including
prepared text, shadows, damage and drawing API checks. Verification is recorded
in build/font-prune-gui.log; the canonical GUI.Forms checkout remains unchanged.

The three new/converted C++ files pass both style and lifetime spelling checks.
Across 29 adapted/new files, 844 imported spelling findings remain; the lifetime
checker reports zero. Neither checker establishes full semantic conversion.

Records: build/font-prune-candidate-4.log, build/font-prune-baseline.log,
build/font-prune-gates.log and build/font-prune-gui.log. No rendering-speed gain is
claimed from the source-count reduction. Native CI for this commit, the owner's
independent review and the remaining full conversion are separate gates.
