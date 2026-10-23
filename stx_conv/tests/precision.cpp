#include <stx_conv/conv.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void require_ok(stx_conv::Status status) { require(status == stx_conv::Status::ok, "precision operation failed"); }
float sample(std::size_t index, std::size_t pattern) {
    const std::uint32_t mixed = static_cast<std::uint32_t>(index) * 2654435761U + 2246822519U;
    switch (pattern) {
        case 0: return 0.25F;
        case 1: return static_cast<float>(index % 97) / 97.0F;
        case 2: return index % 19 < 9 ? -0.5F : 0.75F;
        case 3: return static_cast<float>((mixed ^ (mixed >> 13)) & 65535U) / 32768.0F - 1.0F;
        case 4: return std::sin(static_cast<float>(index) * 0.17F);
        default: return index % 2 == 0 ? -1.0F : 1.0F;
    }
}
const char* phase = "lines";
std::array<std::size_t, 7> context{};
double max_error = 0.0;
std::size_t comparisons = 0;
void compare(float value, double reference, double tolerance = 2e-5) {
    const double error = std::abs(static_cast<double>(value) - reference);
    max_error = std::max(max_error, error);
    ++comparisons;
    if (!std::isfinite(value) || error > tolerance * std::max(1.0, std::abs(reference))) {
        std::fprintf(stderr, "float %.9g double %.17g error %.9g\n", static_cast<double>(value), reference, error);
        std::fprintf(stderr, "%s %zu %zu %zu %zu %zu %zu %zu\n", phase, context[0], context[1], context[2], context[3], context[4], context[5], context[6]);
        require(false, "float32/double agreement");
    }
}
void lines() {
    for (std::size_t length = 1; length <= 96; ++length) {
        for (std::size_t lanes : {1U, 3U, 4U, 7U}) {
            const std::size_t stride = lanes + 2;
            stx_conv::Profile32 profile32(length, lanes);
            stx_conv::Profile64 profile64(length, lanes);
            stx_conv::LineWorkspace32 work32(length, lanes);
            stx_conv::LineWorkspace64 work64(length, lanes);
            require(profile32.storage_bytes() * 2 == profile64.storage_bytes(), "float profile half size");
            for (std::size_t pattern = 0; pattern < 6; ++pattern) {
                std::vector<float> input32(length * stride, std::numeric_limits<float>::quiet_NaN());
                std::vector<double> input64(length * stride, std::numeric_limits<double>::quiet_NaN());
                for (std::size_t node = 0; node < length; ++node) {
                    for (std::size_t lane = 0; lane < lanes; ++lane) {
                        input32[node * stride + lane] = sample(node + lane * 17, pattern);
                        input64[node * stride + lane] = input32[node * stride + lane];
                    }
                }
                require_ok(stx_conv::compile({input32, stride}, profile32, work32));
                require_ok(stx_conv::compile({input64, stride}, profile64, work64));
                for (std::size_t target : {std::size_t(1), std::max(std::size_t(1), length / 3), length, length * 2 + 1}) {
                    for (stx_conv::Sampling sampling : {stx_conv::Sampling::nodal, stx_conv::Sampling::pixel_area}) {
                        context = {length, lanes, pattern, target, static_cast<std::size_t>(sampling), 0, 0};
                        const stx_conv::LinePlan32 plan32(length, target, sampling);
                        const stx_conv::LinePlan64 plan64(length, target, sampling);
                        std::vector<float> out32(target * stride, -17.0F);
                        std::vector<double> out64(target * stride, -17.0);
                        require_ok(stx_conv::evaluate(profile32, plan32, {out32, stride}));
                        require_ok(stx_conv::evaluate(profile64, plan64, {out64, stride}));
                        for (std::size_t node = 0; node < target; ++node) {
                            for (std::size_t lane = 0; lane < lanes; ++lane) {
                                compare(out32[node * stride + lane], out64[node * stride + lane]);
                                if (target == length) { require(out32[node * stride + lane] == input32[node * stride + lane], "float identity exact"); }
                            }
                            require(out32[node * stride + lanes] == -17.0F, "float padding preserved");
                        }
                    }
                }
                const std::array<float, 4> positions32{0.0F, 0.125F * static_cast<float>(length - 1),
                    0.75F * static_cast<float>(length - 1), static_cast<float>(length - 1)};
                const std::array<double, 4> positions64{positions32[0], positions32[1], positions32[2], positions32[3]};
                std::array<stx_conv::SamplePosition32, 4> local{};
                for (std::size_t i = 0; i < 4; ++i) {
                    local[i].cell = static_cast<std::size_t>(positions32[i]);
                    local[i].fraction = positions32[i] - static_cast<float>(local[i].cell);
                }
                const stx_conv::LinePlan32 local_plan = stx_conv::LinePlan32::points(length, local);
                const stx_conv::LinePlan32 points32 = stx_conv::LinePlan32::points(length, positions32);
                const stx_conv::LinePlan64 points64 = stx_conv::LinePlan64::points(length, positions64);
                std::vector<float> out32(4 * lanes, 0.0F);
                std::vector<double> out64(4 * lanes, 0.0);
                require_ok(stx_conv::evaluate(profile32, points32, {out32, lanes}));
                require_ok(stx_conv::evaluate(profile64, points64, {out64, lanes}));
                for (std::size_t i = 0; i < out32.size(); ++i) { compare(out32[i], out64[i]); }
                const std::vector<float> absolute_output = out32;
                require_ok(stx_conv::evaluate(profile32, local_plan, {out32, lanes}));
                require(out32 == absolute_output, "local and absolute point agreement");
            }
        }
    }
}
// An explicit row/column composition checks the 2D scheduler using binary32
// intermediates. Cross-precision agreement is measured separately: CONV's sign
// decisions are discontinuous when a preceding pass rounds onto a plateau.
void axis(const stx_conv::LinePlan32& plan, const std::vector<float>& source, std::vector<float>& output,
          std::size_t source_stride, std::size_t output_stride, std::size_t lines, std::size_t lanes,
          bool vertical) {
    stx_conv::Profile32 profile(plan.source_length(), lanes);
    stx_conv::LineWorkspace32 work(plan.source_length(), lanes);
    for (std::size_t line = 0; line < lines; ++line) {
        const std::size_t source_offset = line * (vertical ? lanes : source_stride);
        const std::size_t output_offset = line * (vertical ? lanes : output_stride);
        const std::span<const float> input(source.data() + source_offset, source.size() - source_offset);
        const std::span<float> destination(output.data() + output_offset, output.size() - output_offset);
        require_ok(stx_conv::compile({input, vertical ? source_stride : lanes}, profile, work));
        require_ok(stx_conv::evaluate(profile, plan, {destination, vertical ? output_stride : lanes}));
    }
}
std::size_t image_samples = 0;
std::size_t image_divergences = 0;
double image_error = 0.0;
void images() {
    phase = "images";
    for (std::size_t width : {1U, 3U, 17U}) {
        for (std::size_t height : {1U, 4U, 23U}) {
            for (std::size_t lanes : {1U, 3U, 4U}) {
                const std::size_t source_stride = width * lanes + 3;
                std::vector<float> input32(height * source_stride, std::numeric_limits<float>::quiet_NaN());
                std::vector<double> input64(height * source_stride, std::numeric_limits<double>::quiet_NaN());
                for (std::size_t pattern = 0; pattern < 6; ++pattern) {
                    for (std::size_t y = 0; y < height; ++y) {
                        for (std::size_t x = 0; x < width * lanes; ++x) {
                            input32[y * source_stride + x] = sample(y * width * lanes + x, pattern);
                            input64[y * source_stride + x] = input32[y * source_stride + x];
                        }
                    }
                    for (std::size_t tw : {std::size_t(1), width, width * 2 + 1}) {
                        for (std::size_t th : {std::size_t(1), height, height * 2 + 1}) {
                            for (stx_conv::Sampling sampling : {stx_conv::Sampling::nodal, stx_conv::Sampling::pixel_area}) {
                                context = {width, height, lanes, pattern, tw, th, static_cast<std::size_t>(sampling)};
                                const std::size_t stride = tw * lanes + 2;
                                const stx_conv::ImagePlan32 plan32(width, height, tw, th, sampling);
                                const stx_conv::ImagePlan64 plan64(width, height, tw, th, sampling);
                                stx_conv::ImageWorkspace32 work32(plan32, lanes);
                                stx_conv::ImageWorkspace64 work64(plan64, lanes);
                                std::vector<float> out32(th * stride, -17.0F);
                                std::vector<double> out64(th * stride, -17.0);
                                require_ok(stx_conv::resize(plan32, {input32, source_stride}, {out32, stride}, work32));
                                require_ok(stx_conv::resize(plan64, {input64, source_stride}, {out64, stride}, work64));
                                const stx_conv::LinePlan32 horizontal(width, tw, sampling);
                                const stx_conv::LinePlan32 vertical(height, th, sampling);
                                std::vector<float> composed(th * stride, -17.0F);
                                if (th < height) {
                                    std::vector<float> middle(width * th * lanes, 0.0F);
                                    axis(vertical, input32, middle, source_stride, width * lanes, width, lanes, true);
                                    axis(horizontal, middle, composed, width * lanes, stride, th, lanes, false);
                                } else {
                                    std::vector<float> middle(tw * height * lanes, 0.0F);
                                    axis(horizontal, input32, middle, source_stride, tw * lanes, height, lanes, false);
                                    axis(vertical, middle, composed, tw * lanes, stride, tw, lanes, true);
                                }
                                require(out32 == composed, "2D equals explicit binary32 line composition");
                                for (std::size_t y = 0; y < th; ++y) {
                                    for (std::size_t x = 0; x < tw * lanes; ++x) {
                                        const float value = out32[y * stride + x];
                                        require(std::isfinite(value), "finite image output");
                                        ++image_samples;
                                        const double error = std::abs(static_cast<double>(value) - out64[y * stride + x]);
                                        image_error = std::max(image_error, error);
                                        if (error > 2e-5) { ++image_divergences; }
                                    }
                                    require(out32[y * stride + tw * lanes] == -17.0F, "image padding preserved");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
void contracts() {
    phase = "contracts";
    stx_conv::Profile32 profile(17);
    stx_conv::LineWorkspace32 work(17);
    const stx_conv::LinePlan32 plan(17, 31);
    std::vector<float> input(17, 1.0F);
    std::vector<float> output(31, -7.0F);
    require(stx_conv::evaluate(profile, plan, {output, 1}) == stx_conv::Status::unprepared_profile, "unprepared float profile");
    require_ok(stx_conv::compile({input, 1}, profile, work));
    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 1e11F}) {
        input[4] = bad;
        const stx_conv::Status expected = std::isfinite(bad) ? stx_conv::Status::input_out_of_range : stx_conv::Status::nonfinite_input;
        require(stx_conv::compile({input, 1}, profile, work) == expected, "float invalid input");
        require_ok(stx_conv::evaluate(profile, plan, {output, 1}));
        require(output[4] == 1.0F, "failed compile preserves float snapshot");
    }
    std::fill(output.begin(), output.end(), -7.0F);
    require(stx_conv::evaluate(profile, plan, {std::span<float>(output.data(), 2), 1}) == stx_conv::Status::insufficient_storage, "float short output");
    require(output[0] == -7.0F, "failed evaluate preserves output");
    stx_conv::Profile32 moved(std::move(profile));
    require(stx_conv::evaluate(profile, plan, {output, 1}) == stx_conv::Status::unprepared_profile, "float moved profile");
    require_ok(stx_conv::evaluate(moved, plan, {output, 1}));
    for (float scale : {1e-10F, 1.0F, 1e10F}) {
        std::vector<double> reference_input(17, 0.0);
        for (std::size_t i = 0; i < 17; ++i) { input[i] = scale * sample(i, 3); reference_input[i] = input[i]; }
        stx_conv::Profile64 reference(17);
        stx_conv::LineWorkspace64 reference_work(17);
        const stx_conv::LinePlan64 reference_plan(17, 31);
        std::vector<double> reference_output(31, 0.0);
        require_ok(stx_conv::compile({input, 1}, moved, work));
        require_ok(stx_conv::compile({reference_input, 1}, reference, reference_work));
        require_ok(stx_conv::evaluate(moved, plan, {output, 1}));
        require_ok(stx_conv::evaluate(reference, reference_plan, {reference_output, 1}));
        for (std::size_t i = 0; i < 31; ++i) { compare(output[i] / scale, reference_output[i] / static_cast<double>(scale)); }
    }
    const std::array<stx_conv::SamplePosition32, 2> local_positions{{{399999998, 0.125F}, {399999999, 0.0F}}};
    const stx_conv::LinePlan32 large_points = stx_conv::LinePlan32::points(400000000, local_positions);
    require(large_points.target_length() == 2, "large local coordinates");
    for (float fraction : {-0.25F, 1.0F, std::numeric_limits<float>::quiet_NaN()}) {
        const stx_conv::SamplePosition32 invalid{0, fraction};
        bool rejected = false;
        try { const stx_conv::LinePlan32 invalid_plan = stx_conv::LinePlan32::points(17, {&invalid, 1}); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid local fraction");
    }
    // Narrow area footprints and singleton closures are still nonempty.
    const std::array<float, 2> tiny_source{0.25F, 0.75F};
    stx_conv::Profile32 tiny_profile(2);
    stx_conv::LineWorkspace32 tiny_work(2);
    require_ok(stx_conv::compile({tiny_source, 1}, tiny_profile, tiny_work));
    const stx_conv::LinePlan32 narrow(2, 100003, stx_conv::Sampling::pixel_area);
    std::vector<float> narrow_output(100003, -7.0F);
    require_ok(stx_conv::evaluate(tiny_profile, narrow, {narrow_output, 1}));
    for (std::size_t i = 0; i < narrow_output.size(); ++i) {
        require(narrow_output[i] >= 0.25F && narrow_output[i] <= 0.75F, "narrow footprint finite and covered");
        if (i != 0) { require(narrow_output[i] >= narrow_output[i - 1], "narrow footprint monotone"); }
    }

    const stx_conv::ImagePlan32 image_plan(17, 1, 17, 1);
    stx_conv::ImageWorkspace32 image_work(image_plan, 1);
    require(stx_conv::resize(image_plan, {input, 17}, {input, 17}, image_work) == stx_conv::Status::overlap, "float overlap");
    // A float endpoint rounded above the last integer node must be rejected.
    const float invalid_position = 16777216.0F;
    bool rejected = false;
    try { const stx_conv::LinePlan32 invalid = stx_conv::LinePlan32::points(16777216, {&invalid_position, 1}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "rounded float coordinate outside domain");
}

template<stx_conv::Precision Scalar>
double timed(const stx_conv::BasicImagePlan<Scalar>& plan, const std::vector<Scalar>& source,
             std::vector<Scalar>& output, stx_conv::BasicImageWorkspace<Scalar>& work,
             std::size_t width, std::size_t target_width) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    require_ok(stx_conv::resize(plan, {source, width * 4}, {output, target_width * 4}, work));
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return ms;
}
void benchmark() {
    constexpr std::size_t width = 512;
    constexpr std::size_t height = 384;
    for (std::size_t pattern : {3U, 1U, 5U}) {
        std::vector<float> source32(width * height * 4, 0.0F);
        std::vector<double> source64(source32.size(), 0.0);
        for (std::size_t i = 0; i < source32.size(); ++i) {
            source32[i] = pattern == 1 ? static_cast<float>(i / 4 % width) / static_cast<float>(width) : sample(i / 4, pattern);
            source64[i] = source32[i];
        }
        for (bool up : {false, true}) {
            const std::size_t tw = up ? 769 : 237;
            const std::size_t th = up ? 577 : 181;
            const stx_conv::ImagePlan32 plan32(width, height, tw, th);
            const stx_conv::ImagePlan64 plan64(width, height, tw, th);
            stx_conv::ImageWorkspace32 work32(plan32, 4);
            stx_conv::ImageWorkspace64 work64(plan64, 4);
            std::vector<float> out32(tw * th * 4, 0.0F);
            std::vector<double> out64(tw * th * 4, 0.0);
            std::vector<double> times32;
            std::vector<double> times64;
            for (std::size_t trial = 0; trial < 12; ++trial) {
                double t32 = 0.0;
                double t64 = 0.0;
                if (trial % 2 == 0) {
                    t32 = timed(plan32, source32, out32, work32, width, tw);
                    t64 = timed(plan64, source64, out64, work64, width, tw);
                } else {
                    t64 = timed(plan64, source64, out64, work64, width, tw);
                    t32 = timed(plan32, source32, out32, work32, width, tw);
                }
                if (trial != 0) { times32.push_back(t32); times64.push_back(t64); }
            }
            double error = 0.0;
            for (std::size_t i = 0; i < out32.size(); ++i) { error = std::max(error, std::abs(static_cast<double>(out32[i]) - out64[i])); }
            std::sort(times32.begin(), times32.end());
            std::sort(times64.begin(), times64.end());
            std::printf("pattern=%zu %zux%zu float_ms=%.6f double_ms=%.6f ratio=%.4f float_range=%.6f:%.6f double_range=%.6f:%.6f max_error=%.9g float_bytes=%zu double_bytes=%zu\n",
                pattern, tw, th, times32[5], times64[5], times32[5] / times64[5], times32.front(), times32.back(),
                times64.front(), times64.back(), error, plan32.storage_bytes() + work32.storage_bytes(), plan64.storage_bytes() + work64.storage_bytes());
        }
    }
}
}
int main(int argc, char** argv) {
    lines();
    images();
    contracts();
    std::printf("PASS: float32 all operations; %zu comparisons; max absolute error %.9g\n", comparisons, max_error);
    std::printf("2D cross-precision diagnostic: %zu / %zu samples differ by >2e-5; maximum %.9g (line-composition checks remain exact)\n", image_divergences, image_samples, image_error);
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark") { benchmark(); }
    return 0;
}
