# Hardening record — 2026-10-22

Intake of the owner's CONV* package into GUI.Forms (PR #259), to the standard
stx_zlib and stx_png were taken in at (stage Z-S): a safe-by-construction
design, an audit of every function that reads caller input, deterministic
sweeps, instrumented runs, and a short libFuzzer smoke per change on CI only.
There is no sustained fuzz campaign (owner decision, 2026-10-20).

## Rules applied

| Rule | Meaning here |
|---|---|
| R1 bounded loops | Every loop has a bound computed before it. |
| R2 no allocation after construction | `compile`, `evaluate` and `resize` never allocate; constructors allocate everything. |
| R3 no recursion | |
| R4 every result checked | Every `Status` and `bool` is tested where it is produced. |
| R5 every parameter validated | Shapes, strides, storage, overlap, sample values, enums and positions are checked before anything is written. |
| R6 impossible states | House style §1: prefer a shape that cannot hold contradictory values. §9: every unexpected input in every state goes to one named outcome. |

Results are **pass**, **fixed** (changed in this PR) or **finding** (recorded
with its remedy).

## Owners and their states

| Owner | States | Transitions |
|---|---|---|
| `BasicLinePlan`, `BasicImagePlan` | constructed, empty | Construction yields a complete plan or throws. Moving out leaves the source empty (lengths zero, no storage); assigning a plan to it makes it constructed again. |
| `BasicProfile` | empty, unprepared, ready | Construction: unprepared. `compile` success: ready. `compile` refusal (shape, storage, sample): unchanged. `numerical_failure`: unprepared. Moving out: empty. Assignment: the assigned owner's state. |
| `BasicLineWorkspace`, `BasicImageWorkspace` | constructed, empty | As plans. An image workspace holds line storage only for the axes its plan changes and the intermediate only when both change. |

Every operation's response to each state:

| Operation | Empty input | Unprepared profile | Mismatched shapes | Short storage, stride below lanes | Bad sample | Overlap |
|---|---|---|---|---|---|---|
| `compile` | `invalid_shape` | compiles | `invalid_shape` | `insufficient_storage` | `nonfinite_input` or `input_out_of_range` (first in node-major order) | not applicable (no public borrow of owner storage) |
| `evaluate` | plan: `invalid_shape`; profile: `unprepared_profile` | `unprepared_profile` | `invalid_shape` | `insufficient_storage` | not applicable | not applicable |
| `resize` | `invalid_shape` | not applicable | `invalid_shape` | `insufficient_storage` | as `compile` | `overlap` |

A refusal writes nothing: the destination keeps its bytes and the profile
keeps its state and contents. Only `numerical_failure` can leave a partial
result (a line profile unprepared, an image partly written).

## Audit

| Function | R1 | R2 | R5 | R6 | Result |
|---|---|---|---|---|---|
| `validate_length`, `checked_product` | — | — | lengths in [1, 4e8]; products checked against `SIZE_MAX / sizeof` | — | pass |
| `fits` | — | — | `count - 1 <= (capacity - lanes) / stride`, no overflow for any `size_t` | — | pass |
| `overlaps` | — | — | address intervals of the two whole spans | — | pass |
| `validate_samples` | count x lanes | none | every admitted sample: finite, magnitude <= limit | — | pass |
| `BasicLinePlan` (regular) | targets; cells per basin | exact reservation | sampling enum; lengths | integer (int64) geometry, at most 6.4e17 | **fixed**: the segment count is computed first and reserved exactly (finding 4) |
| `BasicLinePlan::points` | positions | reserved | finite, in domain, integer endpoint rechecked | — | pass |
| `append_basin` | [first, last) cells | none | — | every touched cell overlaps with positive length (proved; the dead `high <= low` skip removed) | **fixed** |
| Owners' copy and move | — | — | — | copies deleted; moves leave a defined empty state | **fixed** (findings 1 to 3) |
| `BasicImageWorkspace` | — | per changed axis only | channels; plan not empty | optional axis storage, engaged exactly for changed axes | **fixed** (finding 5) |
| `Kernel::compile_checked` | — | none | empty owner; shape; storage; samples | — | **fixed**: one empty-state test replaces three size comparisons |
| `Kernel::compile_unchecked` | nodes, lanes, knots, ten slots per boundary, five currents per cell | none | (callers) | boundary count < length; slots < 5 x cells | pass |
| `project` | six pieces x five slots | none | — | a piece always exists (each cell keeps a slot of its own sign); `false` only past the tolerance, reported as `numerical_failure` | pass |
| `Kernel::evaluate_checked`, `evaluate_unchecked` | targets x segments x lanes | none | ready profile; plan not empty; lengths; storage | zero first weight implies all five zero (nonnegative suffix sums), so no current pointer is formed at an end node | pass |
| `Kernel::resize_checked` | rows or columns x line | none | empty owners; dimensions; storage both sides; overlap; samples | axis storage present exactly when used | **fixed** |

## Findings from the intake review and their disposition

1. Single-argument constructors were implicit. **Fixed**: all owner constructors are `explicit`.
2. Owners were implicitly copyable (large unnamed copies). **Fixed**: copies deleted.
3. The moved-from state relied on `std::vector` leaving its source empty. **Fixed**: explicit moves that release the source.
4. The regular plan reserved too few segments on reduction. **Fixed**: exact two-pass reservation.
5. The image workspace allocated both axes' storage when one was unchanged. **Fixed**.
6. Constructors' `bad_alloc` was undocumented. **Fixed** (README, header).
7. PROVENANCE said binary64 throughout. **Fixed**.
8. No statement of bounds or range. **Fixed**: `PRECISION.md`, "Error bounds and range".
9. CI: the sanitizer runner hid its output; no Windows sanitizer; the oracle differential never ran on CI. **Fixed** (owner decision 3(a) for the oracle).
10. Scripts and docs named the owner's machines. **Fixed**: portable scripts; dated results kept as records (decision 4(a)).
11. No 2D plan from two line plans; no periodic boundary. **Recorded**: no API change now (owner decision 6).

Open:

- **Unproven unreachability of `numerical_failure`.** The argument above shows
  a solution always exists; reaching the fallback needs a multiplier error
  beyond 64 eps s. No sweep, harness or smoke input has reached it; the
  harness treats it as a defect.
- **32-bit hosts** are not built. Size products are checked for them.
- **Speed.** The measured pass (owner decision 1(c), `OPTIMIZATION.md`,
  "Speed pass") changed no computed bit and gained about 10 % on smooth
  faces; 256 to 128 pixels stays near 6 ms single-threaded, above the 4 ms
  bake bound. The next step is an owner decision.

## Evidence

- `stx_conv_sweep` (`tests/sweep.cpp`), both precisions: invalid constructor
  dimensions and positions; for lines of 1 to 7 nodes, 1 to 3 lanes and
  strides with 0 to 2 padding elements, every short source and destination
  size, every invalid value at every sample position, shape mismatches and the
  admitted limit; the empty state of every owner in every operation and its
  recovery; images up to 4 x 4 to 5 x 5 with 1 to 3 channels, every short
  span, every nonfinite sample position, the destination overlapping the
  source at every offset and adjacent spans; and valid lines and two-pass
  images at the numeric extremes (the limit, subnormals, mixtures), which must
  succeed, stay finite and repeat bit for bit. 757,632 checks.
- `stx_conv_fuzz_harness` (`fuzz/conv_fuzz.cpp` with
  `STX_CONV_FUZZ_STANDALONE`): 4,000 fixed pseudo-random inputs through the
  libFuzzer entry (2,306 accepted, 1,694 refused); the entry checks status
  against an independent validity predicate, untouched destinations on
  refusal, finite values and untouched padding on success, and that
  `numerical_failure` never occurs.
- `.github/workflows/fuzz-smoke.yml`, target `conv`: 150 s of libFuzzer with
  ASan and UBSan on Linux x64 and arm64 per change. A build check, not a
  certificate.
- The whole standalone suite (core, precision, sweep, harness, float oracle,
  differential) under ASan and UBSan: locally on Windows x64 (MinGW, no leak
  sanitizer) and on CI for macOS, Linux x64 and arm64 (with leak detection)
  and Windows.
- Every value printed by the precision and differential tests is unchanged by
  these changes, and the face study's digests are unchanged.
