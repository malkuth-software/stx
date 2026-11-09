# Block-list construction boundary

The retained `SkTBlockList` is used by raster clip state. Before this pass,
`pushItem()` advanced the block cursor, last-item offset and live count before
constructing `T`. An exception left the list claiming an object that did not
exist. During nontrivial `concat()`, the source head item was destroyed after
each successful move. A later failed move left the source count pointing at
destroyed objects. The one-item shortcut required copying and therefore could
not concatenate a move-only `T`.

The candidate allocates a tail slot, constructs the element, then publishes
metadata. A local guard returns the slot and any empty new block on failure.
Nontrivial concatenation constructs its destination prefix before destroying
the source head. If a move fails, the target contains its completed prefix and
the source still owns all its live objects, although an earlier source value
may be moved from. Trivial head copies publish each completed segment before
any later allocation. The single-item shortcut moves when `T` cannot be
copied. Release checks guard counts, block sizes, negative reserve and empty
access. Mutation of the same list from an element constructor is outside the
append contract.

The original header is preserved at
`baseline/skia/src/core/SkTBlockList.h` and selected by the baseline include
path. The new `tblock_list_contract` test exercises ordinary append, pop,
indexed access and concat in both builds. Candidate-only cases interrupt
construction on inline and heap storage, interrupt the second move of a head
concat, concatenate a move-only object, and check invalid Release inputs.

On the M4 Mac mini using the repository's `m4build` selector:

- Candidate and baseline Release suites each passed 47/47 tests.
- Candidate Release and Debug ASan/UBSan suites each reported `PASS`.
  Detailed integrity logs remain under their build trees for independent review.
- The generated rendering and serialization artifacts matched the baseline
  byte for byte.
- The CPU-only build-scope check, relocated installed consumer and seven
  selected GUI.Forms renderer tests passed.
- The new test file passed the house-style spelling and lifetime checks;
  `git diff --check` passed. The imported header still has pre-existing style
  findings and is not declared fully converted.

These checks establish the edited boundary. They do not establish a speed
change or a bill of health for the remaining imported renderer.
