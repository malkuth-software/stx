#pragma once
// The stream regression cases: 2,112 images over 22 shapes, 1/3/4 channels,
// both sampling rules, eight patterns and both precisions. The inputs match
// tests/stream/oracle/cases.py bit for bit; see that file for the generator's
// arithmetic rules and the case order.
#include <stx_conv/conv.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stx_conv_tests {

enum class Pattern { noise, gradient, checker, plateaus, ties, constant, wide, smooth };

struct RegressionCase {
    std::size_t index = 0;
    std::size_t source_width = 0;
    std::size_t source_height = 0;
    std::size_t target_width = 0;
    std::size_t target_height = 0;
    std::size_t channels = 0;
    stx_conv::Sampling sampling = stx_conv::Sampling::nodal;
    Pattern pattern = Pattern::noise;
    // 32 or 64.
    std::size_t precision_bits = 64;
    std::uint64_t seed = 0;
};

// Every case in the order of cases.py. Returns a fresh list (2,112 records).
[[nodiscard]] std::vector<RegressionCase> regression_cases();

// Fills `image` with the case's binary64 values, [row][column][channel],
// packed: the storage is reset to source_width * source_height * channels.
void fill_case_image(const RegressionCase& regression_case, std::vector<double>& image);

// "f32 nodal 9x7->17x13 c3 noise": a label for failure messages.
[[nodiscard]] std::string describe(const RegressionCase& regression_case);

} // namespace stx_conv_tests
