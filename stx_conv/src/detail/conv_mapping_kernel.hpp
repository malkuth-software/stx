#pragma once
// Point mapping by an inverse 3x3 homography h (row major), target to source:
//   sx = (h0 x + h1 y + h2) / d,  sy = (h3 x + h4 y + h5) / d,
//   d = h6 x + h7 y + h8.
// Affine maps (h6 = h7 = 0, h8 = 1) skip d: an affine instantiation chosen
// once per call. The checked entry point (stx_conv::kernels::map_points)
// establishes finite results and nonzero d for every point beforehand.
#include "conv_simd_backend.hpp"

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

// Points [0, count): whole lane groups, then the scalar tail. Outputs are
// disjoint from the inputs and from each other.
template<class Scalar, bool Affine>
void mapping_lanes(const std::array<Scalar, 9>& h, const Scalar* x, const Scalar* y, Scalar* source_x,
                   Scalar* source_y, std::size_t count) noexcept {
    using Ops = NativeOps<Scalar>;
    std::size_t lane = 0;
    for (; lane + Ops::width <= count; lane += Ops::width) {
        mapping_group<Ops, Scalar, Affine>(h, x, y, source_x, source_y, lane);
    }
    for (; lane < count; ++lane) {
        mapping_group<ScalarOps<Scalar>, Scalar, Affine>(h, x, y, source_x, source_y, lane);
    }
}

} // namespace stx_conv::kernels::detail
