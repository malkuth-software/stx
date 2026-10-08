#include "packet.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
struct Channels final {
    double left{};
    double right{};
};

// Independent four-quadrant definition, including the nonpositive zero cases.
Channels reference(const double magnitude, const double angle) noexcept {
    if (magnitude > 0.0 && angle > 0.0) {
        const Channels result{.left = magnitude, .right = magnitude - angle};
        return result;
    }
    if (magnitude > 0.0) {
        const Channels result{.left = magnitude + angle, .right = magnitude};
        return result;
    }
    if (angle > 0.0) {
        const Channels result{.left = magnitude, .right = magnitude + angle};
        return result;
    }
    const Channels result{.left = magnitude - angle, .right = magnitude};
    return result;
}

void check(const bool condition, const char* const message) {
    if (condition) return;
    std::fprintf(stderr, "%s\n", message);
    std::abort();
}

void check_value(const double actual, const double expected) {
    if (std::isnan(expected)) {
        check(std::isnan(actual), "coupling preserves NaN classification");
        return;
    }
    const std::uint64_t actual_bits = std::bit_cast<std::uint64_t>(actual);
    const std::uint64_t expected_bits = std::bit_cast<std::uint64_t>(expected);
    check(actual_bits == expected_bits, "coupling exact value, infinity sign and signed zero");
}

void test_coupling() {
    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double tiny = std::numeric_limits<double>::denorm_min();
    const double large = std::numeric_limits<double>::max();
    const std::array<double, 14> values{
        0.0, -0.0, 1.0, -1.0, 0.125, -32.5, tiny, -tiny,
        large, -large, infinity, -infinity, nan, -nan};
    const std::array<std::size_t, 10> lengths{0, 1, 2, 3, 7, 16, 195, 196, 197, 4096};
    // Maximum Vorbis spectrum plus two sentinels. Reused for every case.
    std::array<double, 4098> magnitudes{};
    std::array<double, 4098> angles{};
    constexpr double sentinel = 37.25;
    for (const std::size_t offset : {std::size_t{0}, std::size_t{1}}) {
        for (const std::size_t length : lengths) {
            magnitudes.fill(sentinel);
            angles.fill(sentinel);
            for (std::size_t index = 0; index < length; ++index) {
                magnitudes[offset + index] = values[(index / values.size()) % values.size()];
                angles[offset + index] = values[index % values.size()];
            }
            const std::span<double> magnitude_span(magnitudes.data() + offset, length);
            const std::span<double> angle_span(angles.data() + offset, length);
            stx_vorbis::detail::inverse_couple(magnitude_span, angle_span);
            for (std::size_t index = 0; index < length; ++index) {
                const double magnitude = values[(index / values.size()) % values.size()];
                const double angle = values[index % values.size()];
                const Channels expected = reference(magnitude, angle);
                check_value(magnitudes[offset + index], expected.left);
                check_value(angles[offset + index], expected.right);
            }
            for (std::size_t index = 0; index < offset; ++index) {
                check_value(magnitudes[index], sentinel);
                check_value(angles[index], sentinel);
            }
            for (std::size_t index = offset + length; index < magnitudes.size(); ++index) {
                check_value(magnitudes[index], sentinel);
                check_value(angles[index], sentinel);
            }
        }
    }
}
} // namespace

int main() {
    test_coupling();
    return 0;
}
