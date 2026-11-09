# Raw array storage and triangulation cells

The retained SkTDStorage / SkTDArray implementation is converted to house style.
This remains a contiguous raw-element array, with the original ordinary capacity
growth policy and no new persistent fields, locks, reference counts or registries.
The original header, implementation and polygon caller are preserved byte-for-byte
under baseline/skia and selected by the reference build. The original pin is
2a9b593bab4b2fd019fa494c8d401ff1fab0b883.

## Ownership and extent corrections

- Moving transfers allocation, capacity and length, leaving the source empty and
  reusable. Assignment and reset no longer destroy and placement-reconstruct the
  owner. Raw-storage assignment/swap require equal element widths.
- Reserve validates sizes and allocation multiplication before publication. Byte
  capacity rounding cannot overflow at INT_MAX; compile-time boundary cases
  exercise that arithmetic without enormous allocations.
- Insertion can borrow an existing live source range from the same array. It
  records a byte offset before growth, shifts the tail, then copies the preserved
  source from at most two pieces outside the insertion gap. There is no snapshot
  allocation. A push of an existing element snapshots its value before growth.
- Erase validates the entire interval; count arithmetic is checked before
  mutation. Empty pointer-returning operations avoid null-pointer arithmetic.
- Raw element types must support trivial copying/destruction and implicit object
  creation, and fit malloc alignment. Borrow-returning array operations reject
  temporary owners. Newly exposed slots still require caller initialization.
- The polygon triangulation grid formerly placed intrusive-list objects into raw
  storage without running their constructors. It now owns a typed array of live
  lists. Their initialization replaces the manual reset loop. SkPolyUtils.cpp is
  otherwise unconverted; this change does not claim its whole-file conversion.

These are explicit behavior/correctness changes, not claims of a purely mechanical
rewrite. Bytewise equality remains representation equality, including padding;
this is not a new element-wise comparison policy. Allocation failure still aborts.

## Verification

M4 macOS arm64; non-Apple Clang 23.1.2 for standalone builds. The isolated
GUI.Forms fixture uses its pinned LLVM 22.1 toolchain. The authoritative GUI.Forms
checkout is unchanged.

- All 29 standalone tests pass in both Release profiles. The new fixture covers
  copy, move/reuse, self-assignment, empty operations, shrink, erase and shuffle.
  It checks 33,630 external-source insertion cases against std::vector in both
  profiles, plus 33,630 self-source cases in the candidate. These enumerate sizes
  1–18, every insertion position and source subrange, with and without spare
  capacity. A raw three-byte-element case checks an unaligned byte source across
  the insertion point. Corrected behavior is not run against the broken original.
- Existing intrusive-list tests cover 48 concave-polygon triangulations, including
  both windings and three scales. Their index hash remains 10536865794126918867.
- Nine rendering/mipmap artifacts match byte-for-byte. The broader suite still
  covers masks, layers, sampling, paths and image/cache behavior.
- Release and Debug instrumented deterministic suites: PASS. Prior diagnostic
  logs were preserved before each run. Detailed output remains for the owner's
  independent reviewer; no new randomized review was run.
- Fresh relocated installed consumer and candidate CPU source/symbol scope: PASS.
- Seven selected GUI.Forms tests: PASS.
- Converted array files and new test: zero house-style/lifetime-check findings.
  These automated checks are not a substitute for semantic review.

Reproduction uses tools/build_native.sh for candidate, baseline and
candidate-sanitize, tools/verify_gui.sh with the fixture's toolchain, and
`python3 -B tools/measure_td_array.py` after both Release builds. Preserve an
existing integrity.log before another instrumented run. The measurement runner
requires a new build/td-array-measurements.json destination.

## Cost, including the regression

One unmeasured run per profile followed by seven alternating paired trials on the
M4. Each workload performs 1,000,000 operations. Raw data and compiler details are
in TD_ARRAY_MEASUREMENTS.json. No concurrent build was running during measurements.

| Workload | Original median (range), ns | Candidate median (range), ns | Candidate/original |
|---|---:|---:|---:|
| Reserved integer push, including initial memory writes | 2.0992 (2.0084–2.3054) | 1.9913 (1.9201–2.1203) | 0.949 |
| Insert four external integers at index 3 of eight, then erase | 13.4516 (11.3911–16.1066) | 14.3072 (13.0811–14.6205) | 1.064 |

SkTDStorage and SkTDArray<int> are both 24 bytes in both profiles. The small
insert/erase median regresses about 0.86 ns (6.4%); this is retained explicitly.
Ranges overlap, and these two microbenchmarks do not establish renderer throughput,
large-element behavior, allocation throughput or a general speed improvement.
Self-insertion has correctness coverage rather than an invalid original timing
comparison. The typed grid removes its raw-array capacity management; no independent
triangulation-speed claim is made.

The preceding synchronization head ce06e9f passed native Linux x64, Linux arm64
and Windows CI. This batch must obtain its own platform results after publication.
