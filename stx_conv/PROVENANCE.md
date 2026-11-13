# Provenance

`stx_conv` is a C++20 implementation of the owner's Compact Ordered Nodal
Variation (CONV*) resampler. It is not a generic convolution or Gabor library.

The production behavior reference is `falseywinchnet/plan-paint` revision
`3edc452068d646b1ba68d7ae853abb1aa26ff6de`, specifically `src/conv.cpp` and
`src/conv.hpp`. The original license is retained verbatim in `LICENSE`.
It was compiled only into the differential and binary32 oracle tests, never into
the installed archive; those tests were retired on 2026-11-03 (owner: the design
is proven), and the last results that ran them are under `results/2026-10-22*`.

The mathematical construction is the six-source/five-current quintic version:
fourth-order interior jets, one-sided endpoint closures, ordered sign-transition
admission, projection onto the signed mass fibre, anchored Bernstein-current
synthesis, and exact degree-five basin integration by three-point Gauss quadrature.
Short lines (fewer than five nodes) follow Plan Paint's piecewise-linear closure.

BFFT's `output/pdf/conv_paper_fused.tex` and the later arithmetic work describe
lineage recurrence and scalar-threshold admission. The library implements these
algebraic reductions; it does not import BFFT, its thread pool, platform intrinsic
kernels, alternative fixed orders, or experimental eikonal interpolation.

The 2D operation is Plan Paint's separable composition, including its declared
axis order. It is not a new rotation-invariant or arbitrary-warp interpolator.
Colour-space, alpha premultiplication and output quantization remain explicit
caller operations. Every lane computes in the precision of its owners (binary32
or binary64, chosen at compile time), with floating-point contraction disabled.

## Stream and kernel collection

`src/stream.cpp`, `src/kernels.cpp` and `src/detail/conv_*_kernel.hpp`, with
the public `stream.hpp` and `kernels.hpp`, were written in this repository
from its 2026-10-29 CONV study (preclipped box admission, the fixed
five-capacity threshold and the bounded bubble). The study itself is not
retained. No BFFT code is copied; BFFT's backend, kernel and probe layout
informed the file organisation. The test-only NumPy model in
`tests/stream/oracle/` is a first-party restatement of the study's
references. The profile implementation is unchanged.

## Surfaces

`include/stx_conv/surface.hpp`, `src/surface.cpp` and
`src/detail/surface_kernel.hpp` restate the CPU CONV line cell and tensor
surface of the conv-teapot study (`malkuth-software/conv-teapot`, commit
`fc0a609`, `src/conv_line.hpp` and `src/surface.hpp`, MIT, the owner's own
work) in this repository's house style: seams with cubic-exact end
stencils, the continuous bubble limiter, and exact first derivatives carried
through admission by a one-derivative dual. The teapot's bands, joins, posed
surfaces and model data, its physics (`third_party/zc_phys`, PlaySuite
licence) and its GPU version are not used. `tests/surface/line_reference.txt`
is the first 100 of that repository's reference lines, which its
`tests/make_line_reference.py` wrote from this package's stream model.
