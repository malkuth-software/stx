# TArray equality uses element equality

`TArray::operator==` used each element's `operator!=` to decide whether two
arrays differed. In C++20, an element type normally gets a rewritten `!=` from
its `==`, so a plain equality-only type masked the mismatch. A type that
explicitly deletes `!=` still has a valid equality operation, but the original
array comparison fails to compile. The preserved reference header produces that
compiler error for the focused case.

The candidate now compares elements with `==` and negates the result when
deciding whether to exit early. `TArray::operator!=` remains the complement of
array equality. The candidate-only regression covers equal and unequal arrays
whose element has `==` and a deleted `!=`. No storage layout or runtime
rendering path changes.

On the M4 Mac mini, candidate and reference Release suites pass 49/49 tests
each. The candidate Release ASan/UBSan suite reports `PASS`; detailed output
remains in its integrity log for independent review. The eight generated
rendering and serialization artifacts compared here match the reference byte
for byte. The CPU build-scope check, fresh relocated installed consumer, and
seven selected GUI.Forms renderer integration tests pass. The spelling check on
the touched test and lifetime-rule check on both touched C++ files report zero
findings. The imported header's remaining house-style conversion is open.
