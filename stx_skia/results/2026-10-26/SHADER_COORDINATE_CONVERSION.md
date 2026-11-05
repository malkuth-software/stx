# Shader facade and coordinate wrappers

The shader facade, local-matrix/CTM wrappers, coordinate clamp and empty shader
are converted to explicit ownership, initialized intermediate matrices and named
processing steps. Nested local matrices retain their original multiplication
order. The nested proxy owner moves into its replacement instead of being copied.
The facade's borrowed image query and the wrappers' borrowed matrix accessors
reject temporary shader objects. Coordinate clamp continues to preserve the total
matrix validity used by image filtering; no clamping or sampling formula changes.
An unused, undefined deserializing constructor declaration is removed.

The existing baseline files remain untouched. The facade source is newly
preserved under baseline/skia and selected for the reference build, whose public
shader header already predates conversion.

## Verification

On the M4 mirror, using non-Apple LLVM through the commands in AGENTS.md:

- `sh tools/build_native.sh candidate`: 56/56 tests PASS.
- `sh tools/build_native.sh baseline`: 55/55 tests PASS.
- `sh tools/build_native.sh candidate-sanitize`: PASS. Diagnostics not inspected.
- House spelling and lifetime checks: zero findings on the eight converted
  production files and the new test source.
- All thirteen common output artifacts match byte for byte. The new
  `shader-coordinates.raw` contains 8,892 pixels (35,568 bytes).

`shader_coordinates.cpp` exercises three tile modes, two sampling modes and
three canvas transforms (including perspective), comparing nested versus merged
local matrices and recording clamped and unclamped output. It verifies reported
image/matrix/tile metadata, independently checks clamp edge constancy, rejects
inverted subsets and missing children, and checks that empty/singular shaders
leave the target untouched. Compile-time checks reject temporary image/matrix
borrows. Existing tiled-clip tests continue to cover captured CTM/device offsets.

These checks establish the exercised behavior; they do not establish universal
render equivalence or a speed improvement. Shared shader-base conversion and
image/gradient sampling remain subsequent work.
