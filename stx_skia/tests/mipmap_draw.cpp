#include "include/core/SkColor.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "src/core/SkMipmap.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {
struct Output {
    std::FILE* file = nullptr;
    uint64_t hash = 1469598103934665603ULL;
    size_t bytes = 0;
    int cases = 0;
};

bool record(const SkPixmap& pixels, Output& output) {
    const size_t rowBytes = pixels.info().minRowBytes();
    for (int y = 0; y < pixels.height(); ++y) {
        const uint8_t* row = static_cast<const uint8_t*>(pixels.addr(0, y));
        if (output.file && std::fwrite(row, 1, rowBytes, output.file) != rowBytes) return false;
        for (size_t byte = 0; byte < rowBytes; ++byte) {
            output.hash ^= row[byte];
            output.hash *= 1099511628211ULL;
        }
        output.bytes += rowBytes;
    }
    return true;
}

bool reduction(int width, int height, SkColorType format, bool solid, Output& output) {
    const SkImageInfo info = SkImageInfo::Make(width, height, format, kPremul_SkAlphaType);
    const size_t sourceStride = info.minRowBytes() + 16;
    const size_t sourceExtent = sourceStride * (height - 1) + info.minRowBytes();
    std::vector<uint64_t> input((sourceExtent + 7) / 8, 0);
    const SkPixmap source(info, input.data(), sourceStride);
    if (!source.erase(SK_ColorGREEN)) return false;
    if (!solid) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const SkColor color = SkColorSetARGB((x * 31 + y * 17 + 127) & 255,
                    (x * 71 + y * 23) & 255, (x * 41 + y * 67) & 255, (x * 13 + y * 97) & 255);
                const SkIRect area = SkIRect::MakeXYWH(x, y, 1, 1);
                if (!source.erase(color, area)) return false;
            }
        }
    }
    const std::vector<uint64_t> original = input;
    sk_sp<SkMipmap> chain(SkMipmap::Build(source, nullptr));
    if (!chain) return false;
    SkMipmap::Level first{};
    if (!(*chain).getLevel(0, &first)) return false;
    const SkImageInfo reduced = first.fPixmap.info();
    const size_t outputStride = reduced.minRowBytes() + 16;
    const size_t outputExtent = outputStride * (reduced.height() - 1) + reduced.minRowBytes();
    std::vector<uint64_t> padded((outputExtent + 7) / 8, 0xababababababababULL);
    const SkPixmap destination(reduced, padded.data(), outputStride);
    std::unique_ptr<SkMipmapDownSampler> sampler = SkMipmap::MakeDownSampler(source);
    if (!sampler) return false;
    (*sampler).buildLevel(destination, source);
    for (int y = 0; y < reduced.height(); ++y) {
        if (std::memcmp(destination.addr(0, y), first.fPixmap.addr(0, y), reduced.minRowBytes()) != 0) {
            return false;
        }
        if (y + 1 < reduced.height()) {
            const uint8_t* row = static_cast<const uint8_t*>(destination.addr(0, y));
            for (size_t byte = reduced.minRowBytes(); byte < outputStride; ++byte) {
                if (row[byte] != 0xab) return false;
            }
        }
    }
    const int count = (*chain).countLevels();
    for (int index = 0; index < count; ++index) {
        SkMipmap::Level level{};
        if (!(*chain).getLevel(index, &level)) return false;
        if (solid) {
            const SkColor expected = source.getColor(0, 0);
            for (int y = 0; y < level.fPixmap.height(); ++y) {
                for (int x = 0; x < level.fPixmap.width(); ++x) {
                    if (level.fPixmap.getColor(x, y) != expected) return false;
                }
            }
        }
        if (!record(level.fPixmap, output)) return false;
    }
    if (input != original) return false;
    ++output.cases;
    return true;
}

bool selected_factory() {
    // A centered impulse in 3x3 samples is preserved by this cubic interpolator.
    // The default HQ triangle filter would produce red=63, detecting a mistaken
    // link to the default factory in this test executable.
    const SkImageInfo info = SkImageInfo::Make(3, 3, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    alignas(8) std::array<uint8_t, 36> storage{};
    const SkPixmap source(info, storage.data(), 12);
    if (!source.erase(SK_ColorBLACK)) return false;
    const SkIRect middle = SkIRect::MakeXYWH(1, 1, 1, 1);
    if (!source.erase(SK_ColorRED, middle)) return false;
    sk_sp<SkMipmap> chain(SkMipmap::Build(source, nullptr));
    if (!chain) return false;
    SkMipmap::Level level{};
    if (!(*chain).getLevel(0, &level)) return false;
    if (level.fPixmap.getColor(0, 0) != SK_ColorRED) return false;
#ifndef STX_SKIA_REFERENCE
    std::unique_ptr<SkMipmapDownSampler> sampler = SkMipmap::MakeDownSampler(source);
    const SkPixmap empty{};
    (*sampler).buildLevel(level.fPixmap, empty);
    if (level.fPixmap.getColor(0, 0) != SK_ColorRED) return false;
    (*sampler).buildLevel(empty, source);
#endif
    return true;
}
}

int main(int argc, char** argv) {
    if (!selected_factory()) return 1;
    Output output{};
    if (argc == 2) {
        output.file = std::fopen(argv[1], "wb");
        if (!output.file) return 2;
    }
    bool passed = true;
    constexpr std::array<SkColorType, 4> formats{
        kRGBA_8888_SkColorType, kBGRA_8888_SkColorType, kAlpha_8_SkColorType, kRGBA_F16_SkColorType};
    for (const SkColorType format : formats) {
        for (int height = 1; height <= 17; ++height) {
            for (int width = 1; width <= 17; ++width) {
                if (width == 1 && height == 1) continue;
                for (int solid = 0; solid < 2; ++solid) {
                    if (!reduction(width, height, format, solid != 0, output)) passed = false;
                }
            }
        }
    }
    if (output.file && std::fclose(output.file) != 0) passed = false;
    if (!passed) return 3;
    std::printf("drawing mipmaps: %d cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes, static_cast<unsigned long long>(output.hash));
    return 0;
}
