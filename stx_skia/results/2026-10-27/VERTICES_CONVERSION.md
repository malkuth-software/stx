# Vertices, the private window and VertState — 2026-10-27

Scope (relative to `skia/`): `include/core/SkVertices.h`, `src/core/SkVertices.cpp`,
`src/core/SkVerticesPriv.h`, `src/core/SkVertState.cpp/.h`, plus fixes at two callers
(`src/core/SkCanvas.cpp`, `src/core/SkDevice.cpp`). Intake copies of the five vertices files
are reference baselines, and the reference profile selects them. There are three commits:
f050106 (prune), d7d7d19 (mechanical) and the fix commit that adds this record.

## Pruned (f050106)

`SkVerticesPriv::encode` and `Decode`, their packing masks and the deprecated bone struct have
no callers since picture and flattenable reading were removed. Removing them also drops
`SkVertices.cpp`'s dependence on the read and write buffers, `SkSafeRange` and `SkPicturePriv`.

## Mechanical conversion (d7d7d19)

- The array-carving lambda becomes `take_array`, and the casts are named.
- Multi-name declarations are split.
- The size record zeroes through a named `invalidate()`, not `sk_bzero(this)`.
- The object, its size record, the private window and `VertState` start initialized.
- `VertState`'s procedure `typedef` becomes a `using`, and arrows become `(*p).m`.

Behavior is unchanged: the new corpus below is identical in both builds.

## Defects fixed

1. **Builder indices reached the draw unchecked.** `SkVertices::Builder` is public, and its
   users write indices directly. `MakeCopy` clamped indices to the vertex range and the removed
   `Decode` rejected bad ones, but `detach()` published Builder indices as written. The vertex
   draw indexes positions, colors and texture coordinates with them. `detach()` now clamps every
   index, and an indexed fan's indices before conversion. `MakeCopy` copies and lets `detach()`
   clamp, so indices are clamped once.
2. **The clamp limit wrapped above 65,536 vertices.** `MakeCopy` computed its limit as
   `SkToU16(vertexCount - 1)`. In Release that wraps, so a 70,000-vertex mesh clamped valid
   index 65535 to 4463. The limit is now `min(vertexCount - 1, 65535)`.
3. **Unknown modes crashed the draw.** `VertState::chooseProc` returned null for a mode outside
   the enumeration, and the draw loop calls the result unconditionally. Such a mode now yields a
   procedure that produces no triangles. `Builder` and `MakeCopy` also refuse it, as they refuse
   negative counts. Before, a fan with a count near `INT32_MIN` overflowed `count - 2`.
4. **Atlas counts were narrowed.**
   - `SkCanvas::drawAtlas` passed its `size_t` count to `onDrawAtlas2`'s `int` and widened it
     back unchecked. A count above `INT32_MAX` now draws nothing.
   - The base `SkDevice::drawAtlas` computed six vertices per quad in `size_t` and passed them
     to the builder's `int`. For large counts the builder could be smaller than the loop that
     filled it, and an invalid builder's null vertices reached `drawVertices`. It now checks the
     count and the builder before writing. The bitmap device overrides this path and draws with
     `size_t` counts.

## Verification

- **New corpus:** `tests/vertices.cpp` writes `vertices.raw`, which CI compares. It covers:
  - `MakeCopy` for 3 modes, 8 vertex counts, 4 attribute combinations and 8 index counts, with
    every third index out of range;
  - `Builder` construction in every mode, with and without indices and attributes;
  - the fan vertex limit at 65,535, 65,536 and 65,537;
  - for every result, its mode, counts, bounds, size, arrays and each triangle `VertState`
    yields.

  879 cases, 575,743 bytes, identical hash 17118776336671462943 in both builds, before and
  after the fixes.
- **Candidate-only checks:** Builder indices are clamped in triangles and fans; a
  70,000-vertex mesh keeps its indices; out-of-enumeration modes and negative or `INT32_MIN`
  counts are refused; and the unknown-mode procedure yields nothing. The out-of-enumeration mode
  is 3: `VertexMode` has no fixed underlying type, so 3 is the only non-enumerator value it can
  hold. The first version used 7, which is undefined to form, and the instrumented suite
  reported STOP. With 3 it reports PASS.
- Candidate release: 76/76 PASS. Reference release: 75/75 PASS.
- All shared artifacts match except `image-sampling-corrected.raw`, an earlier intentional
  correction. That includes the canvas corpus's meshes and atlases.
- Instrumented suite: PASS; diagnostics unread.
- House policy: zero AST findings in the unit; the whole-library count falls from 2,076 to
  1,973. The lexical checker reports none in the changed files.

**Not covered:** the atlas count checks are not exercised at their limits, because those
counts would need multi-gigabyte arrays. No timing claim is made. `detach()` adds one clamp pass
over Builder indices, which `MakeCopy` no longer duplicates.
