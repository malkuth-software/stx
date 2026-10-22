# Provenance

`stx_conv` is a C++20 implementation of the owner's Compact Ordered Nodal
Variation (CONV*) resampler. It is not a generic convolution or Gabor library.

The production behavior reference is `falseywinchnet/plan-paint` revision
`3edc452068d646b1ba68d7ae853abb1aa26ff6de`, specifically `src/conv.cpp` and
`src/conv.hpp`. The original license is retained verbatim in `LICENSE`.
`tests/oracle.json` pins all test source digests. The reference is compiled only
inside the optional differential test, never into the installed archive.

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

## Binary32 test oracle

`tools/prepare_float_oracle.py` checks the pinned `conv.cpp` SHA-256, extracts
the original line-profile builder, and generates a test-only float version.
It changes double types, floating literal suffixes, machine epsilon and the
largest finite value. The original active-face enumeration and full boundary
cost recomputation are retained. This is a derived binary32 reference, not an
upstream float API. It is generated in the build directory, never linked into
the runtime library. The same Plan Paint attribution and license apply.
