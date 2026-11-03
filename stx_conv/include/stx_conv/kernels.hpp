#pragma once
// stx_conv::kernels: the CONV arithmetic batches, tensor atlas queries and
// affine/projective point mapping, over 128-bit SIMD lane groups. See
// docs/KERNELS.md.
//
// The leaf batches (capacity to integrate_patch_rgba) borrow finite,
// length-counted structure-of-arrays streams for the call only: every stream
// holds `count` elements, outputs are disjoint from inputs and from each
// other, alignment is unrestricted, and a zero count admits null streams.
// They neither allocate nor validate their inputs. map_points and
// sample_rgba validate shape and access before writing any output.
#include <stx_conv/conv.hpp>
#include <array>
#include <cstddef>
#include <span>

namespace stx_conv::kernels {

// The lane kernels' backend, chosen once per process from stx_auxlib's SIMD
// level: "avx2-256", "neon-128", "sse2-128" or "scalar". Every backend gives
// the same bits.
[[nodiscard]] const char* backend() noexcept;

// Correction thresholds for five nonnegative capacity streams and a mass
// stream with each mass in [0, sum of its capacities] and finite sums.
template<Precision Scalar>
void capacity(const Scalar* const capacities[5], const Scalar* mass, Scalar* output, std::size_t count) noexcept;

// Interior node jets from five node streams (i-2, i-1, i, i+1, i+2), stored
// as first/5, 2*first/5 and second/20. End-node closures are separate.
template<Precision Scalar>
void jet_terms(const Scalar* const source[5], Scalar* fifths, Scalar* two_fifths, Scalar* twentieths,
               std::size_t count) noexcept;

// Raw currents of a cell. Input streams: y0, y1, node 0's first/5,
// 2*first/5, second/20, then node 1's; five output current streams.
template<Precision Scalar>
void raw_currents(const Scalar* const input[8], Scalar* const output[5], std::size_t count) noexcept;

// Bubble amplitude of a cell from its raw and admitted currents, both stored
// [current][lane] with `stride` >= count. Output holds w = 3a/5; synthesis
// weights carry the 5/3. Choose `interior` once per cell, not per lane.
template<Precision Scalar>
void bubble(const Scalar* raw, const Scalar* admitted, Scalar* output, std::size_t stride, std::size_t count,
            bool interior) noexcept;

// Five-current admission of a cell over independent lanes. left_delta and
// left_last carry the preceding cell's secant and last raw current and are
// replaced by this cell's; every other stream is read-only or output.
template<Precision Scalar>
void admit_cell(const Scalar* const raw[5], Scalar* const output[5], const Scalar* y0, const Scalar* y1,
                const Scalar* y2, const Scalar* next, Scalar* left_delta, Scalar* left_last,
                std::size_t count) noexcept;

// One segment's synthesis into output (accumulated onto it when
// `accumulate`). Current planes are contiguous with stride >= count. bubble
// may be null only when bubble_weight is zero.
template<Precision Scalar>
void synthesize(const Scalar* anchors, const Scalar* currents, std::size_t stride,
                const std::array<Scalar, 5>& weights, Scalar anchor_weight, const Scalar* bubble,
                Scalar bubble_weight, Scalar* output, std::size_t count, bool accumulate) noexcept;

// Tensor quintic patches over independent positions: controls[y * 6 + x] is
// a stream; u and v lie in [0, 1]. Output is in control units.
template<Precision Scalar>
void tensor_quintic(const Scalar* const controls[36], const Scalar* u, const Scalar* v, Scalar* output,
                    std::size_t count) noexcept;

// One known RGBA atlas patch: six rows of six RGBA controls, rows
// `row_stride` Scalars apart. The caller has located the patch and supplies
// finite u, v in [0, 1]. Writes four Scalars; no range projection.
template<Precision Scalar>
void tensor_patch_rgba(const Scalar* origin, std::size_t row_stride, Scalar u, Scalar v, Scalar* output) noexcept;

// Positive quadrature within one known patch: u, v and weights hold `count`
// finite elements (fractions in [0, 1], weights nonnegative). Each sample is
// projected to physical premultiplied RGBA and then added, weighted, to four
// finite caller-initialized sums.
template<Precision Scalar>
void integrate_patch_rgba(const Scalar* origin, std::size_t row_stride, const Scalar* u, const Scalar* v,
                          const Scalar* weights, std::size_t count, Scalar* sums) noexcept;

// Maps target points through an inverse (target-to-source) homography,
// row major; an affine map has final row (0, 0, 1). Every point is checked
// before any output is written: nonfinite coefficients or points give
// nonfinite_input, a zero denominator (a horizon) invalid_shape, and a
// nonfinite denominator or result numerical_failure. Outputs must not overlap
// the inputs or each other.
template<Precision Scalar>
[[nodiscard]] Status map_points(const std::array<Scalar, 9>& inverse, std::span<const Scalar> x,
                                std::span<const Scalar> y, std::span<Scalar> source_x,
                                std::span<Scalar> source_y) noexcept;

// A shared tensor-quintic atlas in Plan Paint's [lattice row][lattice
// column][RGBA] layout: five lattice steps per source cell in each direction,
// premultiplied normalized RGBA, at least 2x2 source pixels, finite controls.
// Borrowed for the call.
template<Precision Scalar>
struct TensorAtlasView {
    std::span<const Scalar> controls;
    std::size_t source_width = 0;
    std::size_t source_height = 0;
    // Scalar elements between lattice rows, RGBA included.
    std::size_t lattice_row_stride = 0;
};

enum class ColorProjection { none, premultiplied };

// Samples the atlas at source points (pixel centres at integers). Points
// outside the half-pixel footprint, or nonfinite, are transparent.
// `premultiplied` clamps alpha into [0, 1] and then each colour into
// [0, alpha] at every query, before any filtering.
template<Precision Scalar>
[[nodiscard]] Status sample_rgba(TensorAtlasView<Scalar> atlas, std::span<const Scalar> source_x,
                                 std::span<const Scalar> source_y, std::span<Scalar> rgba,
                                 ColorProjection projection = ColorProjection::premultiplied) noexcept;

// Warps the atlas into a target of target_width x target_height RGBA pixels
// whose rows are `row_stride` Scalars apart (at least 4 * target_width;
// padding is untouched). Target pixel (column c, row r), centre at
// integers, takes the sample at inverse(c, r): at every pixel the result of
// map_points followed by sample_rgba at (c, r), except that the sign of a
// zero result may differ. Both dimensions are at most 2^20; an empty target
// is accepted and writes nothing. Everything is checked before any output
// is written, in this order: projection and atlas shape (invalid_shape),
// target extent (invalid_shape), storage (insufficient_storage), overlap of
// the output with the controls or the map (overlap), then the map at every
// pixel in row-major order as map_points checks it.
template<Precision Scalar>
[[nodiscard]] Status warp_rgba(TensorAtlasView<Scalar> atlas, const std::array<Scalar, 9>& inverse,
                               std::size_t target_width, std::size_t target_height, std::span<Scalar> rgba,
                               std::size_t row_stride,
                               ColorProjection projection = ColorProjection::premultiplied) noexcept;

} // namespace stx_conv::kernels
