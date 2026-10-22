#include "internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

#if defined(_MSC_VER)
#define STX_CONV_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define STX_CONV_RESTRICT __restrict__
#else
#define STX_CONV_RESTRICT
#endif

namespace stx_conv::detail {
namespace {
template<Precision Scalar>
std::int8_t sign_of(Scalar value) noexcept {
    if (value < Scalar(0.0)) { return -1; }
    if (value > Scalar(0.0)) { return 1; }
    return 0;
}
template<Precision Scalar>
void compare_swap(Scalar& left, Scalar& right) noexcept {
    if (left > right) {
        const Scalar saved = left;
        left = right;
        right = saved;
    }
}
template<Precision Scalar>
void sort_five(Scalar values[5]) noexcept {
    compare_swap(values[0], values[1]);
    compare_swap(values[3], values[4]);
    compare_swap(values[2], values[4]);
    compare_swap(values[2], values[3]);
    compare_swap(values[1], values[4]);
    compare_swap(values[0], values[3]);
    compare_swap(values[0], values[2]);
    compare_swap(values[1], values[3]);
    compare_swap(values[1], values[2]);
}
// The equality multiplier lives between consecutive raw-current breakpoints.
// Scan those six affine pieces instead of testing candidate errors on each face.
template<Precision Scalar>
bool project(const Scalar raw[5], const std::int8_t signs[5], Scalar total, Scalar out[5]) noexcept {
    Scalar sum = Scalar(0.0);
    for (std::size_t index = 0; index < 5; ++index) { sum += raw[index]; }
    const Scalar correction = (sum - total) / Scalar(5.0);
    bool feasible = true;
    for (std::size_t index = 0; index < 5; ++index) {
        out[index] = raw[index] - correction;
        if (static_cast<Scalar>(signs[index]) * out[index] < Scalar(0.0)) { feasible = false; }
    }
    if (feasible) { return true; }
    Scalar sorted[5] = {raw[0], raw[1], raw[2], raw[3], raw[4]};
    sort_five(sorted);
    Scalar scale = std::abs(total);
    for (Scalar value : sorted) { scale = std::max(scale, std::abs(value)); }
    const Scalar tolerance = Scalar(64.0) * std::numeric_limits<Scalar>::epsilon() * scale;
    for (std::size_t piece = 0; piece <= 5; ++piece) {
        Scalar numerator = -total;
        std::size_t active = 0;
        for (std::size_t index = 0; index < 5; ++index) {
            const bool above = piece == 0 || raw[index] > sorted[piece - 1];
            const bool selected = signs[index] > 0 ? above : !above;
            if (selected) {
                numerator += raw[index];
                ++active;
            }
        }
        if (active == 0) {
            if (total == Scalar(0.0)) {
                std::fill_n(out, 5, Scalar(0.0));
                return true;
            }
            continue;
        }
        const Scalar multiplier = numerator / static_cast<Scalar>(active);
        if (piece != 0 && multiplier < sorted[piece - 1] - tolerance) { continue; }
        if (piece != 5 && multiplier > sorted[piece] + tolerance) { continue; }
        for (std::size_t index = 0; index < 5; ++index) {
            const Scalar candidate = raw[index] - multiplier;
            out[index] = signs[index] > 0 ? std::max(Scalar(0.0), candidate) : std::min(Scalar(0.0), candidate);
        }
        return true;
    }
    return false;
}
// Contiguous independent lanes let the compiler use common 128-bit groups.
// Arrays are distinct owning vectors; no alignment beyond Scalar is assumed.
template<Precision Scalar>
void jets(const Scalar* STX_CONV_RESTRICT source, Scalar* STX_CONV_RESTRICT first,
          Scalar* STX_CONV_RESTRICT second, std::size_t length, std::size_t lanes) noexcept {
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        const std::size_t last = (length - 1) * lanes + lane;
        const Scalar a = source[lane];
        const Scalar b = source[lanes + lane];
        const Scalar c = source[2 * lanes + lane];
        const Scalar z = source[last];
        const Scalar y = source[last - lanes];
        const Scalar x = source[last - 2 * lanes];
        first[lane] = (-Scalar(3.0) * a + Scalar(4.0) * b - c) / Scalar(2.0);
        first[lanes + lane] = (c - a) / Scalar(2.0);
        first[last - lanes] = (z - x) / Scalar(2.0);
        first[last] = (Scalar(3.0) * z - Scalar(4.0) * y + x) / Scalar(2.0);
        second[lane] = Scalar(2.0) * a - Scalar(5.0) * b + Scalar(4.0) * c - source[3 * lanes + lane];
        second[lanes + lane] = a - Scalar(2.0) * b + c;
        second[last - lanes] = x - Scalar(2.0) * y + z;
        second[last] = Scalar(2.0) * z - Scalar(5.0) * y + Scalar(4.0) * x - source[last - 3 * lanes];
    }
    // Flattening also vectorizes scalar lines across adjacent interior nodes.
    for (std::size_t index = 2 * lanes; index < (length - 2) * lanes; ++index) {
        first[index] = (source[index - 2 * lanes] - Scalar(8.0) * source[index - lanes]
            + Scalar(8.0) * source[index + lanes] - source[index + 2 * lanes]) / Scalar(12.0);
        second[index] = (-source[index + 2 * lanes] + Scalar(16.0) * source[index + lanes]
            - Scalar(30.0) * source[index] + Scalar(16.0) * source[index - lanes] - source[index - 2 * lanes]) / Scalar(12.0);
    }
}
template<Precision Scalar>
void raw_currents(const Scalar* STX_CONV_RESTRICT source, const Scalar* STX_CONV_RESTRICT first,
                  const Scalar* STX_CONV_RESTRICT second, Scalar* STX_CONV_RESTRICT raw,
                  std::size_t cells, std::size_t lanes) noexcept {
    for (std::size_t cell = 0; cell < cells; ++cell) {
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            const std::size_t node = cell * lanes + lane;
            const Scalar y0 = source[node];
            const Scalar y1 = source[node + lanes];
            const Scalar f0 = first[node];
            const Scalar f1 = first[node + lanes];
            const Scalar q0 = second[node];
            const Scalar q1 = second[node + lanes];
            const Scalar c1 = y0 + f0 / Scalar(5.0);
            const Scalar c2 = y0 + Scalar(2.0) * f0 / Scalar(5.0) + q0 / Scalar(20.0);
            const Scalar c3 = y1 - Scalar(2.0) * f1 / Scalar(5.0) + q1 / Scalar(20.0);
            const Scalar c4 = y1 - f1 / Scalar(5.0);
            const std::size_t offset = cell * 5 * lanes + lane;
            raw[offset] = c1 - y0;
            raw[offset + lanes] = c2 - c1;
            raw[offset + 2 * lanes] = c3 - c2;
            raw[offset + 3 * lanes] = c4 - c3;
            raw[offset + 4 * lanes] = y1 - c4;
        }
    }
}
template<bool Accumulate, Precision Scalar>
void synthesize_anchor(const Scalar* STX_CONV_RESTRICT anchors, Scalar weight,
                       Scalar* STX_CONV_RESTRICT output, std::size_t lanes) noexcept {
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        const Scalar sample = weight * anchors[lane];
        if constexpr (Accumulate) { output[lane] += sample; }
        else { output[lane] = sample; }
    }
}
template<bool Accumulate, Precision Scalar>
void synthesize_currents(const Scalar* STX_CONV_RESTRICT anchors,
                         const Scalar* STX_CONV_RESTRICT currents,
                         Scalar anchor_weight, const std::array<Scalar, 5>& weights,
                         Scalar* STX_CONV_RESTRICT output, std::size_t lanes) noexcept {
    const Scalar w0 = weights[0];
    const Scalar w1 = weights[1];
    const Scalar w2 = weights[2];
    const Scalar w3 = weights[3];
    const Scalar w4 = weights[4];
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        Scalar sample = anchor_weight * anchors[lane];
        sample += w0 * currents[lane];
        sample += w1 * currents[lanes + lane];
        sample += w2 * currents[2 * lanes + lane];
        sample += w3 * currents[3 * lanes + lane];
        sample += w4 * currents[4 * lanes + lane];
        if constexpr (Accumulate) { output[lane] += sample; }
        else { output[lane] = sample; }
    }
}
template<Precision Scalar>
Scalar boundary_cost(const Scalar* raw, std::size_t lanes, std::size_t lane,
                     std::size_t low, std::size_t high, std::size_t boundary, std::int8_t left) noexcept {
    Scalar cost = Scalar(0.0);
    for (std::size_t slot = low; slot < high; ++slot) {
        const Scalar value = raw[slot * lanes + lane];
        const std::int8_t expected = slot < boundary ? left : static_cast<std::int8_t>(-left);
        if (static_cast<Scalar>(expected) * value < Scalar(0.0)) { cost += value * value; }
    }
    return cost;
}
}
template<Precision Scalar>
Status Kernel<Scalar>::compile_checked(BasicLineInput<Scalar> source, BasicProfile<Scalar>& profile, BasicLineWorkspace<Scalar>& work) noexcept {
    if (profile.length_ != work.length_ || profile.lanes_ != work.lanes_
        || profile.anchors_.size() != profile.length_ * profile.lanes_
        || work.first_.size() != profile.anchors_.size()) { return Status::invalid_shape; }
    const Status validation = validate_samples(source, profile.length_, profile.lanes_);
    if (validation != Status::ok) { return validation; }
    const Status result = compile_unchecked(source, profile, work);
    return result;
}
template<Precision Scalar>
Status Kernel<Scalar>::compile_unchecked(BasicLineInput<Scalar> source, BasicProfile<Scalar>& profile, BasicLineWorkspace<Scalar>& work) noexcept {
    const std::size_t length = profile.length_;
    const std::size_t lanes = profile.lanes_;
    const std::size_t cells = length - 1;
    profile.ready_ = false;
    for (std::size_t node = 0; node < length; ++node) {
        std::copy_n(source.samples.data() + node * source.node_stride, lanes, profile.anchors_.data() + node * lanes);
    }
    if (length < 5) {
        std::fill(profile.currents_.begin(), profile.currents_.end(), Scalar(0.0));
        for (std::size_t cell = 0; cell < cells; ++cell) {
            for (std::size_t lane = 0; lane < lanes; ++lane) {
                profile.currents_[cell * 5 * lanes + lane] = profile.anchors_[(cell + 1) * lanes + lane]
                    - profile.anchors_[cell * lanes + lane];
            }
        }
        profile.ready_ = true;
        return Status::ok;
    }
    jets(profile.anchors_.data(), work.first_.data(), work.second_.data(), length, lanes);
    raw_currents(profile.anchors_.data(), work.first_.data(), work.second_.data(), work.raw_.data(), cells, lanes);
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        std::size_t first_witness = cells;
        for (std::size_t cell = 0; cell < cells; ++cell) {
            const Scalar delta = profile.anchors_[(cell + 1) * lanes + lane] - profile.anchors_[cell * lanes + lane];
            work.signs_[cell] = sign_of(delta);
            if (work.signs_[cell] != 0 && first_witness == cells) { first_witness = cell; }
        }
        if (first_witness == cells) {
            for (std::size_t slot = 0; slot < cells * 5; ++slot) { profile.currents_[slot * lanes + lane] = Scalar(0.0); }
            continue;
        }
        for (std::size_t cell = 0; cell < first_witness; ++cell) { work.signs_[cell] = work.signs_[first_witness]; }
        for (std::size_t cell = first_witness + 1; cell < cells; ++cell) {
            if (work.signs_[cell] == 0) { work.signs_[cell] = work.signs_[cell - 1]; }
        }
        std::size_t boundary_count = 0;
        std::size_t previous = 0;
        for (std::size_t knot = 1; knot < cells; ++knot) {
            const std::int8_t left = work.signs_[knot - 1];
            if (left == work.signs_[knot]) { continue; }
            const std::size_t centre = 5 * knot;
            const std::size_t low = centre - 5;
            const std::size_t high = centre + 5;
            const std::size_t start = std::max(previous + 1, centre - 4);
            Scalar cost = boundary_cost(work.raw_.data(), lanes, lane, low, high, start, left);
            Scalar best_cost = cost;
            bool best_exact = true;
            Scalar energy = Scalar(0.0);
            for (std::size_t slot = low; slot < high; ++slot) {
                const Scalar value = work.raw_[slot * lanes + lane];
                energy += value * value;
            }
            const Scalar tolerance = Scalar(128.0) * std::numeric_limits<Scalar>::epsilon() * energy;
            std::size_t best = start;
            for (std::size_t candidate = start + 1; candidate < high; ++candidate) {
                const Scalar value = work.raw_[(candidate - 1) * lanes + lane];
                cost -= static_cast<Scalar>(left) * value * std::abs(value);
                // Cancellation can perturb exact ties. Recompute close contenders
                // in the reference summation order; earliest boundary wins ties.
                if (std::abs(cost - best_cost) <= tolerance) {
                    cost = boundary_cost(work.raw_.data(), lanes, lane, low, high, candidate, left);
                    if (!best_exact) {
                        best_cost = boundary_cost(work.raw_.data(), lanes, lane, low, high, best, left);
                        best_exact = true;
                    }
                    if (cost < best_cost) { best_cost = cost; best = candidate; }
                    continue;
                }
                if (cost < best_cost) { best_cost = cost; best = candidate; best_exact = false; }
            }
            work.boundaries_[boundary_count] = best;
            work.boundary_signs_[boundary_count] = work.signs_[knot];
            ++boundary_count;
            previous = best;
        }
        std::size_t boundary_index = 0;
        std::int8_t sign = work.signs_[0];
        for (std::size_t cell = 0; cell < cells; ++cell) {
            Scalar raw[5]{};
            Scalar admitted[5]{};
            std::int8_t signs[5]{};
            for (std::size_t current = 0; current < 5; ++current) {
                const std::size_t slot = cell * 5 + current;
                while (boundary_index < boundary_count && slot >= work.boundaries_[boundary_index]) {
                    sign = work.boundary_signs_[boundary_index];
                    ++boundary_index;
                }
                signs[current] = sign;
                raw[current] = work.raw_[slot * lanes + lane];
            }
            const Scalar delta = profile.anchors_[(cell + 1) * lanes + lane] - profile.anchors_[cell * lanes + lane];
            if (!project(raw, signs, delta, admitted)) { return Status::numerical_failure; }
            for (std::size_t current = 0; current < 5; ++current) {
                profile.currents_[(cell * 5 + current) * lanes + lane] = admitted[current];
            }
        }
    }
    profile.ready_ = true;
    return Status::ok;
}
template<Precision Scalar>
Status Kernel<Scalar>::evaluate_checked(const BasicProfile<Scalar>& profile, const BasicLinePlan<Scalar>& plan, BasicLineOutput<Scalar> output) noexcept {
    if (!profile.ready_ || profile.anchors_.empty()) { return Status::unprepared_profile; }
    if (profile.length_ != plan.source_length_ || plan.offsets_.size() != plan.target_length_ + 1) {
        return Status::invalid_shape;
    }
    if (!fits(plan.target_length_, output.node_stride, profile.lanes_, output.samples.size())) {
        return Status::insufficient_storage;
    }
    evaluate_unchecked(profile, plan, output);
    return Status::ok;
}
template<Precision Scalar>
void Kernel<Scalar>::evaluate_unchecked(const BasicProfile<Scalar>& profile, const BasicLinePlan<Scalar>& plan, BasicLineOutput<Scalar> output) noexcept {
    const std::size_t lanes = profile.lanes_;
    for (std::size_t target = 0; target < plan.target_length_; ++target) {
        Scalar* destination = output.samples.data() + target * output.node_stride;
        const std::size_t first = plan.offsets_[target];
        const std::size_t last = plan.offsets_[target + 1];
        for (std::size_t index = first; index < last; ++index) {
            const typename BasicLinePlan<Scalar>::Segment& segment = plan.segments_[index];
            const Scalar* anchors = profile.anchors_.data() + segment.anchor * lanes;
            // Prepared weights are positive tail integrals, or one linear
            // moment. A zero first weight therefore means all five are zero.
            // Do not form a current pointer for singleton/endpoint anchors.
            if (segment.current_weights[0] == Scalar(0.0)) {
                if (index == first) { synthesize_anchor<false>(anchors, segment.anchor_weight, destination, lanes); }
                else { synthesize_anchor<true>(anchors, segment.anchor_weight, destination, lanes); }
            } else {
                const Scalar* currents = profile.currents_.data() + segment.cell * 5 * lanes;
                if (index == first) {
                    synthesize_currents<false>(anchors, currents, segment.anchor_weight, segment.current_weights, destination, lanes);
                } else {
                    synthesize_currents<true>(anchors, currents, segment.anchor_weight, segment.current_weights, destination, lanes);
                }
            }
        }
    }
}
} // namespace stx_conv::detail
namespace stx_conv {
template<Precision Scalar>
Status compile(BasicLineInput<Scalar> source, BasicProfile<Scalar>& profile, BasicLineWorkspace<Scalar>& workspace) noexcept {
    const Status result = detail::Kernel<Scalar>::compile_checked(source, profile, workspace);
    return result;
}
template<Precision Scalar>
Status evaluate(const BasicProfile<Scalar>& profile, const BasicLinePlan<Scalar>& plan, BasicLineOutput<Scalar> output) noexcept {
    const Status result = detail::Kernel<Scalar>::evaluate_checked(profile, plan, output);
    return result;
}
} // namespace stx_conv

namespace stx_conv {
template Status compile(BasicLineInput<float>, BasicProfile<float>&, BasicLineWorkspace<float>&) noexcept;
template Status evaluate(const BasicProfile<float>&, const BasicLinePlan<float>&, BasicLineOutput<float>) noexcept;
template Status compile(BasicLineInput<double>, BasicProfile<double>&, BasicLineWorkspace<double>&) noexcept;
template Status evaluate(const BasicProfile<double>&, const BasicLinePlan<double>&, BasicLineOutput<double>) noexcept;
}
namespace stx_conv::detail {
template Status Kernel<float>::compile_unchecked(BasicLineInput<float>, BasicProfile<float>&, BasicLineWorkspace<float>&) noexcept;
template void Kernel<float>::evaluate_unchecked(const BasicProfile<float>&, const BasicLinePlan<float>&, BasicLineOutput<float>) noexcept;
template Status Kernel<double>::compile_unchecked(BasicLineInput<double>, BasicProfile<double>&, BasicLineWorkspace<double>&) noexcept;
template void Kernel<double>::evaluate_unchecked(const BasicProfile<double>&, const BasicLinePlan<double>&, BasicLineOutput<double>) noexcept;
}
