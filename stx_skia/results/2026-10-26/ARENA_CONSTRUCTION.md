# Arena constructor failure states

This follows ARENA_BOUNDARY.md. The candidate uses the same raster-facing
SkArenaAlloc API and preserves the original source in the reference profile.

A nontrivial single object receives a footer with a named skip action before
its constructor runs. If construction succeeds, that footer is changed to the
destructor action. If construction throws, no destructor is called on storage
that never held a complete object. The footer remains valid even if the
constructor made nested allocations in this arena. The arena remains usable
after the caller catches the exception; its unreachable slot is reclaimed at
arena reset or destruction.

Nontrivial arrays store both allocated and completed element counts. The
completed count advances only after each constructor succeeds. Destruction
walks the completed prefix and uses the allocated count to find the previous
footer. This preserves traversal if construction stops partway through an
array and nested arena allocations follow. The metadata costs four extra
bytes per nontrivial array and one count update per completed element; no
change is made to the layout of trivial arrays. An empty trivial copy skips
the zero-length `memcpy` with a null source.

The candidate-only fixture interrupts one nested object construction and
every prefix of four-element arrays through default, value, initializer and
copy construction: 17 interruption cases. Shared cases cover zero-length
nontrivial arrays and empty trivial copies. The tests require the failed
constructor to leave the arena usable and verify complete cleanup at arena
destruction. No speed gain is claimed; the metadata cost is explicit.

The complete candidate/reference Release suites pass 46 tests each. Candidate
Release and Debug instrumented suites report PASS. Ten regenerated rendering
and serialization artifacts match reference byte-for-byte. A consumer builds
and runs against a relocated candidate installation, the CPU pixel-only
build-scope check passes, and all seven selected GUI.Forms renderer tests pass
with the pinned LLVM 22.1 toolchain. The test file has zero spelling findings;
the lifetime-rule check on the touched C++ files has none.

The existing 24-scene in-memory renderer measured 6.670 ms candidate versus
6.807 ms reference median over nine paired M4 trials. That suite compares the
whole converted candidate with the retained reference, so it cannot attribute
the difference to this arena change or bound performance on other scenes.

General arena house-style conversion, arbitrary constructor reentrancy such
as resetting the arena during construction, non-M4 platform gates, and the
larger renderer review remain open.
