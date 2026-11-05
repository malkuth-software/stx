# Paint and SkPaintPriv — 2026-10-27

Scope (relative to `skia/`): `include/core/SkPaint.h`, `src/core/SkPaint.cpp`,
`src/core/SkPaintPriv.cpp/.h`, plus the paint entry points of `src/core/SkReadBuffer.h` and
`src/core/SkWriteBuffer.cpp/.h`. Earlier pruning had already given `SkPaint.cpp/.h` and
`SkPaintPriv.cpp` reference baselines. There are four commits:
- 935b40f: prune;
- 175f2c0d: reference repair;
- ca02be0e: mechanical conversion;
- the fix commit that adds this record.

## Pruned (935b40f)

`SkPaintPriv::Flatten` and `Unflatten`, with their v68 packing and `ASSERT_FITS_IN` macro, were
reached only through `SkBinaryWriteBuffer::writePaint` and `SkReadBuffer::readPaint`. Since
pictures were removed, only tests called those. `ComputeLuminanceColor` served glyph caches
that no longer exist. All are removed, along with the buffers' paint dependencies.

The paint test now checks, entry point by entry point, that serialization is gone. The
flattenable stream test keeps its null-effect and token checks.

**Repair (175f2c0d).** That commit edited `SkPaintPriv.h`, which had no baseline copy. The
reference profile's original `SkReadBuffer.h` and `SkPaintPriv.cpp` then compiled against the
pruned header and failed. Only the candidate had been built for it. The intake header now
serves the reference profile; the reference release at 935b40f with it passes 75/75.

## Mechanical conversion (ca02be0e)

- The `EQUAL`, `DEFINE_FIELD_REF` and `DEFINE_FIELD_SET` macros become written functions.
- The disabled `SK_REPORT_API_RANGE_CHECK` blocks are gone.
- The header's enum getters, and the setters and constructor, use `static_cast` instead of
  C-style casts.
- The fast-bounds shortcut tests its two effects directly instead of OR-ing pointer bits.
- Arrows, `auto` and `this->` are gone, and the blend coefficients start initialized.

## Defects fixed

1. **Equality read an inactive union member.** `SkPaint` stored its flags and enums as a
   bitfield struct in a union with a `uint32_t`, and `operator==` compared the integer. Reading
   the member that was not last written is undefined behaviour in C++. It also compared the
   padding bits, which equality has no reason to inspect. The union is gone: equality compares
   the five fields. The header's seven `decltype` relocatability assertions now name their types.
2. **The fast-bounds contract was a Debug assertion.** `computeFastBounds` and
   `computeFastStrokeBounds` are documented as callable only after `canComputeFastBounds()`
   returns true, and only with storage. In Release, a path effect that could not bound its
   output was ignored, returning bounds it never computed, which can cull a visible draw; a
   null storage was dereferenced. Both preconditions are now Release checks, and the header
   says so. The library's two callers, canvas quick-reject and the bitmap device's bounder,
   already check first.

## Verification

- **New corpus:** `tests/paint.cpp` writes `paint.raw`, which CI compares. It covers:
  - the state after every setter, including enum values one past each enumeration (rejected),
    negative, signed-zero and non-finite widths, miter limits and alpha, and colors that are
    out of range, NaN or infinite, in three color spaces;
  - ARGB and 4f color setters, copies, moves and equality across nine variants;
  - `asBlendMode`, `getBlendMode_or`, `isSrcOver` and `nothingToDraw` over all 30 modes,
    three alphas and two kinds of color filter, with a custom blender;
  - `SkPaintPriv::Overwrites` for every shader-override opacity, including a null paint;
  - fast bounds for every style, three widths and every join, with path-effect and mask-filter
    combinations;
  - `ShouldDither` for six color types, and color-filter folding with and without a shader.

  482 cases, 26,553 bytes, identical hash 6978482068064979823 in both builds, before and after
  the fixes.
- Candidate release: 76/76 PASS. Reference release: 75/75 PASS.
- All shared artifacts match except `image-sampling-corrected.raw`, an earlier intentional
  correction.
- Instrumented suite: PASS; diagnostics unread.
- House policy: zero AST findings in the unit; the whole-library count falls from 1,973 to
  1,910. The lexical checker reports none in the changed files.

**Not covered:** the new Release checks are not exercised, because a failing one stops the
program and the suite has no death tests. No timing claim is made; equality compares five
fields instead of one word.
