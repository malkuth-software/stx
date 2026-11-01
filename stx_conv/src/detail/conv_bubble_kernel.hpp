#pragma once
// Bounded odd bubble amplitude of one cell. The correction added to the
// admitted cell is a * B(t), B = [t(1-t)]^3 (2t-1); this kernel stores
// w = 3a/5 (the plan's bubble weights carry the 5/3). The amplitude fits the
// six-node residual and is then clamped so that the three elevated-current
// combinations q0, q1, q2 keep their signs; it is zero where any of them is
// zero. Mass equality removes the outer raw-minus-admitted terms, and the
// sign bounds need no division.
#include "conv_simd_backend.hpp"
#include <limits>

namespace stx_conv::kernels::detail {

// raw and admitted: five planes of `stride` lanes ([current][lane]).
// Interior (cells 2..length-4) adds the fourth-difference term of the raw
// currents; it is chosen once per cell, outside the lane loop.
template<class Ops, class Scalar, bool Interior>
STX_CONV_INLINE void bubble_group(const Scalar* raw, const Scalar* admitted, Scalar* output, std::size_t stride,
                                  std::size_t lane) noexcept {
    using Vector = typename Ops::Value;
    const Vector zero = Ops::set(0);
    const Vector infinity = Ops::set(std::numeric_limits<Scalar>::infinity());
    const Vector negative_infinity = Ops::sub(zero, infinity);
    const Vector raw0 = Ops::load(raw + lane);
    const Vector raw1 = Ops::load(raw + stride + lane);
    const Vector raw2 = Ops::load(raw + 2 * stride + lane);
    const Vector raw3 = Ops::load(raw + 3 * stride + lane);
    const Vector raw4 = Ops::load(raw + 4 * stride + lane);
    Vector amplitude = zero;
    if constexpr (Interior) {
        const Vector outer = Ops::add(raw0, raw4);
        const Vector inner = Ops::add(raw1, raw3);
        const Vector outer_less_inner = Ops::sub(outer, Ops::mul(Ops::set(4), inner));
        const Vector fourth = Ops::add(outer_less_inner, Ops::mul(Ops::set(6), raw2));
        amplitude = Ops::mul(Ops::set(Scalar(-492.0 / 125.0)), fourth);
    }
    const Vector c0 = Ops::load(admitted + lane);
    const Vector c1 = Ops::load(admitted + stride + lane);
    const Vector c2 = Ops::load(admitted + 2 * stride + lane);
    const Vector c3 = Ops::load(admitted + 3 * stride + lane);
    const Vector c4 = Ops::load(admitted + 4 * stride + lane);
    const Vector d1 = Ops::sub(raw1, c1);
    const Vector d2 = Ops::sub(raw2, c2);
    const Vector d3 = Ops::sub(raw3, c3);
    amplitude = Ops::add(amplitude, Ops::mul(Ops::set(31.5), Ops::add(d1, d3)));
    amplitude = Ops::add(amplitude, Ops::mul(Ops::set(46.5), d2));
    // q0 = c0 + 8 c1 + 6 c2; q1 = -1.5 (c1 + 3 c2 + c3); q2 = 6 c2 + 8 c3 + c4.
    const Vector q0_partial = Ops::add(c0, Ops::mul(Ops::set(8), c1));
    const Vector q0 = Ops::add(q0_partial, Ops::mul(Ops::set(6), c2));
    const Vector q1_partial = Ops::add(c1, Ops::mul(Ops::set(3), c2));
    const Vector q1 = Ops::mul(Ops::set(-1.5), Ops::add(q1_partial, c3));
    const Vector q2_partial = Ops::add(Ops::mul(Ops::set(6), c2), Ops::mul(Ops::set(8), c3));
    const Vector q2 = Ops::add(q2_partial, c4);
    const std::array<Vector, 3> q{q0, q1, q2};
    Vector lower = negative_infinity;
    Vector upper = infinity;
    for (std::size_t form = 0; form < 3; ++form) {
        const Vector lower_candidate = Ops::choose_negative(q[form], q[form], negative_infinity);
        const Vector upper_candidate = Ops::choose_negative(Ops::sub(zero, q[form]), q[form], infinity);
        lower = Ops::max(lower, lower_candidate);
        upper = Ops::min(upper, upper_candidate);
    }
    amplitude = Ops::min(upper, Ops::max(lower, amplitude));
    // A zero form pins the amplitude to zero: -|q| is negative unless q is 0.
    for (std::size_t form = 0; form < 3; ++form) {
        const Vector absolute = Ops::max(q[form], Ops::sub(zero, q[form]));
        const Vector negative_absolute = Ops::sub(zero, absolute);
        amplitude = Ops::choose_negative(negative_absolute, amplitude, zero);
    }
    Ops::store(output + lane, amplitude);
}

// Lanes [first, last): whole lane groups, then the scalar tail.
template<class Scalar, bool Interior>
void bubble_lanes(const Scalar* raw, const Scalar* admitted, Scalar* output, std::size_t stride, std::size_t first,
                  std::size_t last) noexcept {
    using Ops = NativeOps<Scalar>;
    std::size_t lane = first;
    for (; lane + Ops::width <= last; lane += Ops::width) {
        bubble_group<Ops, Scalar, Interior>(raw, admitted, output, stride, lane);
    }
    for (; lane < last; ++lane) {
        bubble_group<ScalarOps<Scalar>, Scalar, Interior>(raw, admitted, output, stride, lane);
    }
}

} // namespace stx_conv::kernels::detail
