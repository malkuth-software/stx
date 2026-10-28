# Deferred CPU images — 2026-10-26

SkImage_Lazy.cpp/.h and SkImageGenerator.cpp/.h now use explicit ownership,
locking, initialized validation state and named results. CPU deferred generators
remain supported: they supply pixels, not an embedded image decoder. Public
SkImage facade conversion remains separate work.

## Generator ownership and cache behavior

One shared owner holds the provider and its mutex. Provider metadata and identity
are immutable and may be read without locking; generation and provider callbacks
are serialized. The old pointer-like scoped wrapper, implicit pointer conversion
and anonymous output-check callback are replaced by explicit local locks,
references and a named assertion helper. Shared generator internals are private.

The generated-pixel cache retains its behavior: a cached hit is permitted with
either hint, while disallow prevents adding new entries. Successful generation
releases the provider lock before publishing the cache record. A failed record is
discarded without copying partial generated pixels into the caller's destination.
The last successful color-conversion image is retained under a separate mutex;
converted images share the provider but have their own identities. No provider
lock is acquired while constructing the conversion's immutable metadata.

Reinterpretation materializes pixels under the original color space and labels
the independent bitmap with the requested space. Subsets also materialize. Those
images can outlive the provider. The unused Validator color-space field is gone,
and rejected validation starts with a defined zero identity.

Generator pixel requests now reject invalid shapes/formats, invalid strides and
overflowing byte extents before entering the provider. The caller still owns the
actual buffer and must supply the stated extent; the API has no capacity argument.
Valid requests preserve provider semantics, including permissible conversion
rejection and partial destination writes on provider failure.

## Removed unsupported paths

No retained caller used the lazy image's YUV-plane access or GPU cache listeners.
Their generator APIs, plane cache, planar storage/layout types and implementation
are now reference-only. The texture-generator query and always-false GPU pixel
proxy are removed. Ordinary CPU pixel generation and bitmap-cache invalidation
remain. Color-matrix YUV arithmetic is separate and remains compiled where used.

The lazy image's GPU listener list, public generator-owner escape and unused
metadata accessor are removed. Its size is 80 bytes versus 152 in the original
on M4/macOS arm64. This cumulative comparison includes the preceding base-image
storage reduction; it is not wholly attributable to this batch. No throughput
improvement is claimed. Three compilation units are removed from the candidate.

## Deterministic evidence

A new deferred-image test checks:

- No eager generation; two disallow reads generating twice; allow adding one
  cache entry; subsequent allow/disallow reads hitting that existing entry.
- A provider that writes pixels and then reports failure: no partial output or
  cache admission, followed by successful retry and a cache hit.
- Reused color-conversion identity, separation from original encoded metadata,
  provider ownership across converted images, independent subset/reinterpretation
  images, and exactly one provider destruction after the final dependent image.
- Four workers, two images sharing one generator and 25 uncached reads each.
  Named workers own disjoint output buffers and join before image/state teardown.
  An atomic entry counter detects overlap; all 100 calls complete with none.
  This is a bounded concurrency regression, not exhaustive scheduling proof.
- Null/empty generator factories, invalid direct request rejection before the
  provider, output pixel values and padded-row preservation. Compile-time checks
  distinguish original plane/texture APIs from the candidate's removed APIs.

Candidate and original reference pass 19 tests on M4 macOS arm64 with non-Apple
Clang 23.1.2. Seven rendering artifacts remain byte-identical. Final Release and
Debug instrumented suites report PASS; a relocated installed consumer passes.
These are recorded in build/deferred-image-final.log. Instrumented output is PASS/STOP only; previous
logs are preserved and their detailed contents are not read. The initial gate is
build/deferred-image-gates.log; its validation preceded the final request checks.

The five changed implementation/header/test files have zero spelling and
lifetime-check findings. Original source/header copies match the initial import
and retain their upstream notices. The sequential drivers are retained locally
as build/deferred_image_gate.sh and build/deferred_image_final.sh.

Seven selected GUI.Forms tests pass with pinned non-Apple LLVM 22.1.8, including
prepared text, damage, shadows, group opacity and drawing APIs. The authoritative
GUI.Forms checkout remains unchanged. See build/deferred-image-gui.log.
Candidate archive inspection finds no definitions for the removed planar image
classes/cache or lazy GPU listeners; see build/deferred-image-symbols.log.
Ten newly preserved reference files match the original import byte-for-byte.

Full conversion, native cross-platform CI on this commit and the independent
review remain separate gates.
