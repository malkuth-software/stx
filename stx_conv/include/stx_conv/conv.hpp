#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <type_traits>
#include <limits>
#include <optional>

namespace stx_conv {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559
              && std::numeric_limits<float>::digits == 24);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559
              && std::numeric_limits<double>::digits == 53);
template<class Scalar>
concept Precision = std::is_same_v<Scalar, float> || std::is_same_v<Scalar, double>;
namespace detail { template<Precision Scalar> struct Kernel; }
template<Precision Scalar> class BasicImageWorkspace;

enum class Status { ok, invalid_shape, insufficient_storage, overlap, nonfinite_input,
                    input_out_of_range, unprepared_profile, numerical_failure };
enum class Sampling { nodal, pixel_area };

// A local fraction retains subpixel resolution independently of the cell index.
// fraction is in [0, 1); the last node admits only fraction zero.
template<Precision Scalar>
struct BasicSamplePosition {
    std::size_t cell = 0;
    Scalar fraction = Scalar(0);
};
using SamplePosition32 = BasicSamplePosition<float>;
using SamplePosition64 = BasicSamplePosition<double>;
using SamplePosition = SamplePosition64;

// Owners (plans, profiles, workspaces) are move-only. Constructors allocate
// all storage; invalid dimensions throw invalid_argument, oversized ones
// length_error or bad_alloc, and nothing is left behind. Moving transfers the
// storage and leaves the source empty (zero lengths, no storage), which every
// operation rejects until a new owner is assigned to it.

// Nodal: endpoint-aligned points on enlargement; integrated Voronoi basins on
// reduction. Pixel-area: full target pixel footprints, constant outer half cells.
template<Precision Scalar>
class BasicLinePlan final {
public:
    explicit BasicLinePlan(std::size_t source_length, std::size_t target_length,
                           Sampling sampling = Sampling::nodal);
    static BasicLinePlan<Scalar> points(std::size_t source_length, std::span<const Scalar> positions);
    static BasicLinePlan<Scalar> points(std::size_t source_length,
                                        std::span<const BasicSamplePosition<Scalar>> positions);
    BasicLinePlan(const BasicLinePlan<Scalar>&) = delete;
    BasicLinePlan<Scalar>& operator=(const BasicLinePlan<Scalar>&) = delete;
    BasicLinePlan(BasicLinePlan<Scalar>&& other) noexcept;
    BasicLinePlan<Scalar>& operator=(BasicLinePlan<Scalar>&& other) noexcept;
    // Zero for an empty (moved-from) plan.
    [[nodiscard]] std::size_t source_length() const noexcept;
    [[nodiscard]] std::size_t target_length() const noexcept;
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
private:
    struct Segment {
        std::size_t cell = 0;
        std::size_t anchor = 0;
        Scalar anchor_weight = Scalar(0);
        std::array<Scalar, 5> current_weights{};
    };
    // The exact storage a plan needs, reserved by the constructor so that
    // appending segments never reallocates.
    struct Reservation {
        std::size_t segments = 0;
    };
    BasicLinePlan(std::size_t source_length, std::size_t target_length, Reservation reservation);
    void release() noexcept;
    // Integer location and local fraction avoid losing subpixel precision at
    // large coordinates. The rational factory is used by regular resampling.
    struct Coordinate {
        std::int64_t cell = 0;
        Scalar fraction = Scalar(0);
    };
    static Coordinate coordinate(std::int64_t numerator, std::int64_t denominator);
    void append_point(Coordinate position);
    void append_basin(std::int64_t left, std::int64_t right, std::int64_t denominator);
    std::size_t source_length_ = 0;
    std::size_t target_length_ = 0;
    std::vector<std::size_t> offsets_;
    std::vector<Segment> segments_;
    friend struct detail::Kernel<Scalar>;
};

// Samples are [node][lane], with node_stride measured in elements. Lanes are
// independent signals/channels, contiguous within a node. Padding is not read.
template<Precision Scalar>
struct BasicLineInput {
    std::span<const Scalar> samples;
    std::size_t node_stride = 1;
};
template<Precision Scalar>
struct BasicLineOutput {
    std::span<Scalar> samples;
    std::size_t node_stride = 1;
};

// Owns a source snapshot and five admitted currents per cell/lane. Compile
// once, evaluate many plans. No caller borrows survive a compile call.
// States: empty (moved-from), unprepared (constructed, or after a numerical
// failure) and ready (after a successful compile).
template<Precision Scalar>
class BasicProfile final {
public:
    explicit BasicProfile(std::size_t length, std::size_t lanes = 1);
    BasicProfile(const BasicProfile<Scalar>&) = delete;
    BasicProfile<Scalar>& operator=(const BasicProfile<Scalar>&) = delete;
    BasicProfile(BasicProfile<Scalar>&& other) noexcept;
    BasicProfile<Scalar>& operator=(BasicProfile<Scalar>&& other) noexcept;
    // Zero for an empty (moved-from) profile.
    [[nodiscard]] std::size_t length() const noexcept;
    [[nodiscard]] std::size_t lanes() const noexcept;
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
private:
    void release() noexcept;
    std::size_t length_ = 0;
    std::size_t lanes_ = 0;
    bool ready_ = false;
    std::vector<Scalar> anchors_;
    std::vector<Scalar> currents_;
    friend struct detail::Kernel<Scalar>;
};

// One workspace per concurrent compile. Shape must match the profile exactly.
template<Precision Scalar>
class BasicLineWorkspace final {
public:
    explicit BasicLineWorkspace(std::size_t length, std::size_t lanes = 1);
    BasicLineWorkspace(const BasicLineWorkspace<Scalar>&) = delete;
    BasicLineWorkspace<Scalar>& operator=(const BasicLineWorkspace<Scalar>&) = delete;
    BasicLineWorkspace(BasicLineWorkspace<Scalar>&& other) noexcept;
    BasicLineWorkspace<Scalar>& operator=(BasicLineWorkspace<Scalar>&& other) noexcept;
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
private:
    void release() noexcept;
    std::size_t length_ = 0;
    std::size_t lanes_ = 0;
    std::vector<Scalar> first_;
    std::vector<Scalar> second_;
    std::vector<Scalar> raw_;
    std::vector<std::int8_t> signs_;
    std::vector<std::int8_t> boundary_signs_;
    std::vector<std::size_t> boundaries_;
    friend struct detail::Kernel<Scalar>;
};

// Allocation-free. Finite magnitudes <= 1e10 (float), <= 1e100 (double). Validation
// failure preserves the profile; numerical_failure leaves it unprepared. Source may
// not reference the internals of profile/workspace (they have no public borrows).
// The result is not bounded by the input range: it can overshoot near sign
// transitions (docs/PRECISION.md); callers clamp where their format requires it.
template<Precision Scalar>
[[nodiscard]] Status compile(BasicLineInput<Scalar> source, BasicProfile<Scalar>& profile, BasicLineWorkspace<Scalar>& workspace) noexcept;
// Allocation-free. Validates all extents before writing. Profile/plan immutable;
// output disjoint from their owned storage. Failure leaves destination unchanged.
template<Precision Scalar>
[[nodiscard]] Status evaluate(const BasicProfile<Scalar>& profile, const BasicLinePlan<Scalar>& plan, BasicLineOutput<Scalar> output) noexcept;

template<Precision Scalar>
struct BasicImageInput {
    std::span<const Scalar> samples;
    std::size_t row_stride = 0;
};
template<Precision Scalar>
struct BasicImageOutput {
    std::span<Scalar> samples;
    std::size_t row_stride = 0;
};

// Interleaved channels. Axis order matches Plan Paint: vertical first if height
// shrinks, otherwise horizontal first. Unchanged axes are skipped. The order is
// part of this nonlinear operator, not an interchangeable scheduling decision.
template<Precision Scalar>
class BasicImagePlan final {
public:
    explicit BasicImagePlan(std::size_t source_width, std::size_t source_height,
                            std::size_t target_width, std::size_t target_height,
                            Sampling sampling = Sampling::nodal);
    BasicImagePlan(const BasicImagePlan<Scalar>&) = delete;
    BasicImagePlan<Scalar>& operator=(const BasicImagePlan<Scalar>&) = delete;
    BasicImagePlan(BasicImagePlan<Scalar>&& other) noexcept;
    BasicImagePlan<Scalar>& operator=(BasicImagePlan<Scalar>&& other) noexcept;
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
private:
    BasicLinePlan<Scalar> horizontal_;
    BasicLinePlan<Scalar> vertical_;
    friend struct detail::Kernel<Scalar>;
    friend class BasicImageWorkspace<Scalar>;
};

// Holds line storage only for the axes the plan changes, and the
// intermediate image only when both change. Retains no reference to the plan.
template<Precision Scalar>
class BasicImageWorkspace final {
public:
    explicit BasicImageWorkspace(const BasicImagePlan<Scalar>& plan, std::size_t channels);
    BasicImageWorkspace(const BasicImageWorkspace<Scalar>&) = delete;
    BasicImageWorkspace<Scalar>& operator=(const BasicImageWorkspace<Scalar>&) = delete;
    BasicImageWorkspace(BasicImageWorkspace<Scalar>&& other) noexcept;
    BasicImageWorkspace<Scalar>& operator=(BasicImageWorkspace<Scalar>&& other) noexcept;
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
private:
    // One changed axis: the profile and compile workspace of its lines.
    struct AxisWork {
        BasicProfile<Scalar> profile;
        BasicLineWorkspace<Scalar> work;
    };
    void release() noexcept;
    std::optional<AxisWork> horizontal_;
    std::optional<AxisWork> vertical_;
    std::vector<Scalar> middle_;
    std::size_t source_width_ = 0;
    std::size_t source_height_ = 0;
    std::size_t target_width_ = 0;
    std::size_t target_height_ = 0;
    std::size_t channels_ = 0;
    friend struct detail::Kernel<Scalar>;
};

// Allocation-free; no global state or threads. Different calls may run in
// parallel using separate workspaces. Source/destination must be disjoint.
// Validation failure leaves output unchanged; numerical_failure may leave a
// partially written output. Colour/alpha conversion belongs to the caller.
template<Precision Scalar>
[[nodiscard]] Status resize(const BasicImagePlan<Scalar>& plan, BasicImageInput<Scalar> source, BasicImageOutput<Scalar> output,
                            BasicImageWorkspace<Scalar>& workspace) noexcept;

// Existing names remain binary64. Explicit 32/64 aliases select storage and
// arithmetic together; no runtime dispatch or precision conversion is involved.
using LinePlan = BasicLinePlan<double>;
using LinePlan64 = BasicLinePlan<double>;
using LinePlan32 = BasicLinePlan<float>;
extern template class BasicLinePlan<float>;
extern template class BasicLinePlan<double>;
using LineInput = BasicLineInput<double>;
using LineInput64 = BasicLineInput<double>;
using LineInput32 = BasicLineInput<float>;
using LineOutput = BasicLineOutput<double>;
using LineOutput64 = BasicLineOutput<double>;
using LineOutput32 = BasicLineOutput<float>;
using Profile = BasicProfile<double>;
using Profile64 = BasicProfile<double>;
using Profile32 = BasicProfile<float>;
extern template class BasicProfile<float>;
extern template class BasicProfile<double>;
using LineWorkspace = BasicLineWorkspace<double>;
using LineWorkspace64 = BasicLineWorkspace<double>;
using LineWorkspace32 = BasicLineWorkspace<float>;
extern template class BasicLineWorkspace<float>;
extern template class BasicLineWorkspace<double>;
using ImageInput = BasicImageInput<double>;
using ImageInput64 = BasicImageInput<double>;
using ImageInput32 = BasicImageInput<float>;
using ImageOutput = BasicImageOutput<double>;
using ImageOutput64 = BasicImageOutput<double>;
using ImageOutput32 = BasicImageOutput<float>;
using ImagePlan = BasicImagePlan<double>;
using ImagePlan64 = BasicImagePlan<double>;
using ImagePlan32 = BasicImagePlan<float>;
extern template class BasicImagePlan<float>;
extern template class BasicImagePlan<double>;
using ImageWorkspace = BasicImageWorkspace<double>;
using ImageWorkspace64 = BasicImageWorkspace<double>;
using ImageWorkspace32 = BasicImageWorkspace<float>;
extern template class BasicImageWorkspace<float>;
extern template class BasicImageWorkspace<double>;
} // namespace stx_conv
