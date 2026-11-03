#pragma once
// Tensor quintic evaluation: the 6x6 Bernstein (quintic Bezier) patch
// sum_y B_y(v) sum_x B_x(u) control[y][x], in two forms.
// - tensor_group: independent positions in the lanes, one stream per control
//   (structure of arrays).
// - patch_rgba: one position of a known RGBA atlas patch, the four channels
//   in the lanes. Exact grid lines reduce to one six-control curve and exact
//   nodes to one control; that geometry is decided once per query, before any
//   channel arithmetic.
#include "conv_simd_backend.hpp"
#include <algorithm>
#include <type_traits>

namespace stx_conv::kernels::detail {
inline namespace STX_CONV_ISA {

// The six quintic Bernstein weights at t, s = 1 - t: s^5, 5 s^4 t,
// 10 s^3 t^2, 10 s^2 t^3, 5 s t^4, t^5.
template<class Ops>
STX_CONV_INLINE void quintic_basis(typename Ops::Value t, std::array<typename Ops::Value, 6>& basis) noexcept {
    using Vector = typename Ops::Value;
    const Vector s = Ops::sub(Ops::set(1), t);
    const Vector t2 = Ops::mul(t, t);
    const Vector t3 = Ops::mul(t2, t);
    const Vector s2 = Ops::mul(s, s);
    const Vector s3 = Ops::mul(s2, s);
    basis[0] = Ops::mul(s3, s2);
    basis[1] = Ops::mul(Ops::mul(Ops::set(5), Ops::mul(s3, s)), t);
    basis[2] = Ops::mul(Ops::mul(Ops::set(10), s3), t2);
    basis[3] = Ops::mul(Ops::mul(Ops::set(10), s2), t3);
    basis[4] = Ops::mul(Ops::mul(Ops::set(5), s), Ops::mul(t3, t));
    basis[5] = Ops::mul(t3, t2);
}

// controls[y * 6 + x] is a stream over positions; u and v likewise.
template<class Ops, class Scalar>
STX_CONV_INLINE void tensor_group(const Scalar* const controls[36], const Scalar* u, const Scalar* v,
                                  Scalar* output, std::size_t lane) noexcept {
    using Vector = typename Ops::Value;
    std::array<Vector, 6> basis_x{};
    std::array<Vector, 6> basis_y{};
    quintic_basis<Ops>(Ops::load(u + lane), basis_x);
    quintic_basis<Ops>(Ops::load(v + lane), basis_y);
    Vector sum = Ops::set(0);
    for (std::size_t y = 0; y < 6; ++y) {
        Vector row = Ops::set(0);
        for (std::size_t x = 0; x < 6; ++x) {
            const Vector control = Ops::load(controls[y * 6 + x] + lane);
            row = Ops::add(row, Ops::mul(basis_x[x], control));
        }
        sum = Ops::add(sum, Ops::mul(basis_y[y], row));
    }
    Ops::store(output + lane, sum);
}

// RGBA lane groups in one control: one group of four floats, or two of two
// doubles.
template<class Scalar>
inline constexpr std::size_t rgba_groups = 4 / NativeOps<Scalar>::width;

// One six-control curve through RGBA controls `control_stride` apart.
template<class Scalar>
void curve_rgba(const Scalar* origin, std::size_t control_stride, Scalar t, Scalar* output) noexcept {
    using Ops = NativeOps<Scalar>;
    using Vector = typename Ops::Value;
    std::array<Scalar, 6> basis{};
    quintic_basis<ScalarOps<Scalar>>(t, basis);
    for (std::size_t channel = 0; channel < 4; channel += Ops::width) {
        Vector sum = Ops::set(0);
        for (std::size_t control = 0; control < 6; ++control) {
            const Vector value = Ops::load(origin + control * control_stride + channel);
            sum = Ops::add(sum, Ops::mul(Ops::set(basis[control]), value));
        }
        Ops::store(output + channel, sum);
    }
}

// The 36-control contraction with prepared weights: basis_x[k * stride] and
// basis_y[k * stride] are the six quintic weights in u and v. Each row of six
// controls is summed first, then the rows, in index order from +0.
template<class Scalar>
STX_CONV_INLINE std::array<typename NativeOps<Scalar>::Value, rgba_groups<Scalar>>
weighted_patch_rgba(const Scalar* origin, std::size_t row_stride, const Scalar* basis_x, const Scalar* basis_y,
                    std::size_t basis_stride) noexcept {
    using Ops = NativeOps<Scalar>;
    using Vector = typename Ops::Value;
    constexpr std::size_t groups = rgba_groups<Scalar>;
    std::array<Vector, groups> sums{};
    for (std::size_t group = 0; group < groups; ++group) { sums[group] = Ops::set(0); }
    for (std::size_t y = 0; y < 6; ++y) {
        std::array<Vector, groups> rows{};
        for (std::size_t group = 0; group < groups; ++group) { rows[group] = Ops::set(0); }
        for (std::size_t x = 0; x < 6; ++x) {
            const Vector weight = Ops::set(basis_x[x * basis_stride]);
            const Scalar* control = origin + y * row_stride + x * 4;
            for (std::size_t group = 0; group < groups; ++group) {
                const Vector value = Ops::load(control + group * Ops::width);
                rows[group] = Ops::add(rows[group], Ops::mul(weight, value));
            }
        }
        const Vector weight = Ops::set(basis_y[y * basis_stride]);
        for (std::size_t group = 0; group < groups; ++group) {
            sums[group] = Ops::add(sums[group], Ops::mul(weight, rows[group]));
        }
    }
    return sums;
}

// A channel-lane value weighted by lane Lane of a pixel-lane weight vector.
// Pixel lanes as wide as the channel lanes use mul_lane; a single pixel (the
// tail of a row, Ops = ScalarOps) broadcasts its one weight. Either way the
// product is weight * value, correctly rounded.
template<class Ops, std::size_t Lane, class Scalar>
STX_CONV_INLINE typename NativeOps<Scalar>::Value weigh_channels(typename NativeOps<Scalar>::Value value,
                                                                 typename Ops::Value weights) noexcept {
    using ChannelOps = NativeOps<Scalar>;
    using Channels = typename ChannelOps::Value;
    if constexpr (std::is_same_v<Ops, ChannelOps>) {
        const Channels result = ChannelOps::template mul_lane<Lane>(value, weights);
        return result;
    } else {
        static_assert(Ops::width == 1, "a pixel group narrower than the channel lanes is one pixel");
        const Channels result = ChannelOps::mul(ChannelOps::set(weights), value);
        return result;
    }
}

// weighted_patch_rgba for pixel lane Lane of pixel-lane bases: the same
// products and sums in the same order, so the same bits, but each weight is
// taken from its basis vector by lane instead of from memory.
template<class Ops, std::size_t Lane, class Scalar>
STX_CONV_INLINE std::array<typename NativeOps<Scalar>::Value, rgba_groups<Scalar>>
lane_patch_rgba(const Scalar* origin, std::size_t row_stride, const std::array<typename Ops::Value, 6>& basis_x,
                const std::array<typename Ops::Value, 6>& basis_y) noexcept {
    using ChannelOps = NativeOps<Scalar>;
    using Channels = typename ChannelOps::Value;
    constexpr std::size_t groups = rgba_groups<Scalar>;
    std::array<Channels, groups> sums{};
    for (std::size_t group = 0; group < groups; ++group) { sums[group] = ChannelOps::set(0); }
    for (std::size_t y = 0; y < 6; ++y) {
        std::array<Channels, groups> rows{};
        for (std::size_t group = 0; group < groups; ++group) { rows[group] = ChannelOps::set(0); }
        for (std::size_t x = 0; x < 6; ++x) {
            const Scalar* control = origin + y * row_stride + x * 4;
            for (std::size_t group = 0; group < groups; ++group) {
                const Channels value = ChannelOps::load(control + group * ChannelOps::width);
                rows[group] = ChannelOps::add(rows[group], weigh_channels<Ops, Lane, Scalar>(value, basis_x[x]));
            }
        }
        for (std::size_t group = 0; group < groups; ++group) {
            sums[group] = ChannelOps::add(sums[group], weigh_channels<Ops, Lane, Scalar>(rows[group], basis_y[y]));
        }
    }
    return sums;
}

// The full 36-control patch at an interior (u, v); rows of six RGBA controls
// `row_stride` Scalars apart.
template<class Scalar>
STX_CONV_INLINE void patch_full_rgba(const Scalar* origin, std::size_t row_stride, Scalar u, Scalar v,
                                     Scalar* output) noexcept {
    using Ops = NativeOps<Scalar>;
    using Vector = typename Ops::Value;
    constexpr std::size_t groups = rgba_groups<Scalar>;
    std::array<Scalar, 6> basis_x{};
    std::array<Scalar, 6> basis_y{};
    quintic_basis<ScalarOps<Scalar>>(u, basis_x);
    quintic_basis<ScalarOps<Scalar>>(v, basis_y);
    const std::array<Vector, groups> sums = weighted_patch_rgba(origin, row_stride, basis_x.data(), basis_y.data(), 1);
    for (std::size_t group = 0; group < groups; ++group) { Ops::store(output + group * Ops::width, sums[group]); }
}

// One known atlas patch at (u, v) in [0, 1]^2, RGBA contiguous per control.
// The caller has located and bounds-checked the patch; output has four
// Scalars. A node copies one control (u and v both 0 or 1); a grid line uses
// one curve of six; anything else uses all 36.
template<class Scalar>
void patch_rgba(const Scalar* origin, std::size_t row_stride, Scalar u, Scalar v, Scalar* output) noexcept {
    const bool u_on_line = u == Scalar(0) || u == Scalar(1);
    const bool v_on_line = v == Scalar(0) || v == Scalar(1);
    if (u_on_line) {
        const std::size_t column_offset = u == Scalar(1) ? 20 : 0;
        const Scalar* column = origin + column_offset;
        if (v_on_line) {
            const std::size_t row_offset = v == Scalar(1) ? 5 * row_stride : 0;
            std::copy_n(column + row_offset, 4, output);
            return;
        }
        curve_rgba(column, row_stride, v, output);
        return;
    }
    if (v_on_line) {
        const std::size_t row_offset = v == Scalar(1) ? 5 * row_stride : 0;
        curve_rgba(origin + row_offset, 4, u, output);
        return;
    }
    patch_full_rgba(origin, row_stride, u, v, output);
}

} // inline namespace STX_CONV_ISA
} // namespace stx_conv::kernels::detail
