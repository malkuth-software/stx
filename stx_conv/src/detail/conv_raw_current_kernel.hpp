#pragma once
// Raw currents of one cell: production's raw_currents(). The cell's quintic
// Bezier controls c0..c5 come from the end nodes and their jet terms; the
// five raw currents are the control differences c(k+1) - c(k).
#include "conv_simd_backend.hpp"

namespace stx_conv::kernels::detail {

// Input streams: y0, y1, then node 0's first/5, 2*first/5, second/20, then
// node 1's. Output streams: the five currents, disjoint from the inputs.
template<class Ops, class Scalar>
STX_CONV_INLINE void raw_current_group(const Scalar* const input[8], Scalar* const output[5], std::size_t lane) noexcept {
    using Vector = typename Ops::Value;
    const Vector y0 = Ops::load(input[0] + lane);
    const Vector y1 = Ops::load(input[1] + lane);
    const Vector c1 = Ops::add(y0, Ops::load(input[2] + lane));
    const Vector c2_partial = Ops::add(y0, Ops::load(input[3] + lane));
    const Vector c2 = Ops::add(c2_partial, Ops::load(input[4] + lane));
    const Vector c3_partial = Ops::sub(y1, Ops::load(input[6] + lane));
    const Vector c3 = Ops::add(c3_partial, Ops::load(input[7] + lane));
    const Vector c4 = Ops::sub(y1, Ops::load(input[5] + lane));
    Ops::store(output[0] + lane, Ops::sub(c1, y0));
    Ops::store(output[1] + lane, Ops::sub(c2, c1));
    Ops::store(output[2] + lane, Ops::sub(c3, c2));
    Ops::store(output[3] + lane, Ops::sub(c4, c3));
    Ops::store(output[4] + lane, Ops::sub(y1, c4));
}

// Lanes [first, last): whole lane groups, then the scalar tail.
template<class Scalar>
void raw_current_lanes(const Scalar* const input[8], Scalar* const output[5], std::size_t first, std::size_t last) noexcept {
    using Ops = NativeOps<Scalar>;
    std::size_t lane = first;
    for (; lane + Ops::width <= last; lane += Ops::width) { raw_current_group<Ops>(input, output, lane); }
    for (; lane < last; ++lane) { raw_current_group<ScalarOps<Scalar>>(input, output, lane); }
}

} // namespace stx_conv::kernels::detail
