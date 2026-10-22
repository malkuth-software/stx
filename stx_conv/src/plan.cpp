#include "internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stx_conv::detail {
template<Precision Scalar>
std::size_t checked_product(std::size_t left, std::size_t right) {
    if (right != 0 && left > std::numeric_limits<std::size_t>::max() / right) {
        throw std::length_error("stx_conv storage extent overflow");
    }
    const std::size_t result = left * right;
    if (result > std::numeric_limits<std::size_t>::max() / sizeof(Scalar)) {
        throw std::length_error("stx_conv byte extent overflow");
    }
    return result;
}
void validate_length(std::size_t length) {
    if (length == 0 || length > 400000000) {
        throw std::invalid_argument("stx_conv length must be in [1, 400000000]");
    }
}
bool fits(std::size_t count, std::size_t stride, std::size_t lanes, std::size_t capacity) noexcept {
    if (count == 0 || lanes == 0 || stride < lanes || capacity < lanes) {
        return false;
    }
    const bool result = count - 1 <= (capacity - lanes) / stride;
    return result;
}
template<Precision Scalar>
bool overlaps(std::span<const Scalar> source, std::span<Scalar> destination) noexcept {
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(source.data());
    const std::uintptr_t other = reinterpret_cast<std::uintptr_t>(destination.data());
    if (start <= other) {
        const bool result = other - start < source.size_bytes();
        return result;
    }
    const bool result = start - other < destination.size_bytes();
    return result;
}
template<Precision Scalar>
Status validate_samples(BasicLineInput<Scalar> source, std::size_t count, std::size_t lanes) noexcept {
    if (!fits(count, source.node_stride, lanes, source.samples.size())) {
        return Status::insufficient_storage;
    }
    for (std::size_t node = 0; node < count; ++node) {
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            const Scalar value = source.samples[node * source.node_stride + lane];
            if (!std::isfinite(value)) { return Status::nonfinite_input; }
            if (std::abs(value) > sample_limit<Scalar>) { return Status::input_out_of_range; }
        }
    }
    return Status::ok;
}
} // namespace stx_conv::detail

namespace stx_conv {
namespace {
template<Precision Scalar>
void tail_weights(Scalar u, std::array<Scalar, 5>& weights) {
    const Scalar v = Scalar(1.0) - u;
    const Scalar u2 = u * u;
    const Scalar u3 = u2 * u;
    const Scalar v2 = v * v;
    const Scalar v3 = v2 * v;
    const Scalar basis[5] = {Scalar(5.0) * v3 * v * u, Scalar(10.0) * v3 * u2,
                             Scalar(10.0) * v2 * u3, Scalar(5.0) * v * u3 * u, u3 * u2};
    Scalar suffix = Scalar(0.0);
    for (std::size_t index = 5; index != 0; --index) {
        suffix += basis[index - 1];
        weights[index - 1] = suffix;
    }
}
}
template<Precision Scalar>
BasicLinePlan<Scalar>::BasicLinePlan(std::size_t source_length, std::size_t target_length, bool)
    : source_length_(source_length), target_length_(target_length) {
    detail::validate_length(source_length);
    detail::validate_length(target_length);
    offsets_.reserve(target_length + 1);
    segments_.reserve(target_length);
}
template<Precision Scalar>
BasicLinePlan<Scalar>::BasicLinePlan(std::size_t source_length, std::size_t target_length, Sampling sampling)
    : BasicLinePlan<Scalar>(source_length, target_length, true) {
    if (sampling != Sampling::nodal && sampling != Sampling::pixel_area) {
        throw std::invalid_argument("stx_conv invalid sampling rule");
    }
    const std::int64_t source = static_cast<std::int64_t>(source_length);
    const std::int64_t targets = static_cast<std::int64_t>(target_length);
    const std::int64_t intervals = source - 1;
    for (std::int64_t target = 0; target < targets; ++target) {
        offsets_.push_back(segments_.size());
        if (source == targets || source == 1) {
            append_point({source == 1 ? 0 : target, Scalar(0)});
        } else if (sampling == Sampling::pixel_area) {
            append_basin(2 * target * source - targets,
                         2 * (target + 1) * source - targets, 2 * targets);
        } else if (targets >= source) {
            append_point(coordinate(target * intervals, targets - 1));
        } else if (targets == 1) {
            append_basin(0, intervals, 1);
        } else {
            const std::int64_t denominator = 2 * (targets - 1);
            const std::int64_t left = target == 0 ? 0 : (2 * target - 1) * intervals;
            const std::int64_t right = target + 1 == targets ? intervals * denominator : (2 * target + 1) * intervals;
            append_basin(left, right, denominator);
        }
    }
    offsets_.push_back(segments_.size());
}
template<Precision Scalar>
BasicLinePlan<Scalar> BasicLinePlan<Scalar>::points(std::size_t source_length, std::span<const Scalar> positions) {
    BasicLinePlan<Scalar> result(source_length, positions.size(), true);
    for (Scalar position : positions) {
        if (!std::isfinite(position) || position < Scalar(0.0) || position > static_cast<Scalar>(source_length - 1)) {
            throw std::invalid_argument("stx_conv point outside source domain");
        }
        result.offsets_.push_back(result.segments_.size());
        const std::int64_t cell = static_cast<std::int64_t>(position);
        // Compare the integer endpoint as well: converting it to float may round up.
        if (cell > static_cast<std::int64_t>(source_length - 1)) {
            throw std::invalid_argument("stx_conv point outside source domain");
        }
        const Scalar fraction = position - static_cast<Scalar>(cell);
        result.append_point({cell, fraction});
    }
    result.offsets_.push_back(result.segments_.size());
    return result;
}
template<Precision Scalar>
BasicLinePlan<Scalar> BasicLinePlan<Scalar>::points(std::size_t source_length,
    std::span<const BasicSamplePosition<Scalar>> positions) {
    BasicLinePlan<Scalar> result(source_length, positions.size(), true);
    for (const BasicSamplePosition<Scalar>& position : positions) {
        if (position.cell >= source_length || !std::isfinite(position.fraction)
            || position.fraction < Scalar(0) || position.fraction >= Scalar(1)
            || (position.cell == source_length - 1 && position.fraction != Scalar(0))) {
            throw std::invalid_argument("stx_conv local point outside source domain");
        }
        result.offsets_.push_back(result.segments_.size());
        result.append_point({static_cast<std::int64_t>(position.cell), position.fraction});
    }
    result.offsets_.push_back(result.segments_.size());
    return result;
}
template<Precision Scalar>
typename BasicLinePlan<Scalar>::Coordinate BasicLinePlan<Scalar>::coordinate(
    std::int64_t numerator, std::int64_t denominator) {
    std::int64_t cell = numerator / denominator;
    std::int64_t remainder = numerator % denominator;
    if (remainder < 0) { --cell; remainder += denominator; }
    Scalar fraction = static_cast<Scalar>(remainder) / static_cast<Scalar>(denominator);
    if (fraction == Scalar(1)) { ++cell; fraction = Scalar(0); }
    const Coordinate result{cell, fraction};
    return result;
}
template<Precision Scalar>
void BasicLinePlan<Scalar>::append_point(Coordinate position) {
    Segment segment{};
    segment.anchor_weight = Scalar(1);
    segment.cell = static_cast<std::size_t>(position.cell);
    segment.anchor = segment.cell;
    if (source_length_ < 5) { segment.current_weights[0] = position.fraction; }
    else { tail_weights(position.fraction, segment.current_weights); }
    segments_.push_back(segment);
}
template<Precision Scalar>
void BasicLinePlan<Scalar>::append_basin(std::int64_t left, std::int64_t right, std::int64_t denominator) {
    constexpr Scalar nodes[3] = {Scalar(-0.7745966692414834), Scalar(0), Scalar(0.7745966692414834)};
    constexpr Scalar weights[3] = {Scalar(5) / Scalar(9), Scalar(8) / Scalar(9), Scalar(5) / Scalar(9)};
    // Keep overlap lengths exact until conversion. Subtracting two rounded
    // float coordinates can erase a narrow but nonempty pixel footprint.
    std::int64_t first = left / denominator;
    if (left % denominator < 0) { --first; }
    const std::int64_t last = right / denominator + (right % denominator > 0 ? 1 : 0);
    for (std::int64_t cell = first; cell < last; ++cell) {
        const std::int64_t origin = cell * denominator;
        const std::int64_t low = std::max(left, origin);
        const std::int64_t high = std::min(right, origin + denominator);
        if (high <= low) { continue; }
        const Scalar a = static_cast<Scalar>(low - origin) / static_cast<Scalar>(denominator);
        const Scalar extent = static_cast<Scalar>(high - low) / static_cast<Scalar>(denominator);
        Segment segment{};
        segment.anchor_weight = static_cast<Scalar>(high - low) / static_cast<Scalar>(right - left);
        if (cell < 0) {
            segment.anchor = 0;
        } else if (cell >= static_cast<std::int64_t>(source_length_ - 1)) {
            segment.anchor = source_length_ - 1;
        } else {
            segment.cell = static_cast<std::size_t>(cell);
            segment.anchor = segment.cell;
            if (source_length_ < 5) {
                segment.current_weights[0] = segment.anchor_weight * (a + Scalar(0.5) * extent);
            } else {
                const Scalar middle = a + Scalar(0.5) * extent;
                const Scalar half = Scalar(0.5) * extent;
                for (std::size_t node = 0; node < 3; ++node) {
                    std::array<Scalar, 5> tail{};
                    tail_weights(middle + half * nodes[node], tail);
                    const Scalar factor = Scalar(0.5) * segment.anchor_weight * weights[node];
                    for (std::size_t current = 0; current < 5; ++current) {
                        segment.current_weights[current] += factor * tail[current];
                    }
                }
            }
        }
        segments_.push_back(segment);
    }
}
template<Precision Scalar>
std::size_t BasicLinePlan<Scalar>::source_length() const noexcept { return source_length_; }
template<Precision Scalar>
std::size_t BasicLinePlan<Scalar>::target_length() const noexcept { return target_length_; }
template<Precision Scalar>
std::size_t BasicLinePlan<Scalar>::storage_bytes() const noexcept {
    const std::size_t bytes = offsets_.capacity() * sizeof(std::size_t) + segments_.capacity() * sizeof(Segment);
    return bytes;
}
template<Precision Scalar>
BasicProfile<Scalar>::BasicProfile(std::size_t length, std::size_t lanes) : length_(length), lanes_(lanes) {
    detail::validate_length(length);
    detail::validate_length(lanes);
    anchors_.resize(detail::checked_product<Scalar>(length, lanes));
    currents_.resize(detail::checked_product<Scalar>(detail::checked_product<Scalar>(length - 1, lanes), 5));
}
template<Precision Scalar>
std::size_t BasicProfile<Scalar>::length() const noexcept { return length_; }
template<Precision Scalar>
std::size_t BasicProfile<Scalar>::lanes() const noexcept { return lanes_; }
template<Precision Scalar>
std::size_t BasicProfile<Scalar>::storage_bytes() const noexcept {
    const std::size_t bytes = (anchors_.capacity() + currents_.capacity()) * sizeof(Scalar);
    return bytes;
}
template<Precision Scalar>
BasicLineWorkspace<Scalar>::BasicLineWorkspace(std::size_t length, std::size_t lanes) : length_(length), lanes_(lanes) {
    detail::validate_length(length);
    detail::validate_length(lanes);
    first_.resize(detail::checked_product<Scalar>(length, lanes));
    second_.resize(first_.size());
    raw_.resize(detail::checked_product<Scalar>(detail::checked_product<Scalar>(length - 1, lanes), 5));
    signs_.resize(length);
    boundary_signs_.resize(length);
    boundaries_.resize(length);
}
template<Precision Scalar>
std::size_t BasicLineWorkspace<Scalar>::storage_bytes() const noexcept {
    const std::size_t bytes = (first_.capacity() + second_.capacity() + raw_.capacity()) * sizeof(Scalar)
        + signs_.capacity() + boundary_signs_.capacity() + boundaries_.capacity() * sizeof(std::size_t);
    return bytes;
}
} // namespace stx_conv

namespace stx_conv {
template class BasicLinePlan<float>;
template class BasicLinePlan<double>;
template class BasicProfile<float>;
template class BasicProfile<double>;
template class BasicLineWorkspace<float>;
template class BasicLineWorkspace<double>;
}
namespace stx_conv::detail {
template std::size_t checked_product<float>(std::size_t, std::size_t);
template bool overlaps(std::span<const float>, std::span<float>) noexcept;
template Status validate_samples(BasicLineInput<float>, std::size_t, std::size_t) noexcept;
template std::size_t checked_product<double>(std::size_t, std::size_t);
template bool overlaps(std::span<const double>, std::span<double>) noexcept;
template Status validate_samples(BasicLineInput<double>, std::size_t, std::size_t) noexcept;
}
