#include "regression_cases.hpp"
#include <array>

namespace stx_conv_tests {
namespace {

struct Shape {
    std::size_t source_width = 0;
    std::size_t source_height = 0;
    std::size_t target_width = 0;
    std::size_t target_height = 0;
};

constexpr std::array<Shape, 22> shapes{{
    {1, 1, 1, 1}, {5, 5, 5, 5}, {65, 5, 97, 9}, {257, 5, 129, 7}, {9, 7, 17, 13},
    {9, 7, 5, 4}, {12, 5, 23, 9}, {12, 6, 7, 3}, {6, 6, 11, 2}, {33, 31, 65, 61},
    {33, 31, 16, 15}, {40, 24, 13, 7}, {17, 40, 33, 79}, {3, 20, 7, 41}, {20, 20, 3, 11},
    {5, 9, 1, 1}, {7, 30, 9, 6}, {2, 12, 4, 25}, {4, 4, 9, 9}, {1, 8, 3, 15},
    {16, 16, 16, 31}, {16, 16, 31, 16},
}};
constexpr std::array<std::size_t, 3> channel_counts{1, 3, 4};
constexpr std::array<stx_conv::Sampling, 2> samplings{stx_conv::Sampling::nodal, stx_conv::Sampling::pixel_area};
constexpr std::array<Pattern, 8> patterns{
    Pattern::noise, Pattern::gradient, Pattern::checker, Pattern::plateaus,
    Pattern::ties, Pattern::constant, Pattern::wide, Pattern::smooth,
};
constexpr std::array<std::size_t, 2> precisions{32, 64};
constexpr std::array<double, 7> powers_of_ten{1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0};

// Steele, Lea and Flood's SplitMix64, as in cases.py.
class SplitMix64 final {
public:
    explicit SplitMix64(std::uint64_t seed) noexcept : state(seed) {}

    std::uint64_t next() noexcept {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        const std::uint64_t result = z ^ (z >> 31);
        return result;
    }

    // Uniform in [0, 1): the top 53 bits scaled by 2^-53, exactly.
    double unit() noexcept {
        const std::uint64_t bits = next() >> 11;
        const double result = static_cast<double>(bits) * (1.0 / 9007199254740992.0);
        return result;
    }

private:
    std::uint64_t state = 0;
};

struct PixelPosition {
    std::size_t x = 0;
    std::size_t y = 0;
    std::size_t channel = 0;
};

double pixel(const RegressionCase& regression_case, PixelPosition position, SplitMix64& random) {
    const std::size_t width = regression_case.source_width;
    const std::size_t height = regression_case.source_height;
    const std::size_t channels = regression_case.channels;
    switch (regression_case.pattern) {
    case Pattern::noise: {
        const double result = random.unit();
        return result;
    }
    case Pattern::gradient: {
        const double numerator = static_cast<double>(position.x + 2 * position.y + position.channel);
        const double denominator = static_cast<double>(width + 2 * height + channels);
        const double result = numerator / denominator;
        return result;
    }
    case Pattern::checker: {
        const double result = static_cast<double>((position.x + position.y + position.channel) % 2);
        return result;
    }
    case Pattern::plateaus: {
        // Draw first, so every pixel consumes one word as in cases.py.
        const double drawn = random.unit();
        const bool plateau = position.y < height / 2 + position.x % 3;
        const double result = plateau ? 0.3 : drawn;
        return result;
    }
    case Pattern::ties: {
        const double level = static_cast<double>(random.next() % 4);
        const double result = level / 3.0;
        return result;
    }
    case Pattern::constant:
        return 0.5;
    case Pattern::wide: {
        const double mantissa = 2.0 * random.unit() - 1.0;
        const std::size_t exponent_code = static_cast<std::size_t>(random.next() % 13);
        if (exponent_code >= 6) {
            const double result = mantissa * powers_of_ten[exponent_code - 6];
            return result;
        }
        const double result = mantissa / powers_of_ten[6 - exponent_code];
        return result;
    }
    case Pattern::smooth: {
        // A product of two parabolic waves.
        const double tx = static_cast<double>((7 * position.x + 3 * position.channel) % 20) / 10.0 - 1.0;
        const double ty = static_cast<double>((5 * position.y) % 16) / 8.0 - 1.0;
        const double a = 1.0 - tx * tx;
        const double b = 1.0 - ty * ty;
        const double result = 0.5 + 0.4 * (a * b);
        return result;
    }
    }
    // Unreachable for a valid Pattern: a value no pattern produces.
    return -1.0;
}

const char* pattern_name(Pattern pattern) noexcept {
    switch (pattern) {
    case Pattern::noise: return "noise";
    case Pattern::gradient: return "gradient";
    case Pattern::checker: return "checker";
    case Pattern::plateaus: return "plateaus";
    case Pattern::ties: return "ties";
    case Pattern::constant: return "constant";
    case Pattern::wide: return "wide";
    case Pattern::smooth: return "smooth";
    }
    return "unknown";
}

} // namespace

std::vector<RegressionCase> regression_cases() {
    std::vector<RegressionCase> result;
    result.reserve(shapes.size() * channel_counts.size() * samplings.size() * patterns.size() * precisions.size());
    std::size_t index = 0;
    for (const Shape& shape : shapes) {
        for (const std::size_t channels : channel_counts) {
            for (const stx_conv::Sampling sampling : samplings) {
                for (const Pattern pattern : patterns) {
                    for (const std::size_t precision_bits : precisions) {
                        RegressionCase regression_case{};
                        regression_case.index = index;
                        regression_case.source_width = shape.source_width;
                        regression_case.source_height = shape.source_height;
                        regression_case.target_width = shape.target_width;
                        regression_case.target_height = shape.target_height;
                        regression_case.channels = channels;
                        regression_case.sampling = sampling;
                        regression_case.pattern = pattern;
                        regression_case.precision_bits = precision_bits;
                        regression_case.seed = static_cast<std::uint64_t>(index) + 1;
                        result.push_back(regression_case);
                        ++index;
                    }
                }
            }
        }
    }
    return result;
}

void fill_case_image(const RegressionCase& regression_case, std::vector<double>& image) {
    const std::size_t width = regression_case.source_width;
    const std::size_t height = regression_case.source_height;
    const std::size_t channels = regression_case.channels;
    image.assign(width * height * channels, 0.0);
    SplitMix64 random(regression_case.seed);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        for (std::size_t y = 0; y < height; ++y) {
            for (std::size_t x = 0; x < width; ++x) {
                const PixelPosition position{x, y, channel};
                image[(y * width + x) * channels + channel] = pixel(regression_case, position, random);
            }
        }
    }
}

std::string describe(const RegressionCase& regression_case) {
    std::string result = regression_case.precision_bits == 32 ? "f32 " : "f64 ";
    result += regression_case.sampling == stx_conv::Sampling::nodal ? "nodal " : "pixel_area ";
    result += std::to_string(regression_case.source_width) + "x" + std::to_string(regression_case.source_height);
    result += "->" + std::to_string(regression_case.target_width) + "x" + std::to_string(regression_case.target_height);
    result += " c" + std::to_string(regression_case.channels) + " " + pattern_name(regression_case.pattern);
    return result;
}

} // namespace stx_conv_tests
