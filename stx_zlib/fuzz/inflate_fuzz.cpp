#include "stx_zlib/inflate.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>

#if defined(STX_ZLIB_INFLATE_ORACLE)
#include <zlib.h>
#endif

namespace {
using namespace stx_zlib;
constexpr std::size_t output_limit = 32768;

struct Run final {
    std::array<std::byte, output_limit> bytes{};
    InflateResult result{};
};

void run(InflateStream& stream, std::span<const std::byte> input, Run& output,
         InflateFormat format, bool split) {
    const InflateOptions options{
        .format = format, .window_bits = 15,
        .limits = {.input_bytes = ByteCount{65536}, .output_bytes = ByteCount{output_limit},
                   .work = WorkCount{4000000}}};
    if (stream.reset(options) != InflateError::none) std::abort();
    std::size_t read = 0;
    std::size_t written = 0;
    for (std::size_t calls = 0; calls < 100000; ++calls) {
        std::size_t take = input.size() - read;
        std::size_t room = output.bytes.size() - written;
        if (split && take > 1) take = 1;
        if (split && room > 1) room = 1;
        const std::span<const std::byte> source = input.subspan(read, take);
        const std::span<std::byte> destination(output.bytes.data() + written, room);
        output.result = stream.process(source, destination, read + take == input.size());
        if (output.result.input_consumed > take || output.result.output_produced > room) std::abort();
        read += output.result.input_consumed;
        written += output.result.output_produced;
        if (output.result.status == InflateStatus::error || output.result.status == InflateStatus::stream_end) return;
        if (output.result.input_consumed == 0 && output.result.output_produced == 0) std::abort();
    }
    std::abort();
}

#if defined(STX_ZLIB_INFLATE_ORACLE)
void compare_reference(std::span<const std::byte> input, const Run& candidate, InflateFormat format) {
    if (candidate.result.error == InflateError::work_limit_exceeded ||
        candidate.result.error == InflateError::output_limit_exceeded ||
        candidate.result.error == InflateError::input_limit_exceeded) return;
    std::array<std::byte, output_limit + 1> bytes{};
    z_stream reference{};
    int window = 15;
    if (format == InflateFormat::raw_deflate) window = -15;
    if (inflateInit2(&reference, window) != Z_OK) std::abort();
    reference.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(input.data()));
    reference.avail_in = static_cast<uInt>(input.size());
    reference.next_out = reinterpret_cast<Bytef*>(bytes.data());
    reference.avail_out = static_cast<uInt>(bytes.size());
    const int status = inflate(&reference, Z_FINISH);
    const std::size_t read = reference.total_in;
    const std::size_t written = reference.total_out;
    if (inflateEnd(&reference) != Z_OK) std::abort();
    const bool accepted = candidate.result.status == InflateStatus::stream_end;
    if (accepted != (status == Z_STREAM_END)) std::abort();
    if (accepted) {
        if (read != candidate.result.total_input.value || written != candidate.result.total_output.value) std::abort();
        for (std::size_t i = 0; i < written; ++i) {
            if (bytes[i] != candidate.bytes[i]) std::abort();
        }
    }
}
#endif
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0 || size > 65536) return 0;
    InflateFormat format = InflateFormat::raw_deflate;
    if ((data[0] & 1U) != 0) format = InflateFormat::zlib;
    const std::byte* bytes = reinterpret_cast<const std::byte*>(data + 1);
    const std::span<const std::byte> input(bytes, size - 1);
    std::unique_ptr<InflateStream> stream = std::make_unique<InflateStream>();
    std::unique_ptr<Run> whole = std::make_unique<Run>();
    std::unique_ptr<Run> split = std::make_unique<Run>();
    run(*stream, input, *whole, format, false);
    run(*stream, input, *split, format, true);
    // Work charging includes resumptions, so resource exhaustion is compared
    // separately from malformed-stream classification and output correctness.
    if ((*whole).result.error == InflateError::work_limit_exceeded ||
        (*split).result.error == InflateError::work_limit_exceeded) return 0;
    if ((*whole).result.status != (*split).result.status ||
        (*whole).result.error != (*split).result.error ||
        (*whole).result.total_output.value != (*split).result.total_output.value) std::abort();
    const std::size_t count = static_cast<std::size_t>((*whole).result.total_output.value);
    for (std::size_t i = 0; i < count; ++i) {
        if ((*whole).bytes[i] != (*split).bytes[i]) std::abort();
    }
#if defined(STX_ZLIB_INFLATE_ORACLE)
    compare_reference(input, *whole, format);
#endif
    return 0;
}

#if defined(STX_ZLIB_FUZZ_STANDALONE)
int main() {
    std::array<std::uint8_t, 256> input{};
    std::uint32_t random = 0x9e3779b9;
    for (std::size_t iteration = 0; iteration < 10000; ++iteration) {
        const std::size_t size = 1U + iteration % input.size();
        for (std::size_t i = 0; i < size; ++i) {
            random = random * 1664525U + 1013904223U;
            input[i] = static_cast<std::uint8_t>(random >> 24U);
        }
        LLVMFuzzerTestOneInput(input.data(), size);
    }
    return 0;
}
#endif
