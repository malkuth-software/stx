# Raster stages after compiler removal — 2026-10-26

Removing SkSL left its generated-program operations in the CPU raster pipeline.
This batch removes that unused execution machinery while retaining the stages
used by ordinary shaders, gradients, sampling, pixel conversion and compositing.

## Dependency and implementation scope

The previous candidate declared 519 operation names. The compiler-specific
operation macro plus stack checkpoint/rewind and base-pointer setup accounted
for 290 of them. Their uses were confined to the stage implementation and its
pipeline setup; searches elsewhere found only unrelated helpers/comments with
common words such as shuffle and jump. Removing the declarations and definitions
then compiling the complete candidate establishes that the retained source does
not depend on them.

The candidate now declares 229 operations. The original reference declares 524;
five compiler tracing operations were removed in the preceding batch. Ordinary
pixel loads/stores, sampling, color conversion, blending and memory-context
patching remain. Neither precision nor the selected CPU instruction set changes.

Removed implementation includes compiler control-flow masks, slot arithmetic,
indirect accesses, matrix operations on compiler slots, stack checkpoints and
rewinds, their context records, and the now-unused context packing utility.
Pipeline extension no longer rewrites compiler-tail or rewind contexts. Stage
counting no longer budgets a compiler checkpoint.

Two execution arguments became unused: the compiler slot base pointer forwarded
between high-precision stages, and the shared compiler-tail pointer passed to
pipeline launch. Both are removed from definitions, declarations and call sites.
Partial-row handling still patches live memory contexts to bounded scratch,
executes the retained stages, restores contexts and copies back live pixels.
The tail width is still calculated locally for those operations.

This is dependency pruning, not full house-style conversion of SkRasterPipeline,
SkOpts or their large vector implementation. Original files remain selected by
the reference build. There is no claimed rendering-speed gain from changing
internal argument lists; timing and assembly work belong to a separate measured
optimization. The number of compiled candidate source units remains 310.

## Direct regression test

The new raster_pipeline test runs 3,168 combinations: widths 0–65, heights 0–3,
x offsets 1–3, direct/compiled execution and default/forced-high precision.
Buffers have 79-pixel strides and five rows. Independent expected pixels check
red/blue swapping within the requested rectangle and unchanged sentinels outside
it, including row padding. Context pointers must be restored after each call.
Each case joins separate load/transform/store pipelines, resets the result,
checks empty direct and compiled execution, and rebuilds it by extension.
Arenas and contexts explicitly outlive the pipeline and compiled callback.

## Verification

M4 Mac mini, macOS arm64, non-Apple Clang 23.1.2/C++20. Both candidate and original
reference pass fifteen deterministic tests. All seven complete rendering
artifacts match byte-for-byte, including the 696 blend/vertex scenes and 256
layer scenes. Release and Debug instrumented suites report PASS. Prior diagnostic
logs are preserved and their contents remain unread. The relocated installed
consumer passes, as do seven selected GUI.Forms tests with pinned LLVM 22.1.8.
The authoritative GUI.Forms checkout is unchanged.

Records: build/compiler-ops-reference.log, build/compiler-ops-gates.log,
build/compiler-ops-consumer-gates.log and build/compiler-ops-final.log. The last
record repeats release/reference and instrumented checks after removing the
unused packing header. Native cross-platform checks remain a separate gate.

The preceding 07a618b runtime-removal commit passed Linux arm64 and Windows CI during
this work, including the span constructor-participation correction. Linux x64
was still pending at this record's preparation.
This is not substituted for CI on the current change.
