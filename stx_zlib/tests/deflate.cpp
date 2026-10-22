#include "stx_zlib/deflate.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "deflate: %s\n", message);
    std::abort();
}
stx_zlib::DeflateOptions options_for(std::size_t count) {
    stx_zlib::DeflateOptions options{};
    options.input_limit = stx_zlib::ByteCount{count};
    options.output_limit = stx_zlib::ByteCount{count * 2 + 65536};
    options.work_limit = stx_zlib::WorkCount{1000000000};
    return options;
}
void append(std::vector<std::byte>& destination, std::span<const std::byte> source) {
    destination.insert(destination.end(), source.begin(), source.end());
}
std::vector<std::byte> encode(std::span<const std::byte> input,
    const stx_zlib::DeflateOptions& options, std::size_t input_step, std::size_t output_step,
    bool separate_finish) {
    std::unique_ptr<stx_zlib::DeflateStream> owner = std::make_unique<stx_zlib::DeflateStream>();
    stx_zlib::DeflateStream& stream = *owner;
    require(stream.reset(options) == stx_zlib::DeflateError::none, "reset");
    std::vector<std::byte> output(output_step);
    std::vector<std::byte> encoded{};
    std::size_t position = 0;
    bool finished_input = false;
    for (;;) {
        const std::size_t count = std::min(input_step, input.size() - position);
        const bool final = position + count == input.size() && (!separate_finish || finished_input);
        const stx_zlib::DeflateFlush flush = final ? stx_zlib::DeflateFlush::finish : stx_zlib::DeflateFlush::none;
        const stx_zlib::DeflateResult result = stream.process(input.subspan(position, count), output, flush);
        require(result.status != stx_zlib::DeflateStatus::error, "encoding error");
        require(result.input_consumed <= count && result.output_produced <= output.size(), "counts");
        position += result.input_consumed;
        if (position == input.size()) finished_input = true;
        append(encoded, std::span<const std::byte>{output}.first(result.output_produced));
        require(result.total_input.value == position && result.total_output.value == encoded.size(), "totals");
        if (result.status == stx_zlib::DeflateStatus::stream_end) break;
        require(result.input_consumed != 0 || result.output_produced != 0 ||
            (finished_input && !final), "no progress");
    }
    require(position == input.size(), "input not consumed");
    return encoded;
}
void roundtrip(std::span<const std::byte> input, std::span<const std::byte> encoded,
    const stx_zlib::DeflateOptions& encoder_options) {
    std::unique_ptr<stx_zlib::InflateStream> owner = std::make_unique<stx_zlib::InflateStream>();
    stx_zlib::InflateOptions options{};
    options.format = encoder_options.format == stx_zlib::DeflateFormat::zlib ?
        stx_zlib::InflateFormat::zlib : stx_zlib::InflateFormat::raw_deflate;
    options.window_bits = encoder_options.window_bits;
    if (options.format == stx_zlib::InflateFormat::zlib && options.window_bits == 8) options.window_bits = 9;
    options.limits.input_bytes = stx_zlib::ByteCount{encoded.size()};
    options.limits.output_bytes = stx_zlib::ByteCount{input.size()};
    options.limits.work = stx_zlib::WorkCount{1000000000};
    require((*owner).reset(options) == stx_zlib::InflateError::none, "inflate reset");
    std::vector<std::byte> decoded(input.size() + 1);
    const stx_zlib::InflateResult result = (*owner).process(encoded, decoded, true);
    require(result.status == stx_zlib::InflateStatus::stream_end, "inflate failed");
    require(result.input_consumed == encoded.size() && result.output_produced == input.size(), "inflate extent");
    require(std::equal(input.begin(), input.end(), decoded.begin()), "roundtrip bytes");
}
void write_u32(std::FILE& file, std::uint32_t value) {
    std::array<unsigned char, 4> bytes{};
    for (unsigned index = 0; index < 4; ++index) bytes[index] = static_cast<unsigned char>(value >> (8 * index));
    require(std::fwrite(bytes.data(), 1, bytes.size(), &file) == bytes.size(), "record header write");
}
void record(std::FILE* file, std::span<const std::byte> input, std::span<const std::byte> encoded,
    const stx_zlib::DeflateOptions& options, bool final = true) {
    if (file == nullptr) return;
    write_u32(*file, static_cast<std::uint32_t>(input.size()));
    write_u32(*file, static_cast<std::uint32_t>(encoded.size()));
    const unsigned format = options.format == stx_zlib::DeflateFormat::zlib ? 0U : 1U;
    write_u32(*file, format | (final ? 0U : 2U));
    if (!input.empty()) require(std::fwrite(input.data(), 1, input.size(), file) == input.size(), "input write");
    require(std::fwrite(encoded.data(), 1, encoded.size(), file) == encoded.size(), "encoded write");
}
std::vector<std::byte> fixture(std::size_t count, unsigned kind) {
    std::vector<std::byte> bytes(count);
    std::uint32_t state = 0x91ef0b3dU;
    for (std::size_t index = 0; index < count; ++index) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        unsigned value = 0;
        switch (kind) {
        case 0: value = 65; break;
        case 1: value = state & 255U; break;
        case 2: value = static_cast<unsigned>((index * 13 + index / 137) & 255U); break;
        case 3: value = (state & 15U) == 0 ? (state & 255U) : 0; break;
        case 4: value = static_cast<unsigned>((index % 32768) % 251); break;
        default: value = static_cast<unsigned>((index / 259) & 255U); break;
        }
        bytes[index] = static_cast<std::byte>(value);
    }
    return bytes;
}
std::vector<std::byte> sync_test(std::FILE* file, std::size_t boundary, std::size_t output_step) {
    const std::vector<std::byte> input = fixture(70000, 2);
    const stx_zlib::DeflateOptions options = options_for(input.size());
    std::unique_ptr<stx_zlib::DeflateStream> owner = std::make_unique<stx_zlib::DeflateStream>();
    stx_zlib::DeflateStream& stream = *owner;
    require(stream.reset(options) == stx_zlib::DeflateError::none, "sync reset");
    std::vector<std::byte> output(output_step);
    std::vector<std::byte> encoded{};
    std::size_t position = 0;
    for (;;) {
        const stx_zlib::DeflateResult result = stream.process(
            std::span<const std::byte>{input}.subspan(position, boundary - position), output, stx_zlib::DeflateFlush::sync);
        require(result.status != stx_zlib::DeflateStatus::error, "sync error");
        position += result.input_consumed;
        append(encoded, std::span<const std::byte>{output}.first(result.output_produced));
        if (result.status == stx_zlib::DeflateStatus::flush_complete) break;
    }
    require(position == boundary, "sync boundary");
    record(file, std::span<const std::byte>{input}.first(boundary), encoded, options, false);
    for (;;) {
        const stx_zlib::DeflateResult result = stream.process(
            std::span<const std::byte>{input}.subspan(position), output, stx_zlib::DeflateFlush::finish);
        require(result.status != stx_zlib::DeflateStatus::error, "sync finish error");
        position += result.input_consumed;
        append(encoded, std::span<const std::byte>{output}.first(result.output_produced));
        if (result.status == stx_zlib::DeflateStatus::stream_end) break;
    }
    roundtrip(input, encoded, options);
    record(file, input, encoded, options);
    return encoded;
}
void limits_test() {
    const std::vector<std::byte> input = fixture(4096, 2);
    const stx_zlib::DeflateOptions initial = options_for(input.size());
    std::unique_ptr<stx_zlib::DeflateStream> owner = std::make_unique<stx_zlib::DeflateStream>();
    stx_zlib::DeflateStream& stream = *owner;
    std::vector<std::byte> output(65536);
    require(stream.reset(initial) == stx_zlib::DeflateError::none, "limit reset");
    const stx_zlib::DeflateResult baseline = stream.process(input, output, stx_zlib::DeflateFlush::finish);
    require(baseline.status == stx_zlib::DeflateStatus::stream_end, "limit baseline");
    const std::vector<std::byte> expected(output.begin(), output.begin() + baseline.output_produced);
    stx_zlib::DeflateOptions exact = initial;
    exact.output_limit = baseline.total_output;
    exact.work_limit = baseline.total_work;
    require(stream.reset(exact) == stx_zlib::DeflateError::none, "exact reset");
    const stx_zlib::DeflateResult exact_result = stream.process(input, output, stx_zlib::DeflateFlush::finish);
    require(exact_result.status == stx_zlib::DeflateStatus::stream_end, "exact quotas");
    require(exact_result.total_work.value == baseline.total_work.value &&
        exact_result.output_produced == expected.size() &&
        std::equal(expected.begin(), expected.end(), output.begin()), "quota changed work or bytes");
    for (unsigned fraction = 0; fraction < 64; ++fraction) {
        stx_zlib::DeflateOptions budget = initial;
        budget.work_limit.value = baseline.total_work.value * fraction / 64;
        require(stream.reset(budget) == stx_zlib::DeflateError::none, "budget reset");
        const stx_zlib::DeflateResult limited = stream.process(input, output, stx_zlib::DeflateFlush::finish);
        require(limited.error == stx_zlib::DeflateError::work_limit_exceeded &&
            limited.total_work.value <= budget.work_limit.value, "work budget overspent");
    }
    constexpr std::array<stx_zlib::DeflateError, 3> errors{
        stx_zlib::DeflateError::input_limit_exceeded, stx_zlib::DeflateError::output_limit_exceeded,
        stx_zlib::DeflateError::work_limit_exceeded};
    for (unsigned kind = 0; kind < errors.size(); ++kind) {
        stx_zlib::DeflateOptions short_limit = exact;
        if (kind == 0) --short_limit.input_limit.value;
        if (kind == 1) --short_limit.output_limit.value;
        if (kind == 2) --short_limit.work_limit.value;
        require(stream.reset(short_limit) == stx_zlib::DeflateError::none, "short reset");
        const stx_zlib::DeflateResult result = stream.process(input, output, stx_zlib::DeflateFlush::finish);
        require(result.status == stx_zlib::DeflateStatus::error && result.error == errors[kind], "short quota");
        const stx_zlib::DeflateResult frozen = stream.process(input, output, stx_zlib::DeflateFlush::finish);
        require(frozen.error == result.error && frozen.input_consumed == 0 && frozen.output_produced == 0 &&
            frozen.total_work.value == result.total_work.value, "failed stream changed");
    }
    require(stream.reset(initial) == stx_zlib::DeflateError::none, "reset after failure");
    stx_zlib::DeflateOptions invalid = initial;
    invalid.level = 10;
    require(stream.reset(invalid) == stx_zlib::DeflateError::invalid_argument, "invalid reset accepted");
    const stx_zlib::DeflateResult preserved = stream.process(input, output, stx_zlib::DeflateFlush::finish);
    require(preserved.status == stx_zlib::DeflateStatus::stream_end, "invalid reset destroyed old stream");
    require(stream.reset(initial) == stx_zlib::DeflateError::none, "alias reset");
    const stx_zlib::DeflateResult alias = stream.process(output, output, stx_zlib::DeflateFlush::finish);
    require(alias.error == stx_zlib::DeflateError::invalid_argument && alias.output_produced == 0, "alias accepted");
    require(stream.reset(initial) == stx_zlib::DeflateError::none, "empty output reset");
    const stx_zlib::DeflateResult empty = stream.process(input, {}, stx_zlib::DeflateFlush::finish);
    require(empty.status == stx_zlib::DeflateStatus::need_output && empty.input_consumed == 0, "empty output");
}
void skew_test(std::FILE* file) {
    // Fibonacci frequencies force an unconstrained Huffman tree deeper than
    // DEFLATE's 15-bit limit. Keep matches disabled to preserve that histogram.
    std::vector<std::byte> input{};
    unsigned previous = 0;
    unsigned count = 1;
    for (unsigned symbol = 0; symbol < 21; ++symbol) {
        input.insert(input.end(), count, static_cast<std::byte>(symbol));
        const unsigned next = previous + count;
        previous = count;
        count = next;
    }
    stx_zlib::DeflateOptions options = options_for(input.size());
    options.strategy = stx_zlib::DeflateStrategy::huffman_only;
    const std::vector<std::byte> encoded = encode(input, options, input.size(), 65536, false);
    roundtrip(input, encoded, options);
    record(file, input, encoded, options);
    const std::vector<std::byte> split = encode(input, options, 1, 1, true);
    require(encoded == split, "length-limited tree identity");
    options.level = -1;
    const std::vector<std::byte> default_level = encode(input, options, input.size(), 65536, false);
    require(encoded == default_level, "default level differs from six");
}
void window_edge_tests(std::FILE* file) {
    constexpr std::array<int, 3> levels{1,6,9};
    for (unsigned window = 8; window <= 15; ++window) {
        const std::size_t width = std::size_t{1} << window;
        const std::vector<std::byte> seed = fixture(width, 1);
        std::vector<std::byte> input(3 * width + 257);
        for (std::size_t index = 0; index < input.size(); ++index) input[index] = seed[index % width];
        for (const int level : levels) {
            stx_zlib::DeflateOptions options = options_for(input.size());
            options.format = stx_zlib::DeflateFormat::raw_deflate;
            options.window_bits = window;
            options.level = level;
            const std::vector<std::byte> whole = encode(input, options, input.size(), 65536, false);
            const std::vector<std::byte> split = encode(input, options, 13, 31, true);
            require(whole == split, "window edge schedule changed bytes");
            require(whole.size() < width + width / 4 + 512, "window edge lost long matches");
            roundtrip(input, whole, options);
            record(file, input, whole, options);
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    std::FILE* file = nullptr;
    if (argc == 2) {
        file = std::fopen(argv[1], "wb");
        require(file != nullptr, "open oracle records");
    }
    constexpr std::array<stx_zlib::DeflateStrategy, 5> strategies{
        stx_zlib::DeflateStrategy::normal, stx_zlib::DeflateStrategy::filtered,
        stx_zlib::DeflateStrategy::huffman_only, stx_zlib::DeflateStrategy::rle, stx_zlib::DeflateStrategy::fixed};
    unsigned cases = 0;
    for (int level = 0; level <= 9; ++level) {
        for (unsigned window = 8; window <= 15; ++window) {
            for (unsigned strategy = 0; strategy < strategies.size(); ++strategy) {
                const std::vector<std::byte> input = fixture(1024, (window + strategy) % 6);
                stx_zlib::DeflateOptions options = options_for(input.size());
                options.level = level;
                options.window_bits = window;
                options.memory_level = 1 + (window + strategy) % 9;
                options.strategy = strategies[strategy];
                if ((window + strategy) % 2 != 0) options.format = stx_zlib::DeflateFormat::raw_deflate;
                const std::vector<std::byte> whole = encode(input, options, input.size(), 65536, false);
                const std::vector<std::byte> split = encode(input, options, 1, 1, true);
                require(whole == split, "buffer schedule changed encoding");
                roundtrip(input, whole, options);
                record(file, input, whole, options);
                ++cases;
            }
        }
    }
    constexpr std::array<std::size_t, 12> sizes{0,1,2,3,257,258,259,32767,32768,32769,65536,100000};
    for (unsigned kind = 0; kind < 6; ++kind) {
        for (const std::size_t size : sizes) {
            const std::vector<std::byte> input = fixture(size, kind);
            stx_zlib::DeflateOptions options = options_for(size);
            options.level = 9;
            const std::vector<std::byte> whole = encode(input, options, size, 65536, false);
            const std::vector<std::byte> split = encode(input, options, 7, 17, true);
            require(whole == split, "block boundary changed encoding");
            roundtrip(input, whole, options);
            record(file, input, whole, options);
            if (kind == 0 && size >= 32768) require(whole.size() < size / 10, "missing compression");
            ++cases;
        }
    }
    constexpr std::array<std::size_t, 7> boundaries{1,2,3,7,257,32768,40000};
    for (const std::size_t boundary : boundaries) {
        const std::vector<std::byte> split = sync_test(file, boundary, 7);
        const std::vector<std::byte> whole = sync_test(nullptr, boundary, 65536);
        require(split == whole, "sync output schedule changed encoding");
    }
    limits_test();
    skew_test(file);
    window_edge_tests(file);
    if (file != nullptr) require(std::fclose(file) == 0, "close oracle records");
    std::fprintf(stderr, "deflate: %u roundtrip/identity cases plus sync, skew and quota checks passed; workspace %zu bytes\n",
        cases, sizeof(stx_zlib::DeflateStream));
    return 0;
}
