#pragma once
// The CONV line cell for a scalar type (binary64, or binary64 with one
// forward derivative), shared by surface.cpp's line cells and surfaces.
//
// One cell of a CONV line is a function of the six samples y[i-2..i+3]
// alone (of y[i-3..i+4] next to a seam): two jets propose a quintic, its
// five currents are admitted into the unsigned neighbour box and the mass is
// restored by one capped common shift (preclipped box admission with the
// five-capacity threshold), and a bubble term may be added. The formulas are
// those of the stream kernels (docs/KERNELS.md) restated for seams with
// cubic-exact end stencils and for a bubble bound that is continuous in the
// data; both come from the conv-teapot study (PROVENANCE.md).
//
// A derivative is carried through admission by taking, at every choice of
// one of two values, the derivative of the value chosen: admission is
// piecewise affine in its inputs and this is its derivative wherever one
// exists.

#include <stx_conv/surface.hpp>

#include <array>
#include <cstddef>
#include <limits>
#include <span>

namespace stx_conv::surface::detail {

// A value with its derivative in one parameter.
struct Dual1 final {
    double value = 0.0;
    double derivative = 0.0;

    Dual1() noexcept = default;
    explicit Dual1(double constant) noexcept : value(constant) {}
    Dual1(double constant, double slope) noexcept : value(constant), derivative(slope) {}
};

inline Dual1 operator+(const Dual1& a, const Dual1& b) noexcept {
    const Dual1 sum{a.value + b.value, a.derivative + b.derivative};
    return sum;
}
inline Dual1 operator-(const Dual1& a, const Dual1& b) noexcept {
    const Dual1 difference{a.value - b.value, a.derivative - b.derivative};
    return difference;
}
inline Dual1 operator*(const Dual1& a, const Dual1& b) noexcept {
    const Dual1 product{a.value * b.value, a.derivative * b.value + a.value * b.derivative};
    return product;
}
inline Dual1 operator+(double k, const Dual1& a) noexcept {
    const Dual1 sum{k + a.value, a.derivative};
    return sum;
}
inline Dual1 operator*(double k, const Dual1& a) noexcept {
    const Dual1 product{k * a.value, k * a.derivative};
    return product;
}
inline Dual1 operator*(const Dual1& a, double k) noexcept {
    const Dual1 product{k * a.value, k * a.derivative};
    return product;
}
inline Dual1 operator/(const Dual1& a, double k) noexcept {
    const Dual1 quotient{a.value / k, a.derivative / k};
    return quotient;
}

inline double value_of(double x) noexcept { return x; }
inline double value_of(const Dual1& x) noexcept { return x.value; }

// min and max select one argument whole, so a dual keeps the derivative of
// the selected branch.
template<class T> T least(const T& a, const T& b) noexcept { return value_of(b) < value_of(a) ? b : a; }
template<class T> T greatest(const T& a, const T& b) noexcept { return value_of(b) > value_of(a) ? b : a; }
// [x]+ and [x]-.
template<class T> T positive_part(const T& x) noexcept { return value_of(x) > 0.0 ? x : T(0.0); }
template<class T> T negative_part(const T& x) noexcept { return value_of(x) < 0.0 ? T(0.0) - x : T(0.0); }

// A cell's admitted form: y[i], five currents summing to y[i+1] - y[i], and
// the amplitude of psi(t) = [t(1-t)]^3 (2t-1).
template<class T> struct Cell final {
    T anchor{};
    std::array<T, 5> currents{};
    T bubble{};
};

// The scaled jet (m, q) of the node at window[centre] from its neighbours
// window[centre-3 .. centre+3]; centre is 3 or 4, so every read is inside
// the eight-sample window.
template<class T> void node_jet(std::span<const T, 8> window, std::size_t centre, Jet kind, T& m, T& q) noexcept {
    const T& before3 = window[centre >= 3U ? centre - 3U : 0U];
    const T& before2 = window[centre - 2U];
    const T& before1 = window[centre - 1U];
    const T& here = window[centre];
    const T& after1 = window[centre + 1U];
    const T& after2 = window[centre + 2U];
    const T& after3 = window[centre + 3U <= 7U ? centre + 3U : 7U];
    switch (kind) {
    case Jet::interior:
        m = (before2 - 8.0 * before1 + 8.0 * after1 - after2) / 12.0;
        q = (16.0 * after1 - after2 - 30.0 * here + 16.0 * before1 - before2) / 12.0;
        return;
    case Jet::paper_first:
        m = (4.0 * after1 - 3.0 * here - after2) / 2.0;
        q = 2.0 * here - 5.0 * after1 + 4.0 * after2 - after3;
        return;
    case Jet::paper_central:
    case Jet::central3:
        m = (after1 - before1) / 2.0;
        q = before1 - 2.0 * here + after1;
        return;
    case Jet::paper_last:
        m = (3.0 * here - 4.0 * before1 + before2) / 2.0;
        q = 2.0 * here - 5.0 * before1 + 4.0 * before2 - before3;
        return;
    case Jet::first4:
        m = (18.0 * after1 - 11.0 * here - 9.0 * after2 + 2.0 * after3) / 6.0;
        q = 2.0 * here - 5.0 * after1 + 4.0 * after2 - after3;
        return;
    case Jet::second4:
        m = (6.0 * after1 - 2.0 * before1 - 3.0 * here - after2) / 6.0;
        q = before1 - 2.0 * here + after1;
        return;
    case Jet::penultimate4:
        m = (2.0 * after1 + 3.0 * here - 6.0 * before1 + before2) / 6.0;
        q = before1 - 2.0 * here + after1;
        return;
    case Jet::last4:
        m = (11.0 * here - 18.0 * before1 + 9.0 * before2 - 2.0 * before3) / 6.0;
        q = 2.0 * here - 5.0 * before1 + 4.0 * before2 - before3;
        return;
    case Jet::first3:
        m = (4.0 * after1 - 3.0 * here - after2) / 2.0;
        q = here - 2.0 * after1 + after2;
        return;
    case Jet::last3:
        m = (3.0 * here - 4.0 * before1 + before2) / 2.0;
        q = here - 2.0 * before1 + before2;
        return;
    case Jet::first2:
        m = after1 - here;
        q = T(0.0);
        return;
    case Jet::last2:
        m = here - before1;
        q = T(0.0);
        return;
    default:
        // Unreachable for a shape made by cell_shape: the secant, as the
        // shortest segment has.
        m = after1 - here;
        q = T(0.0);
        return;
    }
}

template<class T> void order_pair(T& a, T& b) noexcept {
    const T low = least(a, b);
    const T high = greatest(a, b);
    a = low;
    b = high;
}

// The capacity threshold: the largest of five prefix forms after a
// nine-comparator sort.
template<class T> T capacity_threshold(std::array<T, 5> capacity, const T& mass) noexcept {
    // The first form is the largest whenever the mass is under five times
    // the smallest capacity (each later form is then smaller than the one
    // before it); 4.5 for 5 keeps the comparison clear of rounding. That is
    // every cell whose currents were not clipped, and the answer the sort
    // below would give.
    double smallest = value_of(capacity[0]);
    for (std::size_t j = 1U; j < 5U; ++j) {
        const double candidate = value_of(capacity[j]);
        smallest = candidate < smallest ? candidate : smallest;
    }
    if (value_of(mass) < 4.5 * smallest) {
        const T even = mass / 5.0;
        return even;
    }
    order_pair(capacity[0], capacity[1]);
    order_pair(capacity[3], capacity[4]);
    order_pair(capacity[2], capacity[4]);
    order_pair(capacity[2], capacity[3]);
    order_pair(capacity[1], capacity[4]);
    order_pair(capacity[0], capacity[3]);
    order_pair(capacity[0], capacity[2]);
    order_pair(capacity[1], capacity[3]);
    order_pair(capacity[1], capacity[2]);
    T result = mass / 5.0;
    T sum = capacity[0];
    result = greatest(result, (mass - sum) / 4.0);
    sum = sum + capacity[1];
    result = greatest(result, (mass - sum) / 3.0);
    sum = sum + capacity[2];
    result = greatest(result, (mass - sum) / 2.0);
    sum = sum + capacity[3];
    result = greatest(result, mass - sum);
    return result;
}

// The standard (one-sided) bubble bound: each touched coefficient keeps its
// sign. A coefficient is d - amplitude/5 (subtracts) or d + 2 amplitude/5.
template<class T> struct StandardBound final {
    T low{-std::numeric_limits<double>::infinity()};
    T high{std::numeric_limits<double>::infinity()};
    bool pinned = false;
};

template<class T> void narrow_standard_bound(StandardBound<T>& bound, const T& coefficient, double scale,
                                             bool amplitude_subtracts) noexcept {
    const double sign_value = value_of(coefficient);
    if (sign_value == 0.0) {
        bound.pinned = true;
        return;
    }
    const T edge = scale * coefficient;
    const bool raises_high = amplitude_subtracts == (sign_value > 0.0);
    if (raises_high) {
        bound.high = least(bound.high, edge);
    } else {
        bound.low = greatest(bound.low, edge);
    }
}

// One cell from its window w[0..7] = y[i-3..i+4]; the cell lies between w[3]
// and w[4]. Entries outside an open line are never read (they may hold
// anything finite).
template<class T> Cell<T> compile_cell(std::span<const T, 8> w, const CellShape& shape, Bubble bubble) noexcept {
    Cell<T> cell;
    cell.anchor = w[3];
    const T zero(0.0);
    T left_m(0.0);
    T left_q(0.0);
    T right_m(0.0);
    T right_q(0.0);
    node_jet(w, 3U, shape.left, left_m, left_q);
    node_jet(w, 4U, shape.right, right_m, right_q);
    const T delta = w[4] - w[3];
    // Raw currents: differences of the six quintic controls.
    std::array<T, 5> raw{};
    raw[0] = left_m / 5.0;
    raw[1] = left_m / 5.0 + left_q / 20.0;
    raw[3] = right_m / 5.0 - right_q / 20.0;
    raw[4] = right_m / 5.0;
    raw[2] = delta - raw[0] - raw[1] - raw[3] - raw[4];

    // The unsigned neighbour box, from the neighbouring cells' deltas and
    // terminal raw currents (away from a seam those equal raw[0] and raw[4]).
    const T previous_delta = shape.has_previous ? w[3] - w[2] : zero;
    const T next_delta = shape.has_next ? w[5] - w[4] : zero;
    T previous_last = zero;
    T next_first = zero;
    if (shape.has_previous) {
        T m = left_m;
        T unused(0.0);
        if (shape.previous != shape.left) node_jet(w, 3U, shape.previous, m, unused);
        previous_last = m / 5.0;
    }
    if (shape.has_next) {
        T m = right_m;
        T unused(0.0);
        if (shape.next != shape.right) node_jet(w, 4U, shape.next, m, unused);
        next_first = m / 5.0;
    }
    std::array<T, 5> room_positive{};
    std::array<T, 5> room_negative{};
    T head_positive = least(positive_part(previous_delta), positive_part(positive_part(raw[0]) - negative_part(previous_last)));
    T head_negative = least(negative_part(previous_delta), positive_part(negative_part(raw[0]) - positive_part(previous_last)));
    for (std::size_t j = 0U; j < 5U; ++j) {
        head_positive = least(head_positive, positive_part(raw[j]));
        head_negative = least(head_negative, negative_part(raw[j]));
        room_positive[j] = head_positive;
        room_negative[j] = head_negative;
    }
    T tail_positive = least(positive_part(next_delta), positive_part(positive_part(raw[4]) - negative_part(next_first)));
    T tail_negative = least(negative_part(next_delta), positive_part(negative_part(raw[4]) - positive_part(next_first)));
    for (std::size_t step = 0U; step < 5U; ++step) {
        const std::size_t j = 4U - step;
        tail_positive = least(tail_positive, positive_part(raw[j]));
        tail_negative = least(tail_negative, negative_part(raw[j]));
        room_positive[j] = room_positive[j] + tail_positive;
        room_negative[j] = room_negative[j] + tail_negative;
    }
    T total_positive = zero;
    T total_negative = zero;
    for (std::size_t j = 0U; j < 5U; ++j) {
        total_positive = total_positive + room_positive[j];
        total_negative = total_negative + room_negative[j];
    }
    const T plus = positive_part(delta);
    const T minus = negative_part(delta);
    const T upper_base = plus + least(total_negative, 4.0 * plus);
    const T lower_base = minus + least(total_positive, 4.0 * minus);

    // Preclip, then restore the mass by one capped common shift.
    std::array<T, 5> clipped{};
    std::array<T, 5> down{};
    std::array<T, 5> up{};
    T sum = zero;
    for (std::size_t j = 0U; j < 5U; ++j) {
        const T high = room_positive[j] + upper_base;
        const T low = zero - (room_negative[j] + lower_base);
        clipped[j] = least(high, greatest(low, raw[j]));
        down[j] = clipped[j] - low;
        up[j] = high - clipped[j];
        sum = sum + clipped[j];
    }
    const T residual = sum - delta;
    const bool lower = value_of(residual) > 0.0;
    const std::array<T, 5>& capacity = lower ? down : up;
    const T mass = lower ? residual : zero - residual;
    const T threshold = capacity_threshold(capacity, mass);
    for (std::size_t j = 0U; j < 5U; ++j) {
        const T shift = least(threshold, capacity[j]);
        cell.currents[j] = lower ? clipped[j] - shift : clipped[j] + shift;
    }
    if (bubble == Bubble::none) return cell;

    // The fitted amplitude: the L2 projection onto psi of the six-sample
    // quintic minus the admitted quintic.
    T fit = zero;
    if (shape.six) {
        const T fifth = (w[6] - w[1]) + 5.0 * (w[2] - w[5]) + 10.0 * (w[4] - w[3]);
        fit = (-41.0 / 30.0) * fifth;
    }
    constexpr std::array<double, 5> projection{35.0, 87.5, 112.5, 87.5, 35.0};
    for (std::size_t j = 0U; j < 5U; ++j) fit = fit + projection[j] * (raw[j] - cell.currents[j]);
    // The admitted derivative's degree-six coefficients at the three
    // positions the bubble touches.
    const std::array<T, 5>& c = cell.currents;
    const T d2 = (c[0] + 8.0 * c[1] + 6.0 * c[2]) / 3.0;
    const T d3 = c[1] + 3.0 * c[2] + c[3];
    const T d4 = (6.0 * c[2] + 8.0 * c[3] + c[4]) / 3.0;
    if (bubble == Bubble::continuous) {
        // |amplitude| <= min(5|d2|, 5|d3|/2, 5|d4|): no coefficient changes
        // sign, and the bound is continuous in the data.
        const T bound2 = 5.0 * greatest(d2, zero - d2);
        const T bound3 = 2.5 * greatest(d3, zero - d3);
        const T bound4 = 5.0 * greatest(d4, zero - d4);
        const T bound = least(least(bound2, bound3), bound4);
        cell.bubble = least(bound, greatest(zero - bound, fit));
        return cell;
    }
    StandardBound<T> standard;
    narrow_standard_bound(standard, d2, 5.0, true);
    narrow_standard_bound(standard, d3, -2.5, false);
    narrow_standard_bound(standard, d4, 5.0, true);
    cell.bubble = standard.pinned ? zero : least(standard.high, greatest(standard.low, fit));
    return cell;
}

// The six functions a cell combines at local parameter t: tau[j], the sum
// of the Bernstein polynomials B_r^5 for r > j, and psi.
template<class U> struct Basis final {
    std::array<U, 5> tau{};
    U psi{};
};

template<class U> Basis<U> make_basis(const U& t) noexcept {
    Basis<U> basis;
    const U s = U(1.0) - t;
    const U t2 = t * t;
    const U t3 = t2 * t;
    const U t4 = t2 * t2;
    const U s2 = s * s;
    const U s3 = s2 * s;
    const U s4 = s2 * s2;
    basis.tau[4] = t4 * t;
    basis.tau[3] = basis.tau[4] + 5.0 * (t4 * s);
    basis.tau[2] = basis.tau[3] + 10.0 * (t3 * s2);
    basis.tau[1] = basis.tau[2] + 10.0 * (t2 * s3);
    basis.tau[0] = basis.tau[1] + 5.0 * (t * s4);
    basis.psi = (t3 * s3) * (2.0 * t - U(1.0));
    return basis;
}

// The value of a binary64 cell for a prepared basis of type U (binary64, or
// carrying the derivative along the cell).
template<class U> U evaluate_cell(const Cell<double>& cell, const Basis<U>& basis) noexcept {
    U value = cell.anchor + cell.currents[0] * basis.tau[0];
    value = value + cell.currents[1] * basis.tau[1];
    value = value + cell.currents[2] * basis.tau[2];
    value = value + cell.currents[3] * basis.tau[3];
    value = value + cell.currents[4] * basis.tau[4];
    value = value + cell.bubble * basis.psi;
    return value;
}

} // namespace stx_conv::surface::detail
