#include <stx_conv/conv.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void require_ok(stx_conv::Status status) { require(status == stx_conv::Status::ok, "operation failed"); }
void line_checks() {
    for (std::size_t length = 1; length <= 65; ++length) {
        const std::size_t lanes = 3;
        std::vector<double> input(length * lanes, 0.0);
        for (std::size_t index = 0; index < length; ++index) {
            input[index * lanes] = 0.25;
            input[index * lanes + 1] = static_cast<double>(index);
            input[index * lanes + 2] = index < length / 2 ? 0.0 : 1.0;
        }
        stx_conv::Profile profile(length, lanes);
        stx_conv::LineWorkspace work(length, lanes);
        require_ok(stx_conv::compile({input, lanes}, profile, work));
        const stx_conv::LinePlan identity(length, length);
        std::vector<double> copied(input.size(), -5.0);
        require_ok(stx_conv::evaluate(profile, identity, {copied, lanes}));
        require(copied == input, "node cardinality");
        const stx_conv::LinePlan up(length, length * 3 + 1);
        std::vector<double> enlarged((length * 3 + 1) * lanes, -5.0);
        require_ok(stx_conv::evaluate(profile, up, {enlarged, lanes}));
        for (std::size_t index = 0; index < length * 3 + 1; ++index) {
            require(std::abs(enlarged[index * lanes] - 0.25) < 1e-12, "constant preservation");
            const double expected = static_cast<double>(index) * static_cast<double>(length - 1) / static_cast<double>(length * 3);
            require(std::abs(enlarged[index * lanes + 1] - expected) < 1e-11, "affine reproduction");
        }
        const stx_conv::LinePlan area(length, 1, stx_conv::Sampling::pixel_area);
        std::vector<double> mean(lanes, 0.0);
        require_ok(stx_conv::evaluate(profile, area, {mean, lanes}));
        require(std::abs(mean[0] - 0.25) < 1e-12, "area constant");
        require(std::abs(mean[1] - 0.5 * static_cast<double>(length - 1)) < 1e-11, "area ramp mean");
    }
}
void validation_checks() {
    std::vector<double> source(8, 1.0);
    std::vector<double> output(15, -7.0);
    stx_conv::Profile profile(8);
    stx_conv::LineWorkspace work(8);
    const stx_conv::LinePlan plan(8, 15);
    require(stx_conv::evaluate(profile, plan, {output, 1}) == stx_conv::Status::unprepared_profile, "unprepared rejection");
    require_ok(stx_conv::compile({source, 1}, profile, work));
    source[3] = std::numeric_limits<double>::quiet_NaN();
    require(stx_conv::compile({source, 1}, profile, work) == stx_conv::Status::nonfinite_input, "NaN rejection");
    require_ok(stx_conv::evaluate(profile, plan, {output, 1}));
    require(output[4] == 1.0, "invalid compile preserves snapshot");
    std::fill(output.begin(), output.end(), -7.0);
    const std::span<double> short_output(output.data(), 3);
    require(stx_conv::evaluate(profile, plan, {short_output, 1}) == stx_conv::Status::insufficient_storage, "capacity rejection");
    require(output[0] == -7.0, "invalid evaluate preserves destination");
    stx_conv::Profile moved(std::move(profile));
    require(stx_conv::evaluate(profile, plan, {output, 1}) == stx_conv::Status::unprepared_profile, "moved profile rejection");
    require_ok(stx_conv::evaluate(moved, plan, {output, 1}));
    bool threw = false;
    try { const stx_conv::LinePlan invalid(0, 1); }
    catch (const std::invalid_argument&) { threw = true; }
    require(threw, "zero dimension rejection");
    const double positions[3] = {0.0, 3.25, 7.0};
    const stx_conv::LinePlan points = stx_conv::LinePlan::points(8, positions);
    require_ok(stx_conv::evaluate(moved, points, {output, 1}));
    require(output[1] == 1.0, "explicit position sampling");
}
void scale_and_stride_checks() {
    constexpr std::size_t length = 31;
    constexpr std::size_t target = 59;
    const stx_conv::LinePlan plan(length, target);
    std::vector<double> base(length, 0.0);
    for (std::size_t i = 0; i < length; ++i) {
        base[i] = std::sin(static_cast<double>(i) * 0.77) + 0.21 * std::cos(static_cast<double>(i) * 1.67);
    }
    stx_conv::Profile profile(length);
    stx_conv::LineWorkspace workspace(length);
    std::vector<double> expected(target, 0.0);
    require_ok(stx_conv::compile({base, 1}, profile, workspace));
    require_ok(stx_conv::evaluate(profile, plan, {expected, 1}));
    for (std::size_t lanes : {1U, 2U, 3U, 7U}) {
        const std::size_t stride = lanes + 2;
        stx_conv::Profile batch(length, lanes);
        stx_conv::LineWorkspace batch_work(length, lanes);
        for (double scale : {1e-80, 1.0, 1e80}) {
            std::vector<double> input(length * stride, std::numeric_limits<double>::quiet_NaN());
            std::vector<double> output(target * stride, -17.0);
            for (std::size_t node = 0; node < length; ++node) {
                for (std::size_t lane = 0; lane < lanes; ++lane) { input[node * stride + lane] = scale * base[node]; }
            }
            require_ok(stx_conv::compile({input, stride}, batch, batch_work));
            require_ok(stx_conv::evaluate(batch, plan, {output, stride}));
            for (std::size_t node = 0; node < target; ++node) {
                for (std::size_t lane = 0; lane < lanes; ++lane) {
                    require(std::abs(output[node * stride + lane] / scale - expected[node]) < 1e-12, "scale/lane equivalence");
                }
                require(output[node * stride + lanes] == -17.0, "strided padding preserved");
            }
        }
    }
}
void image_checks() {
    const stx_conv::ImagePlan plan(9, 7, 14, 4);
    stx_conv::ImageWorkspace work(plan, 3);
    std::vector<double> source(7 * 30, -123.0);
    std::vector<double> output(4 * 45, -456.0);
    for (std::size_t y = 0; y < 7; ++y) {
        for (std::size_t x = 0; x < 27; ++x) { source[y * 30 + x] = 0.5; }
    }
    require_ok(stx_conv::resize(plan, {source, 30}, {output, 45}, work));
    for (std::size_t y = 0; y < 4; ++y) {
        for (std::size_t x = 0; x < 42; ++x) { require(std::abs(output[y * 45 + x] - 0.5) < 1e-12, "2D constant"); }
        require(output[y * 45 + 42] == -456.0, "output padding unchanged");
    }
    const stx_conv::ImagePlan identity(9, 7, 9, 7);
    stx_conv::ImageWorkspace identity_work(identity, 3);
    require(stx_conv::resize(identity, {source, 30}, {source, 30}, identity_work) == stx_conv::Status::overlap, "image overlap rejection");
    source[0] = std::numeric_limits<double>::infinity();
    std::fill(output.begin(), output.end(), -456.0);
    require(stx_conv::resize(plan, {source, 30}, {output, 45}, work) == stx_conv::Status::nonfinite_input, "image infinity rejection");
    require(output[0] == -456.0, "validation before output mutation");
}
}
int main() {
    line_checks();
    validation_checks();
    image_checks();
    scale_and_stride_checks();
    std::puts("PASS: cardinality, affine/constant, basin, shapes, strides, ownership, failure contracts");
    return 0;
}
