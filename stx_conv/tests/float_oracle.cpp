// This generated test fixture retains Plan Paint's independent active-face
// enumeration and direct boundary costs, converted explicitly to binary32.
#include "float_oracle.hpp"
#include <stx_conv/conv.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
float reference_value(const std::vector<float>& source, const float_reference::LineWorkspace& work,
                      std::size_t cell, std::size_t lane, float u) {
    const float v = 1.0F - u;
    const float u2 = u * u;
    const float u3 = u2 * u;
    const float v2 = v * v;
    const float v3 = v2 * v;
    const std::array<float, 5> basis{5.0F * v3 * v * u, 10.0F * v3 * u2,
        10.0F * v2 * u3, 5.0F * v * u3 * u, u3 * u2};
    std::array<float, 5> tails{};
    float suffix = 0.0F;
    for (std::size_t i = 5; i != 0; --i) { suffix += basis[i - 1]; tails[i - 1] = suffix; }
    float result = source[cell * 4 + lane];
    for (std::size_t i = 0; i < 5; ++i) { result += tails[i] * work.admitted_current[(cell * 5 + i) * 4 + lane]; }
    return result;
}
}
int main() {
    float maximum_error = 0.0F;
    for (unsigned length = 5; length <= 96; ++length) {
        std::vector<float> positions((length - 1) * 3, 0.0F);
        for (std::size_t i = 0; i < positions.size(); ++i) {
            positions[i] = static_cast<float>(i / 3) + static_cast<float>(i % 3 + 1) * 0.25F;
        }
        const stx_conv::LinePlan32 plan = stx_conv::LinePlan32::points(length, positions);
        stx_conv::Profile32 profile(length, 4);
        stx_conv::LineWorkspace32 work(length, 4);
        float_reference::LineWorkspace reference(length);
        std::vector<float> source(length * 4, 0.0F);
        std::vector<float> output(positions.size() * 4, 0.0F);
        for (std::size_t pattern = 0; pattern < 7; ++pattern) {
            for (std::size_t i = 0; i < source.size(); ++i) {
                const std::uint32_t mixed = static_cast<std::uint32_t>(i) * 2654435761U + 2246822519U;
                switch (pattern) {
                    case 0: source[i] = 0.25F; break;
                    case 1: source[i] = static_cast<float>(i / 4) / 97.0F; break;
                    case 2: source[i] = i % 19 < 9 ? -0.5F : 0.75F; break;
                    case 3: source[i] = static_cast<float>((mixed ^ (mixed >> 13)) & 65535U) / 32768.0F - 1.0F; break;
                    case 4: source[i] = std::sin(static_cast<float>(i) * 0.17F); break;
                    case 5: source[i] = i % 2 == 0 ? -1.0F : 1.0F; break;
                    // Near-flat values produced by the first image pass.
                    default: source[i] = i % 19 < 9 ? -0.0833333358F : -0.0833333433F; break;
                }
            }
            require(reference.build_line_profile(source.data(), length, 0, 4) != 0, "float reference admission");
            require(stx_conv::compile({source, 4}, profile, work) == stx_conv::Status::ok, "float compile");
            require(stx_conv::evaluate(profile, plan, {output, 4}) == stx_conv::Status::ok, "float evaluate");
            for (std::size_t i = 0; i < positions.size(); ++i) {
                for (std::size_t lane = 0; lane < 4; ++lane) {
                    const float expected = reference_value(source, reference, i / 3, lane, static_cast<float>(i % 3 + 1) * 0.25F);
                    const float error = std::abs(output[i * 4 + lane] - expected);
                    maximum_error = std::max(maximum_error, error);
                    if (error > 2e-5F) {
                        std::fprintf(stderr, "length=%u pattern=%zu point=%zu lane=%zu actual=%.9g ref=%.9g\n", length, pattern, i, lane,
                            static_cast<double>(output[i * 4 + lane]), static_cast<double>(expected));
                        require(false, "independent float profile agreement");
                    }
                }
            }
        }
    }
    std::printf("PASS: independent binary32 profile oracle; max error %.9g\n", static_cast<double>(maximum_error));
    return 0;
}
