# SkArenaAlloc: corpus, rewrite and fixes — 2026-10-27

The last of the requested support-code units. `SkArenaAlloc`, `SkSTArenaAlloc`,
`SkArenaAllocWithReset` and `SkSTArenaAllocWithReset` keep their whole public interface. The
arena backs the raster pipeline, the blitters, shaders, colour filters and the mask blur.

Earlier units had already fixed several things:
- validated allocation extents and null storage (79e55d98);
- a constructor that throws, or that allocates from the arena, no longer corrupts the
  destructor chain (7f5301cd);
- arrays now record how many elements were constructed (7f5301cd).

The profile put the arena at 0.2% of GUI.Forms' Skia render time; no speed claim is made.

## Corpus (fd023083)

`tests/arena_core.cpp` runs seeded sequences on arenas with no, small, inline and
caller-provided storage, including the resetting arenas, which reset at random points.
- **Objects:** made from arguments, by default and through a constructor callable. Some objects
  allocate from the arena while they are being built.
- **Arrays:** every array kind, plus aligned bytes.
- **Recorded:**
  - every value;
  - whether the live allocations stay aligned and disjoint;
  - the bytes left in the current block, and offsets inside caller storage;
  - the order in which destructors run when an arena ends or resets.

Addresses are never recorded, and alignments stay at 8 or below.

**Intentional difference: `arena-core-layout.raw`.** The candidate stores a constructed count
with each nontrivial array, 4 more bytes per array (7f5301cd). So the corpus runs twice:
- **Pass one** makes every kind of allocation. It writes the remaining bytes and the
  caller-storage offsets to `arena-core-layout.raw`, which differs on purpose.
- **Pass two** makes no nontrivial arrays and records both in `arena-core.raw`. That file is
  identical in both builds (12,470 cases), so the block layouts match exactly once arrays are
  set aside.

## Mechanical rewrite (836233a9)

- **`make(Ctor)`:** returns `std::invoke_result_t` instead of a trailing `decltype`.
- **`make<T>(args...)` and `make<T>()`:** construct directly instead of through lambdas. All three
  `make` forms share two private helpers holding the existing sequences:
  - `reserveObject<T>` for trivially destructible objects;
  - `reserveObjectWithFooter<T>` for the rest: the footer goes in before construction, and its
    destroy action is set after.
- **`allocObjectWithFooter`:** its `goto restart` becomes a retry loop.
- **Spelling:** member access through `(*this)`, named casts, `std::memcpy`/`memmove`, `SkASSERT`
  for `assert`, the `_v` traits with `if constexpr`, and initialized members, bit-fields and
  locals.
- **Result:** both artifacts were byte-identical before and after.

## Fixes

### Destructible objects aligned beyond 256 bytes

**Defect.** A footer records the padding before its object in one byte. A destructible object or
array aligned to 512 or more could need more padding. The byte was truncated (only a debug
`assert` checked it), so the destructor chain was walked from a wrong address.

**Measured.** A scratch program at the unfixed sources made six `alignas(512)` destructible
objects per arena, interleaved with small ones, and crashed with exit 139.

**Fix.** Such types no longer compile: a `static_assert` on the object and array paths, with
`kMaxFooterAlignment = 256`, so padding is at most 255. The footer write also checks the padding
in Release. Trivially destructible allocations have no footer and stay unlimited. The scratch
program now fails to compile with the new message.

### `SkArenaAllocWithReset::reset()`

**Defect.** `reset` ended `*this` and constructed a new `SkArenaAllocWithReset` in its storage.
When the object is a base-class subobject, as in `SkSTArenaAllocWithReset` or any subclass, that
is not a transparent replacement ([basic.life]). The old object could not be used afterwards,
yet every caller goes on using it.

**Fix.** `reset` now calls a new protected `SkArenaAlloc::restart`. It runs the destructors,
frees the heap blocks, and reinitializes the fields in place through the same `startAt` the
constructor uses.

**Evidence.** The corpus's reset sequences, on plain, caller-storage and inline resetting arenas,
are identical before and after: the observable behaviour is unchanged. The undefined behaviour
cannot be observed directly.

## Checks (candidate only)

**`wide_alignments`** interleaves narrow and 256-aligned destructible objects and arrays, so that
footers carry the largest paddings, with 512-aligned trivially destructible objects.
- **Result:** every object is aligned, and every destructor runs once.
- **Against the unfixed code:** it also passes there. It guards the boundary the fix keeps
  allowed.

## Verification

All on the M4 mirror, sequentially:
- **Candidate:** 105/105.
- **Reference:** 86/86.
- **Corpora:** `arena-core.raw` is identical in both builds, and both arena artifacts are
  unchanged by the rewrite and the fixes. All shared artifacts are identical apart from the
  intentional differences, now eight with `arena-core-layout.raw`. No warnings.
- **Instrumented suite:** PASS.
- **Installed package:** a fresh relocated install passes `tools/verify_installed.sh`.
- **GUI.Forms fixture:** 9/9 with `tools/verify_gui.sh`.
- **Style:** the lexical checker finds nothing in the arena or its corpus.
- **Inventory:** the AST inventory falls from 339 excluded constructs in 46 files to 309 in 44;
  none remain in the arena.

## Not done

- **Windows and Linux:** CI covers this unit on push.
