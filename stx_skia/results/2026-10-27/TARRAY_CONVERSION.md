# TArray: corpus, rewrite and fixes — 2026-10-27

The third support-code unit. `TArray` and `STArray` keep their whole public interface. The
owner stopped a member-level prune ("dont prune this heavily"): these are general containers
that later work may rely on. Earlier units had already fixed construction, relocation, append
failures, reverse access, alignment and element equality (6ccb8403 to c70eb200). This unit
adds the missing differential corpus, the house-style respelling and one remaining fix. The
profile found `TArray` at 0% of GUI.Forms' Skia render time; no speed claim is made.

## The prune experiment, not adopted

Before the owner stopped it, a compile-driven check removed candidate members and rebuilt.
- **Used by the library:** most members. The path builder uses `at`, `==`, `reserve`, `pop_back`,
  `push_back_n` and the span constructor; gradients use `swap` and `push_back_n`; colour uses
  `resize`; the device clip stack uses `emplace_back`; lattices use the reserving constructor;
  ID listeners use `removeShuffle`.
- **No caller:** a few, among them `shrink_to_fit`, `move_back`, `move_back_n`, `fromBack`,
  `size_bytes`, `operator!=` and the `initializer_list` constructors.

The header was restored unchanged and nothing from the experiment was committed.

## Corpus (7fcd12b3)

`tests/tarray_core.cpp` runs a seeded sequence of 30 operations over the whole public interface.
- **Array types:** `TArray` and `STArray`, each with trivial elements, elements relocated by
  move construction, and nontrivial elements marked trivially relocatable (moved as bytes).
  That makes six array types, run over four seeds.
- **Construction:** reserving, pointer-and-count, span and initializer-list construction.
- **Recorded after every step:**
  - the size, whether capacity covers the size, and emptiness;
  - whether the data is null or inside the object (inline storage);
  - every element value and the number of live elements.

The result is `tarray-core.raw`: 43,383 cases, identical in both builds. Inputs that only the
candidate defines are not used: no element of an array is passed back into it.

**Intentional difference: `tarray-core-capacity.raw`.** It records the size and exact capacity
at every step. The intake move constructor builds its storage from `SkSpan(that)`, which
records the *size* as the capacity, so a moved array under-reports the storage it owns. The
candidate has moved the real capacity since 40875c9c. All 387 differing rows have:
- equal sizes;
- the reference capacity equal to the size;
- a larger candidate capacity.

## Mechanical rewrite (19d568cd)

- **Spelling:** member calls through `(*this)` and `(*p)`, named casts, `std::memcpy`, const and
  initialized locals, and prefix increment.
- **Members:** the ownership and capacity bit-fields get default member initializers.
- **Free `swap`:** `inline` rather than `static inline`.
- **Result:** both artifacts are byte-identical to the pre-rewrite output.

## Fix

**`push_back_n(n, const T[])` past the live elements.** A source among the array's own live
elements had its range checked only when the call grew the array. Without growth, a source
running past the live elements was copied from the slots the call was constructing. For a
nontrivial `T` that read unconstructed objects. The range check now runs whether or not the
array grows; the defensive copy is still made only on growth.

**Not a fix (corrected during the unit).** I first also made `operator[]`, `at`, `front`, `back`
and `pop_back` Release-checked, believing their `SkUNREACHABLE` was undefined behaviour in
Release. It is not: `SkUNREACHABLE` is `__builtin_trap()` (`__fastfail` with MSVC), so those
already stop. The edits were reverted; their stop tests stay as regression guards.

## Checks (candidate only)

- **Own-element appends:** appending a run of the array's own live elements, with and without
  growth, copies them. This also passes against the intake code; it guards the fix's other
  path.
- **Stop tests:** `tarray_core_stops_index-past-end`, `-index-negative`, `-front-empty`,
  `-back-empty`, `-pop-empty` and `-append-past-live`.

**Negative control.** Against the unfixed header (19d568cd), `append-past-live` does not stop and
the other five do. As with the string unit, `cmake/expect_stop.cmake` accepts any signal.

## Verification

All on the M4 mirror, sequentially:
- **Candidate:** 102/102, including the six stop tests.
- **Reference:** 84/84.
- **Corpora:** `tarray-core.raw` is identical in both builds, and both TArray artifacts are
  unchanged by the rewrite and the fix. All shared artifacts are identical apart from the
  intentional differences, now seven with `tarray-core-capacity.raw`. No warnings.
- **Instrumented suite:** PASS.
- **Installed package:** a fresh relocated install passes `tools/verify_installed.sh`.
- **GUI.Forms fixture:** 9/9 with `tools/verify_gui.sh`.
- **Style:** the lexical checker finds nothing in `SkTArray.h` or the corpus.
- **Inventory:** the AST inventory falls from 501 excluded constructs in 51 files to 461 in 49;
  none remain in `SkTArray.h`.

## Not done

- **Windows and Linux:** CI covers this unit on push.
- **Still to do:** `TBlockList`, the block allocator and the arena.
