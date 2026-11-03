#pragma once
// Five-current admission of one cell: preclipped unsigned box bounds and the
// fixed five-capacity threshold correction. The derivation (from the
// 2026-10-29 CONV study, not retained) is restated at capacity_threshold.
// Each vector lane is a distinct line or channel; the five array slots are
// the cell's polynomial currents, never SIMD lanes, so no lane moves.
#include "conv_simd_backend.hpp"

namespace stx_conv::kernels::detail {
inline namespace STX_CONV_ISA {

template<class Ops>
void order_pair(typename Ops::Value& a, typename Ops::Value& b) noexcept {
    const typename Ops::Value low = Ops::min(a, b);
    const typename Ops::Value high = Ops::max(a, b);
    a = low;
    b = high;
}

// The correction threshold for five capacities w and a mass in
// [0, sum(w)]. For a fixed saturated-set size the smallest capacities
// maximize the form (mass - sum) / (5 - size), so the maximum over all 31
// subsets equals the maximum of five prefix forms after ordering. A fixed
// nine-comparator network orders the copy; no lane takes a different path.
template<class Ops>
typename Ops::Value capacity_threshold(std::array<typename Ops::Value, 5> w, typename Ops::Value mass) noexcept {
    using Vector = typename Ops::Value;
    order_pair<Ops>(w[0], w[1]);
    order_pair<Ops>(w[3], w[4]);
    order_pair<Ops>(w[2], w[4]);
    order_pair<Ops>(w[2], w[3]);
    order_pair<Ops>(w[1], w[4]);
    order_pair<Ops>(w[0], w[3]);
    order_pair<Ops>(w[0], w[2]);
    order_pair<Ops>(w[1], w[3]);
    order_pair<Ops>(w[1], w[2]);
    Vector result = Ops::div(mass, Ops::set(5));
    Vector sum = w[0];
    result = Ops::max(result, Ops::div(Ops::sub(mass, sum), Ops::set(4)));
    sum = Ops::add(sum, w[1]);
    result = Ops::max(result, Ops::div(Ops::sub(mass, sum), Ops::set(3)));
    sum = Ops::add(sum, w[2]);
    result = Ops::max(result, Ops::div(Ops::sub(mass, sum), Ops::set(2)));
    sum = Ops::add(sum, w[3]);
    result = Ops::max(result, Ops::sub(mass, sum));
    return result;
}

// max(x, 0).
template<class Ops>
STX_CONV_INLINE typename Ops::Value positive_part(typename Ops::Value x) noexcept {
    const typename Ops::Value result = Ops::max(x, Ops::set(0));
    return result;
}

// The allowances an end of the cell grants each sign: the neighbouring
// secant's part of that sign, limited by the end current's part of it less
// the neighbouring current's part of the opposite sign.
template<class Ops>
STX_CONV_INLINE typename Ops::Value positive_allowance(typename Ops::Value secant, typename Ops::Value end_current,
                                                       typename Ops::Value neighbour_current) noexcept {
    using Vector = typename Ops::Value;
    const Vector zero = Ops::set(0);
    const Vector opposite = positive_part<Ops>(Ops::sub(zero, neighbour_current));
    const Vector room = Ops::sub(positive_part<Ops>(end_current), opposite);
    const Vector result = Ops::min(positive_part<Ops>(secant), positive_part<Ops>(room));
    return result;
}
template<class Ops>
STX_CONV_INLINE typename Ops::Value negative_allowance(typename Ops::Value secant, typename Ops::Value end_current,
                                                       typename Ops::Value neighbour_current) noexcept {
    using Vector = typename Ops::Value;
    const Vector zero = Ops::set(0);
    const Vector opposite = positive_part<Ops>(neighbour_current);
    const Vector room = Ops::sub(positive_part<Ops>(Ops::sub(zero, end_current)), opposite);
    const Vector result = Ops::min(positive_part<Ops>(Ops::sub(zero, secant)), positive_part<Ops>(room));
    return result;
}

// One lane group at lane offset `lane`. raw: the cell's five raw current
// streams. y0, y1: the cell's nodes; y2: the next node (y1 at the last cell).
// next: the following cell's first raw current. left_delta, left_last: the
// preceding cell's secant and last raw current, replaced by this cell's.
// output: five admitted current streams. All streams are disjoint.
template<class Ops, class Scalar>
STX_CONV_INLINE void admit_group(const Scalar* const raw[5], Scalar* const output[5], const Scalar* y0,
                                 const Scalar* y1, const Scalar* y2, const Scalar* next, Scalar* left_delta,
                                 Scalar* left_last, std::size_t lane) noexcept {
    using Vector = typename Ops::Value;
    const Vector zero = Ops::set(0);
    std::array<Vector, 5> a{};
    std::array<Vector, 5> positive_room{};
    std::array<Vector, 5> negative_room{};
    for (std::size_t current = 0; current < 5; ++current) { a[current] = Ops::load(raw[current] + lane); }
    const Vector delta = Ops::sub(Ops::load(y1 + lane), Ops::load(y0 + lane));
    const Vector following_delta = Ops::sub(Ops::load(y2 + lane), Ops::load(y1 + lane));
    const Vector preceding_delta = Ops::load(left_delta + lane);
    const Vector preceding_last = Ops::load(left_last + lane);
    const Vector following_first = Ops::load(next + lane);
    // Head allowances run forward through the currents, tail allowances
    // backward; each slot's room is their sum.
    Vector head_positive = positive_allowance<Ops>(preceding_delta, a[0], preceding_last);
    Vector head_negative = negative_allowance<Ops>(preceding_delta, a[0], preceding_last);
    for (std::size_t current = 0; current < 5; ++current) {
        head_positive = Ops::min(head_positive, positive_part<Ops>(a[current]));
        head_negative = Ops::min(head_negative, positive_part<Ops>(Ops::sub(zero, a[current])));
        positive_room[current] = head_positive;
        negative_room[current] = head_negative;
    }
    Vector tail_positive = positive_allowance<Ops>(following_delta, a[4], following_first);
    Vector tail_negative = negative_allowance<Ops>(following_delta, a[4], following_first);
    for (std::size_t current = 5; current != 0; --current) {
        tail_positive = Ops::min(tail_positive, positive_part<Ops>(a[current - 1]));
        tail_negative = Ops::min(tail_negative, positive_part<Ops>(Ops::sub(zero, a[current - 1])));
        positive_room[current - 1] = Ops::add(positive_room[current - 1], tail_positive);
        negative_room[current - 1] = Ops::add(negative_room[current - 1], tail_negative);
    }
    Vector positive_total = zero;
    Vector negative_total = zero;
    for (std::size_t current = 0; current < 5; ++current) {
        positive_total = Ops::add(positive_total, positive_room[current]);
        negative_total = Ops::add(negative_total, negative_room[current]);
    }
    const Vector plus = positive_part<Ops>(delta);
    const Vector minus = positive_part<Ops>(Ops::sub(zero, delta));
    const Vector upper_base = Ops::add(plus, Ops::min(negative_total, Ops::mul(Ops::set(4), plus)));
    const Vector lower_base = Ops::add(minus, Ops::min(positive_total, Ops::mul(Ops::set(4), minus)));
    Ops::store(left_delta + lane, delta);
    Ops::store(left_last + lane, a[4]);
    // Preclip each current into its box; the rooms become the distances to
    // the box's lower and upper faces.
    Vector sum = zero;
    for (std::size_t current = 0; current < 5; ++current) {
        const Vector high = Ops::add(positive_room[current], upper_base);
        const Vector low = Ops::sub(zero, Ops::add(negative_room[current], lower_base));
        a[current] = Ops::min(high, Ops::max(low, a[current]));
        positive_room[current] = Ops::sub(a[current], low);
        negative_room[current] = Ops::sub(high, a[current]);
        sum = Ops::add(sum, a[current]);
    }
    const Vector residual = Ops::sub(sum, delta);
    // Only one correction direction is active per lane: the capacities toward
    // the face that removes the residual, kept in their original slots (the
    // threshold orders a copy).
    std::array<Vector, 5> capacity{};
    for (std::size_t current = 0; current < 5; ++current) {
        capacity[current] = Ops::choose_negative(residual, negative_room[current], positive_room[current]);
    }
    const Vector mass = Ops::max(residual, Ops::sub(zero, residual));
    const Vector threshold = capacity_threshold<Ops>(capacity, mass);
    for (std::size_t current = 0; current < 5; ++current) {
        const Vector correction = Ops::min(threshold, capacity[current]);
        const Vector down = Ops::choose_negative(residual, zero, correction);
        const Vector up = Ops::choose_negative(residual, correction, zero);
        const Vector result = Ops::add(Ops::sub(a[current], down), up);
        Ops::store(output[current] + lane, result);
    }
}

// Lanes [0, count): whole lane groups, then the scalar tail.
template<class Scalar>
void admit_lanes(const Scalar* const raw[5], Scalar* const output[5], const Scalar* y0, const Scalar* y1,
                 const Scalar* y2, const Scalar* next, Scalar* left_delta, Scalar* left_last, std::size_t count) noexcept {
    using Ops = LaneOps<Scalar>;
    // An empty range reads no parameter: callers may pass null arrays with
    // no lanes.
    if (count == 0) { return; }
    // Read once, before the loop: the x86-64 unaligned vector stores
    // (_mm_storeu_ps, _mm_storeu_pd) may alias any object, so a pointer or
    // weight read through a parameter array inside the loop is reloaded after
    // every store. Local copies are not (x86-64 review, 2026-11-01).
    const std::array<const Scalar*, 5> raws{raw[0], raw[1], raw[2], raw[3], raw[4]};
    const std::array<Scalar*, 5> outputs{output[0], output[1], output[2], output[3], output[4]};
    std::size_t lane = 0;
    for (; lane + Ops::width <= count; lane += Ops::width) {
        admit_group<Ops>(raws.data(), outputs.data(), y0, y1, y2, next, left_delta, left_last, lane);
    }
    for (; lane < count; ++lane) {
        admit_group<ScalarOps<Scalar>>(raws.data(), outputs.data(), y0, y1, y2, next, left_delta, left_last, lane);
    }
}

} // inline namespace STX_CONV_ISA
} // namespace stx_conv::kernels::detail
