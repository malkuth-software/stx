# From intake to an owned CPU raster library

1. **Reproduce the baseline.** Build the pinned source with the recorded CPU
   profiles using the non-Apple LLVM toolchain. Retain compile commands, source
   list, archive symbols and linked consumer map. Run GUI.Forms control rendering,
   GUI.Drawing, prepared text, shadow, damage and image-composition comparisons.
   Establish identical inputs and pixel hashes before assigning improvements.
2. **Prune by subsystem.** Start with uncalled serialization/recording and codec
   infrastructure, then investigate unused Skia text internals and general image
   filters. Resolve dependent SkSL/runtime machinery explicitly. Each removal
   gets an independent change and the same rendering gate. Keep useful upstream
   tests and document the exact supported subset.
3. **Convert to house style.** Work through ownership/buffer boundaries, then
   pixel kernels, masks/blur, scan conversion and geometry. Separate mechanical
   naming/spelling changes from altered lifetime, allocation or arithmetic.
   Preserve precision and exceptional behavior unless a measured change is
   explicitly accepted. Do not mass-rewrite all upstream syntax in one commit.
4. **Optimize measured work.** Reduce redundant copies and allocation, hoist
   invariant dispatch, separate interior/edge kernels and establish alias rules.
   Inspect generated common-width vector code and measure small, awkward-stride,
   clipped, translucent and large inputs. Preserve fast paths that already work.
5. **Independent review and integration.** The owner brings a Daybreak sibling
   for detailed review after the relevant boundary is stable. Findings are fixed
   and rechecked. Integrate through GUI.Forms' private renderer seam only after
   its cross-platform and stage gates pass. A review is evidence with a scope,
   not a guarantee that no defects can remain.

## First implementation gate

Completed: explicit CPU CMake source closure, installed package, codec-scaffolding
and unused-effect removal, excluded source cleanup, common-width dispatch policy,
SkData implementation conversion, owned pixel transfer, and reusable consumer
gradient buffers. The isolated GUI.Forms fixture passes seven selected tests;
24 standalone render cases match the retained baseline byte for byte.

The first mechanical conversion is intentionally confined to SkData. Six imported
dispatch files receive one conditional change each; they are not claimed to be
house-style conversions. The remaining large imported implementation is still
explicitly third-party-derived source awaiting conversion.

Next: native Linux x64/arm64 and Windows CLANG64 gates; coordinator integration;
then continue conversion of retained canvas/device and raster implementation.
Picture/text dependencies are removed with matching render comparisons. General image filters and their SkSL/runtime
dependencies are now removed under the clarified owner contract.
Retain blur masks, matrix color filters and shader masks. Continue conversion into
pixel storage, masks and exercised kernels with per-change measurements.

The independent Daybreak review and full GUI.Forms stage gate remain separate.
No single primitive suite establishes universal pixel equivalence or speed.

Full conversion remains active. See [the conversion ledger](CONVERSION.md) for
completed ownership boundaries and the remaining production scope.

Worker execution follows the owner's GUI.Forms threadpool_atomic_fast reference:
batched publication, chunk claims, locally accumulated completion and small-work
execution on the caller. The unused Skia executor/task-group pool is removed.
Do not infer that atomics alone make the shared cache graph safe: cache records,
hash/list mutation, publication and destruction still require one coordinated
access protocol. Future parallel raster work must first identify independent
buffers/workspaces and measure dispatch cost against useful work.
