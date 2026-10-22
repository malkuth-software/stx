// Malformed-input, extreme-dimension and state sweeps (hardening, 2026-10-22).
//
// Every public operation is driven through each class of invalid request
// (shape, storage, stride, overlap, nonfinite and out-of-range samples,
// empty moved-from owners, unprepared profiles) at every position of small
// lines and images, in both precisions. Each failure must return its named
// status and leave the destination and the profile exactly as they were.
// Valid requests at the numeric extremes (the sample limit, subnormals and
// mixtures of both) must succeed, never report numerical_failure, and give
// finite, repeatable results.
#include <stx_conv/conv.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
std::size_t checks = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

template<stx_conv::Precision Scalar>
constexpr Scalar sample_limit() {
    if constexpr (std::is_same_v<Scalar, float>) {
        return 1.0e10F;
    } else {
        return 1.0e100;
    }
}

// Bitwise equality, so that a canary of NaN compares equal to itself.
template<stx_conv::Precision Scalar>
bool same_bits(const std::vector<Scalar>& left, const std::vector<Scalar>& right) {
    if (left.size() != right.size()) return false;
    const bool equal = std::memcmp(left.data(), right.data(), left.size() * sizeof(Scalar)) == 0;
    return equal;
}

// A deterministic generator for sample values (xorshift64*).
struct Generator {
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    std::uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        const std::uint64_t value = state * 2685821657736338717ULL;
        return value;
    }
    double unit() {
        const double value = static_cast<double>(next() >> 11) / 9007199254740992.0;
        return value;
    }
};

// Constructors: invalid dimensions throw invalid_argument and leave nothing.
template<stx_conv::Precision Scalar>
void constructor_sweep() {
    const std::array<std::array<std::size_t, 2>, 5> invalid{{
        {0, 1}, {1, 0}, {400000001, 1}, {1, 400000001}, {std::numeric_limits<std::size_t>::max(), 1}}};
    for (const std::array<std::size_t, 2>& shape : invalid) {
        bool plan_threw = false;
        try {
            const stx_conv::BasicLinePlan<Scalar> plan(shape[0], shape[1]);
        } catch (const std::invalid_argument&) {
            plan_threw = true;
        }
        require(plan_threw, "invalid plan dimensions rejected");
        bool profile_threw = false;
        try {
            const stx_conv::BasicProfile<Scalar> profile(shape[0], shape[1]);
        } catch (const std::invalid_argument&) {
            profile_threw = true;
        }
        require(profile_threw, "invalid profile dimensions rejected");
        bool work_threw = false;
        try {
            const stx_conv::BasicLineWorkspace<Scalar> work(shape[0], shape[1]);
        } catch (const std::invalid_argument&) {
            work_threw = true;
        }
        require(work_threw, "invalid workspace dimensions rejected");
    }
    bool sampling_threw = false;
    try {
        const stx_conv::BasicLinePlan<Scalar> plan(4, 7, static_cast<stx_conv::Sampling>(7));
    } catch (const std::invalid_argument&) {
        sampling_threw = true;
    }
    require(sampling_threw, "unknown sampling rule rejected");
    // Absolute positions: outside the domain, nonfinite, or none at all.
    const std::array<Scalar, 5> bad_positions{Scalar(-0.5), Scalar(7.5), std::numeric_limits<Scalar>::infinity(),
                                              std::numeric_limits<Scalar>::quiet_NaN(), Scalar(6.0001)};
    for (Scalar position : bad_positions) {
        bool threw = false;
        try {
            const stx_conv::BasicLinePlan<Scalar> plan = stx_conv::BasicLinePlan<Scalar>::points(7, {&position, 1});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        require(threw, "absolute position outside the domain rejected");
    }
    bool empty_threw = false;
    try {
        const stx_conv::BasicLinePlan<Scalar> plan =
            stx_conv::BasicLinePlan<Scalar>::points(7, std::span<const Scalar>());
    } catch (const std::invalid_argument&) {
        empty_threw = true;
    }
    require(empty_threw, "an empty point list rejected");
    // Local positions: cell past the end, fraction outside [0, 1), and a
    // nonzero fraction on the last node.
    const std::array<stx_conv::BasicSamplePosition<Scalar>, 5> bad_local{{
        {7, Scalar(0)}, {2, Scalar(1)}, {2, Scalar(-0.25)}, {2, std::numeric_limits<Scalar>::quiet_NaN()},
        {6, Scalar(0.5)}}};
    for (const stx_conv::BasicSamplePosition<Scalar>& position : bad_local) {
        bool threw = false;
        try {
            const stx_conv::BasicLinePlan<Scalar> plan = stx_conv::BasicLinePlan<Scalar>::points(7, {&position, 1});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        require(threw, "local position outside the domain rejected");
    }
    // The largest admitted length with a short point list is accepted.
    const std::array<stx_conv::BasicSamplePosition<Scalar>, 2> far{{{399999998, Scalar(0.5)}, {399999999, Scalar(0)}}};
    const stx_conv::BasicLinePlan<Scalar> far_plan = stx_conv::BasicLinePlan<Scalar>::points(400000000, far);
    require(far_plan.source_length() == 400000000 && far_plan.target_length() == 2, "largest admitted length");
    // Extreme ratios, both ways, under both rules.
    for (stx_conv::Sampling sampling : {stx_conv::Sampling::nodal, stx_conv::Sampling::pixel_area}) {
        const stx_conv::BasicLinePlan<Scalar> enlarge(2, 200003, sampling);
        const stx_conv::BasicLinePlan<Scalar> reduce(200003, 1, sampling);
        const stx_conv::BasicLinePlan<Scalar> reduce_two(200003, 2, sampling);
        require(enlarge.target_length() == 200003 && reduce.target_length() == 1 && reduce_two.target_length() == 2,
                "extreme ratios construct");
    }
    // Image workspaces: zero channels, and an empty plan.
    const stx_conv::BasicImagePlan<Scalar> image_plan(5, 4, 9, 2);
    bool channels_threw = false;
    try {
        const stx_conv::BasicImageWorkspace<Scalar> work(image_plan, 0);
    } catch (const std::invalid_argument&) {
        channels_threw = true;
    }
    require(channels_threw, "zero channels rejected");
    stx_conv::BasicImagePlan<Scalar> source_plan(5, 4, 9, 2);
    const stx_conv::BasicImagePlan<Scalar> taken(std::move(source_plan));
    bool empty_plan_threw = false;
    try {
        const stx_conv::BasicImageWorkspace<Scalar> work(source_plan, 3);
    } catch (const std::invalid_argument&) {
        empty_plan_threw = true;
    }
    require(empty_plan_threw, "workspace from an empty plan rejected");
    require(taken.storage_bytes() > 0 && source_plan.storage_bytes() == 0, "moved image plan is empty");
}

// Fills `values` (length x stride) with finite samples in the lanes and NaN
// in the padding, which no operation may read.
template<stx_conv::Precision Scalar>
void fill_line(std::vector<Scalar>& values, std::size_t length, std::size_t lanes, std::size_t stride,
               Generator& generator) {
    values.assign(length * stride, std::numeric_limits<Scalar>::quiet_NaN());
    for (std::size_t node = 0; node < length; ++node) {
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            values[node * stride + lane] = static_cast<Scalar>(generator.unit() * 2.0 - 1.0);
        }
    }
}

// Line operations: every invalid request at every position, for lengths
// 1 to 7 (the short-line closure, the five-node switch and the general
// path), lanes 1 to 3 and strides with zero to two padding elements.
template<stx_conv::Precision Scalar>
void line_sweep() {
    Generator generator{};
    const Scalar limit = sample_limit<Scalar>();
    const Scalar beyond = std::nextafter(limit, std::numeric_limits<Scalar>::infinity());
    for (std::size_t length = 1; length <= 7; ++length) {
        for (std::size_t lanes = 1; lanes <= 3; ++lanes) {
            for (std::size_t stride = lanes; stride <= lanes + 2; ++stride) {
                stx_conv::BasicProfile<Scalar> profile(length, lanes);
                stx_conv::BasicLineWorkspace<Scalar> work(length, lanes);
                const stx_conv::BasicLinePlan<Scalar> plan(length, 2 * length + 1);
                const std::size_t out_stride = lanes + 1;
                std::vector<Scalar> output(plan.target_length() * out_stride, Scalar(-7));
                require(stx_conv::evaluate(profile, plan, {output, out_stride}) == stx_conv::Status::unprepared_profile,
                        "unprepared profile");
                std::vector<Scalar> source;
                fill_line(source, length, lanes, stride, generator);
                require(stx_conv::compile({source, stride}, profile, work) == stx_conv::Status::ok, "valid compile");
                require(stx_conv::evaluate(profile, plan, {output, out_stride}) == stx_conv::Status::ok, "valid evaluate");
                for (std::size_t node = 0; node < plan.target_length(); ++node) {
                    for (std::size_t lane = 0; lane < lanes; ++lane) {
                        require(std::isfinite(output[node * out_stride + lane]), "finite output");
                    }
                    require(output[node * out_stride + lanes] == Scalar(-7), "output padding untouched");
                }
                const std::vector<Scalar> reference = output;
                // Short source storage at every size below the requirement.
                const std::size_t needed = (length - 1) * stride + lanes;
                for (std::size_t size = 0; size < needed; ++size) {
                    const std::span<const Scalar> short_source(source.data(), size);
                    require(stx_conv::compile({short_source, stride}, profile, work)
                                == stx_conv::Status::insufficient_storage, "short source");
                }
                if (lanes > 1) {
                    require(stx_conv::compile({source, lanes - 1}, profile, work)
                                == stx_conv::Status::insufficient_storage, "stride below lanes");
                }
                // Each invalid value at each position: named status, profile kept.
                const std::array<Scalar, 5> invalid_values{std::numeric_limits<Scalar>::quiet_NaN(),
                    std::numeric_limits<Scalar>::infinity(), -std::numeric_limits<Scalar>::infinity(), beyond, -beyond};
                for (std::size_t node = 0; node < length; ++node) {
                    for (std::size_t lane = 0; lane < lanes; ++lane) {
                        for (Scalar invalid : invalid_values) {
                            std::vector<Scalar> broken = source;
                            broken[node * stride + lane] = invalid;
                            const stx_conv::Status expected = std::isfinite(invalid)
                                ? stx_conv::Status::input_out_of_range : stx_conv::Status::nonfinite_input;
                            require(stx_conv::compile({broken, stride}, profile, work) == expected, "invalid sample status");
                        }
                    }
                }
                std::fill(output.begin(), output.end(), Scalar(-7));
                require(stx_conv::evaluate(profile, plan, {output, out_stride}) == stx_conv::Status::ok, "kept profile");
                require(same_bits(output, reference), "failed compiles keep the profile");
                // Short destination storage: destination untouched.
                const std::size_t out_needed = (plan.target_length() - 1) * out_stride + lanes;
                for (std::size_t size = 0; size < out_needed; ++size) {
                    std::vector<Scalar> canary(out_needed, std::numeric_limits<Scalar>::quiet_NaN());
                    const std::vector<Scalar> before = canary;
                    const std::span<Scalar> short_output(canary.data(), size);
                    require(stx_conv::evaluate(profile, plan, {short_output, out_stride})
                                == stx_conv::Status::insufficient_storage, "short destination");
                    require(same_bits(canary, before), "short destination untouched");
                }
                // Shape mismatches.
                const stx_conv::BasicLinePlan<Scalar> other_plan(length + 1, 3);
                require(stx_conv::evaluate(profile, other_plan, {output, out_stride}) == stx_conv::Status::invalid_shape,
                        "plan of another length");
                stx_conv::BasicLineWorkspace<Scalar> other_work(length + 1, lanes);
                require(stx_conv::compile({source, stride}, profile, other_work) == stx_conv::Status::invalid_shape,
                        "workspace of another length");
                stx_conv::BasicLineWorkspace<Scalar> other_lanes(length, lanes + 1);
                require(stx_conv::compile({source, stride + 1}, profile, other_lanes) == stx_conv::Status::invalid_shape,
                        "workspace of other lanes");
                // Exactly the limit is admitted.
                std::vector<Scalar> edge = source;
                edge[0] = limit;
                edge[(length - 1) * stride + lanes - 1] = -limit;
                require(stx_conv::compile({edge, stride}, profile, work) == stx_conv::Status::ok, "limit admitted");
            }
        }
    }
}

// The empty (moved-from) state of each owner, in each operation, and its
// recovery by assignment.
template<stx_conv::Precision Scalar>
void state_sweep() {
    std::vector<Scalar> source(9, Scalar(0));
    for (std::size_t index = 0; index < source.size(); ++index) source[index] = static_cast<Scalar>(index % 4);
    std::vector<Scalar> output(17, Scalar(-7));
    stx_conv::BasicProfile<Scalar> profile(9);
    stx_conv::BasicLineWorkspace<Scalar> work(9);
    stx_conv::BasicLinePlan<Scalar> plan(9, 17);
    require(stx_conv::compile({source, 1}, profile, work) == stx_conv::Status::ok, "state compile");
    // Profile: ready -> moved-from (empty) -> reassigned (unprepared) -> ready.
    stx_conv::BasicProfile<Scalar> held(std::move(profile));
    require(profile.length() == 0 && profile.lanes() == 0 && profile.storage_bytes() == 0, "moved profile empty");
    require(stx_conv::evaluate(profile, plan, {output, 1}) == stx_conv::Status::unprepared_profile, "empty profile evaluate");
    require(stx_conv::compile({source, 1}, profile, work) == stx_conv::Status::invalid_shape, "empty profile compile");
    require(stx_conv::evaluate(held, plan, {output, 1}) == stx_conv::Status::ok, "moved-to profile ready");
    profile = stx_conv::BasicProfile<Scalar>(9);
    require(stx_conv::evaluate(profile, plan, {output, 1}) == stx_conv::Status::unprepared_profile, "new profile unprepared");
    require(stx_conv::compile({source, 1}, profile, work) == stx_conv::Status::ok, "reassigned profile compiles");
    // Workspace.
    stx_conv::BasicLineWorkspace<Scalar> held_work(std::move(work));
    require(work.storage_bytes() == 0, "moved workspace empty");
    require(stx_conv::compile({source, 1}, profile, work) == stx_conv::Status::invalid_shape, "empty workspace compile");
    require(stx_conv::compile({source, 1}, profile, held_work) == stx_conv::Status::ok, "moved-to workspace");
    work = std::move(held_work);
    require(stx_conv::compile({source, 1}, profile, work) == stx_conv::Status::ok, "reassigned workspace");
    // Plan.
    stx_conv::BasicLinePlan<Scalar> held_plan(std::move(plan));
    require(plan.source_length() == 0 && plan.target_length() == 0 && plan.storage_bytes() == 0, "moved plan empty");
    std::fill(output.begin(), output.end(), Scalar(-7));
    require(stx_conv::evaluate(profile, plan, {output, 1}) == stx_conv::Status::invalid_shape, "empty plan evaluate");
    require(output[0] == Scalar(-7), "empty plan leaves the destination");
    require(stx_conv::evaluate(profile, held_plan, {output, 1}) == stx_conv::Status::ok, "moved-to plan");
    // Images.
    stx_conv::BasicImagePlan<Scalar> image_plan(3, 3, 5, 2);
    stx_conv::BasicImageWorkspace<Scalar> image_work(image_plan, 1);
    const std::vector<Scalar> pixels(9, Scalar(0.5));
    std::vector<Scalar> image_output(10, Scalar(-7));
    require(stx_conv::resize(image_plan, {pixels, 3}, {image_output, 5}, image_work) == stx_conv::Status::ok, "image ok");
    stx_conv::BasicImageWorkspace<Scalar> held_image_work(std::move(image_work));
    require(image_work.storage_bytes() == 0, "moved image workspace empty");
    std::fill(image_output.begin(), image_output.end(), Scalar(-7));
    require(stx_conv::resize(image_plan, {pixels, 3}, {image_output, 5}, image_work) == stx_conv::Status::invalid_shape,
            "empty image workspace");
    require(image_output[0] == Scalar(-7), "empty image workspace leaves the destination");
    stx_conv::BasicImagePlan<Scalar> held_image_plan(std::move(image_plan));
    require(stx_conv::resize(image_plan, {pixels, 3}, {image_output, 5}, held_image_work) == stx_conv::Status::invalid_shape,
            "empty image plan");
    require(stx_conv::resize(held_image_plan, {pixels, 3}, {image_output, 5}, held_image_work) == stx_conv::Status::ok,
            "moved-to image owners");
    // A workspace prepared for other dimensions.
    const stx_conv::BasicImagePlan<Scalar> other(3, 3, 5, 3);
    require(stx_conv::resize(other, {pixels, 3}, {image_output, 5}, held_image_work) == stx_conv::Status::invalid_shape,
            "workspace of other dimensions");
}

// Images: every invalid request for shapes up to 4 x 4, targets up to 5 x 5
// and one to three channels; a valid resize leaves padding untouched.
template<stx_conv::Precision Scalar>
void image_sweep() {
    Generator generator{};
    for (std::size_t sw = 1; sw <= 4; ++sw) {
        for (std::size_t sh = 1; sh <= 4; ++sh) {
            for (std::size_t tw = 1; tw <= 5; tw += 2) {
                for (std::size_t th = 1; th <= 5; th += 2) {
                    for (std::size_t channels = 1; channels <= 3; ++channels) {
                        const stx_conv::BasicImagePlan<Scalar> plan(sw, sh, tw, th);
                        stx_conv::BasicImageWorkspace<Scalar> work(plan, channels);
                        const std::size_t source_stride = sw * channels + 1;
                        const std::size_t output_stride = tw * channels + 2;
                        std::vector<Scalar> source(sh * source_stride, std::numeric_limits<Scalar>::quiet_NaN());
                        for (std::size_t y = 0; y < sh; ++y) {
                            for (std::size_t x = 0; x < sw * channels; ++x) {
                                source[y * source_stride + x] = static_cast<Scalar>(generator.unit());
                            }
                        }
                        std::vector<Scalar> output(th * output_stride, Scalar(-7));
                        require(stx_conv::resize(plan, {source, source_stride}, {output, output_stride}, work)
                                    == stx_conv::Status::ok, "valid resize");
                        for (std::size_t y = 0; y < th; ++y) {
                            for (std::size_t x = 0; x < tw * channels; ++x) {
                                require(std::isfinite(output[y * output_stride + x]), "finite image output");
                            }
                            require(output[y * output_stride + tw * channels] == Scalar(-7), "image padding untouched");
                        }
                        const std::vector<Scalar> before = output;
                        const std::size_t source_needed = (sh - 1) * source_stride + sw * channels;
                        for (std::size_t size = 0; size < source_needed; ++size) {
                            const std::span<const Scalar> short_source(source.data(), size);
                            require(stx_conv::resize(plan, {short_source, source_stride}, {output, output_stride}, work)
                                        == stx_conv::Status::insufficient_storage, "short image source");
                        }
                        const std::size_t output_needed = (th - 1) * output_stride + tw * channels;
                        for (std::size_t size = 0; size < output_needed; ++size) {
                            const std::span<Scalar> short_output(output.data(), size);
                            require(stx_conv::resize(plan, {source, source_stride}, {short_output, output_stride}, work)
                                        == stx_conv::Status::insufficient_storage, "short image output");
                        }
                        if (sh > 1) {
                            require(stx_conv::resize(plan, {source, sw * channels - 1}, {output, output_stride}, work)
                                        == stx_conv::Status::insufficient_storage, "row stride below the row");
                        }
                        for (std::size_t y = 0; y < sh; ++y) {
                            for (std::size_t x = 0; x < sw * channels; ++x) {
                                std::vector<Scalar> broken = source;
                                broken[y * source_stride + x] = std::numeric_limits<Scalar>::infinity();
                                require(stx_conv::resize(plan, {broken, source_stride}, {output, output_stride}, work)
                                            == stx_conv::Status::nonfinite_input, "image nonfinite sample");
                            }
                        }
                        require(same_bits(output, before), "failed resizes leave the destination");
                        // Overlap: in one shared buffer, the destination is its
                        // first output.size() elements and the source starts at
                        // every offset inside it.
                        const std::size_t source_span = source.size();
                        const std::size_t output_span = output.size();
                        std::vector<Scalar> shared(source_span + output_span, Scalar(0.25));
                        const std::span<Scalar> shared_output(shared.data(), output_span);
                        for (std::size_t offset = 0; offset < output_span; ++offset) {
                            const std::span<const Scalar> shared_source(shared.data() + offset, source_span);
                            const std::vector<Scalar> shared_before = shared;
                            require(stx_conv::resize(plan, {shared_source, source_stride}, {shared_output, output_stride}, work)
                                        == stx_conv::Status::overlap, "overlapping spans");
                            require(same_bits(shared, shared_before), "overlap leaves the buffer");
                        }
                        // Adjacent but disjoint spans are not an overlap.
                        const std::span<const Scalar> adjacent(shared.data() + output_span, source_span);
                        require(stx_conv::resize(plan, {adjacent, source_stride}, {shared_output, output_stride}, work)
                                    == stx_conv::Status::ok, "adjacent spans");
                    }
                }
            }
        }
    }
}

// Valid lines at the numeric extremes: the limit, subnormals and their
// mixtures, under every rule, never fail and are finite and repeatable.
template<stx_conv::Precision Scalar>
void extreme_value_sweep() {
    const Scalar limit = sample_limit<Scalar>();
    const Scalar tiny = std::numeric_limits<Scalar>::denorm_min();
    const Scalar small = std::numeric_limits<Scalar>::min();
    const std::array<Scalar, 12> palette{Scalar(0), limit, -limit, limit / Scalar(3), -limit / Scalar(7), tiny, -tiny,
                                         small, Scalar(1), Scalar(-1), std::nextafter(limit, Scalar(0)), Scalar(1e-30)};
    Generator generator{};
    for (std::size_t length = 1; length <= 40; ++length) {
        for (std::size_t trial = 0; trial < 24; ++trial) {
            const std::size_t lanes = 1 + trial % 3;
            std::vector<Scalar> source(length * lanes, Scalar(0));
            for (std::size_t index = 0; index < source.size(); ++index) {
                source[index] = palette[static_cast<std::size_t>(generator.next() % palette.size())];
            }
            stx_conv::BasicProfile<Scalar> profile(length, lanes);
            stx_conv::BasicLineWorkspace<Scalar> work(length, lanes);
            require(stx_conv::compile({source, lanes}, profile, work) == stx_conv::Status::ok, "extreme compile");
            for (stx_conv::Sampling sampling : {stx_conv::Sampling::nodal, stx_conv::Sampling::pixel_area}) {
                for (std::size_t target : {std::size_t(1), length / 2 + 1, length * 3 + 1}) {
                    const stx_conv::BasicLinePlan<Scalar> plan(length, target, sampling);
                    std::vector<Scalar> first(target * lanes, Scalar(0));
                    std::vector<Scalar> second(target * lanes, Scalar(0));
                    require(stx_conv::evaluate(profile, plan, {first, lanes}) == stx_conv::Status::ok, "extreme evaluate");
                    for (Scalar value : first) require(std::isfinite(value), "extreme output finite");
                    require(stx_conv::compile({source, lanes}, profile, work) == stx_conv::Status::ok, "extreme recompile");
                    require(stx_conv::evaluate(profile, plan, {second, lanes}) == stx_conv::Status::ok, "extreme repeat");
                    require(same_bits(first, second), "extreme output repeatable");
                }
            }
        }
    }
    // Two passes over alternating extremes: the second pass reads the
    // first pass's overshoot without revalidation; it must stay finite.
    for (std::size_t size = 2; size <= 12; ++size) {
        std::vector<Scalar> image(size * size * 2, Scalar(0));
        for (std::size_t index = 0; index < image.size(); ++index) {
            image[index] = (index / 2 + index / (2 * size)) % 2 == 0 ? limit : -limit;
        }
        for (stx_conv::Sampling sampling : {stx_conv::Sampling::nodal, stx_conv::Sampling::pixel_area}) {
            const stx_conv::BasicImagePlan<Scalar> plan(size, size, size * 2 + 1, size + 3, sampling);
            stx_conv::BasicImageWorkspace<Scalar> work(plan, 2);
            std::vector<Scalar> output((size * 2 + 1) * (size + 3) * 2, Scalar(0));
            require(stx_conv::resize(plan, {image, size * 2}, {output, (size * 2 + 1) * 2}, work) == stx_conv::Status::ok,
                    "two extreme passes");
            for (Scalar value : output) require(std::isfinite(value), "two-pass output finite");
        }
    }
}

template<stx_conv::Precision Scalar>
void all_sweeps() {
    constructor_sweep<Scalar>();
    line_sweep<Scalar>();
    state_sweep<Scalar>();
    image_sweep<Scalar>();
    extreme_value_sweep<Scalar>();
}
} // namespace

int main() {
    all_sweeps<float>();
    all_sweeps<double>();
    std::printf("PASS: constructor, line, state, image and extreme-value sweeps; %zu checks\n", checks);
    return 0;
}
