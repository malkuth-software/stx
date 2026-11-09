# External glyph coverage, without Skia text drawing — 2026-10-26

GUI.Forms shapes and rasterizes text in its own text engine. Its renderer supplies
finished A8 coverage to this library. This change removes the unused Skia text
front end while preserving that image-composition path.

## Removed dependency boundary

Canvas simple-text, glyph, transformed-glyph, text-blob and slug entry points are
removed, together with their virtual forwarding, recording and capture hooks.
Canvas no longer owns or allocates its glyph-run builder collection. Bitmap
devices no longer contain a glyph painter; the internal Draw object no longer
inherits the glyph-painter interface. Ordinary bitmap drawing remains.

GlyphRun, SkGlyphRunPainter, SkDraw_text, SkTextUtils and SkOverdrawCanvas are
reference-only, including their headers. Font and text-blob interception queries
are removed because they reconstructed glyph runs. Serialized text drawing
operations invalidate the playback reader instead of pretending to draw text.
An unused glyph-shader inverse-matrix helper is removed with its caller.

The candidate compiles 305 source units versus 310 before this change and 492 in
the original reference. On macOS arm64, sizeof(SkCanvas) is 3,232 bytes versus
3,264 in the original. The removed initialization allocated a GlyphRunBuilder
and inserted its owning pointer into a vector for each canvas. This is structural
allocation evidence, not a timing or whole-process memory measurement.

This does **not** finish font removal. Picture serialization still owns text
blobs and typefaces; font, glyph and strike-cache internals remain compiled.
Removing that dependency chain is the next step. Adapted canvas/device/record
files remain imported code awaiting full conversion. The new regression test
follows house style; the adapted imported files retain existing spelling debt.

## Reference and checks

Original files remain under baseline/skia, selected by the reference CMake
profile. The 22 new reference files were compared byte-for-byte with the initial
Skia import, commit 490c9bd4a97884d268a5b3f9924748ebf31954bb. Existing original
copies were preserved. Upstream copyright and license notices remain.

The new external_glyph_mask test constructs a 16×16 A8 mask containing every
coverage value, with a 19-byte row stride. It draws into explicit RGBA8888
premultiplied pixels with destination padding. Four opaque primary/white paint
colors, with and without integer clipping, are checked against independently
calculated pixel values; destination padding must remain unchanged. Compile-time
checks require text-blob drawing to exist in the reference and be absent in the
candidate. No Skia font or text drawing is invoked by this coverage test.

M4 Mac mini, macOS arm64, non-Apple Clang 23.1.2/C++20: sixteen deterministic tests
pass for candidate and reference. All seven complete rendering artifacts compare
byte-for-byte, including blur masks, shader masks, gradients, sampling and layers.
Release and Debug instrumented suites report PASS. Earlier diagnostic logs were
preserved and their contents remain unread. A fresh relocated installed package
builds and runs its consumer. Final cleanup was followed by repeat candidate,
artifact comparison, instrumented and relocated-consumer checks.

Seven selected GUI.Forms tests pass using its pinned LLVM 22.1.8 toolchain,
including prepared text, shadows, damage and drawing API checks. Verification is
recorded separately in build/text-drawing-gui.log.
The authoritative GUI.Forms checkout is unchanged. Native Linux and Windows
checks on this commit and the owner's independent review remain separate gates.

Records: build/text-drawing-candidate-final.log, build/text-drawing-gates.log,
build/text-drawing-final.log and build/text-drawing-gui.log. House-style checking
of the 27 adapted/new C++ files reports 1,621 imported spelling findings; the
lifetime-rule checker reports zero. The new test alone has zero findings in
both checks. This batch does not claim those imported files are converted.
