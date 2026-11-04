// Automated memory integrity review. The runner exposes only PASS or STOP.
#include "stx_zlib/deflate.hpp"

#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {
void require(bool condition) {
    if (!condition) std::abort();
}
struct Encoding final {
    stx_zlib::DeflateResult result{};
    std::size_t size = 0;
    std::size_t sync_size = 0;
};

// Output belongs to this invocation and is disjoint from the input and stream.
// Both schedules use the same explicit sync boundary and finality decisions.
Encoding encode(stx_zlib::DeflateStream& stream, const stx_zlib::DeflateOptions& options,
    std::span<const std::byte> input, std::span<std::byte> output,
    std::size_t input_step, std::size_t output_step, bool sync, std::size_t boundary,
    stx_zlib::DeflateError expected_error = stx_zlib::DeflateError::none) {
    require(stream.reset(options) == stx_zlib::DeflateError::none);
    Encoding encoded{};
    std::size_t read = 0;
    bool syncing = sync;
    const std::size_t call_bound = 2 * input.size() + output.size() + 8;
    for (std::size_t calls = 0; calls < call_bound; ++calls) {
        const std::size_t end = syncing ? boundary : input.size();
        const std::size_t take = std::min(input_step, end - read);
        const std::size_t room = std::min(output_step, output.size() - encoded.size);
        stx_zlib::DeflateFlush flush = stx_zlib::DeflateFlush::none;
        if (read + take == end) flush = syncing ? stx_zlib::DeflateFlush::sync : stx_zlib::DeflateFlush::finish;
        encoded.result = stream.process(input.subspan(read, take), output.subspan(encoded.size, room), flush);
        require(encoded.result.input_consumed <= take && encoded.result.output_produced <= room);
        read += encoded.result.input_consumed;
        encoded.size += encoded.result.output_produced;
        require(encoded.result.total_input.value == read && encoded.result.total_output.value == encoded.size);
        require(read <= options.input_limit.value && encoded.size <= options.output_limit.value);
        require(encoded.result.total_work.value <= options.work_limit.value);
        if (encoded.result.status == stx_zlib::DeflateStatus::error) {
            require(expected_error != stx_zlib::DeflateError::none && encoded.result.error == expected_error);
            const stx_zlib::DeflateResult frozen = stream.process({}, {}, stx_zlib::DeflateFlush::finish);
            require(frozen.status == stx_zlib::DeflateStatus::error && frozen.error == expected_error);
            require(frozen.input_consumed == 0 && frozen.output_produced == 0);
            require(frozen.total_input.value == read && frozen.total_output.value == encoded.size);
            require(frozen.total_work.value == encoded.result.total_work.value);
            return encoded;
        }
        if (encoded.result.status == stx_zlib::DeflateStatus::flush_complete) {
            require(syncing && read == boundary);
            syncing = false;
            encoded.sync_size = encoded.size;
            continue;
        }
        if (encoded.result.status == stx_zlib::DeflateStatus::stream_end) {
            require(!syncing && read == input.size() && expected_error == stx_zlib::DeflateError::none);
            return encoded;
        }
        require(encoded.result.input_consumed != 0 || encoded.result.output_produced != 0);
    }
    std::abort();
}

void verify(std::span<const std::byte> original, std::span<const std::byte> encoded,
    stx_zlib::DeflateFormat format, bool final) {
    std::vector<std::byte> decoded(original.size() + 1);
    z_stream reference{};
    const int window = format == stx_zlib::DeflateFormat::raw_deflate ? -15 : 15;
    require(inflateInit2(&reference, window) == Z_OK);
    reference.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(encoded.data()));
    reference.avail_in = static_cast<uInt>(encoded.size());
    reference.next_out = reinterpret_cast<Bytef*>(decoded.data());
    reference.avail_out = static_cast<uInt>(decoded.size());
    const int status = inflate(&reference, final ? Z_FINISH : Z_SYNC_FLUSH);
    const bool accepted = final ? status == Z_STREAM_END : status == Z_OK || status == Z_BUF_ERROR;
    const bool extent = reference.avail_in == 0 && reference.total_out == original.size();
    require(inflateEnd(&reference) == Z_OK);
    require(accepted && extent && std::equal(original.begin(), original.end(), decoded.begin()));
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t controls = 8;
    if (size < controls || size > controls + 65536) return 0;
    const std::span<const std::byte> input(reinterpret_cast<const std::byte*>(data + controls), size - controls);
    constexpr std::array<stx_zlib::DeflateStrategy, 5> strategies{
        stx_zlib::DeflateStrategy::normal, stx_zlib::DeflateStrategy::filtered,
        stx_zlib::DeflateStrategy::huffman_only, stx_zlib::DeflateStrategy::rle, stx_zlib::DeflateStrategy::fixed};
    stx_zlib::DeflateOptions options{};
    options.level = data[0] % 10;
    options.strategy = strategies[data[1] % strategies.size()];
    options.window_bits = 8 + data[2] % 8;
    options.memory_level = 1 + data[3] % 9;
    options.format = (data[4] & 1U) != 0 ? stx_zlib::DeflateFormat::raw_deflate : stx_zlib::DeflateFormat::zlib;
    options.input_limit = stx_zlib::ByteCount{input.size()};
    options.output_limit = stx_zlib::ByteCount{2 * input.size() + 65536};
    options.work_limit = stx_zlib::WorkCount{1000000000000};
    const bool sync = (data[4] & 2U) != 0;
    const std::size_t boundary = input.size() * data[7] / 255;
    std::unique_ptr<stx_zlib::DeflateStream> owner = std::make_unique<stx_zlib::DeflateStream>();
    stx_zlib::DeflateStream& stream = *owner;
    std::vector<std::byte> whole(static_cast<std::size_t>(options.output_limit.value));
    std::vector<std::byte> split(whole.size());
    const Encoding first = encode(stream, options, input, whole, input.size() + 1, whole.size(), sync, boundary);
    const Encoding second = encode(stream, options, input, split, 1 + data[5], 1 + data[6], sync, boundary);
    require(first.size == second.size && first.sync_size == second.sync_size);
    require(first.result.total_work.value == second.result.total_work.value);
    require(std::equal(whole.begin(), whole.begin() + first.size, split.begin()));
    verify(input, std::span<const std::byte>{whole}.first(first.size), options.format, true);
    if (sync) verify(input.first(boundary), std::span<const std::byte>{whole}.first(first.sync_size), options.format, false);
    // A separately selected short quota must fail without exceeding its budget,
    // then preserve the failure and counters on subsequent calls.
    stx_zlib::DeflateError expected_error = stx_zlib::DeflateError::work_limit_exceeded;
    const unsigned quota = (data[4] >> 2) % 3;
    if (quota == 0) {
        options.work_limit.value = first.result.total_work.value * data[7] / 256;
    } else if (quota == 1) {
        options.output_limit.value = first.size * data[7] / 256;
        expected_error = stx_zlib::DeflateError::output_limit_exceeded;
    } else {
        if (input.empty()) return 0;
        options.input_limit.value = input.size() * data[7] / 256;
        expected_error = stx_zlib::DeflateError::input_limit_exceeded;
    }
    encode(stream, options, input, split, 1 + data[5], 1 + data[6], sync, boundary, expected_error);
    return 0;
}
