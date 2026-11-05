# Optional drawing mipmap conversion — 2026-10-26

The drawing-based downsampler is converted and tested independently. Production
continues to use the HQ downsampler; this change neither switches algorithms nor
claims a speed improvement.

## Implementation and reference

The converted source names the sampling choice, uses explicit types and draw
state, and declares storage/state before the draw context that borrows them.
Empty or null views return before division. Failed source bitmap wrapping leaves
the destination unchanged. The internal caller supplies complete, disjoint pixel
buffers with valid halved dimensions. No pixel ownership is transferred.

Exactly halved width and height use bilinear sampling. Other shapes, including
odd dimensions and single-axis reductions, retain Catmull–Rom interpolation.
The paint uses source replacement. Pixel arithmetic and sampling choices remain.

The inactive optional source at the original import did not compile against its
own current interfaces: it named SkDraw instead of skcpu::Draw and used the older
five-argument drawBitmap call. The candidate uses the current name and explicit
null mipmap argument. The original source is preserved byte-for-byte under
baseline/skia. tests/mipmap_draw_reference.cpp supplies only a class/signature
adapter and includes that original. This is an algorithm comparison with an
interface adapter, not a claim that the original optional configuration built
unaided. The initial compiler failure is retained in the local build log.

## Deterministic checks

The test executable directly supplies the alternate factory. A centered red
impulse in a 3×3 opaque-black field must reduce to pure red; HQ would produce a
red value of 63. This distinguishes the selected implementation before comparing
outputs and prevents accidentally testing the default factory twice.

Widths and heights 1 through 17, excluding 1×1, cover RGBA8, BGRA8, A8 and
RGBA_F16 with constant and varying-color/alpha inputs. The 2,304 cases compare
whole chains against the reference, padded first-level output against the chain,
unchanged input storage, untouched row padding and constant-color preservation.
Candidate-only empty-view cases require unchanged output. The resulting artifact
is 235,280 bytes, hash 1962620760754339955, identical in both profiles.

## Verification

On M4 macOS arm64, non-Apple Clang 23.1.2:

- All 22 candidate and reference release tests pass.
- All nine rendering/mipmap artifacts compare byte-for-byte.
- Release and Debug instrumented suites report PASS; their detailed diagnostic
  files remain unread and preceding files are preserved.
- A fresh relocated installed-package consumer passes.
- Three converted source/test files pass spelling and lifetime checks; these
  checks do not replace semantic review.
- The preserved original source matches the import exactly.

Seven selected GUI.Forms tests pass against the pinned isolated fixture with
non-Apple LLVM 22.1.8. The authoritative GUI.Forms checkout is unchanged.

Local records: build/mipmap-draw-candidate.log (initial interface failure),
build/mipmap-draw-candidate-2.log, build/mipmap-draw-gates.log and
build/mipmap-draw-gui.log. The sequential gate driver is retained locally as
build/mipmap_draw_gate.sh. Full retained-source conversion, current-head native
platform gates, independent review and production integration remain separate.
