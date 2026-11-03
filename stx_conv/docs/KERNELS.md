# CONV stream and kernel collection

`stx_conv::stream` (`<stx_conv/stream.hpp>`) is the streaming 2D resizer with
bounded bubble correction. `stx_conv::kernels` (`<stx_conv/kernels.hpp>`)
exposes its arithmetic batches, tensor-quintic atlas queries and
affine/projective point mapping. Both are built and installed by
`stx_conv/CMakeLists.txt` beside the profile library (`stx_conv::stx_conv`,
`<stx_conv/conv.hpp>`), which they link and which stays the reference API.
There is no runtime BFFT, codec, graphics or thread-pool dependency.

## Layout

The layout follows BFFT's: each algorithm is written once over a small
primitive vocabulary, the vocabulary has scalar, NEON and SSE2 versions, and
schedules, public owners and instruction probes are separate files. The FFT
transform and BFFT's FMA rounding policy are not copied.

| Source | Responsibility |
|---|---|
| `src/detail/conv_simd_backend.hpp` | `ScalarOps`, NEON and SSE2 `HardwareOps` (f32x4, f64x2), `NativeOps` |
| `src/detail/conv_jet_kernel.hpp` | Five-point interior jets: first/5, 2*first/5, second/20 |
| `src/detail/conv_raw_current_kernel.hpp` | Endpoint jets into five quintic currents |
| `src/detail/conv_admission_kernel.hpp` | Unsigned box bounds, five-capacity threshold, admission |
| `src/detail/conv_bubble_kernel.hpp` | Bounded odd bubble amplitude (stored as 3a/5) |
| `src/detail/conv_synthesis_kernel.hpp` | Opening/accumulating synthesis, with or without bubble |
| `src/detail/conv_tensor_kernel.hpp` | Tensor quintic batches and RGBA patch, curve and node queries |
| `src/detail/conv_mapping_kernel.hpp` | Affine and projective coordinate lane groups |
| `src/detail/conv_warp_kernel.hpp` | Whole-row atlas warp and the affine footprint interval |
| `src/stream.cpp` | Plans, validation, node rings, pass order and output ownership |
| `src/kernels.cpp` | Public batches, checked mapping and atlas boundaries |
| `tools/kernels/probes.cpp` | One `extern "C"` symbol per lane group and precision |

Every kernel is a `*_group` (one 128-bit lane group, or one scalar lane)
and a `*_lanes` loop of whole groups followed by a scalar tail. Vector lanes
are independent lines, channels or positions; the five currents of a cell are
array slots, never lanes. One primitive moves data between lanes:
`mul_lane<Lane>` weights channel lanes by one lane of a pixel-lane weight
vector, the same rounded product as a broadcast multiply. `STX_CONV_FORCE_SCALAR=ON` builds
both libraries with `ScalarOps` throughout.

The six lane loops (jets, raw currents, admission, bubble, synthesis and
mapping) are compiled once per SIMD variant by `src/lanes.cpp`: scalar,
128-bit (SSE2 or NEON) and, on x86-64 with Clang or GCC, 256-bit AVX2 lane
groups, each in its own inline namespace and table
(`src/detail/conv_lane_kernels.hpp`). `src/lane_dispatch.cpp` picks one
table per process from `stx_auxlib::simd`'s level, so `STX_SIMD_LEVEL=scalar`,
`vector128` or `avx2_fma` forces a variant; `kernels::backend()` names it. A
standalone build that finds no `stx_auxlib_simd` package runs the 128-bit
variant alone. Capacity, tensor and warp stay 128-bit. The stream regression,
kernel and warp tests run again at each forced level.

## Loop shape (house style section 8)

- No decision inside a lane loop. Sampling, pass order, the bubble toggle,
  interior versus closure cells, opening versus accumulating synthesis and
  affine versus projective mapping are template parameters or are chosen per
  cell or per call, before the loop. The capacity threshold is a fixed
  nine-comparator min/max network, not a selection among 31 subset forms;
  the admission bounds are min/max forms; the exact grid-line and node
  reductions of an atlas query are chosen once per query.
- Kept references. The scalar backend is the backends' reference: on the
  M4, NEON and scalar outputs are identical for every kernel and for all
  2,112 stream regression cases. The NumPy model in `tests/stream/oracle/`
  is the stream's reference (below), and `tests/capacity.cpp` keeps an
  independent 31-form threshold.
- Mechanics: 128-bit or, dispatched, 256-bit lane groups; no intrinsics outside the backend,
  no fast-math and `-ffp-contract=off` on the libraries and their tests, so
  the backends agree bit for bit on finite data.

## Standard streaming use

```cpp
#include <stx_conv/stream.hpp>
stx_conv::stream::Workspace<double> workspace(sw, sh, tw, th, channels, stx_conv::Sampling::pixel_area);
// Bubble correction is on by default; set_bubble_enabled(false) gives the base.
const stx_conv::Status status = stx_conv::stream::resize<double>(workspace, {input, input_stride}, {output, output_stride});
```

Link `stx_conv::stream`. Input and output are disjoint; strides count
Scalar elements. The workspace allocates all repeated-use storage at
construction, is fixed-shape, neither copyable nor movable, and serves one
caller at a time. The toggle may change between calls without allocation;
the disabled path skips the amplitude and its loads. Unchanged axes and the
profile library's short-line closures are kept. An input validation failure
leaves the output unchanged; a numerical failure may leave partial output.

## Kernels and transforms

Link `stx_conv::kernels`. Leaf batches borrow length-counted SoA streams for
the call, neither allocate nor validate, and require disjoint outputs and
finite intermediates (see the header). `map_points` and `sample_rgba`
validate shape and access before writing any output.

An inverse 3x3 homography maps target points to source coordinates. Its
affine form covers resize, stretch, rotation, skew and translation without a
division; the projective form divides by the denominator, and a zero
denominator (a horizon) or a nonfinite result fails before any output is
written. These are point kernels: area sampling also needs target footprints
and positive quadrature.

`tensor_patch_rgba` reads six rows of six RGBA controls at a row stride; the
caller has located the patch. `sample_rgba` reads a shared atlas of five
lattice steps per source cell, handles the half-pixel footprint and returns
transparent outside it; its premultiplied projection clamps alpha, then each
colour into [0, alpha], at each query before any filtering.
`integrate_patch_rgba` projects each sample before adding it, weighted. Bubble
enrichment of a 2D warp atlas is not inferred from the five currents.

`warp_rgba` warps the atlas into a whole RGBA target through an inverse
homography. At every pixel it equals `map_points` followed by `sample_rgba`
at the pixel's integer centre, except for the sign of a zero; that point
path is its kept reference (`tests/warp.cpp`). It reformulates rather than
vectorizes that path: target pixels are the lanes for mapping, clamping
and both quintic bases, RGBA channels the lanes of the contraction, which
takes each pixel's weights from the basis vectors by lane (`mul_lane`); the
per-pixel grid-line and node branches are dropped (the full contraction
reproduces them exactly with finite controls) and replaced by per-row
segments on which one coordinate's fraction is 0 or 1 at every pixel, which
run the six-control curve. For projective maps a rounding bound over the
target (`projective_bound`: the denominator certainly one sign and away from
zero, every result certainly finite) splits each row into columns certainly
outside the footprint (+0, no contraction), certainly inside (unmasked) and
at most two masked bands between; certainty is checked with the warp's own
rounded coordinate against a margin of twice the bound, so it carries along
the row, where the exact coordinate is monotone. Without a bound the row is
masked throughout. For affine maps the footprint is an exact per-row column interval
found by bisection before the loop (an affine map's rounded coordinates are
monotone along a row). An affine map of at least half a source cell per
target pixel streams the atlas (about 25 controls per source pixel) faster
than the caches hold it, so its general rows prefetch the cells of the group
16 pixels ahead; smaller maps reuse cached cells and do not. Affine validation checks the four corners for the
same reason; projective validation checks every pixel only when there is
no bound.

The atlas layout is Plan Paint's; its adapter lives in that repository and
its checks are described there.

## Build and verify

```sh
cmake -S stx_conv -B build/stx-conv-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/stx-conv-release --parallel
ctest --test-dir build/stx-conv-release --output-on-failure

cmake -S stx_conv -B build/stx-conv-sanitize -DCMAKE_BUILD_TYPE=Debug -DSTX_CONV_SANITIZE=ON
cmake --build build/stx-conv-sanitize --parallel
ctest --test-dir build/stx-conv-sanitize --output-on-failure

cmake -S stx_conv -B build/stx-conv-scalar -DCMAKE_BUILD_TYPE=Release -DSTX_CONV_FORCE_SCALAR=ON
cmake --build build/stx-conv-scalar --parallel
ctest --test-dir build/stx-conv-scalar --output-on-failure -R 'stx_conv_(kernels|capacity|stream_.*)'
```

The tests of this collection:

| Test | Checks |
|---|---|
| `stx_conv_kernels` | Leaf batches on closed forms with guard samples; tensor against a long-double de Casteljau; patch specializations; projection before quadrature; five maps of an affine ramp; exteriors, horizons, aliasing, overflow; the bubble toggle |
| `stx_conv_warp` | `warp_rgba` equal to map_points + sample_rgba at every pixel over rotations (0, 90, 180 degrees and others), scales, skews and perspective maps, both projections; refusals leave the output unchanged |
| `stx_conv_capacity` | 100,003 capacity sets per precision against the scalar backend (bitwise) and the 31-form threshold |
| `stx_conv_stream_contracts` | Workspace construction, validation and preservation contracts |
| `stx_conv_stream_regression` | 2,112 cases, bubble off, bit for bit against the NumPy base model's digests; bubble on against the recorded digests |
| `stx_conv_stream_bubble_oracle` | Bubble on against the NumPy bubble model to 256 eps of max(1, largest source magnitude) |

The 2,112 cases (22 shapes, 1/3/4 channels, nodal and pixel-area sampling,
eight patterns, both precisions; case k is seeded k + 1) are generated by a
SplitMix64 generator written identically in `tests/stream/regression_cases.cpp`
and `tests/stream/oracle/cases.py`; both digest files carry a digest of every
input to keep the two in step. `base_oracle.sha256` holds the NumPy base
model's output digests and is regenerated only when the model changes:

```sh
python3 -B stx_conv/tests/stream/oracle/make_base_digests.py stx_conv/tests/stream/base_oracle.sha256
```

`bubble_record.sha256` is the stream's own output as folded; it detects later
changes and is not an oracle. Rewrite it only for an intended change, after
the bubble oracle passes:

```sh
build/stx-conv-release/stx_conv_stream_regression --write-bubble-record \
    stx_conv/tests/stream/base_oracle.sha256 stx_conv/tests/stream/bubble_record.sha256
```

The bubble oracle needs Python with NumPy (`-DSTX_CONV_ORACLE_PYTHON=...`, or an
environment variable of that name at configure time, selects the interpreter);
without it the test reports skipped. Its first run
generates the fixture into the build tree (about 90 s on an M4) and reuses it
while the oracle sources are unchanged.

## Instruction workflow

Configure with `-DSTX_CONV_BUILD_KERNEL_PROBES=ON` for the probe object, or
emit both architectures' assembly directly:

```sh
CXX=clang++ sh stx_conv/tools/kernels/inspect.sh build/conv-kernel-assembly
python3 -B stx_conv/tools/kernels/audit.py build/conv-kernel-assembly
```

`inspect.sh` writes NEON and baseline SSE2 assembly (no AVX or FMA) with
`-O3 -ffp-contract=off`; it executes nothing. The probes are an inspection
target, not an interface.

Configure with `-DSTX_CONV_BUILD_KERNEL_BENCHMARKS=ON` for
`stx_conv_kernel_benchmark`: 65,536 prepared positions, one warming call, the
median of five samples of eight calls, allocation excluded; checked mapping
includes its validation. The scalar build's compiler may still vectorize
scalar code. Kernel timings say nothing about whole images or threaded use.

`stx_conv_warp_benchmark` (same option) times `warp_rgba` against its
reference on a 256x256 atlas and target: median, minimum and maximum of
seven runs per map. `rotate_17_zoom_0.25` runs the same general contraction
as `rotate_17` over a sixteenth of the atlas per pixel, which separates
arithmetic from atlas traffic. `STX_CONV_WARP_PREFETCH_DISTANCE` (a compile
definition, default 16, 0 for none) repeats the prefetch measurement.

Build the library and an installed consumer with the same toolchain: LLVM 22
and Apple Clang 21 mangle constrained function templates differently.

## Scope

Measured on the M4 (arm64, NEON, LLVM 22.1.8) only. SSE2 correctness and
timing are not run: x86-64 execution belongs on an x86-64 host, and the
cross-host bitwise claim for SSE2 rests on CI's Linux runs. No image-quality
or speed claim follows from these tests.
