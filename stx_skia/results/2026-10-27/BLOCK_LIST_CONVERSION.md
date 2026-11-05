# SkBlockAllocator and SkTBlockList: corpus, rewrite and fixes — 2026-10-27

The fourth support-code unit. Both keep their whole public interface. `SkTBlockList` backs
`SkRasterClipStack`, and is the block allocator's only user. GUI.Forms uses neither directly.
Earlier units had already fixed several things:
- constructor input validation (ef37a98c);
- block-list state after construction failures (75f096c3);
- the poisoned suffix of a shrinking resize (87c1d1c0).

The profile found both at 0% of GUI.Forms' Skia render time; no speed claim is made.

## Corpus (fc0d9551, corrected in 3f3d9758)

`tests/block_list_core.cpp` writes `block-list-core.raw`: 17,277 cases, identical in both
builds.

**Allocator.** Seeded sequences run for every growth policy and three increments, with plain and
inline (`SkSBlockAllocator`) head blocks. They cover:
- `allocate` at six alignment and padding pairs;
- `release`, of the newest reservation and of others;
- `resize`;
- `reserve` with each flag combination;
- `releaseBlock`, `stealHeapBlocks`, `reset` and `resetScratchSpace`;
- block and allocator metadata, `owningBlock` and `findOwningBlock`.

Each step records the offsets returned, every block's available bytes and metadata, forward and
reverse block order, the current block and the size totals. Addresses are never recorded. Every
reservation is written over, so the instrumented build checks that it is addressable.

**Block list.** Seeded sequences on trivial and nontrivial items cover:
- `push_back` (default, copy and move) and `emplace_back`;
- `pop_back`, `reserve` and `reset`;
- `item`, `front` and `back`;
- `concat`, with equal and different starting sizes.

Each step records every item forward and backward, and the live item count.

**Correction (3f3d9758).** The first version used 16-byte alignments. Where
`alignof(std::max_align_t)` is 8 (Apple arm64, MSVC, wasm), that is over-aligned, and an offset
then depends on where a block lands. The builds agreed only because macOS returns 16-aligned
memory. The alignments are now 1, 2, 4 and 8.

## Mechanical rewrite (e5c6d293)

- **Spelling:** member access through `(*p)` and `(*this)`.
- **Allocator access:** `SkTBlockList` reaches its allocator through a private `store()`
  reference. `SkSBlockAllocator::operator->` stays in the API; the lexical checker flags its two
  definitions.
- **Language:** named casts, `if constexpr` where the condition was already constant,
  `std::conditional_t` and the `_v` traits, `std::memcpy` and an explicit `::operator new`.
- **Initialization:** default member initializers for blocks, byte ranges, iterators and the
  growth bit-fields, and const, initialized locals.
- **Names:** `addBlock`'s rounding lambda becomes `align_alloc_size`, and the `auto`
  declarations name their types.

**Result:** `block-list-core.raw` was byte-identical before and after.

## Fixes

### Over-aligned allocations overran their new block

**Defect.** A new block's address is only guaranteed `kAddressAlign`, which is
`alignof(std::max_align_t)`: 8 on this M4, 16 on Linux x64. For a larger `Align`, the first
aligned offset depends on that address. It can exceed `BlockOverhead<Align, Padding>()`, which
sized the block, by up to `Align - kAddressAlign` bytes. The reservation then ended past the
block.

**Measured** with a scratch program at the unfixed sources, fresh allocators, sizes 1–400:
- **32-byte alignment:** 88 of 1,064 heap allocations overran their block. For example,
  `allocate<32>(1)` got a 40-byte block and ended at offset 49.
- **64-byte alignment:** 23 of 1,064 overran.
- **16- and 128-byte alignment:** none overran in that run. The 16-byte case is luck: macOS
  returns 16-aligned memory.

**Fix.** `allocate` and `reserve` add `AddressSlack<Align>()` (`Align - kAddressAlign`, or 0) to
a new block's minimum size, and `MaxBlockSize` includes it. After making a new block, `allocate`
Release-checks that the reservation fits.

**Reach.** No current user over-aligns: the clip stack's records align to 8. It is a heap overrun
in a kept general-purpose API.

### `stealHeapBlocks` from itself

**Defect.** Stealing from itself detached every heap block. The blocks leaked, with their live
reservations, and only the head block was left. It now stops the program, as does a null
source.

### `concat` copy condition (not a behaviour change here)

**Change.** The byte-copying path of `SkTBlockList::concat` is now chosen by
`is_trivially_copyable` instead of `is_trivially_copy_constructible`. Copying items as bytes
leaves the sources unended, which is correct only for trivially copyable items.

**Why it changes nothing here.** With clang, the old trait already counts the destructor (LWG
2116), so an item with a trivial copy and a nontrivial destructor took the moving path anyway.
The change names the exact condition rather than depend on how a standard library reads the
older trait.

## Checks (candidate only)

- **`over_aligned_fit`:** at 16-, 32-, 64- and 128-byte alignment, through `allocate` and through
  `reserve`, every heap reservation fits its block and is aligned. Each is written over.
- **`trivial_copy_concat`:** the guard for the `concat` condition. Ten items are concatenated,
  four from the source's head block. Fourteen objects end, and the values arrive in order.
- **Stop test:** `block_list_core_stops_steal-self`.

**Negative control** against the unfixed sources (e5c6d293):
- `over_aligned_fit` fails at 32 and 64 and passes at 16 and 128;
- `trivial_copy_concat` passes, as expected;
- the self-steal exits normally, so its stop test fails.

## Verification

All on the M4 mirror, sequentially:
- **Candidate:** 104/104, including the stop test.
- **Reference:** 85/85.
- **Corpora:** `block-list-core.raw` is identical in both builds and unchanged by the rewrite and
  the fixes. All shared artifacts are identical apart from the seven intentional differences.
  No warnings.
- **Instrumented suite:** PASS.
- **Installed package:** a fresh relocated install passes `tools/verify_installed.sh`.
- **GUI.Forms fixture:** 9/9 with `tools/verify_gui.sh`.
- **Inventory:** the AST inventory falls from 461 excluded constructs in 49 files to 339 in 46;
  none remain in the three files.

## Not done

- **Windows and Linux:** CI covers this unit on push.
- **Growth counters:** `releaseBlock` undoes a growth step even for blocks that did not take one,
  namely `reserve` with `kIgnoreGrowthPolicy_Flag` and reactivated scratch blocks. The guard
  keeps the counters at or above their start, so this only makes later blocks smaller than the
  policy intends. It changes sizes, not safety, and is left as is.
- **Still to do:** the arena.
