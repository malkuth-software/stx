# stx_conv

Standalone C++20 ordered-current CONV* resampling in one and two dimensions.
No GUI.Forms, BFFT, image-codec, operating-system, thread-pool or GPU dependency.
The Plan Paint implementation is a pinned test oracle, not a runtime dependency.

The library separates reusable sampling plans, owned compiled profiles, and
mutable workspaces. Construction allocates; `compile`, `evaluate` and `resize`
do not allocate. There is no global mutable state. Plans and compiled profiles
can be shared for concurrent read-only evaluation; compilation and image resize
require separate workspaces per concurrent operation.

## Operations and numerical contract

- Independent interleaved 1D lanes, strided input/output, arbitrary in-domain
  evaluation positions, and reusable source profiles.
- Nodal resizing: endpoint-aligned samples when enlarging; integrated nodal
  basins when reducing. Reducing to one value integrates the whole nodal domain.
- Pixel-area resizing: target footprints cover the source pixel domain including
  constant half-pixel endpoint extensions. Unchanged axes preserve samples.
- 2D interleaved-channel resizing with explicit row strides. Height reduction
  runs vertically first; otherwise horizontal first. Unchanged axes are skipped.
- Five admitted currents per interval, native binary32 or binary64 throughout. Short lines
  use the reference piecewise-linear closure. Singleton lines are constant.
- Nonfinite input and magnitudes above `1e10` (float32) or `1e100` (float64) are rejected. Dimensions are
  bounded to 400 million per axis/lane count; checked allocation products still
  apply. Real allocation limits are usually much smaller.
- No automatic color conversion, clipping, premultiplication or quantization.
  For Plan Paint's image behavior, supply premultiplied sRGB and reproduce its
  output conversion. Scalar scientific/audio use requires no image conversion.

`Profile` owns a source snapshot and currents. It retains no borrowed input.
An invalid compile request preserves the previous profile; a numerical failure
invalidates it. Evaluation validates before writing. Image validation errors
preserve output; a numerical failure can leave partial output. Image input and
output must not overlap, including their declared spans. Padding is untouched.
Moved-from plans/profiles/workspaces are not usable for processing until replaced.

## Example

```cpp
#include <stx_conv/conv.hpp>
#include <array>

const std::array<double, 5> source{0, 1, 2, 3, 4};
std::array<double, 9> destination{};
const stx_conv::LinePlan plan(5, 9);
stx_conv::Profile profile(5);
stx_conv::LineWorkspace workspace(5);
stx_conv::Status status = stx_conv::compile({source, 1}, profile, workspace);
if (status == stx_conv::Status::ok) {
    status = stx_conv::evaluate(profile, plan, {destination, 1});
}
```

For a new image, construct `ImagePlan(sw, sh, dw, dh)` and
`ImageWorkspace(plan, channels)` once, then call `resize` with source and target
spans and row strides measured in elements. Pixel channels are contiguous.
`storage_bytes()` reports allocated payload capacity, excluding object/allocator
metadata and caller input/output buffers.

## Native 32-bit precision

Every operation is available through `LinePlan32`, `Profile32`,
`LineWorkspace32`, `LineInput32`, `LineOutput32`, `ImagePlan32`,
`ImageWorkspace32`, `ImageInput32` and `ImageOutput32`. Call the same `compile`,
`evaluate` and `resize` functions; their precision is inferred from the owners.
Use `float` buffers. Existing unsuffixed names remain double precision, and
explicit `64` aliases are also available. Mixed-precision owners are rejected
at compile time. The `Basic…<float>`/`Basic…<double>` templates are explicitly
instantiated in the library; no kernel implementation is exposed in the header.

```cpp
const std::array<float, 5> source32{0, 1, 2, 3, 4};
std::array<float, 9> destination32{};
const stx_conv::LinePlan32 plan32(5, 9);
stx_conv::Profile32 profile32(5);
stx_conv::LineWorkspace32 workspace32(5);
stx_conv::Status status32 = stx_conv::compile({source32, 1}, profile32, workspace32);
if (status32 == stx_conv::Status::ok) {
    status32 = stx_conv::evaluate(profile32, plan32, {destination32, 1});
}
```

Samples, jets, currents, quadrature, coefficients and accumulation all use the
selected precision. Float32 does not stage data through doubles. Index and
capacity arithmetic remains integer. Regular plans calculate rational cell
locations using integers, then local fractions in the selected precision;
subpixel accuracy does not deteriorate with absolute image position.

`LinePlan32::points` accepts either absolute float positions or
`SamplePosition32 {cell, fraction}` positions. Use the latter when a large
integer location would consume the mantissa bits needed for its fraction.
Fractions must be in `[0, 1)`; the last source node accepts only zero.
Float64 has the corresponding overload. The library retains neither input span.

Float32 is not numerically interchangeable with float64 for all images. CONV's
sign-selected structure is discontinuous near equal neighboring values. A first
axis pass can round onto or off a plateau, changing the second pass's structure.
The stress battery records those differences separately from native-float
correctness. See [precision measurements](docs/PRECISION.md) for the observed
worst case, validation, memory and timing results. No tolerance-based flattening
or implicit double fallback has been added.

## Build and install

```sh
cmake -S stx_conv -B build/stx-conv-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/stx-conv-release --parallel
ctest --test-dir build/stx-conv-release --output-on-failure
cmake --install build/stx-conv-release --prefix build/stx-conv-prefix
```

Use `add_subdirectory(stx_conv)` or installed `find_package(stx_conv CONFIG)`;
link `stx_conv::stx_conv`. No third-party package is required. The repository's
LLVM 22 toolchain is used for recorded measurements; no AppleClang timing claim.

For differential checks, fetch the pinned oracle with
`python3 stx_conv/tools/fetch_oracle.py build-stx-conv-sources`, then configure
with `-DSTX_CONV_ORACLE=/absolute/path/to/build-stx-conv-sources`. CMake verifies
all source hashes. `stx_conv_comparison --benchmark` additionally measures paired
single-threaded line processing with both sides' plans/workspaces prepared.
Use a separate adjacent build with `-DSTX_CONV_SANITIZE=ON` for instrumentation.

## Optimization scope

The lineage objective is updated incrementally. Close floating-point contenders
are recomputed in the reference summation order to preserve earliest-index ties.
Signed-current admission first checks the full active face, then visits the six
multiplier intervals of the five raw-current breakpoints. It avoids repeated
candidate-error evaluation. Plans share preintegrated weights between lanes.

Portable loops expose contiguous independent lanes; restricted-pointer contracts
are private to disjoint owned arrays. No ISA-specific intrinsic or wider-register
algorithm is required. No fast-math is enabled. The measured initial results, assembly evidence, memory capacities and validation
limits are in [optimization measurements](docs/OPTIMIZATION.md).
