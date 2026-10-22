// Reset and encoding are timed; fixture creation, allocation and independent
// decode verification are outside the timed region. Both encoders reuse state.
#include "stx_zlib/deflate.hpp"
#include <zlib.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
void require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "%s\n", message);
    std::abort();
}
void verify(std::span<const std::byte> input, std::span<const std::byte> encoded) {
    std::vector<std::byte> output(input.size() + 1);
    z_stream stream{};
    require(inflateInit(&stream) == Z_OK, "oracle init");
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(encoded.data()));
    stream.avail_in = static_cast<uInt>(encoded.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    const int result = inflate(&stream, Z_FINISH);
    const bool complete = result == Z_STREAM_END && stream.total_out == input.size() && stream.avail_in == 0;
    require(inflateEnd(&stream) == Z_OK, "oracle end");
    require(complete && std::equal(input.begin(), input.end(), output.begin()), "oracle bytes");
}
struct Sample final {
    double seconds = 0;
    std::size_t compressed = 0;
    std::uint64_t work = 0;
};
Sample batch(stx_zlib::DeflateStream& stream, z_stream& reference,
    const stx_zlib::DeflateOptions& options, std::span<const std::byte> input,
    std::span<std::byte> output, unsigned implementation, unsigned iterations) {
    Sample sample{};
    const Clock::time_point start = Clock::now();
    if (implementation == 0) {
        for (unsigned iteration = 0; iteration < iterations; ++iteration) {
            require(stream.reset(options) == stx_zlib::DeflateError::none, "owned reset");
            const stx_zlib::DeflateResult result = stream.process(input, output, stx_zlib::DeflateFlush::finish);
            require(result.status == stx_zlib::DeflateStatus::stream_end, "owned encode");
            sample.compressed = result.output_produced;
            sample.work = result.total_work.value;
        }
    } else {
        for (unsigned iteration = 0; iteration < iterations; ++iteration) {
            require(deflateReset(&reference) == Z_OK, "reference reset");
            reference.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(input.data()));
            reference.avail_in = static_cast<uInt>(input.size());
            reference.next_out = reinterpret_cast<Bytef*>(output.data());
            reference.avail_out = static_cast<uInt>(output.size());
            require(deflate(&reference, Z_FINISH) == Z_STREAM_END, "reference encode");
            sample.compressed = reference.total_out;
        }
    }
    const Clock::time_point stop = Clock::now();
    const std::chrono::duration<double> elapsed = stop - start;
    sample.seconds = elapsed.count();
    verify(input, output.first(sample.compressed));
    return sample;
}
void measure(std::span<const std::byte> input, const char* name, int level) {
    std::unique_ptr<stx_zlib::DeflateStream> owner = std::make_unique<stx_zlib::DeflateStream>();
    stx_zlib::DeflateStream& stream = *owner;
    stx_zlib::DeflateOptions options{};
    options.level = level;
    options.input_limit = stx_zlib::ByteCount{input.size()};
    options.output_limit = stx_zlib::ByteCount{input.size() * 2 + 65536};
    options.work_limit = stx_zlib::WorkCount{100000000000};
    std::vector<std::byte> output(static_cast<std::size_t>(options.output_limit.value));
    z_stream reference{};
    require(deflateInit2(&reference, level, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) == Z_OK, "reference init");
    std::array<unsigned, 2> iterations{1,1};
    std::array<Sample, 2> last{};
    std::array<std::array<double, 9>, 2> samples{};
    for (unsigned implementation = 0; implementation < 2; ++implementation) {
        for (;;) {
            last[implementation] = batch(stream, reference, options, input, output,
                implementation, iterations[implementation]);
            if (last[implementation].seconds >= 0.01 || iterations[implementation] >= 1048576) break;
            iterations[implementation] *= 2;
        }
    }
    // Alternate the first encoder in each pair, so changes in host load do not
    // systematically favour the implementation measured later in the run.
    for (unsigned pair = 0; pair < 11; ++pair) {
        for (unsigned order = 0; order < 2; ++order) {
            const unsigned implementation = (order + pair) % 2;
            last[implementation] = batch(stream, reference, options, input, output,
                implementation, iterations[implementation]);
            if (pair >= 2) samples[implementation][pair - 2] = last[implementation].seconds;
        }
    }
    std::array<double, 9> paired_ratios{};
    for (unsigned pair = 0; pair < paired_ratios.size(); ++pair) {
        paired_ratios[pair] = (samples[1][pair] / iterations[1]) / (samples[0][pair] / iterations[0]);
    }
    std::sort(paired_ratios.begin(), paired_ratios.end());
    for (unsigned implementation = 0; implementation < 2; ++implementation) {
        std::array<double, 9>& seconds = samples[implementation];
        std::sort(seconds.begin(), seconds.end());
        const double mib = static_cast<double>(input.size()) * iterations[implementation] / 1048576.0;
        std::printf("%s,%s,%zu,%d,%zu,%.3f,%.3f,%.3f,%llu,%.3f\n",
            implementation == 0 ? "owned" : "upstream", name, input.size(), level, last[implementation].compressed,
            mib / seconds[4], mib / seconds[8], mib / seconds[0],
            static_cast<unsigned long long>(last[implementation].work), paired_ratios[4]);
        std::fflush(stdout);
    }
    require(deflateEnd(&reference) == Z_OK, "reference end");
}
} // namespace

int main(int argc, char** argv) {
    std::puts("implementation,workload,bytes,level,compressed,median_mib_s,min_mib_s,max_mib_s,work_units,paired_owned_speed_ratio");
    constexpr std::array<int, 4> levels{0,1,6,9};
    if (argc == 2) {
        std::FILE* file = std::fopen(argv[1], "rb");
        require(file != nullptr, "fixture open");
        constexpr std::size_t maximum = 16 * 1024 * 1024;
        std::vector<std::byte> bytes(maximum + 1);
        const std::size_t count = std::fread(bytes.data(), 1, bytes.size(), file);
        const bool failed = std::ferror(file) != 0;
        const int closed = std::fclose(file);
        require(!failed && closed == 0 && count != 0 && count <= maximum, "fixture read/size");
        bytes.resize(count);
        for (const int level : levels) measure(bytes, "png-filtered", level);
        return 0;
    }
    std::vector<std::byte> input(262144);
    constexpr std::array<const char*, 3> names{"zero", "ui", "entropy"};
    for (unsigned kind = 0; kind < names.size(); ++kind) {
        if (argc == 3 && kind != static_cast<unsigned>(std::atoi(argv[1]))) continue;
        std::uint32_t random = 0x6a09e667U;
        for (std::size_t index = 0; index < input.size(); ++index) {
            random = random * 1664525U + 1013904223U;
            unsigned byte = 0;
            if (kind == 1) byte = static_cast<unsigned>((index / 1024U + index % 13U) & 255U);
            if (kind == 2) byte = random >> 24U;
            input[index] = static_cast<std::byte>(byte);
        }
        if (argc == 3) measure(input, names[kind], std::atoi(argv[2]));
        else {
            for (const int level : levels) measure(input, names[kind], level);
            measure(std::span<const std::byte>{input}.first(256), names[kind], 6);
        }
    }
    return 0;
}
