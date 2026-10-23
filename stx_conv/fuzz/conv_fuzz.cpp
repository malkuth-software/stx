// libFuzzer entry for stx_conv's public operations (hardening, 2026-10-22).
//
// The input chooses the precision, the operation (line plan, point plan or
// image), the sampling rule, the dimensions, the strides and the samples'
// raw bits (so NaN, infinities, subnormals and values past the limit all
// occur). The harness checks the contract, not particular values:
//   - a request is refused exactly when it is invalid, with the status the
//     validation order names (storage, then the first bad sample);
//   - a refused request leaves the destination's bytes unchanged;
//   - an accepted one writes finite values and leaves padding unchanged;
//   - numerical_failure never occurs for an admitted input.
// CI runs it briefly per change (fuzz-smoke.yml). Built with
// STX_CONV_FUZZ_STANDALONE, main() replays a fixed pseudo-random set of
// inputs instead, as an ordinary test.
#include <stx_conv/conv.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
// Outcomes, so the standalone run can show that it reaches both sides.
std::size_t accepted = 0;
std::size_t refused = 0;

void require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "conv fuzz: %s\n", message);
    std::abort();
}

// Reads the input front to back; past its end every read gives zero.
struct Reader {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t at = 0;
    std::uint8_t byte() {
        if (at >= size) return 0;
        const std::uint8_t value = data[at];
        ++at;
        return value;
    }
    std::uint32_t bits32() {
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < 4; ++index) value = (value << 8) | byte();
        return value;
    }
};

// A sample: an ordinary value in [-1, 1], or, when the case admits raw bits,
// one time in eight the raw bits of the next bytes.
template<stx_conv::Precision Scalar>
Scalar sample(Reader& reader, bool raw_allowed) {
    const std::uint8_t kind = reader.byte();
    if (!raw_allowed || kind < 224) {
        const Scalar value = static_cast<Scalar>(reader.byte()) / Scalar(127.5) - Scalar(1);
        return value;
    }
    if constexpr (std::is_same_v<Scalar, float>) {
        const std::uint32_t raw = reader.bits32();
        float value = 0.0F;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    } else {
        const std::uint64_t high = reader.bits32();
        const std::uint64_t raw = (high << 32) | reader.bits32();
        double value = 0.0;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }
}

template<stx_conv::Precision Scalar>
constexpr Scalar sample_limit() {
    if constexpr (std::is_same_v<Scalar, float>) {
        return 1.0e10F;
    } else {
        return 1.0e100;
    }
}

// The status validation gives the first `count` x `lanes` samples, in the
// library's order: node-major, nonfinite before range within one sample.
template<stx_conv::Precision Scalar>
stx_conv::Status expected_samples(const std::vector<Scalar>& values, std::size_t count, std::size_t stride,
                                  std::size_t lanes) {
    for (std::size_t node = 0; node < count; ++node) {
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            const Scalar value = values[node * stride + lane];
            if (!std::isfinite(value)) return stx_conv::Status::nonfinite_input;
            if (std::abs(value) > sample_limit<Scalar>()) return stx_conv::Status::input_out_of_range;
        }
    }
    return stx_conv::Status::ok;
}

template<stx_conv::Precision Scalar>
bool same_bytes(const std::vector<Scalar>& left, const std::vector<Scalar>& right) {
    const bool equal = std::memcmp(left.data(), right.data(), left.size() * sizeof(Scalar)) == 0;
    return equal;
}

// One line: compile, then evaluate through a regular or a point plan.
template<stx_conv::Precision Scalar>
void line_case(Reader& reader) {
    const std::size_t length = 1 + reader.byte() % 48;
    const std::size_t target = 1 + reader.byte() % 64;
    const std::size_t lanes = 1 + reader.byte() % 4;
    const std::size_t stride = lanes + reader.byte() % 3;
    const std::uint8_t mode = reader.byte();
    const stx_conv::Sampling sampling = (mode & 1U) != 0 ? stx_conv::Sampling::pixel_area : stx_conv::Sampling::nodal;
    const bool point_plan = (mode & 2U) != 0;
    const bool short_source = (mode & 4U) != 0 && (mode & 64U) != 0;
    const bool raw_allowed = (mode & 8U) != 0 && (mode & 128U) != 0;
    std::vector<Scalar> source(length * stride, std::numeric_limits<Scalar>::quiet_NaN());
    for (std::size_t node = 0; node < length; ++node) {
        for (std::size_t lane = 0; lane < lanes; ++lane) source[node * stride + lane] = sample<Scalar>(reader, raw_allowed);
    }
    std::size_t source_size = source.size();
    if (short_source) source_size = reader.byte() % (source.size() + 1);
    const std::span<const Scalar> source_span(source.data(), source_size);
    stx_conv::BasicProfile<Scalar> profile(length, lanes);
    stx_conv::BasicLineWorkspace<Scalar> work(length, lanes);
    const std::size_t needed = (length - 1) * stride + lanes;
    stx_conv::Status expected = stx_conv::Status::insufficient_storage;
    if (source_size >= needed) expected = expected_samples(source, length, stride, lanes);
    const stx_conv::Status compiled = stx_conv::compile({source_span, stride}, profile, work);
    require(compiled != stx_conv::Status::numerical_failure, "numerical failure on admitted input");
    require(compiled == expected, "compile status");
    std::vector<Scalar> positions;
    if (point_plan) {
        positions.resize(target);
        for (std::size_t index = 0; index < target; ++index) {
            const Scalar fraction = static_cast<Scalar>(reader.byte()) / Scalar(255);
            positions[index] = fraction * static_cast<Scalar>(length - 1);
        }
    }
    const stx_conv::BasicLinePlan<Scalar> plan = point_plan
        ? stx_conv::BasicLinePlan<Scalar>::points(length, positions)
        : stx_conv::BasicLinePlan<Scalar>(length, target, sampling);
    const std::size_t out_stride = lanes + 1;
    std::vector<Scalar> output(target * out_stride, Scalar(-7));
    const std::vector<Scalar> before = output;
    const stx_conv::Status evaluated = stx_conv::evaluate(profile, plan, {output, out_stride});
    if (compiled != stx_conv::Status::ok) {
        require(evaluated == stx_conv::Status::unprepared_profile, "evaluate of an unprepared profile");
        require(same_bytes(output, before), "refused evaluate leaves the destination");
        ++refused;
        return;
    }
    require(evaluated == stx_conv::Status::ok, "evaluate of a ready profile");
    for (std::size_t node = 0; node < target; ++node) {
        for (std::size_t lane = 0; lane < lanes; ++lane) require(std::isfinite(output[node * out_stride + lane]), "finite");
        require(output[node * out_stride + lanes] == Scalar(-7), "line padding");
    }
    ++accepted;
}

// One image resize, with possibly short spans.
template<stx_conv::Precision Scalar>
void image_case(Reader& reader) {
    const std::size_t sw = 1 + reader.byte() % 16;
    const std::size_t sh = 1 + reader.byte() % 16;
    const std::size_t tw = 1 + reader.byte() % 24;
    const std::size_t th = 1 + reader.byte() % 24;
    const std::size_t channels = 1 + reader.byte() % 4;
    const std::uint8_t mode = reader.byte();
    const stx_conv::Sampling sampling = (mode & 1U) != 0 ? stx_conv::Sampling::pixel_area : stx_conv::Sampling::nodal;
    const std::size_t source_stride = sw * channels + (mode >> 1) % 3;
    const std::size_t output_stride = tw * channels + (mode >> 3) % 3;
    const bool raw_allowed = (mode & 64U) != 0 && (mode & 128U) != 0;
    std::vector<Scalar> source(sh * source_stride, std::numeric_limits<Scalar>::quiet_NaN());
    for (std::size_t y = 0; y < sh; ++y) {
        for (std::size_t x = 0; x < sw * channels; ++x) source[y * source_stride + x] = sample<Scalar>(reader, raw_allowed);
    }
    std::vector<Scalar> output(th * output_stride, Scalar(-7));
    std::size_t output_size = output.size();
    if ((mode & 32U) != 0 && (mode & 64U) == 0) output_size = reader.byte() % (output.size() + 1);
    const stx_conv::BasicImagePlan<Scalar> plan(sw, sh, tw, th, sampling);
    stx_conv::BasicImageWorkspace<Scalar> work(plan, channels);
    const std::vector<Scalar> before = output;
    const std::span<Scalar> output_span(output.data(), output_size);
    const std::size_t output_needed = (th - 1) * output_stride + tw * channels;
    stx_conv::Status expected = stx_conv::Status::insufficient_storage;
    if (output_size >= output_needed) expected = expected_samples(source, sh, source_stride, sw * channels);
    const stx_conv::Status resized = stx_conv::resize(plan, {source, source_stride}, {output_span, output_stride}, work);
    require(resized != stx_conv::Status::numerical_failure, "numerical failure on admitted image");
    require(resized == expected, "resize status");
    if (resized != stx_conv::Status::ok) {
        require(same_bytes(output, before), "refused resize leaves the destination");
        ++refused;
        return;
    }
    for (std::size_t y = 0; y < th; ++y) {
        for (std::size_t x = 0; x < tw * channels; ++x) require(std::isfinite(output[y * output_stride + x]), "finite image");
        for (std::size_t x = tw * channels; x < output_stride; ++x) require(output[y * output_stride + x] == Scalar(-7), "image padding");
    }
    ++accepted;
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    Reader reader{data, size, 0};
    const std::uint8_t choice = reader.byte();
    const bool use_float = (choice & 1U) != 0;
    const bool image = (choice & 2U) != 0;
    if (use_float && image) image_case<float>(reader);
    if (use_float && !image) line_case<float>(reader);
    if (!use_float && image) image_case<double>(reader);
    if (!use_float && !image) line_case<double>(reader);
    return 0;
}

#if defined(STX_CONV_FUZZ_STANDALONE)
int main() {
    // A fixed pseudo-random set of inputs (xorshift32), so the ordinary test
    // run exercises the harness and every branch of the input decoding.
    std::uint32_t state = 20261022U;
    std::vector<std::uint8_t> input(1024, 0);
    for (std::size_t run = 0; run < 4000; ++run) {
        for (std::size_t index = 0; index < input.size(); ++index) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            input[index] = static_cast<std::uint8_t>(state >> 24);
        }
        const std::size_t length = 1 + run % input.size();
        LLVMFuzzerTestOneInput(input.data(), length);
    }
    require(accepted >= 1000 && refused >= 200, "the fixed inputs reach both accepted and refused requests");
    std::printf("PASS: conv fuzz harness, 4000 fixed inputs; %zu accepted, %zu refused\n", accepted, refused);
    return 0;
}
#endif
