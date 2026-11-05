#include "stx_skia/pixel_storage.hpp"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "peek_pixels.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

namespace {
bool verify_transfer() {
    std::vector<std::byte> pixels(128U * 128U * 4U, std::byte{0x5A});
    const void* original = pixels.data();
    sk_sp<SkData> data = stx_skia::take_pixel_data(pixels);
    if (!data || !pixels.empty() || (*data).data() != original) return false;
    sk_sp<SkData> subset = (*data).shareSubset(16, 128);
    if (!subset) return false;
    data.reset();
    pixels.assign(8192, std::byte{0x17});
    for (std::size_t i = 0; i < (*subset).size(); ++i) {
        if ((*subset).bytes()[i] != 0x5AU) return false;
    }
    subset.reset();
    const SkImageInfo info = SkImageInfo::MakeN32Premul(16, 16);
    pixels.assign(16U * 16U * 4U, std::byte{0xFF});
    data = stx_skia::take_pixel_data(pixels);
    sk_sp<SkImage> image = SkImages::RasterFromData(info, data, 16U * 4U);
    if (!image) return false;
    data.reset();
    const std::optional<SkPixmap> viewView = peek_pixels(*image);
    if (!viewView) return false;
    const SkPixmap& view = *viewView;
    const std::uint32_t* samples = view.addr32();
    for (int i = 0; i < 256; ++i) {
        if (samples[i] != 0xFFFFFFFFU) return false;
    }
    return true;
}

template<bool transfer>
double measure(std::size_t size, int repeats, std::uint64_t& checksum) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) {
        std::vector<std::byte> pixels(size, static_cast<std::byte>(i & 255));
        sk_sp<SkData> data;
        if constexpr (transfer) {
            data = stx_skia::take_pixel_data(pixels);
        } else {
            data = SkData::MakeWithCopy(pixels.data(), pixels.size());
        }
        if (!data) return -1.0;
        checksum += (*data).bytes()[size / 2U];
    }
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(end - start).count();
    return elapsed;
}
}

int main(int argc, char** argv) {
    if (!verify_transfer()) return 1;
    std::vector<std::byte> empty;
    sk_sp<SkData> data = stx_skia::take_pixel_data(empty);
    if (!data || (*data).size() != 0) return 2;
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) {
        std::uint64_t checksum = 0;
        const std::size_t sizes[4] = {4096, 65536, 1048576, 4194304};
        for (const std::size_t size : sizes) {
            const int repeats = 100;
            for (int trial = 0; trial < 7; ++trial) {
                double copied = 0;
                double transferred = 0;
                if (trial % 2 == 0) {
                    copied = measure<false>(size, repeats, checksum);
                    transferred = measure<true>(size, repeats, checksum);
                } else {
                    transferred = measure<true>(size, repeats, checksum);
                    copied = measure<false>(size, repeats, checksum);
                }
                std::printf("%zu,%d,%.9f,%.9f\n", size, trial, copied, transferred);
            }
        }
        std::printf("checksum=%llu\n", static_cast<unsigned long long>(checksum));
    }
    std::puts("PASS");
    return 0;
}
