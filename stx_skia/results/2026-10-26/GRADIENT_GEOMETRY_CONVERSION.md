# Gradient geometry and factory conversion

The linear, radial, sweep and two-point conical gradient implementations and
headers now use explicit types, initialized setup state, named owned results and
separate matrix construction. Factory validation and early-return order are
visible in each procedure; the control-flow macro is removed. Sweep angle
recovery uses named start/end fields instead of a tuple and structured binding.
Conical focal data starts initialized, including when radial/strip setup does
not use it. Borrowed endpoints, centers and focal metadata reject temporaries.
Unused, undefined legacy constructor/context declarations are removed.

The conical implementation retains concentric/radial, equal-radius strip,
focal-on-circle, well-behaved, smaller/greater and swapped-focal stages. Context
storage belongs to the caller's arena and remains live through pre/post pipelines.
Focal setup failure may leave its local matrix/data changed; callers discard the
failed setup. Numerical expressions, matrix multiplication order, degenerate
thresholds, hard-stop behavior and factory rejection policy are preserved.
Original source/header overlays remain untouched for differential verification.
The shared gradient base is not claimed converted; only its unused macro is cut.

## Rendering and boundary checks

The new gradient_geometry_contract covers 5,472 renders at 33 by 23 pixels:
19 geometries, four tile modes, single/uniform/explicit hard-stop color layouts,
four interpolation spaces, both premultiplication choices and three local
matrices. The geometries include coincident/nearly coincident line endpoints,
zero-radius radial gradients, full/partial/degenerate sweeps, concentric rings,
equal-radius strips and the focal variants above. Matrices are identity, affine
and perspective; the affine case also uses a linear-sRGB destination. Explicit
stops have duplicated positions and implicit endpoint colors. Non-single-color
stops include translucent and fully transparent values.

Factory checks cover empty colors, singular local matrices, null linear points,
negative radii, reversed sweep angles and selected infinite coordinates. They
preserve the existing single-color distinction: linear/radial/sweep return a
solid before testing matrix inversion, while conical validates the matrix first.
Four compile-time checks reject representative temporary metadata borrows.
The generated gradient-geometry.raw is 16,612,992 bytes; CI compares it along
with the fifteen previous common artifacts on both native workflow paths.

## Gates and limits

M4 arm64, non-Apple LLVM, sequential AGENTS.md commands:

- Candidate release: 59/59 PASS.
- Reference release: 58/58 PASS.
- All sixteen common raw artifacts match byte for byte.
- Complete instrumented deterministic candidate suite: PASS; diagnostics unread.
- House spelling/lifetime checks: zero findings in the eight converted production
  files and the new test. The base header's three spelling findings disappear
  with the macro, but its ownership and storage semantics remain unconverted.

This is rendering/factory evidence, not an exhaustive numerical proof or a
performance result. Effect deserialization remains disabled by the candidate
policy; retained compatibility factory bodies compile but are not claimed as
an admitted decode interface. Shared color-stop storage, interpolation setup,
color conversion and raster gradient kernels remain separate conversion work.
