// stx_conv::stream: line plans, the lockstep line stream and the two-factor
// image resize. The arithmetic of each step is a src/detail kernel; this
// file owns plan construction, validation, the node and target rings, pass
// order and output.
#include <stx_conv/stream.hpp>
#include "detail/conv_admission_kernel.hpp"
#include "detail/conv_bubble_kernel.hpp"
#include "detail/conv_jet_kernel.hpp"
#include "detail/conv_lane_kernels.hpp"
#include "detail/conv_raw_current_kernel.hpp"
#include "detail/conv_synthesis_kernel.hpp"
// stx_conv's internal validation helpers, so inputs are rejected exactly as
// the profile API rejects them.
#include "internal.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace stx_conv::stream {
namespace {
namespace kernel = stx_conv::kernels::detail;
using stx_conv::Precision;
using stx_conv::Sampling;
using stx_conv::Status;

// --------------------------------- plan (src/plan.cpp arithmetic, binary64) --
void tail_weights(double u, std::array<double, 5>& weights) {
    const double v = 1.0 - u;
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double v2 = v * v;
    const double v3 = v2 * v;
    const std::array<double, 5> basis{5.0 * v3 * v * u, 10.0 * v3 * u2, 10.0 * v2 * u3, 5.0 * v * u3 * u, u3 * u2};
    double suffix = 0.0;
    for (std::size_t index = 5; index != 0; --index) {
        suffix += basis[index - 1];
        weights[index - 1] = suffix;
    }
}
enum class SampleKind { point, basin };
struct RegularSample {
    SampleKind kind = SampleKind::point;
    std::int64_t start = 0;
    std::int64_t end = 0;
    std::int64_t denominator = 1;
};
RegularSample regular_sample(std::int64_t target, std::int64_t source, std::int64_t targets, Sampling sampling) {
    const std::int64_t intervals = source - 1;
    if (source == targets) { return RegularSample{SampleKind::point, target, target, 1}; }
    if (source == 1) { return RegularSample{SampleKind::point, 0, 0, 1}; }
    if (sampling == Sampling::pixel_area) {
        const std::int64_t start = 2 * target * source - targets;
        const std::int64_t end = 2 * (target + 1) * source - targets;
        return RegularSample{SampleKind::basin, start, end, 2 * targets};
    }
    if (targets >= source) {
        return RegularSample{SampleKind::point, target * intervals, target * intervals, targets - 1};
    }
    if (targets == 1) { return RegularSample{SampleKind::basin, 0, intervals, 1}; }
    const std::int64_t denominator = 2 * (targets - 1);
    const std::int64_t left = target == 0 ? 0 : (2 * target - 1) * intervals;
    const std::int64_t right = target + 1 == targets ? intervals * denominator : (2 * target + 1) * intervals;
    return RegularSample{SampleKind::basin, left, right, denominator};
}
std::int64_t first_basin_cell(std::int64_t start, std::int64_t denominator) {
    std::int64_t first = start / denominator;
    if (start % denominator < 0) { --first; }
    return first;
}
std::int64_t end_basin_cell(std::int64_t end, std::int64_t denominator) {
    const std::int64_t last = end / denominator + (end % denominator > 0 ? 1 : 0);
    return last;
}
template<Precision Scalar>
Segment<Scalar> finish_segment(std::size_t cell, std::size_t anchor, double anchor_weight,
                               const std::array<double, 5>& weights) {
    Segment<Scalar> segment{};
    segment.cell = cell;
    segment.anchor = anchor;
    segment.anchor_weight = static_cast<Scalar>(anchor_weight);
    segment.currents = weights[0] != 0.0;
    for (std::size_t current = 0; current < 5; ++current) {
        segment.weights[current] = static_cast<Scalar>(weights[current]);
    }
    return segment;
}
template<Precision Scalar>
void append_point(LinePlan<Scalar>& plan, std::int64_t numerator, std::int64_t denominator) {
    std::int64_t cell = numerator / denominator;
    std::int64_t remainder = numerator % denominator;
    if (remainder < 0) {
        --cell;
        remainder += denominator;
    }
    double fraction = static_cast<double>(remainder) / static_cast<double>(denominator);
    if (fraction == 1.0) {
        ++cell;
        fraction = 0.0;
    }
    std::array<double, 5> weights{};
    if (plan.source_length < 5) {
        weights[0] = fraction;
    } else {
        tail_weights(fraction, weights);
    }
    const std::size_t index = static_cast<std::size_t>(cell);
    Segment<Scalar> segment = finish_segment<Scalar>(index, index, 1.0, weights);
    // Point correction (5/3) B(t), B = [t(1-t)]^3 (2t-1); none on short lines.
    if (plan.source_length >= 5) {
        const double v = fraction * (1.0 - fraction);
        segment.bubble_weight = static_cast<Scalar>((5.0 / 3.0) * v * v * v * (2.0 * fraction - 1.0));
    }
    plan.bubble_active = plan.bubble_active || segment.bubble_weight != Scalar(0);
    plan.segments.push_back(segment);
}
template<Precision Scalar>
void append_basin(LinePlan<Scalar>& plan, std::int64_t left, std::int64_t right, std::int64_t denominator) {
    constexpr double nodes[3] = {-0.7745966692414834, 0.0, 0.7745966692414834};
    constexpr double gauss[3] = {5.0 / 9.0, 8.0 / 9.0, 5.0 / 9.0};
    const std::int64_t first = first_basin_cell(left, denominator);
    const std::int64_t last = end_basin_cell(right, denominator);
    for (std::int64_t cell = first; cell < last; ++cell) {
        const std::int64_t origin = cell * denominator;
        const std::int64_t low = std::max(left, origin);
        const std::int64_t high = std::min(right, origin + denominator);
        const double a = static_cast<double>(low - origin) / static_cast<double>(denominator);
        const double extent = static_cast<double>(high - low) / static_cast<double>(denominator);
        const double anchor_weight = static_cast<double>(high - low) / static_cast<double>(right - left);
        std::array<double, 5> weights{};
        std::size_t index = 0;
        std::size_t anchor = 0;
        if (cell < 0) {
            anchor = 0;
        } else if (cell >= static_cast<std::int64_t>(plan.source_length - 1)) {
            anchor = plan.source_length - 1;
        } else {
            index = static_cast<std::size_t>(cell);
            anchor = index;
            if (plan.source_length < 5) {
                weights[0] = anchor_weight * (a + 0.5 * extent);
            } else {
                const double middle = a + 0.5 * extent;
                const double half = 0.5 * extent;
                for (std::size_t node = 0; node < 3; ++node) {
                    std::array<double, 5> tail{};
                    tail_weights(middle + half * nodes[node], tail);
                    const double factor = 0.5 * anchor_weight * gauss[node];
                    for (std::size_t current = 0; current < 5; ++current) {
                        weights[current] += factor * tail[current];
                    }
                }
            }
        }
        Segment<Scalar> segment = finish_segment<Scalar>(index, anchor, anchor_weight, weights);
        // Basin correction: (5/3) times the mean of B over the clipped
        // interval, through F(t) = -[t(1-t)]^4 / 4.
        if (segment.currents && plan.source_length >= 5) {
            const double v0 = a * (1.0 - a);
            const double high_fraction = a + extent;
            const double v1 = high_fraction * (1.0 - high_fraction);
            const double integral = (v0 * v0 * v0 * v0 - v1 * v1 * v1 * v1) * 0.25;
            // Evaluated left to right: ((5/3) integral denominator) / width.
            const double scaled = (5.0 / 3.0) * integral * static_cast<double>(denominator);
            segment.bubble_weight = static_cast<Scalar>(scaled / static_cast<double>(right - left));
        }
        plan.bubble_active = plan.bubble_active || segment.bubble_weight != Scalar(0);
        plan.segments.push_back(segment);
    }
}
// The stream's readiness rule (LineStream::ready_segments): at step i,
// the segments of cells <= i and the anchor-only segments with anchor <= i+1
// (all remaining at the last step). The ring holds every target touched in
// one step, one still open from before, and a spare.
template<Precision Scalar>
std::size_t ring_size(const LinePlan<Scalar>& plan) {
    const std::size_t steps = plan.source_length == 1 ? 1 : plan.source_length - 1;
    const std::size_t total = plan.segments.size();
    std::size_t cursor = 0;
    std::size_t target = 0;
    std::size_t widest = 1;
    for (std::size_t step = 0; step < steps; ++step) {
        const bool last = step + 1 == steps;
        std::size_t touched = 0;
        std::size_t previous = plan.target_length;
        while (cursor < total) {
            const Segment<Scalar>& segment = plan.segments[cursor];
            if (segment.currents && segment.cell > step) { break; }
            if (!segment.currents && segment.anchor > step + 1 && !last) { break; }
            while (cursor >= plan.offsets[target + 1]) { ++target; }
            if (target != previous) { ++touched; previous = target; }
            ++cursor;
        }
        widest = std::max(widest, touched);
    }
    const std::size_t result = widest + 2;
    return result;
}

// ------------------------------------------------------------ arithmetic --
// production jets() reduced to the node terms (first/5, 2 first/5, second/20).
template<Precision Scalar>
inline void store_terms(Scalar first, Scalar second, Scalar& fifths, Scalar& two_fifths, Scalar& twentieths) noexcept {
    fifths = first / Scalar(5.0);
    two_fifths = Scalar(2.0) * first / Scalar(5.0);
    twentieths = second / Scalar(20.0);
}

} // namespace

template<stx_conv::Precision Scalar>
LinePlan<Scalar> build_line_plan(std::size_t source_length, std::size_t target_length, Sampling sampling) {
    stx_conv::detail::validate_length(source_length);
    stx_conv::detail::validate_length(target_length);
    if (sampling != Sampling::nodal && sampling != Sampling::pixel_area) {
        throw std::invalid_argument("stx_conv invalid sampling rule");
    }
    const std::int64_t source = static_cast<std::int64_t>(source_length);
    const std::int64_t targets = static_cast<std::int64_t>(target_length);
    // Count the segments first, so the plan reserves once.
    std::size_t segments = 0;
    for (std::int64_t target = 0; target < targets; ++target) {
        const RegularSample sample = regular_sample(target, source, targets, sampling);
        if (sample.kind == SampleKind::point) {
            segments += 1;
            continue;
        }
        const std::int64_t first = first_basin_cell(sample.start, sample.denominator);
        const std::int64_t end = end_basin_cell(sample.end, sample.denominator);
        segments += static_cast<std::size_t>(end - first);
    }
    LinePlan<Scalar> plan;
    plan.source_length = source_length;
    plan.target_length = target_length;
    plan.offsets.reserve(target_length + 1);
    plan.segments.reserve(segments);
    for (std::int64_t target = 0; target < targets; ++target) {
        plan.offsets.push_back(plan.segments.size());
        const RegularSample sample = regular_sample(target, source, targets, sampling);
        if (sample.kind == SampleKind::point) {
            append_point(plan, sample.start, sample.denominator);
        } else {
            append_basin(plan, sample.start, sample.end, sample.denominator);
        }
    }
    plan.offsets.push_back(plan.segments.size());
    plan.ring = ring_size(plan);
    return plan;
}

// ---------------------------------------------------------------- stream --
template<stx_conv::Precision Scalar>
LineStream<Scalar>::LineStream(std::size_t length, std::size_t capacity, std::size_t ring)
    : length_(length), capacity_(capacity), ring_(ring) {
    stx_conv::detail::validate_length(length);
    stx_conv::detail::validate_length(capacity);
    const std::size_t terms = stx_conv::detail::checked_product<Scalar>(term_ring, capacity);
    fifths_.resize(terms);
    two_fifths_.resize(terms);
    twentieths_.resize(terms);
    raw_.resize(stx_conv::detail::checked_product<Scalar>(10, capacity));
    bubble_.resize(capacity);
    currents_.resize(stx_conv::detail::checked_product<Scalar>(5, capacity));
    carry_delta_.resize(capacity);
    carry_last_.resize(capacity);
    targets_.resize(stx_conv::detail::checked_product<Scalar>(ring, capacity));
}

template<stx_conv::Precision Scalar>
void LineStream<Scalar>::begin(const LinePlan<Scalar>& plan, std::size_t lanes, bool bubble_enabled) noexcept {
    bubble_enabled_ = bubble_enabled;
    plan_ = &plan;
    lanes_ = lanes;
    nodes_.fill(nullptr);
    supplied_ = 0;
    next_ = 0;
    cursor_ = 0;
    target_ = 0;
    failed_ = false;
    // Cell 0 has no left knot: no slot-0 allowance.
    std::fill(carry_delta_.begin(), carry_delta_.end(), Scalar(0));
    std::fill(carry_last_.begin(), carry_last_.end(), Scalar(0));
}

template<stx_conv::Precision Scalar>
void LineStream<Scalar>::supply(const Scalar* input) noexcept {
    nodes_[supplied_ % node_ring] = input;
    ++supplied_;
}

template<stx_conv::Precision Scalar>
const Scalar* LineStream<Scalar>::node(std::size_t index) const noexcept {
    const Scalar* result = nodes_[index % node_ring];
    return result;
}

// production jets(): one-sided at the two nodes of each end, centred
// fourth-order inside, reduced to the node terms. Which closure applies is
// decided per node, before the lane loops.
template<stx_conv::Precision Scalar>
void LineStream<Scalar>::compute_terms(std::size_t index, std::size_t first, std::size_t last) noexcept {
    const std::size_t stride = capacity_;
    const std::size_t end_node = length_ - 1;
    const std::size_t row = (index % term_ring) * stride;
    Scalar* fifths = fifths_.data() + row;
    Scalar* two_fifths = two_fifths_.data() + row;
    Scalar* twentieths = twentieths_.data() + row;
    if (index == 0) {
        const Scalar* r0 = node(0);
        const Scalar* r1 = node(1);
        const Scalar* r2 = node(2);
        const Scalar* r3 = node(3);
        #pragma omp simd
        for (std::size_t lane = first; lane < last; ++lane) {
            const Scalar a = r0[lane];
            const Scalar b = r1[lane];
            const Scalar c = r2[lane];
            const Scalar d = r3[lane];
            const Scalar jet = (-Scalar(3.0) * a + Scalar(4.0) * b - c) / Scalar(2.0);
            const Scalar second = Scalar(2.0) * a - Scalar(5.0) * b + Scalar(4.0) * c - d;
            store_terms(jet, second, fifths[lane], two_fifths[lane], twentieths[lane]);
        }
        return;
    }
    if (index == 1) {
        const Scalar* r0 = node(0);
        const Scalar* r1 = node(1);
        const Scalar* r2 = node(2);
        #pragma omp simd
        for (std::size_t lane = first; lane < last; ++lane) {
            const Scalar a = r0[lane];
            const Scalar b = r1[lane];
            const Scalar c = r2[lane];
            const Scalar jet = (c - a) / Scalar(2.0);
            const Scalar second = a - Scalar(2.0) * b + c;
            store_terms(jet, second, fifths[lane], two_fifths[lane], twentieths[lane]);
        }
        return;
    }
    if (index == end_node) {
        const Scalar* rz = node(end_node);
        const Scalar* ry = node(end_node - 1);
        const Scalar* rx = node(end_node - 2);
        const Scalar* rw = node(end_node - 3);
        #pragma omp simd
        for (std::size_t lane = first; lane < last; ++lane) {
            const Scalar z = rz[lane];
            const Scalar y = ry[lane];
            const Scalar x = rx[lane];
            const Scalar w = rw[lane];
            const Scalar jet = (Scalar(3.0) * z - Scalar(4.0) * y + x) / Scalar(2.0);
            const Scalar second = Scalar(2.0) * z - Scalar(5.0) * y + Scalar(4.0) * x - w;
            store_terms(jet, second, fifths[lane], two_fifths[lane], twentieths[lane]);
        }
        return;
    }
    if (index + 1 == end_node) {
        const Scalar* rz = node(end_node);
        const Scalar* ry = node(end_node - 1);
        const Scalar* rx = node(end_node - 2);
        #pragma omp simd
        for (std::size_t lane = first; lane < last; ++lane) {
            const Scalar z = rz[lane];
            const Scalar y = ry[lane];
            const Scalar x = rx[lane];
            const Scalar jet = (z - x) / Scalar(2.0);
            const Scalar second = x - Scalar(2.0) * y + z;
            store_terms(jet, second, fifths[lane], two_fifths[lane], twentieths[lane]);
        }
        return;
    }
    const std::array<const Scalar*, 5> source{node(index - 2), node(index - 1), node(index), node(index + 1),
                                              node(index + 2)};
    (*kernel::lane_kernels<Scalar>().jet)(source.data(), fifths, two_fifths, twentieths, first, last);
}

// production raw_currents() for one cell.
template<stx_conv::Precision Scalar>
void LineStream<Scalar>::compute_raw(std::size_t cell, std::size_t first, std::size_t last) noexcept {
    const std::size_t stride = capacity_;
    const std::size_t row0 = (cell % term_ring) * stride;
    const std::size_t row1 = ((cell + 1) % term_ring) * stride;
    const std::array<const Scalar*, 8> input{
        node(cell), node(cell + 1),
        fifths_.data() + row0, two_fifths_.data() + row0, twentieths_.data() + row0,
        fifths_.data() + row1, two_fifths_.data() + row1, twentieths_.data() + row1,
    };
    Scalar* raw = raw_.data() + (cell % 2) * 5 * stride;
    const std::array<Scalar*, 5> output{raw, raw + stride, raw + 2 * stride, raw + 3 * stride, raw + 4 * stride};
    (*kernel::lane_kernels<Scalar>().raw_current)(input.data(), output.data(), first, last);
}

// One cell's admitted currents for lanes [first, last): preclipped unsigned
// box admission over independent SIMD lanes.
template<stx_conv::Precision Scalar>
void LineStream<Scalar>::admit_cell(std::size_t cell, std::size_t first, std::size_t last) noexcept {
    const std::size_t stride = capacity_;
    const bool right_knot = cell + 1 < length_ - 1;
    const Scalar* y0 = node(cell) + first;
    const Scalar* y1 = node(cell + 1) + first;
    const Scalar* y2 = (right_knot ? node(cell + 2) : node(cell + 1)) + first;
    std::array<const Scalar*, 5> raw{};
    std::array<Scalar*, 5> output{};
    for (std::size_t current = 0; current < 5; ++current) {
        raw[current] = raw_.data() + ((cell % 2) * 5 + current) * stride + first;
        output[current] = currents_.data() + current * stride + first;
    }
    const Scalar* next = raw_.data() + ((cell + 1) % 2) * 5 * stride + first;
    Scalar* left_delta = carry_delta_.data() + first;
    Scalar* left_last = carry_last_.data() + first;
    (*kernel::lane_kernels<Scalar>().admit)(raw.data(), output.data(), y0, y1, y2, next, left_delta, left_last,
                                            last - first);
}

// The bubble amplitude of one cell; cells 2..length-4 are interior.
template<stx_conv::Precision Scalar>
void LineStream<Scalar>::compute_bubble(std::size_t cell, std::size_t first, std::size_t last) noexcept {
    const Scalar* raw = raw_.data() + (cell % 2) * 5 * capacity_;
    const bool interior = cell >= 2 && cell + 3 < length_;
    const kernel::LaneKernels<Scalar>& lanes = kernel::lane_kernels<Scalar>();
    if (interior) {
        (*lanes.bubble_interior)(raw, currents_.data(), bubble_.data(), capacity_, first, last);
        return;
    }
    (*lanes.bubble_edge)(raw, currents_.data(), bubble_.data(), capacity_, first, last);
}

// production's short-line closure (length 2..4): the secant in the first slot.
template<stx_conv::Precision Scalar>
void LineStream<Scalar>::short_cell(std::size_t cell, std::size_t first, std::size_t last) noexcept {
    const std::size_t stride = capacity_;
    const Scalar* y0s = node(cell);
    const Scalar* y1s = node(cell + 1);
    Scalar* currents = currents_.data();
    #pragma omp simd
    for (std::size_t lane = first; lane < last; ++lane) {
        bubble_[lane] = Scalar(0);
        currents[lane] = y1s[lane] - y0s[lane];
        currents[stride + lane] = Scalar(0);
        currents[2 * stride + lane] = Scalar(0);
        currents[3 * stride + lane] = Scalar(0);
        currents[4 * stride + lane] = Scalar(0);
    }
}

// The segments ready at `step` start at cursor_ and end at `end`. False when a
// ready segment's anchor has left the node window (the plan's order excludes
// it; reported rather than assumed).
template<stx_conv::Precision Scalar>
bool LineStream<Scalar>::ready_segments(std::size_t step, std::size_t& end) const noexcept {
    const LinePlan<Scalar>& plan = *plan_;
    const std::size_t total = plan.segments.size();
    const std::size_t steps = length_ == 1 ? 1 : length_ - 1;
    const bool last = step + 1 == steps;
    std::size_t index = cursor_;
    while (index < total) {
        const Segment<Scalar>& segment = plan.segments[index];
        if (segment.currents && segment.cell > step) { break; }
        if (!segment.currents && segment.anchor > step + 1 && !last) { break; }
        const bool anchor_supplied = segment.anchor < supplied_;
        const bool anchor_retained = segment.anchor + node_ring >= supplied_;
        if (!anchor_supplied || !anchor_retained) { return false; }
        ++index;
    }
    end = index;
    return true;
}

// Applies segments [cursor_, end) to lanes [first, last) in production order.
// Each segment's kind (anchor only, with or without bubble, opening or
// accumulating) selects its kernel before the lane loop.
template<stx_conv::Precision Scalar>
void LineStream<Scalar>::apply_segments(std::size_t end, std::size_t first, std::size_t last) noexcept {
    const LinePlan<Scalar>& plan = *plan_;
    const std::size_t stride = capacity_;
    const Scalar* currents = currents_.data();
    const Scalar* bubble = bubble_.data();
    // The selected variant's synthesis kernels, read once for the segments.
    const kernel::LaneKernels<Scalar>& lanes = kernel::lane_kernels<Scalar>();
    std::size_t target = target_;
    for (std::size_t index = cursor_; index < end; ++index) {
        const Segment<Scalar>& segment = plan.segments[index];
        while (index >= plan.offsets[target + 1]) { ++target; }
        Scalar* destination = targets_.data() + (target % ring_) * stride;
        const bool opening = index == plan.offsets[target];
        const Scalar* anchors = node(segment.anchor);
        const Scalar anchor_weight = segment.anchor_weight;
        if (!segment.currents && opening) {
            #pragma omp simd
            for (std::size_t lane = first; lane < last; ++lane) { destination[lane] = anchor_weight * anchors[lane]; }
            continue;
        }
        if (!segment.currents) {
            #pragma omp simd
            for (std::size_t lane = first; lane < last; ++lane) {
                const Scalar sample = anchor_weight * anchors[lane];
                destination[lane] = destination[lane] + sample;
            }
            continue;
        }
        const Scalar weight = bubble_enabled_ ? segment.bubble_weight : Scalar(0);
        const std::array<Scalar, 5>& weights = segment.weights;
        // [Bubble][Accumulate]: the opening segment stores, later ones add.
        const std::size_t bubble_index = weight == Scalar(0) ? 0 : 1;
        const std::size_t accumulate_index = opening ? 0 : 1;
        const Scalar* bubble_source = weight == Scalar(0) ? nullptr : bubble;
        (*lanes.synthesis[bubble_index][accumulate_index])(anchors, currents, stride, weights, anchor_weight,
                                                           bubble_source, weight, destination, first, last);
    }
}

template<stx_conv::Precision Scalar>
bool LineStream<Scalar>::advance() noexcept {
    if (failed_) { return false; }
    const std::size_t steps = length_ == 1 ? 1 : length_ - 1;
    if (next_ == steps) { return false; }
    const std::size_t step = next_;
    // Nodes the step needs: through min(step + 4, length - 1) on a full line,
    // through step + 1 on a short one.
    std::size_t needed = 1;
    if (length_ >= 5) {
        needed = std::min(step + 4, length_ - 1) + 1;
    } else if (length_ > 1) {
        needed = step + 2;
    }
    if (supplied_ < needed) { return false; }
    std::size_t end = cursor_;
    if (!ready_segments(step, end)) {
        failed_ = true;
        return false;
    }
    const std::size_t cells = length_ - 1;
    const bool bubble_active = bubble_enabled_ && (*plan_).bubble_active;
    for (std::size_t first = 0; first < lanes_; first += lane_block) {
        const std::size_t last = std::min(first + lane_block, lanes_);
        if (length_ >= 5) {
            if (step == 0) {
                compute_terms(0, first, last);
                compute_terms(1, first, last);
                compute_terms(2, first, last);
                compute_raw(0, first, last);
                compute_raw(1, first, last);
            } else {
                if (step + 2 <= length_ - 1) { compute_terms(step + 2, first, last); }
                if (step + 1 < cells) { compute_raw(step + 1, first, last); }
            }
            admit_cell(step, first, last);
            if (bubble_active) { compute_bubble(step, first, last); }
        } else if (length_ >= 2) {
            short_cell(step, first, last);
        }
        apply_segments(end, first, last);
    }
    const LinePlan<Scalar>& plan = *plan_;
    while (cursor_ < end) {
        ++cursor_;
        if (cursor_ == plan.offsets[target_ + 1]) { ++target_; }
    }
    ++next_;
    return true;
}

template<stx_conv::Precision Scalar>
bool LineStream<Scalar>::finished() const noexcept {
    const std::size_t steps = length_ == 1 ? 1 : length_ - 1;
    const bool result = next_ == steps;
    return result;
}

template<stx_conv::Precision Scalar>
bool LineStream<Scalar>::failed() const noexcept {
    return failed_;
}

template<stx_conv::Precision Scalar>
std::size_t LineStream<Scalar>::completed() const noexcept {
    return target_;
}

template<stx_conv::Precision Scalar>
const Scalar* LineStream<Scalar>::target(std::size_t index) const noexcept {
    const Scalar* result = targets_.data() + (index % ring_) * capacity_;
    return result;
}

template<stx_conv::Precision Scalar>
std::size_t LineStream<Scalar>::storage_bytes() const noexcept {
    const std::size_t term_scalars = fifths_.capacity() + two_fifths_.capacity() + twentieths_.capacity();
    const std::size_t cell_scalars = raw_.capacity() + currents_.capacity() + bubble_.capacity();
    const std::size_t lane_scalars = carry_delta_.capacity() + carry_last_.capacity() + targets_.capacity();
    const std::size_t result = (term_scalars + cell_scalars + lane_scalars) * sizeof(Scalar);
    return result;
}

// ------------------------------------------------------------- workspace --
// The vertical factor runs first when it shrinks (th < sh), so its lanes
// span the source width; otherwise they span the target width.
template<stx_conv::Precision Scalar>
Workspace<Scalar>::Workspace(std::size_t sw, std::size_t sh, std::size_t tw, std::size_t th,
                             std::size_t channel_count, Sampling sampling)
    : source_width(sw), source_height(sh), target_width(tw), target_height(th), channels(channel_count),
      horizontal_plan(build_line_plan<Scalar>(sw, tw, sampling)),
      vertical_plan(build_line_plan<Scalar>(sh, th, sampling)),
      horizontal(sw, stx_conv::detail::checked_product<Scalar>(group_rows, channel_count), horizontal_plan.ring),
      vertical(sh, stx_conv::detail::checked_product<Scalar>(th < sh ? sw : tw, channel_count), vertical_plan.ring) {
    gathered.resize(stx_conv::detail::checked_product<Scalar>(8 * group_rows, channel_count));
    const bool vertical_first = th < sh;
    const std::size_t width = vertical_first ? sw : tw;
    const std::size_t rows = vertical_first ? group_rows : group_rows + 8;
    const std::size_t row_scalars = stx_conv::detail::checked_product<Scalar>(width, channel_count);
    middle.resize(stx_conv::detail::checked_product<Scalar>(row_scalars, rows));
}
template<stx_conv::Precision Scalar>
std::size_t Workspace<Scalar>::storage_bytes() const noexcept {
    const std::size_t offsets = horizontal_plan.offsets.capacity() + vertical_plan.offsets.capacity();
    const std::size_t segments = horizontal_plan.segments.capacity() + vertical_plan.segments.capacity();
    const std::size_t plans = offsets * sizeof(std::size_t) + segments * sizeof(Segment<Scalar>);
    const std::size_t rows = (gathered.capacity() + middle.capacity()) * sizeof(Scalar);
    const std::size_t result = plans + horizontal.storage_bytes() + vertical.storage_bytes() + rows;
    return result;
}

// One group of up to group_rows rows through the horizontal factor: each
// node gathers [row][channel] across the group; finished target columns are
// scattered back to the destination rows.
template<Precision Scalar>
bool Workspace<Scalar>::horizontal_group(const Scalar* const rows[], std::size_t count, Scalar* const outputs[]) {
    LineStream<Scalar>& stream = horizontal;
    const std::size_t lane_channels = channels;
    stream.begin(horizontal_plan, count * lane_channels, bubble_enabled);
    std::size_t drained = 0;
    for (std::size_t x = 0; x < source_width; ++x) {
        Scalar* gathered_values = gathered.data() + (x % 8) * group_rows * lane_channels;
        for (std::size_t r = 0; r < count; ++r) {
            const Scalar* pixel = rows[r] + x * lane_channels;
            std::copy_n(pixel, lane_channels, gathered_values + r * lane_channels);
        }
        stream.supply(gathered_values);
        while (stream.advance()) {
            for (; drained < stream.completed(); ++drained) {
                const Scalar* values = stream.target(drained);
                for (std::size_t r = 0; r < count; ++r) {
                    Scalar* pixel = outputs[r] + drained * lane_channels;
                    std::copy_n(values + r * lane_channels, lane_channels, pixel);
                }
            }
        }
        if (stream.failed()) { return false; }
    }
    const bool result = stream.finished();
    return result;
}

template<stx_conv::Precision Scalar>
Status resize(Workspace<Scalar>& work, stx_conv::BasicImageInput<Scalar> source,
                       stx_conv::BasicImageOutput<Scalar> output) noexcept {
    const std::size_t sw = work.source_width;
    const std::size_t sh = work.source_height;
    const std::size_t tw = work.target_width;
    const std::size_t th = work.target_height;
    const std::size_t channels = work.channels;
    const std::size_t source_row = sw * channels;
    const std::size_t target_row = tw * channels;
    if (!stx_conv::detail::fits(sh, source.row_stride, source_row, source.samples.size())
        || !stx_conv::detail::fits(th, output.row_stride, target_row, output.samples.size())) {
        return Status::insufficient_storage;
    }
    if (stx_conv::detail::overlaps(source.samples, output.samples)) { return Status::overlap; }
    const stx_conv::BasicLineInput<Scalar> source_lines{source.samples, source.row_stride};
    const Status validated = stx_conv::detail::validate_samples<Scalar>(source_lines, sh, source_row);
    if (validated != Status::ok) { return validated; }
    const Scalar* source_rows = source.samples.data();
    Scalar* output_rows = output.samples.data();
    constexpr std::size_t group = Workspace<Scalar>::group_rows;
    if (sw == tw && sh == th) {
        for (std::size_t r = 0; r < sh; ++r) {
            std::copy_n(source_rows + r * source.row_stride, source_row, output_rows + r * output.row_stride);
        }
        return Status::ok;
    }
    if (sh == th) {
        for (std::size_t g = 0; g < sh; g += group) {
            const std::size_t count = std::min(group, sh - g);
            const Scalar* rows[group];
            Scalar* outs[group];
            for (std::size_t r = 0; r < count; ++r) {
                rows[r] = source_rows + (g + r) * source.row_stride;
                outs[r] = output_rows + (g + r) * output.row_stride;
            }
            if (!work.horizontal_group(rows, count, outs)) { return Status::numerical_failure; }
        }
        return Status::ok;
    }
    LineStream<Scalar>& vertical = work.vertical;
    if (sw == tw) {
        vertical.begin(work.vertical_plan, source_row, work.bubble_enabled);
        std::size_t drained = 0;
        for (std::size_t r = 0; r < sh; ++r) {
            vertical.supply(source_rows + r * source.row_stride);
            while (vertical.advance()) {
                for (; drained < vertical.completed(); ++drained) {
                    std::copy_n(vertical.target(drained), target_row, output_rows + drained * output.row_stride);
                }
            }
            if (vertical.failed()) { return Status::numerical_failure; }
        }
        const Status result = vertical.finished() ? Status::ok : Status::numerical_failure;
        return result;
    }
    if (th < sh) {
        // Vertical first over source rows; finished middle rows are resized
        // horizontally a group at a time.
        vertical.begin(work.vertical_plan, source_row, work.bubble_enabled);
        std::size_t drained = 0;
        std::size_t grouped = 0;
        Scalar* middle_rows[group];
        const Scalar* middle_inputs[group];
        for (std::size_t r = 0; r < group; ++r) {
            middle_rows[r] = work.middle.data() + r * source_row;
            middle_inputs[r] = middle_rows[r];
        }
        for (std::size_t r = 0; r < sh; ++r) {
            vertical.supply(source_rows + r * source.row_stride);
            while (vertical.advance()) {
                for (; drained < vertical.completed(); ++drained) {
                    std::copy_n(vertical.target(drained), source_row, middle_rows[drained - grouped]);
                    const bool full = drained + 1 - grouped == group || drained + 1 == th;
                    if (full) {
                        const std::size_t count = drained + 1 - grouped;
                        Scalar* outs[group];
                        for (std::size_t k = 0; k < count; ++k) {
                            outs[k] = output_rows + (grouped + k) * output.row_stride;
                        }
                        if (!work.horizontal_group(middle_inputs, count, outs)) { return Status::numerical_failure; }
                        grouped = drained + 1;
                    }
                }
            }
            if (vertical.failed()) { return Status::numerical_failure; }
        }
        const bool complete = vertical.finished() && grouped == th;
        const Status result = complete ? Status::ok : Status::numerical_failure;
        return result;
    }
    // Horizontal first: groups of source rows into a ring of group + 8 middle
    // rows, each fed to the vertical stream in order.
    const std::size_t ring_rows = group + 8;
    vertical.begin(work.vertical_plan, target_row, work.bubble_enabled);
    std::size_t drained = 0;
    for (std::size_t g = 0; g < sh; g += group) {
        const std::size_t count = std::min(group, sh - g);
        const Scalar* rows[group];
        Scalar* middle_rows[group];
        for (std::size_t r = 0; r < count; ++r) {
            rows[r] = source_rows + (g + r) * source.row_stride;
            middle_rows[r] = work.middle.data() + ((g + r) % ring_rows) * target_row;
        }
        if (!work.horizontal_group(rows, count, middle_rows)) { return Status::numerical_failure; }
        for (std::size_t r = 0; r < count; ++r) {
            vertical.supply(middle_rows[r]);
            while (vertical.advance()) {
                for (; drained < vertical.completed(); ++drained) {
                    std::copy_n(vertical.target(drained), target_row, output_rows + drained * output.row_stride);
                }
            }
            if (vertical.failed()) { return Status::numerical_failure; }
        }
    }
    const Status result = vertical.finished() ? Status::ok : Status::numerical_failure;
    return result;
}

template LinePlan<float> build_line_plan<float>(std::size_t, std::size_t, Sampling);
template LinePlan<double> build_line_plan<double>(std::size_t, std::size_t, Sampling);
template class LineStream<float>;
template class LineStream<double>;
template class Workspace<float>;
template class Workspace<double>;
template Status resize(Workspace<float>&, stx_conv::BasicImageInput<float>, stx_conv::BasicImageOutput<float>) noexcept;
template Status resize(Workspace<double>&, stx_conv::BasicImageInput<double>, stx_conv::BasicImageOutput<double>) noexcept;
} // namespace stx_conv::stream
