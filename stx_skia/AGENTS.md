# stx_skia working contract

stx_skia is the CPU Skia derivative GUI.Forms draws with: the intake pruned to
the CPU closure GUI.Forms reaches, then converted to house style and measured.
It lives in GUI.Forms as `stx_skia/` and is ordinary GUI.Forms code. The
repository `AGENTS.md` and `planning/PROGRAMMING_HOUSE_STYLE.md` apply here in
full; this file adds what is particular to this directory.

Read docs/INTAKE.md, docs/PLAN.md and PROVENANCE.md before edits. Preserve the
actual consumer contract; do not substitute a rectangle-only renderer for the
required drawing library. No GPU backend is admitted. Codec and font-backend
ownership stays outside this library.

The imported files under `skia/` are third-party source being converted. The
house-style findings still in them are listed, file by file, in
`house_style_allowance.txt`; `tools/check_stx_skia_style.py` (at the GUI.Forms
root) fails on any finding not listed, on a count that rises, and on an entry
that is now lower or clean, so the list only shrinks. New or rewritten C++
follows the full house style. Keep mechanical rewrites separate from algorithm
changes and keep upstream notices and source provenance.

Source inventory is not a link dependency proof. A function absent from direct
consumer calls can still be an internal prerequisite. Preserve upstream test
sources relevant to retained behavior. Record negative measurements too.

## Profiles

From this directory, with the GUI.Forms LLVM toolchain on the path:

```sh
sh tools/build_native.sh candidate
STX_SKIA_REFERENCE_ROOT=/path/to/stx_skia-at-pin sh tools/build_native.sh baseline
sh tools/build_native.sh candidate-sanitize
python3 tools/compare_profiles.py build/baseline build/candidate
```

`candidate` builds this tree. `baseline` is the reference profile: its library
comes from a checkout of `malkuth-software/stx_skia` at the pin recorded in
PROVENANCE.md, and its tests come from this tree. `candidate-sanitize` runs the
suite under AddressSanitizer and UndefinedBehaviorSanitizer with diagnostics
on the console; a memory error or undefined behaviour is a defect to fix.
`compare_profiles.py` requires every corpus file to match between the profiles
except those named in `tests/intentional_differences.txt`, which must differ.
Generated output goes under `build/`.

The `stx_skia_*_stops_*` tests run a program in a mode that breaks a
Release-checked precondition; `cmake/expect_stop.cmake` passes only when the
program ends in the trap that check raises, not in any other crash. A negative
control (proving a test fails on the code it guards against) runs in a fresh
build tree, or after `cmake --build <tree> --clean-first`, so stale objects
cannot stand in for the code under test.

`tools/verify_installed.sh` requires fresh installation output directories.
No blanket fast-math, guessed restrict contracts, or unmeasured SIMD speed
claims. New SIMD work targets the common 128-bit SSE/NEON width and portable
compiler-visible kernels; imported upstream optimized code is retained as the
reference.
