// Test-only pinned oracle: include its implementation so the double-precision
// line/profile routines can be compared before RGBA quantization or threading.
#include "conv.cpp"
#include "image.cpp"
#include "../tests/fixtures/conv_reference.hpp"
#include <stx_conv/conv.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace {
using Clock = std::chrono::steady_clock;
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void oracle_line(const std::vector<double>& source, std::size_t length, std::size_t target,
                 bool area, std::vector<double>& output) {
    if (length == target) { output = source; return; }
    if (length < 5) {
        if (area) { paint::short_area_line(source, 0, 4, static_cast<int>(length), output, 0, 4, static_cast<int>(target)); }
        else { paint::short_line(source, 0, 4, static_cast<int>(length), output, 0, 4, static_cast<int>(target)); }
        return;
    }
    paint::resample_plan plan;
    if (area) { paint::LineWorkspace::build_pixel_area_plan(plan, static_cast<unsigned>(length), static_cast<unsigned>(target)); }
    else { paint::LineWorkspace::build_resample_plan(plan, static_cast<unsigned>(length), static_cast<unsigned>(target)); }
    paint::LineWorkspace workspace(static_cast<unsigned>(length));
    const int result = workspace.resample_line(source.data(), static_cast<unsigned>(length), 0, 4, plan, output.data(), 0, 4);
    check(result != 0, "reference admission");
}
double maximum_error = 0.0;
void compare(const std::vector<double>& actual, const std::vector<double>& expected) {
    check(actual.size() == expected.size(), "comparison shape");
    for (std::size_t index = 0; index < actual.size(); ++index) {
        const double error = std::abs(actual[index] - expected[index]);
        maximum_error = std::max(maximum_error, error);
        if (!std::isfinite(actual[index]) || error > 2e-11 * std::max(1.0, std::abs(expected[index]))) {
            std::fprintf(stderr, "index=%zu actual=%.17g expected=%.17g error=%.9g\n", index, actual[index], expected[index], error);
            check(false, "differential mismatch");
        }
    }
}
void line_battery() {
    std::mt19937 engine(221026);
    std::uniform_real_distribution<double> random(-1.0, 1.0);
    for (std::size_t length = 1; length <= 96; ++length) {
        for (std::size_t pattern = 0; pattern < 7; ++pattern) {
            std::vector<double> source(length * 4, 0.0);
            for (std::size_t i = 0; i < source.size(); ++i) {
                const double x = static_cast<double>(i / 4);
                switch (pattern) {
                case 0: source[i] = random(engine); break;
                case 1: source[i] = 0.4; break;
                case 2: source[i] = x / static_cast<double>(length); break;
                case 3: source[i] = x < static_cast<double>(length / 2) ? -0.75 : 0.75; break;
                case 4: source[i] = std::sin(x * 1.27 + static_cast<double>(i % 4)); break;
                case 5: source[i] = static_cast<double>((i / 4) % 2); break;
                default: source[i] = std::round(random(engine) * 3.0) / 3.0; break;
                }
            }
            stx_conv::Profile profile(length, 4);
            stx_conv::LineWorkspace workspace(length, 4);
            check(stx_conv::compile({source, 4}, profile, workspace) == stx_conv::Status::ok, "new line compile");
            const std::size_t targets[4] = {1, length, length / 2 + 1, length * 2 + 3};
            for (std::size_t target : targets) {
                for (bool area : {false, true}) {
                    const stx_conv::Sampling sampling = area ? stx_conv::Sampling::pixel_area : stx_conv::Sampling::nodal;
                    const stx_conv::LinePlan plan(length, target, sampling);
                    std::vector<double> actual(target * 4, 0.0);
                    std::vector<double> expected(target * 4, 0.0);
                    check(stx_conv::evaluate(profile, plan, {actual, 4}) == stx_conv::Status::ok, "new line evaluate");
                    oracle_line(source, length, target, area, expected);
                    compare(actual, expected);
                }
            }
        }
    }
}
void image_battery() {
    std::mt19937 engine(223344);
    std::uniform_real_distribution<double> random(0.0, 1.0);
    for (int sh : {1, 3, 5, 17, 33}) {
        for (int sw : {1, 4, 6, 19}) {
            const std::size_t count = static_cast<std::size_t>(sw * sh * 4);
            std::vector<double> source(count, 0.0);
            for (double& value : source) { value = random(engine); }
            for (int th : {1, sh, sh + 3}) {
                for (int tw : {1, sw, sw * 2 + 1}) {
                    for (bool area : {false, true}) {
                        const stx_conv::Sampling sampling = area ? stx_conv::Sampling::pixel_area : stx_conv::Sampling::nodal;
                        const stx_conv::ImagePlan plan(static_cast<std::size_t>(sw), static_cast<std::size_t>(sh),
                                                     static_cast<std::size_t>(tw), static_cast<std::size_t>(th), sampling);
                        stx_conv::ImageWorkspace work(plan, 4);
                        std::vector<double> actual(static_cast<std::size_t>(tw * th * 4), 0.0);
                        check(stx_conv::resize(plan, {source, static_cast<std::size_t>(sw * 4)},
                                              {actual, static_cast<std::size_t>(tw * 4)}, work) == stx_conv::Status::ok, "2D resize");
                        std::vector<double> middle;
                        std::vector<double> expected;
                        if (sw == tw && sh == th) { expected = source; }
                        else if (sw == tw) { paint::resize_axis(source, sw, sh, th, true, expected, area); }
                        else if (sh == th) { paint::resize_axis(source, sw, sh, tw, false, expected, area); }
                        else if (th < sh) {
                            paint::resize_axis(source, sw, sh, th, true, middle, area);
                            paint::resize_axis(middle, sw, th, tw, false, expected, area);
                        } else {
                            paint::resize_axis(source, sw, sh, tw, false, middle, area);
                            paint::resize_axis(middle, tw, sh, th, true, expected, area);
                        }
                        compare(actual, expected);
                    }
                }
            }
        }
    }
}
void pixel_fixture_checks() {
    for (const fixtures::Case& fixture : fixtures::cases) {
        const std::size_t sw = static_cast<std::size_t>(fixture.width);
        const std::size_t sh = static_cast<std::size_t>(fixture.height);
        const std::size_t tw = static_cast<std::size_t>(fixture.out_width);
        const std::size_t th = static_cast<std::size_t>(fixture.out_height);
        std::vector<double> source(sw * sh * 4, 0.0);
        std::vector<double> output(tw * th * 4, 0.0);
        for (std::size_t pixel = 0; pixel < sw * sh; ++pixel) {
            const double alpha = fixture.input[pixel * 4 + 3] / 255.0;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                source[pixel * 4 + channel] = fixture.input[pixel * 4 + channel] / 255.0 * alpha;
            }
            source[pixel * 4 + 3] = alpha;
        }
        const stx_conv::ImagePlan plan(sw, sh, tw, th);
        stx_conv::ImageWorkspace work(plan, 4);
        check(stx_conv::resize(plan, {source, sw * 4}, {output, tw * 4}, work) == stx_conv::Status::ok, "pixel fixture resize");
        for (std::size_t pixel = 0; pixel < tw * th; ++pixel) {
            const double alpha = std::clamp(output[pixel * 4 + 3], 0.0, 1.0);
            const double inverse = alpha > 1.0 / 65535.0 ? 1.0 / alpha : 0.0;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const double value = channel == 3 ? alpha : std::clamp(output[pixel * 4 + channel] * inverse, 0.0, 1.0);
                const std::uint8_t byte = static_cast<std::uint8_t>(std::round(255.0 * value));
                check(byte == fixture.expected[pixel * 4 + channel], "pinned RGBA fixture exact bytes");
            }
        }
    }
}
#include "image_benchmark.inc"
void timings() {
    constexpr std::size_t length = 2048;
    constexpr std::size_t target = 3071;
    std::mt19937 engine(667788);
    std::uniform_real_distribution<double> random(-1.0, 1.0);
    for (int pattern = 0; pattern < 4; ++pattern) {
        std::vector<double> source(length * 4, 0.0);
        for (std::size_t i = 0; i < source.size(); ++i) {
            switch (pattern) {
            case 0: source[i] = random(engine); break;
            case 1: source[i] = static_cast<double>(i / 4) / static_cast<double>(length); break;
            case 2: source[i] = static_cast<double>((i / 4) % 2); break;
            default: source[i] = std::sin(static_cast<double>(i / 4) * 0.15); break;
            }
        }
        stx_conv::Profile profile(length, 4);
        stx_conv::LineWorkspace work(length, 4);
        const stx_conv::LinePlan plan(length, target);
        paint::LineWorkspace old_work(static_cast<unsigned>(length));
        paint::resample_plan old_plan;
        paint::LineWorkspace::build_resample_plan(old_plan, static_cast<unsigned>(length), static_cast<unsigned>(target));
        std::vector<double> actual(target * 4, 0.0);
        std::vector<double> expected(target * 4, 0.0);
        std::vector<double> new_times;
        std::vector<double> old_times;
        for (int repeat = 0; repeat < 33; ++repeat) {
            for (int half = 0; half < 2; ++half) {
                const bool candidate = ((repeat + half) % 2) == 0;
                const Clock::time_point begin = Clock::now();
                if (candidate) {
                    check(stx_conv::compile({source, 4}, profile, work) == stx_conv::Status::ok, "benchmark compile");
                    check(stx_conv::evaluate(profile, plan, {actual, 4}) == stx_conv::Status::ok, "benchmark evaluate");
                } else {
                    check(old_work.resample_line(source.data(), static_cast<unsigned>(length), 0, 4, old_plan, expected.data(), 0, 4) != 0, "benchmark oracle");
                }
                const double milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
                if (repeat != 0) {
                    if (candidate) { new_times.push_back(milliseconds); }
                    else { old_times.push_back(milliseconds); }
                }
            }
            compare(actual, expected);
        }
        std::sort(new_times.begin(), new_times.end());
        std::sort(old_times.begin(), old_times.end());
        std::printf("pattern=%d samples=2048 lanes=4 target=3071 repeats=32 new_ms=%.6f old_ms=%.6f time_ratio=%.4f profile_bytes=%zu workspace_bytes=%zu plan_bytes=%zu\n",
                    pattern, new_times[16], old_times[16], new_times[16] / old_times[16],
                    profile.storage_bytes(), work.storage_bytes(), plan.storage_bytes());
    }
}
}
int main(int argc, char**) {
    line_battery();
    image_battery();
    pixel_fixture_checks();
    std::printf("PASS: 5376 line cases, 360 image cases, 5 exact RGBA fixtures; maximum_absolute_error=%.17g\n", maximum_error);
    if (argc > 1) { timings(); image_timings(); }
    return 0;
}
