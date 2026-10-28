# Convert storage writing without adding ownership state

SkWriter32.h/.cpp now follow house style. The owner still has its original five
fields: selected data pointer, capacity, live length, external pointer and internal
allocation owner. M4 Release sizes are unchanged: 40 bytes for SkWriter32 and
64 bytes for SkSWriter32<24>. The inline writer now has explicitly aligned byte
storage instead of an alignment union. Its constructor lends that storage only
after member lifetime begins; the base destructor never dereferences the borrow.

The original files are preserved byte-for-byte from the initial import in
baseline/skia, and the reference profile selects their implementation and header.

## Bounds, lifetime and retained behavior

- Initial storage is caller-owned, aligned, and disjoint from the writer object
  and its internal allocation. Capacity is rounded down to a multiple of four.
  Reset discards live bytes while retaining the existing internal allocation policy.
- Reservations check admitted extents and addition before growth; ordinary growth
  remains 4096 + max(required, capacity + capacity/2). If speculative spare capacity
  would exceed the representable extent, growth requests the exact required size.
  Metadata is published after allocation and any external-to-internal copy.
- Empty reservations, writes, flattening and snapshots have defined behavior.
  No null-pointer arithmetic or zero-length copy with null pointers is needed.
- Trivially copyable record reads return values, so later growth/overwrite cannot
  invalidate a returned reference. Both reads and overwrites validate the complete
  record extent. Overwrites support overlap within existing storage.
- Scalar writes explicitly construct the trivial record in reserved storage.
  This begins its C++ lifetime and retains typed-store alias information without
  allocating an object or adding lifetime metadata. Bulk writes remain byte copies.
- String-length sentinels are compared as size_t rather than converted through
  platform-dependent long. Length prefixes, terminators, padding and data-size
  calculations are checked. Null-string size calculation now agrees with writing
  the empty string. Ordinary serialized bytes are unchanged.
- The uncalled readFromStream helper is removed. No retained writer source calls
  it after the prior binary-writer input pruning. Stream output remains.

Invalid programmer-supplied extents terminate through the existing fatal-contract
mechanism. No failure flag or exception protocol is added. Near-limit checks are
reviewed arithmetic guards; the fixture does not attempt impractically large
allocations.

## Preserve the fitting reservation path

The initial checked conversion, commit ea010e7, put an overflow check on every
reservation and used byte copies for scalar records. It passed correctness gates
but made the tested writing loops roughly three times slower. That result is
preserved in WRITER32_MEASUREMENTS-PRIOR-1.json.

The final path validates size <= PTRDIFF_MAX and relies on live length already
being bounded by that same limit. A static assertion proves the sum of those two
limits fits size_t. Thus addition cannot wrap; a result that fits the validated
capacity is already valid, and the remaining extent check belongs on growth.
This restores the original add/compare fitting path. Explicit scalar construction
also lets the compiler retain the record's type. Neither change adds state.

The intermediate subtraction-based fitting check improved the first regression
but remained 11–17% slower (-PRIOR-2). A first trial of the final add/compare form
was near parity for growth cases and about 8% slower with full supplied capacity
(-PRIOR-3). All trials are retained. The final repeat is effectively at parity:

| Supplied capacity | Original median ns/word | Candidate median ns/word | Candidate/original |
|---|---:|---:|---:|
| 0 bytes | 0.270 | 0.269 | 0.998 |
| 64 bytes | 0.277 | 0.279 | 1.009 |
| 4,096 bytes | 0.242 | 0.243 | 1.006 |

M4 macOS arm64, non-Apple Clang 23.1.2, Release. Each workload constructs 8,192
writers, appends 1,024 words to each, observes the last word and destroys the
writer. The first two capacities require fallback allocation; the third fits.
Times are amortized per word and include that work. Seven alternating paired
trials follow one warmup per profile. Final timing ranges overlap; the earlier
8% result prevents claiming universal equality or a speedup. This is not rendering
throughput or a measured process-memory reduction.

WRITER32_NATIVE.s records a native standalone append probe. The fitting branch
contains a direct scalar store and no constructor call or allocation call. The
probe still has a stack frame/register saves for its possible growth call; it is
not evidence of entirely register-resident execution or x86 performance.

## Verification

- Both Release profiles pass all 34 tests.
- New independent byte expectations cover fourteen supplied capacities, including
  odd byte capacities, padding lengths 0–33, strings, masked integers, signed zero,
  multiple growth transitions, record overwrite/read, rewind, snapshots and reset.
  Both profiles produce hash 6518763112313890887. Inline fallback/reset and
  candidate empty states are also checked; compile checks establish value-returning
  reads and absence of stream input.
- Nine rendering/mipmap artifacts and the 24-case 3,344-byte effect-stream artifact
  remain identical. The high-level writer stays 80 bytes and the reader 64 bytes.
- Final Release and Debug instrumented deterministic suites: PASS. Earlier logs
  are preserved, detailed diagnostics were not inspected, and no new randomized
  memory review was run.
- Fresh relocated installed consumer and seven selected GUI.Forms tests: PASS.
- Converted source and fixture pass house-style and lifetime checks. Compile/archive
  scope checks pass. Original-reference writer files match the import exactly.

The preceding b98106c commit passes Linux x64, Linux arm64 and Windows CI. This
batch requires its own current-head platform gates after publication. Tests use
an isolated GUI.Forms fixture with its pinned LLVM 22.1 toolchain; the authoritative
GUI.Forms checkout is unchanged.

Reproduce with tools/build_native.sh candidate/baseline/candidate-sanitize,
normal artifact comparisons, the relocated consumer and tools/verify_gui.sh.
Run python3 -B tools/measure_writer32.py sequentially after builds. The native
probe is append_word(SkWriter32& writer, int32_t value) calling writer.write32(value),
compiled with non-Apple clang++ -std=c++20 -O3 -DNDEBUG -ffp-contract=off -fno-rtti.
Remaining allocation helpers, geometry, canvas/device, shaders, color and raster
implementation are not claimed converted by this storage-writer batch.
