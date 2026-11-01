#pragma once
// stx_conv::stream: the separable CONV resize as a lockstep stream of line
// factors, with preclipped unsigned-box admission and bounded odd bubble
// correction (enabled by default). See docs/KERNELS.md.
#include <stx_conv/conv.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace stx_conv::stream {

// One target's contribution from one source cell or end anchor, as in
// stx_conv::BasicLinePlan::Segment. Weights are computed in binary64 and
// rounded once to Scalar. `currents` false means anchor only.
template<stx_conv::Precision Scalar>
struct Segment {
    std::size_t cell = 0;
    std::size_t anchor = 0;
    Scalar anchor_weight = Scalar(0);
    std::array<Scalar, 5> weights{};
    Scalar bubble_weight = Scalar(0);
    bool currents = false;
};

// A regular line plan in production order: target t owns segments
// [offsets[t], offsets[t + 1]). `ring` is the number of target rows a stream
// needs when its caller drains finished targets after every step.
template<stx_conv::Precision Scalar>
struct LinePlan {
    std::size_t source_length = 0;
    std::size_t target_length = 0;
    std::vector<std::size_t> offsets;
    std::vector<Segment<Scalar>> segments;
    std::size_t ring = 0;
    bool bubble_active = false;
};

template<stx_conv::Precision Scalar>
LinePlan<Scalar> build_line_plan(std::size_t source_length, std::size_t target_length, stx_conv::Sampling sampling);

// One test-free CONV factor over up to `capacity` lines in lockstep, fed one
// node (all lanes) at a time. Step i (cell i) needs nodes up to
// min(i + 4, length - 1); a supplied node stays borrowed until eight more have
// been supplied. Finished targets are rows read through target() until
// `plan.ring` later targets have been started.
template<stx_conv::Precision Scalar>
class LineStream final {
public:
    LineStream(std::size_t length, std::size_t capacity, std::size_t ring);
    LineStream(const LineStream&) = delete;
    LineStream& operator=(const LineStream&) = delete;

    void begin(const LinePlan<Scalar>& plan, std::size_t lanes, bool bubble_enabled = true) noexcept;
    void supply(const Scalar* node) noexcept;
    // Performs the next step if its nodes are available; false when waiting,
    // finished, or failed.
    [[nodiscard]] bool advance() noexcept;
    [[nodiscard]] bool finished() const noexcept;
    // A segment whose anchor left the node window (excluded by the plan's
    // order; reported rather than assumed).
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::size_t completed() const noexcept;
    [[nodiscard]] const Scalar* target(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

private:
    static constexpr std::size_t node_ring = 8;
    static constexpr std::size_t term_ring = 4;
    static constexpr std::size_t lane_block = 256;
    const Scalar* node(std::size_t index) const noexcept;
    void compute_terms(std::size_t index, std::size_t first, std::size_t last) noexcept;
    void compute_raw(std::size_t cell, std::size_t first, std::size_t last) noexcept;
    void admit_cell(std::size_t cell, std::size_t first, std::size_t last) noexcept;
    void compute_bubble(std::size_t cell, std::size_t first, std::size_t last) noexcept;
    void short_cell(std::size_t cell, std::size_t first, std::size_t last) noexcept;
    bool ready_segments(std::size_t step, std::size_t& end) const noexcept;
    void apply_segments(std::size_t end, std::size_t first, std::size_t last) noexcept;

    std::size_t length_ = 0;
    std::size_t capacity_ = 0;
    std::size_t ring_ = 0;
    std::size_t lanes_ = 0;
    const LinePlan<Scalar>* plan_ = nullptr;
    std::array<const Scalar*, node_ring> nodes_{};
    std::size_t supplied_ = 0;
    std::size_t next_ = 0;
    std::size_t cursor_ = 0;
    std::size_t target_ = 0;
    bool failed_ = false;
    bool bubble_enabled_ = true;
    // Lane arrays have stride `capacity_`.
    // [node % 4][lane]: production's node terms m/5, 2m/5, q/20.
    std::vector<Scalar> fifths_;
    std::vector<Scalar> two_fifths_;
    std::vector<Scalar> twentieths_;
    // [cell % 2][current][lane]: raw currents.
    std::vector<Scalar> raw_;
    // [current][lane]: admitted currents of the cell in hand.
    std::vector<Scalar> currents_;
    std::vector<Scalar> bubble_;
    // [lane]: preceding cell secant and final raw current.
    std::vector<Scalar> carry_delta_;
    std::vector<Scalar> carry_last_;
    // [target % ring][lane].
    std::vector<Scalar> targets_;
};

// Same shapes and statuses as stx_conv::resize. Finite samples with
// magnitude at most 1e10 (binary32) or 1e100 (binary64).
template<stx_conv::Precision Scalar>
class Workspace final {
public:
    // Allocates every buffer resize uses; throws std::invalid_argument for a
    // zero or oversized extent, std::length_error when a size product
    // overflows, and std::bad_alloc.
    Workspace(std::size_t source_width, std::size_t source_height, std::size_t target_width,
              std::size_t target_height, std::size_t channels, stx_conv::Sampling sampling);
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

    // One caller per workspace. May change between calls without allocation.
    void set_bubble_enabled(bool enabled) noexcept { bubble_enabled = enabled; }
    [[nodiscard]] bool is_bubble_enabled() const noexcept { return bubble_enabled; }
    static constexpr std::size_t group_rows = 8;
private:
    bool bubble_enabled = true;
    bool horizontal_group(const Scalar* const rows[], std::size_t count, Scalar* const outputs[]);
    template<stx_conv::Precision T>
    friend stx_conv::Status resize(Workspace<T>&, stx_conv::BasicImageInput<T>, stx_conv::BasicImageOutput<T>) noexcept;
    std::size_t source_width = 0;
    std::size_t source_height = 0;
    std::size_t target_width = 0;
    std::size_t target_height = 0;
    std::size_t channels = 0;
    LinePlan<Scalar> horizontal_plan;
    LinePlan<Scalar> vertical_plan;
    // Horizontal factor: up to group_rows rows x channels lanes.
    LineStream<Scalar> horizontal;
    // Vertical factor: one image row of lanes.
    LineStream<Scalar> vertical;
    // Transposed nodes for the horizontal factor: [node % 8][row][channel].
    std::vector<Scalar> gathered;
    // First-factor rows: horizontal first, a ring of group_rows + 8 rows of
    // the target width; vertical first, one group of rows of the source width.
    std::vector<Scalar> middle;
};

// Resizes source into output with the workspace's shape. Rows are strided in
// Scalar elements; source and output must not overlap. A validation failure
// (insufficient_storage, overlap, nonfinite_input, input_out_of_range)
// leaves output unchanged; numerical_failure may leave it partly written.
template<stx_conv::Precision Scalar>
[[nodiscard]] stx_conv::Status resize(Workspace<Scalar>& workspace, stx_conv::BasicImageInput<Scalar> source,
                                      stx_conv::BasicImageOutput<Scalar> output) noexcept;

} // namespace stx_conv::stream
