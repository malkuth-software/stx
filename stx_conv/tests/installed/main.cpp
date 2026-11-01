// Links all three installed components and checks one result from each: the
// profile API on a ramp, a stream resize of a constant image, and an affine
// point map. Returns a distinct nonzero code per failed check.
#include <stx_conv/conv.hpp>
#include <stx_conv/kernels.hpp>
#include <stx_conv/stream.hpp>
#include <array>
#include <cmath>
#include <vector>

template<stx_conv::Precision Scalar>
int check_profile() {
    const std::array<Scalar, 5> source{0, 1, 2, 3, 4};
    std::array<Scalar, 9> output{};
    const stx_conv::BasicLinePlan<Scalar> plan(5, 9);
    stx_conv::BasicProfile<Scalar> profile(5);
    stx_conv::BasicLineWorkspace<Scalar> workspace(5);
    if (stx_conv::compile({source, 1}, profile, workspace) != stx_conv::Status::ok) { return 1; }
    if (stx_conv::evaluate(profile, plan, {output, 1}) != stx_conv::Status::ok) { return 2; }
    for (std::size_t index = 0; index < output.size(); ++index) {
        const Scalar expected = static_cast<Scalar>(index) / Scalar(2);
        if (std::abs(output[index] - expected) > Scalar(1e-5)) { return 3; }
    }
    return 0;
}

template<stx_conv::Precision Scalar>
int check_stream() {
    const std::vector<Scalar> input(7 * 5, Scalar(0.25));
    std::vector<Scalar> output(11 * 3, Scalar(0));
    stx_conv::stream::Workspace<Scalar> workspace(7, 5, 11, 3, 1, stx_conv::Sampling::pixel_area);
    if (stx_conv::stream::resize<Scalar>(workspace, {input, 7}, {output, 11}) != stx_conv::Status::ok) { return 4; }
    for (const Scalar value : output) {
        if (std::abs(value - Scalar(0.25)) > Scalar(1e-5)) { return 5; }
    }
    return 0;
}

template<stx_conv::Precision Scalar>
int check_kernels() {
    const std::array<Scalar, 9> translate{1, 0, Scalar(0.5), 0, 1, Scalar(-1), 0, 0, 1};
    const std::array<Scalar, 3> x{0, 1, 2};
    const std::array<Scalar, 3> y{3, 4, 5};
    std::array<Scalar, 3> source_x{};
    std::array<Scalar, 3> source_y{};
    if (stx_conv::kernels::map_points<Scalar>(translate, x, y, source_x, source_y) != stx_conv::Status::ok) { return 6; }
    for (std::size_t index = 0; index < x.size(); ++index) {
        const bool mapped = source_x[index] == x[index] + Scalar(0.5) && source_y[index] == y[index] - Scalar(1);
        if (!mapped) { return 7; }
    }
    return 0;
}

template<stx_conv::Precision Scalar>
int check() {
    const int profile = check_profile<Scalar>();
    if (profile != 0) { return profile; }
    const int stream = check_stream<Scalar>();
    if (stream != 0) { return stream; }
    const int kernels = check_kernels<Scalar>();
    return kernels;
}

int main() {
    const int result32 = check<float>();
    if (result32 != 0) { return result32; }
    const int result64 = check<double>();
    return result64;
}
