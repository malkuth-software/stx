#pragma once
// Interior node jets: production's centred fourth-order first and second
// derivatives at node i from nodes i-2..i+2, reduced to the three node terms
// the raw currents use (first/5, 2*first/5, second/20). The two nodes at each
// end use one-sided closures in src/stream.cpp.
#include "conv_simd_backend.hpp"

namespace stx_conv::kernels::detail {

// One lane group at lane offset `lane`. Source streams: nodes i-2, i-1, i,
// i+1, i+2. Outputs are disjoint from the sources.
template<class Ops, class Scalar>
STX_CONV_INLINE void jet_group(const Scalar* const source[5], Scalar* fifths, Scalar* two_fifths,
                               Scalar* twentieths, std::size_t lane) noexcept {
    using Vector = typename Ops::Value;
    const Vector m2 = Ops::load(source[0] + lane);
    const Vector m1 = Ops::load(source[1] + lane);
    const Vector centre = Ops::load(source[2] + lane);
    const Vector p1 = Ops::load(source[3] + lane);
    const Vector p2 = Ops::load(source[4] + lane);
    // first = (m2 - 8 m1 + 8 p1 - p2) / 12, summed left to right.
    Vector first = Ops::sub(m2, Ops::mul(Ops::set(8), m1));
    first = Ops::add(first, Ops::mul(Ops::set(8), p1));
    first = Ops::sub(first, p2);
    first = Ops::div(first, Ops::set(12));
    // second = (-p2 + 16 p1 - 30 c + 16 m1 - m2) / 12, summed left to right.
    Vector second = Ops::sub(Ops::mul(Ops::set(16), p1), p2);
    second = Ops::sub(second, Ops::mul(Ops::set(30), centre));
    second = Ops::add(second, Ops::mul(Ops::set(16), m1));
    second = Ops::sub(second, m2);
    second = Ops::div(second, Ops::set(12));
    const Vector first_fifth = Ops::div(first, Ops::set(5));
    const Vector twice_first = Ops::mul(Ops::set(2), first);
    const Vector twice_first_fifth = Ops::div(twice_first, Ops::set(5));
    const Vector second_twentieth = Ops::div(second, Ops::set(20));
    Ops::store(fifths + lane, first_fifth);
    Ops::store(two_fifths + lane, twice_first_fifth);
    Ops::store(twentieths + lane, second_twentieth);
}

// Lanes [first, last): whole lane groups, then the scalar tail.
template<class Scalar>
void jet_lanes(const Scalar* const source[5], Scalar* fifths, Scalar* two_fifths, Scalar* twentieths,
               std::size_t first, std::size_t last) noexcept {
    using Ops = NativeOps<Scalar>;
    // An empty range reads no parameter: callers may pass null arrays with
    // no lanes.
    if (first >= last) { return; }
    // Read once, before the loop: the x86-64 unaligned vector stores
    // (_mm_storeu_ps, _mm_storeu_pd) may alias any object, so a pointer or
    // weight read through a parameter array inside the loop is reloaded after
    // every store. Local copies are not (x86-64 review, 2026-11-01).
    const std::array<const Scalar*, 5> sources{source[0], source[1], source[2], source[3], source[4]};
    std::size_t lane = first;
    for (; lane + Ops::width <= last; lane += Ops::width) {
        jet_group<Ops>(sources.data(), fifths, two_fifths, twentieths, lane);
    }
    for (; lane < last; ++lane) {
        jet_group<ScalarOps<Scalar>>(sources.data(), fifths, two_fifths, twentieths, lane);
    }
}

} // namespace stx_conv::kernels::detail
