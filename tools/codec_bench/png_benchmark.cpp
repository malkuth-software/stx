#include "png_decoder.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

extern "C" int stx_png_oracle(const unsigned char*, std::size_t,
    unsigned char*, std::size_t, std::uint32_t, std::uint32_t);

namespace gui_forms::image {
using DecodeSamples = decltype(decode_png_samples);
DecodeSamples baseline_decode_png_samples;
}

namespace {
using namespace gui_forms;
struct Trial final { double median = 0; double minimum = 0; double maximum = 0; };
struct Storage final {
    std::unique_ptr<compression::InflateStream> workspace = std::make_unique<compression::InflateStream>();
    std::vector<std::byte> rows{};
    std::vector<std::uint16_t> rgba{};
    std::vector<std::byte> metadata{};
    std::vector<image::PngMetadataRecord> records{};
    std::vector<unsigned char> reference{};
};
enum class Decoder { owned, baseline, oracle };
// Mode selection precedes each timed batch. Decoder storage is reused; libpng's
// normal context and row-pointer allocation remain part of its measured call.
double batch(const std::vector<std::byte>& png, Decoder decoder, unsigned iterations,
             Storage& storage, const image::PngSampleLayout& layout) {
    const unsigned char* input = reinterpret_cast<const unsigned char*>(png.data());
    image::DecodeSamples* decode = decoder == Decoder::baseline
        ? &image::baseline_decode_png_samples : &image::decode_png_samples;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    if (decoder == Decoder::oracle) {
        for (unsigned index = 0; index < iterations; ++index) {
            const int valid = stx_png_oracle(input, png.size(), storage.reference.data(),
                storage.reference.size(), layout.width, layout.height);
            if (valid == 0) throw std::runtime_error("oracle decode failed");
        }
    } else {
        for (unsigned index = 0; index < iterations; ++index) {
            const image::PngDecodeResult result = decode(png, {}, *storage.workspace,
                storage.rows, storage.rgba, storage.metadata, storage.records);
            if (result.error != image::PngDecodeError::none) throw std::runtime_error("owned decode failed");
        }
    }
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    const double seconds = elapsed.count();
    return seconds;
}
void check_samples(const Storage& storage) {
    for (std::size_t index = 0; index < storage.rgba.size(); ++index) {
        const unsigned expected = static_cast<unsigned>(storage.reference[index * 2]) * 256
            + storage.reference[index * 2 + 1];
        if (storage.rgba[index] != expected) throw std::runtime_error("benchmark sample mismatch");
    }
}
Trial summarize(std::array<double, 7> samples) {
    std::sort(samples.begin(), samples.end());
    const Trial trial{.median=samples[3], .minimum=samples[0], .maximum=samples[6]};
    return trial;
}
void benchmark(const std::filesystem::path& path) {
    const std::uintmax_t size = std::filesystem::file_size(path);
    if (size > 32ULL * 1024 * 1024) throw std::runtime_error("benchmark file too large");
    std::vector<std::byte> png(static_cast<std::size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    stream.read(reinterpret_cast<char*>(png.data()), static_cast<std::streamsize>(size));
    if (!stream) throw std::runtime_error("benchmark read failed");
    const image::PngDecodeResult inspected = image::inspect_png_samples(png, {});
    if (inspected.error != image::PngDecodeError::none) throw std::runtime_error("benchmark inspection failed");
    const image::PngSampleLayout& layout = inspected.layout;
    Storage storage{};
    storage.rows.resize(layout.row_storage_bytes);
    storage.rgba.resize(layout.rgba_samples);
    storage.metadata.resize(4096);
    storage.records.resize(layout.metadata_records);
    storage.reference.resize(layout.rgba_samples * 2);
    constexpr std::array<Decoder, 3> decoders{Decoder::owned, Decoder::baseline, Decoder::oracle};
    // Establish an independent answer, then check both owned paths before timing.
    batch(png, Decoder::oracle, 1, storage, layout);
    batch(png, Decoder::owned, 1, storage, layout);
    check_samples(storage);
    batch(png, Decoder::baseline, 1, storage, layout);
    check_samples(storage);
    std::array<unsigned, 3> iterations{1,1,1};
    for (unsigned mode = 0; mode < decoders.size(); ++mode) {
        while (batch(png, decoders[mode], iterations[mode], storage, layout) < 0.05) {
            if (iterations[mode] >= 1048576) throw std::runtime_error("benchmark calibration exceeded cap");
            iterations[mode] *= 2;
        }
    }
    std::array<std::array<double, 7>, 3> samples{};
    std::array<double, 7> ratios{};
    for (unsigned round = 0; round < 9; ++round) {
        // Rotate first/middle/last position and alternate direction. Seven kept
        // rounds follow two warmups; retain paired ratios, not a ratio of medians.
        for (unsigned step = 0; step < decoders.size(); ++step) {
            const unsigned direction = round % 2 == 0 ? step : 2 - step;
            const unsigned mode = (round + direction) % 3;
            const double seconds = batch(png, decoders[mode], iterations[mode], storage, layout);
            if (round >= 2) {
                const double bytes = static_cast<double>(layout.rgba_samples) * 2 * iterations[mode];
                samples[mode][round - 2] = bytes / (seconds * 1048576.0);
            }
        }
        if (round >= 2) ratios[round - 2] = samples[0][round - 2] / samples[1][round - 2];
    }
    const Trial relative = summarize(ratios);
    std::cout << path.filename().string() << ',' << png.size() << ',' << layout.row_storage_bytes;
    for (unsigned mode = 0; mode < decoders.size(); ++mode) {
        const Trial trial = summarize(samples[mode]);
        std::cout << ',' << trial.median << ',' << trial.minimum << ',' << trial.maximum << ',' << iterations[mode];
    }
    std::cout << ',' << relative.median << ',' << relative.minimum << ',' << relative.maximum << '\n';
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        std::vector<std::filesystem::path> paths{};
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(argv[1])) {
            if (entry.is_regular_file()) paths.push_back(entry.path());
        }
        std::sort(paths.begin(), paths.end());
        std::cout << "workload,encoded_bytes,row_storage_bytes,owned_mib_s,owned_min_mib_s,owned_max_mib_s,owned_iterations,"
            "baseline_mib_s,baseline_min_mib_s,baseline_max_mib_s,baseline_iterations,"
            "libpng_mib_s,libpng_min_mib_s,libpng_max_mib_s,libpng_iterations,"
            "paired_speedup,paired_min_speedup,paired_max_speedup\n";
        std::cout << std::fixed << std::setprecision(4);
        for (const std::filesystem::path& path : paths) benchmark(path);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
