#pragma once
// Point mapping by an inverse 3x3 homography h (row major), target to source:
//   sx = (h0 x + h1 y + h2) / d,  sy = (h3 x + h4 y + h5) / d,
//   d = h6 x + h7 y + h8.
// Affine maps (h6 = h7 = 0, h8 = 1) skip d: an affine instantiation chosen
// once per call. The checked entry point (stx_conv::kernels::map_points)
// establishes finite results and nonzero d for every point beforehand.
#include "conv_simd_backend.hpp"
#include <algorithm>
#include <limits>
#include <optional>

namespace stx_conv::kernels::detail {

template<class Ops, class Scalar, bool Affine>
STX_CONV_INLINE void mapping_group(const std::array<Scalar, 9>& h, const Scalar* x, const Scalar* y, Scalar* source_x,
                                   Scalar* source_y, std::size_t lane) noexcept {
    using Vector = typename Ops::Value;
    const Vector px = Ops::load(x + lane);
    const Vector py = Ops::load(y + lane);
    const Vector ax = Ops::add(Ops::mul(Ops::set(h[0]), px), Ops::mul(Ops::set(h[1]), py));
    Vector mapped_x = Ops::add(ax, Ops::set(h[2]));
    const Vector ay = Ops::add(Ops::mul(Ops::set(h[3]), px), Ops::mul(Ops::set(h[4]), py));
    Vector mapped_y = Ops::add(ay, Ops::set(h[5]));
    if constexpr (!Affine) {
        const Vector ad = Ops::add(Ops::mul(Ops::set(h[6]), px), Ops::mul(Ops::set(h[7]), py));
        const Vector denominator = Ops::add(ad, Ops::set(h[8]));
        mapped_x = Ops::div(mapped_x, denominator);
        mapped_y = Ops::div(mapped_y, denominator);
    }
    Ops::store(source_x + lane, mapped_x);
    Ops::store(source_y + lane, mapped_y);
}

// What rounding can do to the projective mapping over a target rectangle,
// for any evaluation order of its sums (mapping_group's included). Each of
// d, nx = h0 x + h1 y + h2 and ny = h3 x + h4 y + h5 has two products and two
// sums: |computed - exact| <= 4 u S + tiny, where S is the sum of the terms'
// magnitudes, u the unit roundoff and tiny a bound on the underflow loss.
// d is affine, so its extremes over the rectangle are at the corners; when
// the computed corners share a sign and clear twice the error, the exact and
// the computed d keep that sign everywhere and |computed d| >= low.
template<class Scalar>
struct MappingBound {
    // Bounds on |computed - exact| of each mapped coordinate, doubled for the
    // rounding of this bound's own arithmetic.
    Scalar error_x = Scalar(0);
    Scalar error_y = Scalar(0);
    // Bounds on |computed| of each mapped coordinate.
    Scalar magnitude_x = Scalar(0);
    Scalar magnitude_y = Scalar(0);
};

// The bound over columns [0, last_column] and rows [first_row, last_row], or
// nothing when d is not certainly away from zero or a bound is not well
// inside the finite range (callers then check point by point). h finite.
template<class Scalar>
std::optional<MappingBound<Scalar>> projective_bound(const std::array<Scalar, 9>& h, Scalar last_column,
                                                     Scalar first_row, Scalar last_row) noexcept {
    const Scalar unit = std::numeric_limits<Scalar>::epsilon() / Scalar(2);
    const Scalar tiny = Scalar(8) * std::numeric_limits<Scalar>::denorm_min();
    const Scalar limit = std::numeric_limits<Scalar>::max() / Scalar(16);
    const std::array<Scalar, 2> columns{Scalar(0), last_column};
    const std::array<Scalar, 2> rows{first_row, last_row};
    Scalar sum_d = Scalar(0);
    Scalar sum_x = Scalar(0);
    Scalar sum_y = Scalar(0);
    Scalar smallest_d = std::numeric_limits<Scalar>::infinity();
    std::size_t positive = 0;
    for (const Scalar y : rows) {
        for (const Scalar x : columns) {
            const Scalar d = h[6] * x + h[7] * y + h[8];
            if (d > Scalar(0)) { ++positive; }
            smallest_d = std::min(smallest_d, std::fabs(d));
            sum_d = std::max(sum_d, std::fabs(h[6] * x) + std::fabs(h[7] * y) + std::fabs(h[8]));
            sum_x = std::max(sum_x, std::fabs(h[0] * x) + std::fabs(h[1] * y) + std::fabs(h[2]));
            sum_y = std::max(sum_y, std::fabs(h[3] * x) + std::fabs(h[4] * y) + std::fabs(h[5]));
        }
    }
    const bool sums_finite = sum_d < limit && sum_x < limit && sum_y < limit;
    if (!sums_finite || (positive != 0 && positive != 4)) { return std::nullopt; }
    const Scalar error_d = Scalar(4) * unit * sum_d + tiny;
    // exact_low bounds |exact d|, low |computed d|, over the rectangle.
    const Scalar exact_low = smallest_d - error_d;
    const Scalar low = smallest_d - Scalar(2) * error_d;
    if (!(low > Scalar(0))) { return std::nullopt; }
    const Scalar error_nx = Scalar(4) * unit * sum_x + tiny;
    const Scalar error_ny = Scalar(4) * unit * sum_y + tiny;
    MappingBound<Scalar> bound{};
    bound.magnitude_x = (sum_x + error_nx) / low * (Scalar(1) + unit) + tiny;
    bound.magnitude_y = (sum_y + error_ny) / low * (Scalar(1) + unit) + tiny;
    if (!(bound.magnitude_x < limit && bound.magnitude_y < limit)) { return std::nullopt; }
    // |n~/d~ - n/d| <= e_n / |d~| + |n| e_d / (|d| |d~|), then the division's
    // own rounding.
    const Scalar quotient_x = error_nx / low + sum_x * error_d / (exact_low * low);
    const Scalar quotient_y = error_ny / low + sum_y * error_d / (exact_low * low);
    bound.error_x = Scalar(2) * (quotient_x + unit * bound.magnitude_x + tiny);
    bound.error_y = Scalar(2) * (quotient_y + unit * bound.magnitude_y + tiny);
    if (!(bound.error_x < limit && bound.error_y < limit)) { return std::nullopt; }
    return bound;
}

// Points [0, count): whole lane groups, then the scalar tail. Outputs are
// disjoint from the inputs and from each other.
template<class Scalar, bool Affine>
void mapping_lanes(const std::array<Scalar, 9>& h, const Scalar* x, const Scalar* y, Scalar* source_x,
                   Scalar* source_y, std::size_t count) noexcept {
    using Ops = NativeOps<Scalar>;
    // An empty range reads no parameter: callers may pass null arrays with
    // no lanes.
    if (count == 0) { return; }
    // Read once, before the loop: the x86-64 unaligned vector stores
    // (_mm_storeu_ps, _mm_storeu_pd) may alias any object, so a pointer or
    // weight read through a parameter array inside the loop is reloaded after
    // every store. Local copies are not (x86-64 review, 2026-11-01).
    const std::array<Scalar, 9> map = h;
    std::size_t lane = 0;
    for (; lane + Ops::width <= count; lane += Ops::width) {
        mapping_group<Ops, Scalar, Affine>(map, x, y, source_x, source_y, lane);
    }
    for (; lane < count; ++lane) {
        mapping_group<ScalarOps<Scalar>, Scalar, Affine>(map, x, y, source_x, source_y, lane);
    }
}

} // namespace stx_conv::kernels::detail
