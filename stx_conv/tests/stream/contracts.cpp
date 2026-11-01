// Contracts of stx_conv::stream::resize: padded and packed rows give the same
// samples, row padding is neither read nor written, a workspace is reusable,
// and each refusal (nonfinite or out-of-range input, short storage or stride,
// overlap) leaves the output unchanged. Zero extents refuse construction.
#include <stx_conv/stream.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

struct Shape {
    std::size_t source_width = 0;
    std::size_t source_height = 0;
    std::size_t target_width = 0;
    std::size_t target_height = 0;
};

constexpr std::array<Shape, 7> shapes{{
    {9, 7, 17, 13}, {9, 7, 5, 4}, {1, 8, 3, 15}, {16, 16, 16, 31}, {16, 16, 31, 16}, {9, 7, 9, 7}, {257, 5, 129, 9},
}};
constexpr std::array<std::size_t, 4> channel_counts{1, 3, 5, 7};
constexpr std::array<stx_conv::Sampling, 2> samplings{stx_conv::Sampling::nodal, stx_conv::Sampling::pixel_area};

template<stx_conv::Precision Scalar>
void check_shape(const Shape& shape, std::size_t channels, stx_conv::Sampling sampling) {
    const Scalar untouched = Scalar(-777);
    const std::size_t source_row = shape.source_width * channels;
    const std::size_t target_row = shape.target_width * channels;
    // Padded strides; the source padding is NaN, so reading it would fail.
    const std::size_t source_stride = source_row + 3;
    const std::size_t target_stride = target_row + 5;
    std::vector<Scalar> padded(source_stride * shape.source_height, std::numeric_limits<Scalar>::quiet_NaN());
    std::vector<Scalar> packed(source_row * shape.source_height);
    for (std::size_t y = 0; y < shape.source_height; ++y) {
        for (std::size_t x = 0; x < source_row; ++x) {
            const double phase = static_cast<double>(y * source_row + x) * 0.43;
            const Scalar value = static_cast<Scalar>(std::sin(phase));
            padded[y * source_stride + x] = value;
            packed[y * source_row + x] = value;
        }
    }
    std::vector<Scalar> output(target_stride * shape.target_height, untouched);
    std::vector<Scalar> expected(target_row * shape.target_height);
    stx_conv::stream::Workspace<Scalar> workspace(
        shape.source_width, shape.source_height, shape.target_width, shape.target_height, channels, sampling);
    const stx_conv::Status packed_status = stx_conv::stream::resize<Scalar>(workspace, {packed, source_row}, {expected, target_row});
    require(packed_status == stx_conv::Status::ok, "packed resize failed");
    const stx_conv::Status padded_status = stx_conv::stream::resize<Scalar>(workspace, {padded, source_stride}, {output, target_stride});
    require(padded_status == stx_conv::Status::ok, "padded resize failed");
    for (std::size_t y = 0; y < shape.target_height; ++y) {
        for (std::size_t x = 0; x < target_row; ++x) {
            require(output[y * target_stride + x] == expected[y * target_row + x], "padded rows differ from packed rows");
        }
        for (std::size_t x = target_row; x < target_stride; ++x) {
            require(output[y * target_stride + x] == untouched, "output padding written");
        }
    }
    std::fill(output.begin(), output.end(), untouched);
    padded[0] = std::numeric_limits<Scalar>::infinity();
    const stx_conv::Status nonfinite = stx_conv::stream::resize<Scalar>(workspace, {padded, source_stride}, {output, target_stride});
    require(nonfinite == stx_conv::Status::nonfinite_input, "nonfinite input accepted");
    padded[0] = sizeof(Scalar) == 4 ? Scalar(1e11) : Scalar(1e101);
    const stx_conv::Status out_of_range = stx_conv::stream::resize<Scalar>(workspace, {padded, source_stride}, {output, target_stride});
    require(out_of_range == stx_conv::Status::input_out_of_range, "out-of-range input accepted");
    padded[0] = packed[0];
    const std::span<Scalar> one_sample = std::span<Scalar>(output).first(1);
    const stx_conv::Status short_output = stx_conv::stream::resize<Scalar>(workspace, {padded, source_stride}, {one_sample, target_stride});
    require(short_output == stx_conv::Status::insufficient_storage, "short output accepted");
    const stx_conv::Status short_stride = stx_conv::stream::resize<Scalar>(workspace, {padded, source_row - 1}, {output, target_stride});
    require(short_stride == stx_conv::Status::insufficient_storage, "short source stride accepted");
    for (const Scalar value : output) { require(value == untouched, "a refused resize changed the output"); }
    std::vector<Scalar> shared(std::max(source_row * shape.source_height, target_row * shape.target_height));
    const stx_conv::Status overlap = stx_conv::stream::resize<Scalar>(workspace, {shared, source_row}, {shared, target_row});
    require(overlap == stx_conv::Status::overlap, "overlapping input and output accepted");
}

template<stx_conv::Precision Scalar>
void check() {
    for (const std::size_t channels : channel_counts) {
        for (const Shape& shape : shapes) {
            for (const stx_conv::Sampling sampling : samplings) { check_shape<Scalar>(shape, channels, sampling); }
        }
    }
    bool rejected = false;
    try {
        const stx_conv::stream::Workspace<Scalar> empty(0, 5, 7, 9, 3, stx_conv::Sampling::nodal);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "a zero extent was accepted");
    std::printf("stream contracts f%zu passed\n", 8 * sizeof(Scalar));
}

} // namespace

int main() {
    try {
        check<float>();
        check<double>();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
