#pragma once
// Synthesis of one plan segment into a target row: anchor_weight * anchor
// plus the five weighted currents, plus the weighted bubble amplitude when
// Bubble, added to the row's earlier segments when Accumulate (otherwise the
// segment opens the row). The four (Bubble, Accumulate) cases are separate
// instantiations chosen once per segment, outside the lane loop.
#include "conv_simd_backend.hpp"

namespace stx_conv::kernels::detail {
inline namespace STX_CONV_ISA {

// currents: five planes of `stride` lanes. bubble: one plane, read only when
// Bubble. output is disjoint from every input.
template<class Ops, class Scalar, bool Bubble, bool Accumulate>
STX_CONV_INLINE void synthesis_group(const Scalar* anchors, const Scalar* currents, std::size_t stride,
                                     const std::array<Scalar, 5>& weights, Scalar anchor_weight,
                                     const Scalar* bubble, Scalar bubble_weight, Scalar* output,
                                     std::size_t lane) noexcept {
    using Vector = typename Ops::Value;
    Vector value = Ops::mul(Ops::set(anchor_weight), Ops::load(anchors + lane));
    for (std::size_t current = 0; current < 5; ++current) {
        const Vector term = Ops::mul(Ops::set(weights[current]), Ops::load(currents + current * stride + lane));
        value = Ops::add(value, term);
    }
    if constexpr (Bubble) {
        const Vector correction = Ops::mul(Ops::set(bubble_weight), Ops::load(bubble + lane));
        value = Ops::add(value, correction);
    }
    if constexpr (Accumulate) { value = Ops::add(Ops::load(output + lane), value); }
    Ops::store(output + lane, value);
}

// Lanes [first, last): whole lane groups, then the scalar tail.
template<class Scalar, bool Bubble, bool Accumulate>
void synthesis_lanes(const Scalar* anchors, const Scalar* currents, std::size_t stride,
                     const std::array<Scalar, 5>& weights, Scalar anchor_weight, const Scalar* bubble,
                     Scalar bubble_weight, Scalar* output, std::size_t first, std::size_t last) noexcept {
    using Ops = LaneOps<Scalar>;
    // An empty range reads no parameter: callers may pass null arrays with
    // no lanes.
    if (first >= last) { return; }
    // Read once, before the loop: the x86-64 unaligned vector stores
    // (_mm_storeu_ps, _mm_storeu_pd) may alias any object, so a pointer or
    // weight read through a parameter array inside the loop is reloaded after
    // every store. Local copies are not (x86-64 review, 2026-11-01).
    const std::array<Scalar, 5> current_weights = weights;
    std::size_t lane = first;
    for (; lane + Ops::width <= last; lane += Ops::width) {
        synthesis_group<Ops, Scalar, Bubble, Accumulate>(anchors, currents, stride, current_weights, anchor_weight,
                                                         bubble, bubble_weight, output, lane);
    }
    for (; lane < last; ++lane) {
        synthesis_group<ScalarOps<Scalar>, Scalar, Bubble, Accumulate>(anchors, currents, stride, current_weights,
                                                                       anchor_weight, bubble, bubble_weight,
                                                                       output, lane);
    }
}

} // inline namespace STX_CONV_ISA
} // namespace stx_conv::kernels::detail
