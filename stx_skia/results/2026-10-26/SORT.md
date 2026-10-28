# Geometry and raster sorting utilities

SkTSort.h is converted to house style. The original header remains byte-for-byte
under baseline/skia/src/core and is selected by the reference profile. CPU callers
include path operations, contour ordering, antialiased scan conversion, ordinary
scan conversion and region edge sorting. No callers are replaced by a reduced
renderer or geometry implementation.

The retained strategy is middle-pivot introsort, insertion sort at 32 or fewer
elements, and heap sort when the recursion budget expires. Left recursion/right
iteration, comparison choices and tie handling remain. Keeping these decisions
allows direct comparison of identity ordering used by geometry; the conversion
does not silently replace that behavior with another sort implementation.

## Changes and ownership

A separate correction commit handles empty inputs before subtracting one from a
heap count or forming insertion-sort pointers before the beginning of a range.
Equal begin/end pointers return before pointer subtraction, admitting null/null
as an empty range. Insertion counts must be nonnegative; nonempty quicksort ranges
must be within the integer count limit. Raw pointers still require valid live
ranges in one caller-owned allocation; the function cannot infer allocation
extent from an arbitrary address.

Heap helpers establish that a child exists before doubling a one-based parent
index. This keeps arithmetic bounded without relying on a shifted leaf index
remaining representable. Named value/pointee comparators replace anonymous
functions. Explicit counts, depth and pointer differences replace implicit
narrowing and condensed operations. Insertion sort uses an exclusive end pointer
only after the empty/singleton cases have returned.

Sorting mutates supplied live elements, retains no borrows, and creates no
auxiliary array or persistent state. Element operations can have their own storage
costs, as with the tested owning strings. Comparators must be strict weak orders,
observe operands only, and neither throw nor reenter the same storage. Element
copy/move/swap must not throw. Insertion sort remains stable; introsort/heap sort
retain their original decisions but do not promise stability.

## Verification

M4 macOS arm64, non-Apple Clang 23.1.2 Release. The isolated GUI.Forms fixture
uses its pinned LLVM 22.1 toolchain; the authoritative checkout is unchanged.

- All 31 tests pass in both Release profiles.
- The sort fixture checks 53,553 permutation and boundary-pattern cases, including
  duplicates, sorted/reversed/equal/few-key/permuted/organ-pipe inputs, lengths
  around 16/32/64, and forced heap fallback. Independent checks establish ordering,
  identity preservation, guard preservation and insertion stability.
- The reference and candidate both make 2,492,793 traced comparator calls with
  matching trace hash 6864612751107199514. The sorted identity/key output hash also agrees:
  5361751140576373687. These hashes cover the deterministic fixture, not every
  possible comparator/type or input.
- Additional cases cover default value ordering, const-pointee ordering, and
  owning strings through heap, insertion and introsort. Direct empty/null cases
  exercise the candidate corrections; they are not required of the broken original.
- Nine rendering/mipmap artifacts match byte-for-byte.
- Release and Debug instrumented deterministic suites: PASS. Prior diagnostic
  files were preserved before rerunning; detailed output stays with the owner's
  independent reviewer. No new randomized memory review was started.
- Fresh relocated installed consumer, CPU source/symbol scope and seven selected
  GUI.Forms integration tests: PASS.
- Converted header/test: zero house-style and lifetime-check findings. This is
  supplementary evidence, not a semantic proof from syntax alone.

The preceding priority-queue head 50d9374 passed Linux x64, Linux arm64 and Windows
CI. This sort batch requires its own current-head results after publication.

## Timing, including small-input costs

One warmup per profile, seven alternating paired trials, sequential with builds.
Each trial resets and sorts 262,144 eight-byte records in batches of 32, 256 or
4,096. Input copies and endpoint hashing are included. Timed sorting is instantiated
in the test from the respective header; it does not time another library operation.
Raw timings, compiler and platform are in SORT_MEASUREMENTS.json.

| Pattern/count | Original median, ns/element | Converted median, ns/element | Converted/original |
|---|---:|---:|---:|
| Ascending 32 | 0.561 | 0.599 | 1.067 |
| Descending 32 | 6.162 | 6.170 | 1.001 |
| Equal 32 | 0.582 | 0.651 | 1.119 |
| Few keys 32 | 2.816 | 2.938 | 1.043 |
| Permuted 32 | 3.191 | 3.254 | 1.020 |
| Organ pipe 32 | 3.026 | 3.124 | 1.032 |
| Ascending 256 | 2.353 | 2.364 | 1.005 |
| Descending 256 | 6.390 | 6.559 | 1.026 |
| Equal 256 | 12.590 | 11.753 | 0.934 |
| Few keys 256 | 5.777 | 5.412 | 0.937 |
| Permuted 256 | 7.060 | 6.869 | 0.973 |
| Organ pipe 256 | 19.341 | 19.365 | 1.001 |
| Ascending 4,096 | 4.398 | 4.262 | 0.969 |
| Descending 4,096 | 8.303 | 8.221 | 0.990 |
| Equal 4,096 | 19.140 | 17.894 | 0.935 |
| Few keys 4,096 | 18.180 | 17.194 | 0.946 |
| Permuted 4,096 | 17.378 | 14.863 | 0.855 |
| Organ pipe 4,096 | 33.614 | 31.976 | 0.951 |

The large permuted case here improves about 14%, but small cases
regress: ascending/equal 32 add approximately 0.04/0.07 ns per element, while the
few-key case adds about 0.12 ns. Many timing ranges overlap. Preserve those costs;
do not present this as universally faster or as a renderer-throughput improvement.
There is no changed sort threshold, platform intrinsic, wider SIMD requirement,
new allocation, or extra state selected by these measurements.

Reproduce with tools/build_native.sh candidate, baseline and candidate-sanitize;
preserve each existing integrity.log before the instrumented run. Run
`python3 -B tools/measure_sort.py` only after both Release builds, without concurrent
build work. Existing raw measurement files are archived by the runner. GUI.Forms
verification uses tools/verify_gui.sh with the pinned fixture/toolchain.
