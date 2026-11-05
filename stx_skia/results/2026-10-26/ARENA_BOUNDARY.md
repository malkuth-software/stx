# Arena allocation boundary

The retained raster library uses SkArenaAlloc in blitters, bitmap processing,
scan conversion and raster pipelines. This pass preserves the pinned original
header and implementation under `baseline/skia/src/core/` and selects them in
the reference profile. It fixes allocation-boundary defects; the full arena
implementation remains an imported source pending house-style conversion.

## Corrected states

Heap-only construction no longer forms `nullptr + 0`, and the first object
allocation obtains a block before subtracting or aligning arena pointers.
Heap-only reset's empty check no longer forms an address from a null first
block. Inline and heap block lifetimes and destruction order remain as before.

Initial block sizes and first-heap sizes must fit the arena's 32-bit accounting
before conversion. A borrowed initial block is required for positive inline
extent. The raw-byte entry validates nonzero power-of-two alignment before
forming a mask. A nontrivial object's footer size and a new block's skip footer
are checked before addition. Footer alignment now uses integer addresses and
the actual available byte extent; it forms the object pointer only after the
offset fits the allocation.

## Evidence and limits

The shared deterministic fixture covers heap-only, inline and reset arenas,
trivial and nontrivial elements, array construction and destruction, and a
64-byte aligned allocation. Candidate-only process checks confirm Release
rejection of invalid alignment and an initial block length beyond `uint32_t`.
The reference profile runs the valid cases from the preserved source.

The complete candidate/reference Release suites pass 46 tests each. Candidate
Release and Debug instrumented suites report PASS. Ten regenerated rendering
and serialization artifacts match the reference byte-for-byte. A consumer
builds and runs against a relocated candidate installation, the CPU pixel-only
build-scope check passes, and all seven selected GUI.Forms renderer tests pass
with the pinned LLVM 22.1 toolchain. The new test file has zero spelling
findings, and lifetime-rule checks on the touched C++ files find none.

Constructor-throw cleanup and remaining source-style conversion are separate
open work; this pass makes no claim about those paths or a whole-library bill
of health.
