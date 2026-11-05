# Remove unused reader factory state

The retained source has no caller of SkReadBuffer::setFactoryPlayback or its
recursion-limit controls. Picture playback and nested image-filter decoding are
excluded. After the global registry removal, the reader's name dictionary could
only hold null factories before invalidating the stream.

This change removes that dictionary, the borrowed factory-array pointer/count,
the nesting counter and their setup methods. It adds no owner, token, lock,
reference count or replacement registry. The original SkReadBuffer header and
implementation remain unchanged in baseline/skia for the reference profile.

The candidate accepts exactly the existing four-byte zero null-effect marker.
Any other effect token invalidates the reader, which advances its cursor to the
end as before. Named effects were already rejected by the candidate. The unused
private explicit factory-array API is intentionally removed. Primitive, array,
paint and callback-driven image reading remain; this is not a whole-file reader
conversion. Retained effect rendering and ordinary writer bytes are unchanged.

## Measured state

On M4 macOS arm64 with non-Apple Clang 23.1.2, a controlled sizeof probe using
all current dependency headers and only substituting the immediately previous
SkReadBuffer header measured **96 bytes before and 64 bytes after**. The reduction
is 32 bytes per reader, with no replacement persistent state. The full upstream
reference measures 184 bytes, but that larger difference includes earlier pruning
and is not attributed to this batch. No rendering-speed improvement is claimed.

Reproduce the controlled comparison by exporting
`bc87d43:skia/src/core/SkReadBuffer.h` to a separate header, including it instead
of the current header in a program printing sizeof(SkReadBuffer), and compiling
both programs with `clang++ -std=c++20 -DNDEBUG -I skia`. No reader instance or
library link is needed. Local probe sources and output are preserved under build/.

## Verification

- Both Release profiles pass all 32 tests.
- The extended effect fixture checks a zero marker followed by an unrelated word,
  exact cursor advancement, three plain paint styles followed by that word, five
  unsupported effect tokens, and an empty non-null input extent. Candidate-only
  compile checks establish that factory playback and recursion setup are absent.
- The 24 ordinary serialization cases still produce 3,344 identical bytes,
  hash 16215645112662696255. The reference continues to reconstruct named effects;
  the candidate continues to reject them.
- Nine render/mipmap artifacts remain byte-for-byte identical.
- Release and Debug instrumented deterministic suites: PASS. Prior logs were
  preserved. Detailed diagnostics were not inspected and no new randomized
  memory review was started.
- Fresh relocated installed-package consumer: PASS.
- Seven selected GUI.Forms integration tests: PASS.
- Source/compile/archive scope check: PASS.
- Changed fixture has zero house-style and lifetime-check findings. The new
  reader functions follow house style; the remaining imported reader is not
  represented as converted.

The preceding bc87d43 head passed Linux x64, Linux arm64 and Windows CI. This
batch requires its own native CI after publication. GUI.Forms verification uses
the pinned isolated fixture and LLVM 22.1; the authoritative checkout is unchanged.
