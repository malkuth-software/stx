// Isolated benchmark: pinned zlib supplies fixtures and a separately timed
// reference. Fixture generation, allocation and byte comparison are not timed.
#include "gui_forms/compression/inflate.hpp"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <fstream>
#include <string>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace gui_forms::compression;
using Clock = std::chrono::steady_clock;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::byte> encode(std::span<const std::byte> input, int level, int window, int strategy) {
    z_stream stream{};
    const int initialized = deflateInit2(&stream, level, Z_DEFLATED, window, 8, strategy);
    require(initialized == Z_OK, "encode initialization failed");
    std::vector<std::byte> output(input.size() * 2 + 1024);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    const int result = deflate(&stream, Z_FINISH);
    const std::size_t count = stream.total_out;
    const int ended = deflateEnd(&stream);
    require(result == Z_STREAM_END && ended == Z_OK, "encode failed");
    output.resize(count);
    return output;
}

void measure(std::span<const std::byte> input, const char* name, int level, int strategy, bool wrapped) {
    int window = -15;
    InflateFormat format = InflateFormat::raw_deflate;
    if (wrapped) {
        window = 15;
        format = InflateFormat::zlib;
    }
    const std::vector<std::byte> encoded = encode(input, level, window, strategy);
    const std::string stem = std::string(name) + "-" + std::to_string(level) + "-" + std::to_string(strategy) + "-" + std::to_string(wrapped);
    std::ofstream fixture(stem + ".deflate", std::ios::binary);
    fixture.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    require(static_cast<bool>(fixture), "fixture write failed");
    fixture.close();
    std::ofstream original(stem + ".raw", std::ios::binary);
    original.write(reinterpret_cast<const char*>(input.data()), static_cast<std::streamsize>(input.size()));
    require(static_cast<bool>(original), "original write failed");
    original.close();
    std::vector<std::byte> output(input.size());
    std::unique_ptr<InflateStream> owner = std::make_unique<InflateStream>();
    InflateStream& stream = *owner;
    const InflateOptions options{.format = format, .window_bits = 15,
        .limits = {.input_bytes = ByteCount{encoded.size()}, .output_bytes = ByteCount{input.size()},
                   .work = WorkCount{1000000000}}};
    z_stream reference{};
    const int initialized = inflateInit2(&reference, window);
    require(initialized == Z_OK, "reference initialization failed");
    for (unsigned implementation = 0; implementation < 2; ++implementation) {
        std::array<double, 5> seconds{};
        unsigned iterations = 12;
        // Two warmup batches, then five independently timed batches. The output
        // and stream workspace are reused; reset is included in both timings.
        unsigned batch = 0;
        while (batch < 7) {
            const Clock::time_point start = Clock::now();
            for (unsigned iteration = 0; iteration < iterations; ++iteration) {
                if (implementation == 0) {
                    require(stream.reset(options) == InflateError::none, "owned reset failed");
                    const InflateResult result = stream.process(encoded, output, true);
                    require(result.status == InflateStatus::stream_end && result.output_produced == input.size(), "owned decode failed");
                } else {
                    require(inflateReset(&reference) == Z_OK, "reference reset failed");
                    reference.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(encoded.data()));
                    reference.avail_in = static_cast<uInt>(encoded.size());
                    reference.next_out = reinterpret_cast<Bytef*>(output.data());
                    reference.avail_out = static_cast<uInt>(output.size());
                    const int result = inflate(&reference, Z_FINISH);
                    require(result == Z_STREAM_END && reference.total_out == input.size(), "reference decode failed");
                }
            }
            const Clock::time_point stop = Clock::now();
            const std::chrono::duration<double> elapsed = stop - start;
            require(std::equal(input.begin(), input.end(), output.begin()), "benchmark output mismatch");
            if (batch == 0 && elapsed.count() < 0.05 && iterations < 1048576) {
                iterations *= 2;
                continue;
            }
            if (batch >= 2) seconds[batch - 2] = elapsed.count();
            ++batch;
        }
        std::sort(seconds.begin(), seconds.end());
        const double mib = static_cast<double>(input.size() * iterations) / 1048576.0;
        const double median = mib / seconds[2];
        const double minimum = mib / seconds[4];
        const double maximum = mib / seconds[0];
        const char* implementation_name = "owned";
        if (implementation != 0) implementation_name = "upstream";
        std::cout << implementation_name << ',' << name << ',' << input.size() << ',' << level << ','
                  << strategy << ',' << wrapped << ',' << encoded.size() << ',' << median << ',' << minimum << ',' << maximum << '\n';
    }
    require(inflateEnd(&reference) == Z_OK, "reference end failed");
}
} // namespace

int main() {
    try {
        std::cout << "implementation,workload,bytes,level,strategy,zlib,compressed,median_mib_s,min_mib_s,max_mib_s\n";
        std::cout << std::fixed << std::setprecision(3);
        std::vector<std::byte> input(262144);
        constexpr std::array<const char*, 3> names{"zero", "ui", "entropy"};
        constexpr std::array<const char*, 3> tiny_names{"tiny-zero", "tiny-ui", "tiny-entropy"};
        constexpr std::array<int, 3> levels{0, 6, 6};
        constexpr std::array<int, 3> strategies{Z_DEFAULT_STRATEGY, Z_DEFAULT_STRATEGY, Z_HUFFMAN_ONLY};
        for (unsigned kind = 0; kind < names.size(); ++kind) {
            std::uint32_t random = 0x6a09e667U;
            for (std::size_t i = 0; i < input.size(); ++i) {
                random = random * 1664525U + 1013904223U;
                unsigned byte = 0;
                if (kind == 1) byte = static_cast<unsigned>((i / 1024U + i % 13U) & 255U);
                if (kind == 2) byte = random >> 24U;
                input[i] = static_cast<std::byte>(byte);
            }
            for (unsigned mode = 0; mode < levels.size(); ++mode) {
                for (unsigned wrapped = 0; wrapped < 2; ++wrapped) {
                    measure(input, names[kind], levels[mode], strategies[mode], wrapped != 0);
                }
            }
            const std::span<const std::byte> tiny(input.data(), 256);
            measure(tiny, tiny_names[kind], 6, Z_DEFAULT_STRATEGY, true);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
