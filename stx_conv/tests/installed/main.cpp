#include <stx_conv/conv.hpp>
#include <array>
#include <cmath>
template<stx_conv::Precision Scalar>
int check() {
    const std::array<Scalar, 5> source{0, 1, 2, 3, 4};
    std::array<Scalar, 9> output{};
    const stx_conv::BasicLinePlan<Scalar> plan(5, 9);
    stx_conv::BasicProfile<Scalar> profile(5);
    stx_conv::BasicLineWorkspace<Scalar> workspace(5);
    if (stx_conv::compile({source, 1}, profile, workspace) != stx_conv::Status::ok) { return 1; }
    if (stx_conv::evaluate(profile, plan, {output, 1}) != stx_conv::Status::ok) { return 2; }
    for (std::size_t index = 0; index < output.size(); ++index) {
        if (std::abs(output[index] - static_cast<Scalar>(index) / Scalar(2)) > Scalar(1e-5)) { return 3; }
    }
    return 0;
}

int main() {
    const int result32 = check<float>();
    if (result32 != 0) { return result32; }
    const int result64 = check<double>();
    return result64;
}
