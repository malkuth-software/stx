// Numerical check of stx_conv::stream (bubble correction enabled) against the
// NumPy bubble model, in binary32 and binary64.
//
// usage: stx_conv_stream_bubble_oracle FIXTURE
//
// tests/stream/oracle/run_bubble_oracle.py writes FIXTURE and runs this
// program. The model indexes whole lines while the stream works in blocks,
// and the two evaluate the bubble terms in different orders, so the claim is
// numerical: every sample within 256 * epsilon * max(1, largest source
// magnitude) of the model. The base factor is checked bit for bit separately
// (tests/stream/regression.cpp).
#include "regression_cases.hpp"
#include <stx_conv/stream.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

static_assert(std::endian::native == std::endian::little, "the fixture is little-endian");

struct FixtureHeader {
    std::int64_t source_width = 0;
    std::int64_t source_height = 0;
    std::int64_t target_width = 0;
    std::int64_t target_height = 0;
    std::int64_t channels = 0;
    std::int64_t sampling = 0;
    std::int64_t precision_bits = 0;
};

// Owns the open fixture; closes it on every exit.
class FixtureFile final {
public:
    explicit FixtureFile(const char* path) noexcept : file(std::fopen(path, "rb")) {}
    FixtureFile(const FixtureFile&) = delete;
    FixtureFile& operator=(const FixtureFile&) = delete;
    ~FixtureFile() {
        if (file != nullptr) { std::fclose(file); }
    }
    [[nodiscard]] bool is_open() const noexcept { return file != nullptr; }
    [[nodiscard]] bool read(void* destination, std::size_t bytes) noexcept {
        const bool complete = std::fread(destination, 1, bytes, file) == bytes;
        return complete;
    }

private:
    std::FILE* file = nullptr;
};

bool header_matches(const FixtureHeader& header, const stx_conv_tests::RegressionCase& regression_case) {
    const std::int64_t sampling = regression_case.sampling == stx_conv::Sampling::nodal ? 0 : 1;
    const bool result = header.source_width == static_cast<std::int64_t>(regression_case.source_width)
        && header.source_height == static_cast<std::int64_t>(regression_case.source_height)
        && header.target_width == static_cast<std::int64_t>(regression_case.target_width)
        && header.target_height == static_cast<std::int64_t>(regression_case.target_height)
        && header.channels == static_cast<std::int64_t>(regression_case.channels)
        && header.sampling == sampling
        && header.precision_bits == static_cast<std::int64_t>(regression_case.precision_bits);
    return result;
}

struct CaseResult {
    bool read = false;
    bool within_tolerance = false;
    double largest_scaled_error = 0.0;
};

template<stx_conv::Precision Scalar>
CaseResult check_case(FixtureFile& fixture, const stx_conv_tests::RegressionCase& regression_case,
                      const std::vector<double>& image) {
    const std::size_t channels = regression_case.channels;
    const std::size_t source_row = regression_case.source_width * channels;
    const std::size_t target_row = regression_case.target_width * channels;
    std::vector<Scalar> expected(target_row * regression_case.target_height);
    CaseResult result{};
    result.read = fixture.read(expected.data(), expected.size() * sizeof(Scalar));
    if (!result.read) { return result; }
    std::vector<Scalar> source(image.size());
    double scale = 1.0;
    for (std::size_t index = 0; index < image.size(); ++index) {
        source[index] = static_cast<Scalar>(image[index]);
        scale = std::max(scale, std::abs(static_cast<double>(source[index])));
    }
    stx_conv::stream::Workspace<Scalar> workspace(
        regression_case.source_width, regression_case.source_height, regression_case.target_width,
        regression_case.target_height, channels, regression_case.sampling);
    std::vector<Scalar> actual(expected.size(), Scalar(0));
    const stx_conv::Status status = stx_conv::stream::resize<Scalar>(workspace, {source, source_row}, {actual, target_row});
    const double epsilon = static_cast<double>(std::numeric_limits<Scalar>::epsilon());
    const double tolerance = 256.0 * epsilon * scale;
    bool within = status == stx_conv::Status::ok;
    for (std::size_t index = 0; index < actual.size(); ++index) {
        const double error = std::abs(static_cast<double>(actual[index]) - static_cast<double>(expected[index]));
        if (!std::isfinite(actual[index]) || error > tolerance) { within = false; }
        result.largest_scaled_error = std::max(result.largest_scaled_error, error / (epsilon * scale));
    }
    result.within_tolerance = within;
    return result;
}

} // namespace

int main(int argument_count, char** arguments) {
    if (argument_count != 2) {
        std::fprintf(stderr, "usage: stx_conv_stream_bubble_oracle FIXTURE\n");
        return 2;
    }
    FixtureFile fixture(arguments[1]);
    if (!fixture.is_open()) {
        std::fprintf(stderr, "cannot read %s\n", arguments[1]);
        return 2;
    }
    const std::vector<stx_conv_tests::RegressionCase> cases = stx_conv_tests::regression_cases();
    std::int64_t count = 0;
    if (!fixture.read(&count, sizeof(count)) || count != static_cast<std::int64_t>(cases.size())) {
        std::fprintf(stderr, "fixture case count does not match the regression cases\n");
        return 2;
    }
    std::vector<double> image;
    std::size_t mismatches = 0;
    double largest_f32 = 0.0;
    double largest_f64 = 0.0;
    for (const stx_conv_tests::RegressionCase& regression_case : cases) {
        FixtureHeader header{};
        if (!fixture.read(&header, sizeof(header)) || !header_matches(header, regression_case)) {
            std::fprintf(stderr, "fixture header %zu does not match its case\n", regression_case.index);
            return 2;
        }
        stx_conv_tests::fill_case_image(regression_case, image);
        const bool single = regression_case.precision_bits == 32;
        const CaseResult result = single ? check_case<float>(fixture, regression_case, image)
                                         : check_case<double>(fixture, regression_case, image);
        if (!result.read) {
            std::fprintf(stderr, "fixture truncated at case %zu\n", regression_case.index);
            return 2;
        }
        if (single) { largest_f32 = std::max(largest_f32, result.largest_scaled_error); }
        else { largest_f64 = std::max(largest_f64, result.largest_scaled_error); }
        if (!result.within_tolerance) {
            ++mismatches;
            const std::string label = stx_conv_tests::describe(regression_case);
            std::printf("MISMATCH case %zu %s: largest error %.3g epsilon-scaled\n", regression_case.index,
                        label.c_str(), result.largest_scaled_error);
        }
    }
    std::printf("cases=%zu mismatches=%zu largest_error_in_scaled_epsilon f32=%.3g f64=%.3g (tolerance 256)\n",
                cases.size(), mismatches, largest_f32, largest_f64);
    const int result = mismatches == 0 ? 0 : 1;
    return result;
}
