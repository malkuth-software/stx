// stx_conv::kernels: the public batch entry points over src/detail's
// kernels, and the checked mapping and atlas-sampling boundaries.
#include <stx_conv/kernels.hpp>
#include "detail/conv_admission_kernel.hpp"
#include "detail/conv_bubble_kernel.hpp"
#include "detail/conv_jet_kernel.hpp"
#include "detail/conv_lane_kernels.hpp"
#include "detail/conv_mapping_kernel.hpp"
#include "detail/conv_raw_current_kernel.hpp"
#include "detail/conv_synthesis_kernel.hpp"
#include "detail/conv_tensor_kernel.hpp"
#include "detail/conv_warp_kernel.hpp"
#include "internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace stx_conv::kernels {

const char* backend() noexcept {
    const char* result = detail::lane_variant_name();
    return result;
}

template<Precision Scalar>
void capacity(const Scalar* const capacities[5], const Scalar* mass, Scalar* output, std::size_t count) noexcept {
    using Ops = detail::NativeOps<Scalar>;
    using Vector = typename Ops::Value;
    // An empty range reads no parameter: callers may pass null arrays with
    // no lanes.
    if (count == 0) { return; }
    // Read once, before the loop: the x86-64 unaligned vector stores
    // (_mm_storeu_ps, _mm_storeu_pd) may alias any object, so a pointer or
    // weight read through a parameter array inside the loop is reloaded after
    // every store. Local copies are not (x86-64 review, 2026-11-01).
    const std::array<const Scalar*, 5> slots{capacities[0], capacities[1], capacities[2], capacities[3],
                                             capacities[4]};
    std::size_t lane = 0;
    for (; lane + Ops::width <= count; lane += Ops::width) {
        std::array<Vector, 5> group{};
        for (std::size_t slot = 0; slot < 5; ++slot) { group[slot] = Ops::load(slots[slot] + lane); }
        const Vector threshold = detail::capacity_threshold<Ops>(group, Ops::load(mass + lane));
        Ops::store(output + lane, threshold);
    }
    for (; lane < count; ++lane) {
        std::array<Scalar, 5> single{};
        for (std::size_t slot = 0; slot < 5; ++slot) { single[slot] = slots[slot][lane]; }
        output[lane] = detail::capacity_threshold<detail::ScalarOps<Scalar>>(single, mass[lane]);
    }
}

template<Precision Scalar>
void jet_terms(const Scalar* const source[5], Scalar* fifths, Scalar* two_fifths, Scalar* twentieths,
               std::size_t count) noexcept {
    (*detail::lane_kernels<Scalar>().jet)(source, fifths, two_fifths, twentieths, 0, count);
}

template<Precision Scalar>
void raw_currents(const Scalar* const input[8], Scalar* const output[5], std::size_t count) noexcept {
    (*detail::lane_kernels<Scalar>().raw_current)(input, output, 0, count);
}

template<Precision Scalar>
void bubble(const Scalar* raw, const Scalar* admitted, Scalar* output, std::size_t stride, std::size_t count,
            bool interior) noexcept {
    const detail::LaneKernels<Scalar>& lanes = detail::lane_kernels<Scalar>();
    if (interior) {
        (*lanes.bubble_interior)(raw, admitted, output, stride, 0, count);
        return;
    }
    (*lanes.bubble_edge)(raw, admitted, output, stride, 0, count);
}

template<Precision Scalar>
void admit_cell(const Scalar* const raw[5], Scalar* const output[5], const Scalar* y0, const Scalar* y1,
                const Scalar* y2, const Scalar* next, Scalar* left_delta, Scalar* left_last, std::size_t count) noexcept {
    (*detail::lane_kernels<Scalar>().admit)(raw, output, y0, y1, y2, next, left_delta, left_last, count);
}

template<Precision Scalar>
void synthesize(const Scalar* anchors, const Scalar* currents, std::size_t stride, const std::array<Scalar, 5>& weights,
                Scalar anchor_weight, const Scalar* bubble, Scalar bubble_weight, Scalar* output, std::size_t count,
                bool accumulate) noexcept {
    const bool with_bubble = bubble_weight != Scalar(0);
    const std::size_t bubble_index = with_bubble ? 1 : 0;
    const std::size_t accumulate_index = accumulate ? 1 : 0;
    const detail::LaneKernels<Scalar>& lanes = detail::lane_kernels<Scalar>();
    (*lanes.synthesis[bubble_index][accumulate_index])(anchors, currents, stride, weights, anchor_weight, bubble,
                                                         bubble_weight, output, 0, count);
}

template<Precision Scalar>
void tensor_quintic(const Scalar* const controls[36], const Scalar* u, const Scalar* v, Scalar* output,
                    std::size_t count) noexcept {
    using Ops = detail::NativeOps<Scalar>;
    // An empty range reads no parameter: callers may pass null arrays with
    // no lanes.
    if (count == 0) { return; }
    // Read once, before the loop: the x86-64 unaligned vector stores
    // (_mm_storeu_ps, _mm_storeu_pd) may alias any object, so a pointer or
    // weight read through a parameter array inside the loop is reloaded after
    // every store. Local copies are not (x86-64 review, 2026-11-01).
    std::array<const Scalar*, 36> control_streams{};
    std::copy(controls, controls + 36, control_streams.begin());
    std::size_t lane = 0;
    for (; lane + Ops::width <= count; lane += Ops::width) {
        detail::tensor_group<Ops>(control_streams.data(), u, v, output, lane);
    }
    for (; lane < count; ++lane) {
        detail::tensor_group<detail::ScalarOps<Scalar>>(control_streams.data(), u, v, output, lane);
    }
}

template<Precision Scalar>
void tensor_patch_rgba(const Scalar* origin, std::size_t row_stride, Scalar u, Scalar v, Scalar* output) noexcept {
    detail::patch_rgba(origin, row_stride, u, v, output);
}

template<Precision Scalar>
void integrate_patch_rgba(const Scalar* origin, std::size_t row_stride, const Scalar* u, const Scalar* v,
                          const Scalar* weights, std::size_t count, Scalar* sums) noexcept {
    if (count == 0) { return; }
    using Ops = detail::NativeOps<Scalar>;
    using Vector = typename Ops::Value;
    constexpr std::size_t groups = detail::rgba_groups<Scalar>;
    std::array<Vector, groups> accumulated{};
    for (std::size_t group = 0; group < groups; ++group) { accumulated[group] = Ops::load(sums + group * Ops::width); }
    for (std::size_t sample_index = 0; sample_index < count; ++sample_index) {
        std::array<Scalar, 4> sample{};
        detail::patch_full_rgba(origin, row_stride, u[sample_index], v[sample_index], sample.data());
        // Physical premultiplied projection before the positive weight:
        // alpha into [0, 1], then each channel into [0, alpha].
        const Vector alpha = Ops::set(std::clamp(sample[3], Scalar(0), Scalar(1)));
        const Vector weight = Ops::set(weights[sample_index]);
        for (std::size_t group = 0; group < groups; ++group) {
            const Vector raw = Ops::load(sample.data() + group * Ops::width);
            const Vector projected = Ops::min(alpha, Ops::max(raw, Ops::set(0)));
            accumulated[group] = Ops::add(accumulated[group], Ops::mul(weight, projected));
        }
    }
    for (std::size_t group = 0; group < groups; ++group) { Ops::store(sums + group * Ops::width, accumulated[group]); }
}

namespace {

template<class Scalar>
bool aliases(std::span<const Scalar> a, std::span<Scalar> b) noexcept {
    const bool result = stx_conv::detail::overlaps(a, b);
    return result;
}

// Every point's denominator and result, before any output is written.
template<Precision Scalar>
Status validate_mapping(const std::array<Scalar, 9>& h, std::span<const Scalar> x, std::span<const Scalar> y) noexcept {
    for (const Scalar coefficient : h) {
        if (!std::isfinite(coefficient)) { return Status::nonfinite_input; }
    }
    for (std::size_t point = 0; point < x.size(); ++point) {
        if (!std::isfinite(x[point]) || !std::isfinite(y[point])) { return Status::nonfinite_input; }
        const Scalar denominator = h[6] * x[point] + h[7] * y[point] + h[8];
        if (denominator == Scalar(0)) { return Status::invalid_shape; }
        if (!std::isfinite(denominator)) { return Status::numerical_failure; }
        const Scalar mapped_x = (h[0] * x[point] + h[1] * y[point] + h[2]) / denominator;
        const Scalar mapped_y = (h[3] * x[point] + h[4] * y[point] + h[5]) / denominator;
        if (!std::isfinite(mapped_x) || !std::isfinite(mapped_y)) { return Status::numerical_failure; }
    }
    return Status::ok;
}

} // namespace

template<Precision Scalar>
Status map_points(const std::array<Scalar, 9>& h, std::span<const Scalar> x, std::span<const Scalar> y,
                  std::span<Scalar> source_x, std::span<Scalar> source_y) noexcept {
    if (y.size() != x.size() || source_x.size() < x.size() || source_y.size() < x.size()) {
        return Status::insufficient_storage;
    }
    const std::span<const Scalar> coefficients(h);
    const std::span<const Scalar> source_x_view(source_x);
    const bool overlapping = aliases(coefficients, source_x) || aliases(coefficients, source_y)
        || aliases(x, source_x) || aliases(x, source_y) || aliases(y, source_x) || aliases(y, source_y)
        || aliases(source_x_view, source_y);
    if (overlapping) { return Status::overlap; }
    const Status validated = validate_mapping(h, x, y);
    if (validated != Status::ok) { return validated; }
    const bool affine = h[6] == Scalar(0) && h[7] == Scalar(0) && h[8] == Scalar(1);
    const detail::LaneKernels<Scalar>& lanes = detail::lane_kernels<Scalar>();
    if (affine) {
        (*lanes.mapping_affine)(h, x.data(), y.data(), source_x.data(), source_y.data(), x.size());
    } else {
        (*lanes.mapping_projective)(h, x.data(), y.data(), source_x.data(), source_y.data(), x.size());
    }
    return Status::ok;
}

template<Precision Scalar>
Status sample_rgba(TensorAtlasView<Scalar> atlas, std::span<const Scalar> source_x, std::span<const Scalar> source_y,
                   std::span<Scalar> rgba, ColorProjection projection) noexcept {
    if (projection != ColorProjection::none && projection != ColorProjection::premultiplied) {
        return Status::invalid_shape;
    }
    constexpr std::size_t extent_limit = 1048576;
    const bool valid_extent = atlas.source_width >= 2 && atlas.source_height >= 2
        && atlas.source_width <= extent_limit && atlas.source_height <= extent_limit;
    if (!valid_extent) { return Status::invalid_shape; }
    // Lattice nodes: five per source cell in each direction, plus one.
    const std::size_t lattice_width = 5 * (atlas.source_width - 1) + 1;
    const std::size_t lattice_height = 5 * (atlas.source_height - 1) + 1;
    const bool storage_fits = source_x.size() <= std::numeric_limits<std::size_t>::max() / 4
        && source_y.size() == source_x.size() && rgba.size() >= 4 * source_x.size()
        && stx_conv::detail::fits(lattice_height, atlas.lattice_row_stride, lattice_width * 4, atlas.controls.size());
    if (!storage_fits) { return Status::insufficient_storage; }
    if (aliases(atlas.controls, rgba) || aliases(source_x, rgba) || aliases(source_y, rgba)) { return Status::overlap; }
    const Scalar half = Scalar(0.5);
    const Scalar right_edge = Scalar(atlas.source_width) - half;
    const Scalar bottom_edge = Scalar(atlas.source_height) - half;
    for (std::size_t point = 0; point < source_x.size(); ++point) {
        Scalar* output = rgba.data() + 4 * point;
        const Scalar px = source_x[point];
        const Scalar py = source_y[point];
        // Outside the half-pixel footprint (or nonfinite): transparent.
        const bool inside = std::isfinite(px) && std::isfinite(py) && px >= -half && py >= -half
            && px < right_edge && py < bottom_edge;
        if (!inside) {
            std::fill_n(output, 4, Scalar(0));
            continue;
        }
        const Scalar x = std::clamp(px, Scalar(0), Scalar(atlas.source_width - 1));
        const Scalar y = std::clamp(py, Scalar(0), Scalar(atlas.source_height - 1));
        const std::size_t cell_x = std::min(static_cast<std::size_t>(x), atlas.source_width - 2);
        const std::size_t cell_y = std::min(static_cast<std::size_t>(y), atlas.source_height - 2);
        const Scalar* origin = atlas.controls.data() + cell_y * 5 * atlas.lattice_row_stride + cell_x * 5 * 4;
        detail::patch_rgba(origin, atlas.lattice_row_stride, x - Scalar(cell_x), y - Scalar(cell_y), output);
        if (projection == ColorProjection::premultiplied) {
            output[3] = std::clamp(output[3], Scalar(0), Scalar(1));
            for (std::size_t channel = 0; channel < 3; ++channel) {
                output[channel] = std::clamp(output[channel], Scalar(0), output[3]);
            }
        }
    }
    return Status::ok;
}

namespace {

// The map at every target pixel, in row-major order, as validate_mapping
// checks a point list. An affine map (final row 0, 0, 1) has denominator 1
// and source coordinates monotone in the column and in the row, so its
// largest magnitudes, and any overflow, occur at the four corners.
template<Precision Scalar>
Status validate_warp(const std::array<Scalar, 9>& h, std::size_t target_width, std::size_t target_height,
                     bool affine) noexcept {
    for (const Scalar coefficient : h) {
        if (!std::isfinite(coefficient)) { return Status::nonfinite_input; }
    }
    if (affine) {
        const std::array<Scalar, 2> columns{Scalar(0), static_cast<Scalar>(target_width - 1)};
        const std::array<Scalar, 2> rows{Scalar(0), static_cast<Scalar>(target_height - 1)};
        for (const Scalar row : rows) {
            const std::array<Scalar, 2> corner_rows{row, row};
            const Status corners = validate_mapping(h, std::span<const Scalar>(columns), std::span<const Scalar>(corner_rows));
            if (corners != Status::ok) { return corners; }
        }
        return Status::ok;
    }
    // A projective map whose denominator is certainly away from zero over the
    // whole target, with every result certainly finite, needs no per-pixel
    // check (detail::projective_bound); otherwise every pixel is checked.
    const std::optional<detail::MappingBound<Scalar>> bound = detail::projective_bound(
        h, static_cast<Scalar>(target_width - 1), Scalar(0), static_cast<Scalar>(target_height - 1));
    if (bound.has_value()) { return Status::ok; }
    for (std::size_t row = 0; row < target_height; ++row) {
        const Scalar y = static_cast<Scalar>(row);
        for (std::size_t column = 0; column < target_width; ++column) {
            const Scalar x = static_cast<Scalar>(column);
            const Scalar denominator = h[6] * x + h[7] * y + h[8];
            if (denominator == Scalar(0)) { return Status::invalid_shape; }
            if (!std::isfinite(denominator)) { return Status::numerical_failure; }
            const Scalar mapped_x = (h[0] * x + h[1] * y + h[2]) / denominator;
            const Scalar mapped_y = (h[3] * x + h[4] * y + h[5]) / denominator;
            if (!std::isfinite(mapped_x) || !std::isfinite(mapped_y)) { return Status::numerical_failure; }
        }
    }
    return Status::ok;
}

// One segment of an affine row, by its shape.
template<Precision Scalar, bool Premultiplied, bool Prefetch>
void warp_segment(const detail::WarpGeometry<Scalar>& geometry, std::size_t row, const detail::RowSegment& segment,
                  Scalar* output) noexcept {
    switch (segment.shape) {
    case detail::RowShape::row_curve:
        detail::curve_columns<Scalar, detail::RowShape::row_curve, Premultiplied>(geometry, row, segment.columns,
                                                                                 output);
        return;
    case detail::RowShape::column_curve:
        detail::curve_columns<Scalar, detail::RowShape::column_curve, Premultiplied>(geometry, row, segment.columns,
                                                                                    output);
        return;
    case detail::RowShape::general:
        detail::warp_columns<Scalar, true, false, Premultiplied, Prefetch>(geometry, row, segment.columns, output);
        return;
    }
    // Fallback for a shape outside the enumeration: the general path is
    // correct for every segment.
    detail::warp_columns<Scalar, true, false, Premultiplied, Prefetch>(geometry, row, segment.columns, output);
}

// Every row of a validated warp. Affine rows run unmasked over their exact
// footprint interval and fill the rest with +0; segments whose samples lie
// on one lattice line run the six-control curve (detail::row_segments).
template<Precision Scalar, bool Affine, bool Premultiplied, bool Prefetch>
void warp_rows(const detail::WarpGeometry<Scalar>& geometry, std::size_t target_width, std::size_t target_height,
               Scalar* rgba, std::size_t row_stride) noexcept {
    // Projective rows with a bound over the whole target split into certain
    // zero, unmasked and masked columns (detail::projective_row); without
    // one they run masked throughout.
    std::optional<detail::MappingBound<Scalar>> bound{};
    if constexpr (!Affine) {
        bound = detail::projective_bound(geometry.inverse, static_cast<Scalar>(target_width - 1), Scalar(0),
                                         static_cast<Scalar>(target_height - 1));
    }
    for (std::size_t row = 0; row < target_height; ++row) {
        Scalar* output = rgba + row * row_stride;
        if constexpr (Affine) {
            const detail::ColumnInterval inside = detail::affine_footprint(geometry, row, target_width);
            std::fill(output, output + 4 * inside.begin, Scalar(0));
            std::fill(output + 4 * inside.end, output + 4 * target_width, Scalar(0));
            if (inside.begin == inside.end) { continue; }
            const detail::RowSegments segments = detail::row_segments(geometry, row, inside);
            for (std::size_t index = 0; index < segments.count; ++index) {
                const detail::RowSegment& segment = segments.segments[index];
                warp_segment<Scalar, Premultiplied, Prefetch>(geometry, row, segment, output);
            }
        } else if (bound.has_value()) {
            const detail::ProjectiveRow layout = detail::projective_row(geometry, *bound, row, target_width);
            const detail::ColumnInterval nonzero = layout.nonzero;
            const detail::ColumnInterval unmasked = layout.unmasked;
            std::fill(output, output + 4 * nonzero.begin, Scalar(0));
            std::fill(output + 4 * nonzero.end, output + 4 * target_width, Scalar(0));
            const detail::ColumnInterval leading{nonzero.begin, unmasked.begin};
            const detail::ColumnInterval trailing{unmasked.end, nonzero.end};
            detail::warp_columns<Scalar, false, true, Premultiplied, false>(geometry, row, leading, output);
            detail::warp_columns<Scalar, false, false, Premultiplied, false>(geometry, row, unmasked, output);
            detail::warp_columns<Scalar, false, true, Premultiplied, false>(geometry, row, trailing, output);
        } else {
            const detail::ColumnInterval all{0, target_width};
            detail::warp_columns<Scalar, false, true, Premultiplied, false>(geometry, row, all, output);
        }
    }
}

} // namespace

template<Precision Scalar>
Status warp_rgba(TensorAtlasView<Scalar> atlas, const std::array<Scalar, 9>& inverse, std::size_t target_width,
                 std::size_t target_height, std::span<Scalar> rgba, std::size_t row_stride,
                 ColorProjection projection) noexcept {
    if (projection != ColorProjection::none && projection != ColorProjection::premultiplied) {
        return Status::invalid_shape;
    }
    constexpr std::size_t extent_limit = 1048576;
    const bool valid_extent = atlas.source_width >= 2 && atlas.source_height >= 2
        && atlas.source_width <= extent_limit && atlas.source_height <= extent_limit;
    if (!valid_extent) { return Status::invalid_shape; }
    if (target_width > extent_limit || target_height > extent_limit) { return Status::invalid_shape; }
    if (target_width == 0 || target_height == 0) { return Status::ok; }
    const std::size_t lattice_width = 5 * (atlas.source_width - 1) + 1;
    const std::size_t lattice_height = 5 * (atlas.source_height - 1) + 1;
    const bool storage_fits = stx_conv::detail::fits(target_height, row_stride, 4 * target_width, rgba.size())
        && stx_conv::detail::fits(lattice_height, atlas.lattice_row_stride, lattice_width * 4, atlas.controls.size());
    if (!storage_fits) { return Status::insufficient_storage; }
    const std::span<const Scalar> coefficients(inverse);
    if (aliases(atlas.controls, rgba) || aliases(coefficients, rgba)) { return Status::overlap; }
    const bool affine = inverse[6] == Scalar(0) && inverse[7] == Scalar(0) && inverse[8] == Scalar(1);
    const Status validated = validate_warp(inverse, target_width, target_height, affine);
    if (validated != Status::ok) { return validated; }
    const detail::WarpGeometry<Scalar> geometry{atlas.controls.data(), atlas.lattice_row_stride, atlas.source_width,
                                                atlas.source_height, inverse};
    const bool premultiplied = projection == ColorProjection::premultiplied;
    // An affine map taking at least half a source cell per target pixel
    // streams the atlas faster than the cache keeps it: prefetch ahead.
    // Smaller areas reuse cached cells, where prefetching only costs.
    const Scalar area = std::fabs(inverse[0] * inverse[4] - inverse[1] * inverse[3]);
    const bool streaming = affine && area >= Scalar(0.5);
    if (streaming && premultiplied) {
        warp_rows<Scalar, true, true, true>(geometry, target_width, target_height, rgba.data(), row_stride);
    } else if (streaming) {
        warp_rows<Scalar, true, false, true>(geometry, target_width, target_height, rgba.data(), row_stride);
    } else if (affine && premultiplied) {
        warp_rows<Scalar, true, true, false>(geometry, target_width, target_height, rgba.data(), row_stride);
    } else if (affine) {
        warp_rows<Scalar, true, false, false>(geometry, target_width, target_height, rgba.data(), row_stride);
    } else if (premultiplied) {
        warp_rows<Scalar, false, true, false>(geometry, target_width, target_height, rgba.data(), row_stride);
    } else {
        warp_rows<Scalar, false, false, false>(geometry, target_width, target_height, rgba.data(), row_stride);
    }
    return Status::ok;
}

template void capacity<float>(const float* const[5], const float*, float*, std::size_t) noexcept;
template void capacity<double>(const double* const[5], const double*, double*, std::size_t) noexcept;
template void jet_terms<float>(const float* const[5], float*, float*, float*, std::size_t) noexcept;
template void jet_terms<double>(const double* const[5], double*, double*, double*, std::size_t) noexcept;
template void raw_currents<float>(const float* const[8], float* const[5], std::size_t) noexcept;
template void raw_currents<double>(const double* const[8], double* const[5], std::size_t) noexcept;
template void bubble<float>(const float*, const float*, float*, std::size_t, std::size_t, bool) noexcept;
template void bubble<double>(const double*, const double*, double*, std::size_t, std::size_t, bool) noexcept;
template void admit_cell<float>(const float* const[5], float* const[5], const float*, const float*, const float*,
                                const float*, float*, float*, std::size_t) noexcept;
template void admit_cell<double>(const double* const[5], double* const[5], const double*, const double*,
                                 const double*, const double*, double*, double*, std::size_t) noexcept;
template void synthesize<float>(const float*, const float*, std::size_t, const std::array<float, 5>&, float,
                                const float*, float, float*, std::size_t, bool) noexcept;
template void synthesize<double>(const double*, const double*, std::size_t, const std::array<double, 5>&, double,
                                 const double*, double, double*, std::size_t, bool) noexcept;
template void tensor_quintic<float>(const float* const[36], const float*, const float*, float*, std::size_t) noexcept;
template void tensor_quintic<double>(const double* const[36], const double*, const double*, double*,
                                     std::size_t) noexcept;
template void tensor_patch_rgba<float>(const float*, std::size_t, float, float, float*) noexcept;
template void tensor_patch_rgba<double>(const double*, std::size_t, double, double, double*) noexcept;
template void integrate_patch_rgba<float>(const float*, std::size_t, const float*, const float*, const float*,
                                          std::size_t, float*) noexcept;
template void integrate_patch_rgba<double>(const double*, std::size_t, const double*, const double*, const double*,
                                           std::size_t, double*) noexcept;
template Status map_points<float>(const std::array<float, 9>&, std::span<const float>, std::span<const float>,
                                  std::span<float>, std::span<float>) noexcept;
template Status map_points<double>(const std::array<double, 9>&, std::span<const double>, std::span<const double>,
                                   std::span<double>, std::span<double>) noexcept;
template Status sample_rgba<float>(TensorAtlasView<float>, std::span<const float>, std::span<const float>,
                                   std::span<float>, ColorProjection) noexcept;
template Status sample_rgba<double>(TensorAtlasView<double>, std::span<const double>, std::span<const double>,
                                    std::span<double>, ColorProjection) noexcept;

template Status warp_rgba<float>(TensorAtlasView<float>, const std::array<float, 9>&, std::size_t, std::size_t,
                                 std::span<float>, std::size_t, ColorProjection) noexcept;
template Status warp_rgba<double>(TensorAtlasView<double>, const std::array<double, 9>&, std::size_t, std::size_t,
                                  std::span<double>, std::size_t, ColorProjection) noexcept;

} // namespace stx_conv::kernels
