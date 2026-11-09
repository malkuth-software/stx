#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "src/core/SkConvertPixels.h"
#include "src/core/SkRectMemcpy.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>

namespace {
struct CloseFile {
    void operator()(std::FILE* file) const {
        if (file != nullptr) std::fclose(file);
    }
};

struct Record {
    std::FILE* output = nullptr; // Borrowed; the file owner outlives this record.
    uint64_t hash = 1469598103934665603ULL;
    size_t cases = 0;
    size_t bytes = 0;
    bool append(const uint8_t* pixels, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            hash ^= pixels[i];
            hash *= 1099511628211ULL;
        }
        if (output != nullptr && std::fwrite(pixels, 1, count, output) != count) return false;
        bytes += count;
        ++cases;
        return true;
    }
};

// Own only the active extent plus guard bytes. In particular, the last row has
// no stride padding. Byte-array allocation supplies alignment for typed pixels.
struct Pixels {
    SkImageInfo info;
    size_t stride;
    size_t extent;
    std::unique_ptr<uint8_t[]> storage;
    Pixels(const SkImageInfo& image_info, size_t padding_pixels)
        : info(image_info),
          stride(info.minRowBytes() + padding_pixels * static_cast<size_t>(info.bytesPerPixel())),
          extent(info.computeByteSize(stride)),
          storage(new uint8_t[extent + 16]) {
        std::memset(storage.get(), 0xCD, extent + 16);
    }
    bool untouched_padding() const {
        const size_t active = info.minRowBytes();
        for (int y = 0; y + 1 < info.height(); ++y) {
            for (size_t x = active; x < stride; ++x) {
                if (storage[static_cast<size_t>(y) * stride + x] != 0xCD) return false;
            }
        }
        for (size_t x = extent; x < extent + 16; ++x) {
            if (storage[x] != 0xCD) return false;
        }
        return true;
    }
};

bool formats(Record& record) {
    constexpr int width = 17;
    constexpr int height = 3;
    std::array<float, width * height * 4> colors{};
    for (size_t i = 0; i < colors.size() / 4; ++i) {
        colors[i * 4] = static_cast<float>((i * 31) % 257) / 256.0f;
        colors[i * 4 + 1] = static_cast<float>((i * 71) % 257) / 256.0f;
        colors[i * 4 + 2] = static_cast<float>((i * 137) % 257) / 256.0f;
        colors[i * 4 + 3] = static_cast<float>(i % 9) / 8.0f;
    }
    const std::array<sk_sp<SkColorSpace>, 3> spaces = {
        nullptr, SkColorSpace::MakeSRGB(), SkColorSpace::MakeSRGBLinear()};
    const std::array<SkAlphaType, 3> alphas = {
        kOpaque_SkAlphaType, kPremul_SkAlphaType, kUnpremul_SkAlphaType};
    for (const sk_sp<SkColorSpace>& space : spaces) {
        const SkImageInfo input = SkImageInfo::Make(width, height, kRGBA_F32_SkColorType,
                                                   kUnpremul_SkAlphaType, space);
        for (int src_type = 1; src_type <= kLastEnum_SkColorType; ++src_type) {
            for (const SkAlphaType src_alpha : alphas) {
                const SkColorType src_color = static_cast<SkColorType>(src_type);
                const SkImageInfo src_info = SkImageInfo::Make(width, height, src_color, src_alpha, space);
                Pixels source(src_info, 3);
                if (!SkConvertPixels(source.info, source.storage.get(), source.stride,
                                      input, colors.data(), input.minRowBytes())) return false;
                if (!source.untouched_padding()) return false;
                for (int dst_type = 1; dst_type <= kLastEnum_SkColorType; ++dst_type) {
                    for (const SkAlphaType dst_alpha : alphas) {
                        const SkColorType dst_color = static_cast<SkColorType>(dst_type);
                        const SkImageInfo dst_info = SkImageInfo::Make(width, height, dst_color,
                                                                     dst_alpha, spaces[1]);
                        Pixels destination(dst_info, 5);
                        if (!SkConvertPixels(destination.info, destination.storage.get(), destination.stride,
                                             source.info, source.storage.get(), source.stride)) return false;
                        if (!destination.untouched_padding() || !source.untouched_padding()) return false;
                        if (!record.append(destination.storage.get(), destination.extent)) return false;
                    }
                }
            }
        }
    }
    return true;
}

bool rgba_rows(Record& record) {
    const std::array<int, 14> widths = {1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 63, 65};
    for (const int width : widths) {
        for (const int height : {1, 2, 5}) {
            const SkImageInfo rgba = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType,
                                                      kUnpremul_SkAlphaType);
            for (const size_t padding : {size_t{0}, size_t{3}}) {
                Pixels source(rgba, padding);
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        const uint32_t red = static_cast<uint32_t>((x * 11 + y * 17) % 256);
                        const uint32_t green = static_cast<uint32_t>((x * 23 + y * 31) % 256);
                        const uint32_t blue = static_cast<uint32_t>((x * 47 + y * 53) % 256);
                        const uint32_t alpha = static_cast<uint32_t>((x * 73 + y * 97) % 256);
                        const uint32_t pixel = red | (green << 8) | (blue << 16) | (alpha << 24);
                        const size_t offset = static_cast<size_t>(y) * source.stride + static_cast<size_t>(x) * 4;
                        std::memcpy(source.storage.get() + offset, &pixel, sizeof(pixel));
                    }
                }
                Pixels alpha(SkImageInfo::MakeA8(width, height), padding);
                Pixels bgra(rgba.makeColorType(kBGRA_8888_SkColorType), padding);
                Pixels copy(rgba, padding + 1);
                if (!SkConvertPixels(alpha.info, alpha.storage.get(), alpha.stride,
                                      source.info, source.storage.get(), source.stride)) return false;
                if (!SkConvertPixels(bgra.info, bgra.storage.get(), bgra.stride,
                                      source.info, source.storage.get(), source.stride)) return false;
                if (!SkConvertPixels(copy.info, copy.storage.get(), copy.stride,
                                      source.info, source.storage.get(), source.stride)) return false;
                for (int y = 0; y < height; ++y) {
                    const size_t row = static_cast<size_t>(y);
                    for (int x = 0; x < width; ++x) {
                        const size_t column = static_cast<size_t>(x);
                        uint32_t original = 0;
                        uint32_t swapped = 0;
                        std::memcpy(&original, source.storage.get() + row * source.stride + column * 4, 4);
                        std::memcpy(&swapped, bgra.storage.get() + row * bgra.stride + column * 4, 4);
                        const uint32_t expected = (original & 0xFF00FF00U) |
                                                  ((original & 0xFFU) << 16) | ((original >> 16) & 0xFFU);
                        if (swapped != expected || alpha.storage[row * alpha.stride + column] != original >> 24) return false;
                    }
                    if (std::memcmp(copy.storage.get() + row * copy.stride,
                                    source.storage.get() + row * source.stride, rgba.minRowBytes()) != 0) return false;
                }
                if (!copy.untouched_padding() || !alpha.untouched_padding() || !bgra.untouched_padding()) return false;
                if (!record.append(alpha.storage.get(), alpha.extent) ||
                    !record.append(bgra.storage.get(), bgra.extent) ||
                    !record.append(copy.storage.get(), copy.extent)) return false;
                // Equal-size in-place swizzling must produce the same active rows.
                if (!SkConvertPixels(bgra.info, source.storage.get(), source.stride,
                                      source.info, source.storage.get(), source.stride)) return false;
                for (int y = 0; y < height; ++y) {
                    const size_t row = static_cast<size_t>(y);
                    if (std::memcmp(source.storage.get() + row * source.stride,
                                    bgra.storage.get() + row * bgra.stride, rgba.minRowBytes()) != 0) return false;
                }
            }
        }
    }
    // One-row stride-zero arrays are a real internal color-conversion contract.
    std::array<uint32_t, 3> packed = {0xFF000000U, 0xFF112233U, 0xFFFFFFFFU};
    std::array<float, 12> floating{};
    const SkImageInfo packed_info = SkImageInfo::Make(3, 1, kBGRA_8888_SkColorType, kUnpremul_SkAlphaType);
    const SkImageInfo float_info = packed_info.makeColorType(kRGBA_F32_SkColorType);
    if (!SkConvertPixels(float_info, floating.data(), 0, packed_info, packed.data(), 0)) return false;
    if (floating[0] != 0.0f || floating[3] != 1.0f || floating[8] != 1.0f) return false;
    return true;
}

bool exact_extents() {
    for (const int width : {1, 3, 7, 16, 17, 33}) {
        const SkImageInfo src_info = SkImageInfo::Make(width, 3, kRGBA_8888_SkColorType,
                                                      kUnpremul_SkAlphaType);
        const SkImageInfo dst_info = src_info.makeColorType(kBGRA_8888_SkColorType);
        const size_t stride = src_info.minRowBytes() + 12;
        const size_t extent = src_info.computeByteSize(stride);
        std::unique_ptr<uint8_t[]> src(new uint8_t[extent]);
        std::unique_ptr<uint8_t[]> dst(new uint8_t[extent]);
        std::memset(src.get(), 0x77, extent);
        std::memset(dst.get(), 0, extent);
        if (!SkConvertPixels(dst_info, dst.get(), stride, src_info, src.get(), stride)) return false;
        const SkImageInfo alpha_info = SkImageInfo::MakeA8(width, 3);
        const size_t alpha_stride = static_cast<size_t>(width) + 3;
        const size_t alpha_extent = alpha_info.computeByteSize(alpha_stride);
        std::unique_ptr<uint8_t[]> alpha(new uint8_t[alpha_extent]);
        if (!SkConvertPixels(alpha_info, alpha.get(), alpha_stride, src_info, src.get(), stride)) return false;
        const size_t last = static_cast<size_t>(2) * alpha_stride + static_cast<size_t>(width - 1);
        if (alpha[last] != 0x77) return false;
    }
    return true;
}

bool benchmark() {
    struct Case {
        const char* name;
        SkColorType color;
        SkAlphaType alpha;
        bool linear;
    };
    const std::array<Case, 5> cases = {{
        {"copy", kRGBA_8888_SkColorType, kUnpremul_SkAlphaType, false},
        {"swap", kBGRA_8888_SkColorType, kUnpremul_SkAlphaType, false},
        {"premul", kRGBA_8888_SkColorType, kPremul_SkAlphaType, false},
        {"alpha", kAlpha_8_SkColorType, kPremul_SkAlphaType, false},
        {"linear", kRGBA_8888_SkColorType, kPremul_SkAlphaType, true}
    }};
    for (const int width : {32, 256, 1920}) {
        const int height = width == 1920 ? 1080 : width;
        const int repeats = width == 32 ? 10000 : (width == 256 ? 300 : 10);
        const SkImageInfo input = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType,
                                                   kUnpremul_SkAlphaType, SkColorSpace::MakeSRGB());
        Pixels source(input, 3);
        std::memset(source.storage.get(), 0x87, source.extent);
        for (const Case& item : cases) {
            sk_sp<SkColorSpace> space = item.linear ? SkColorSpace::MakeSRGBLinear() : SkColorSpace::MakeSRGB();
            const SkImageInfo output = SkImageInfo::Make(width, height, item.color, item.alpha, space);
            Pixels destination(output, 3);
            if (!SkConvertPixels(output, destination.storage.get(), destination.stride,
                                  input, source.storage.get(), source.stride)) return false;
            for (int trial = 0; trial < 5; ++trial) {
                const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
                for (int repeat = 0; repeat < repeats; ++repeat) {
                    if (!SkConvertPixels(output, destination.storage.get(), destination.stride,
                                          input, source.storage.get(), source.stride)) return false;
                }
                const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = end - start;
                const double seconds = elapsed.count();
                std::printf("%s,%d,%d,%d,%d,%.9f\n", item.name, width, height, repeats, trial, seconds);
            }
            if (!destination.untouched_padding()) return false;
        }
    }
    return true;
}

#ifndef STX_SKIA_REFERENCE
bool rejected_inputs() {
    const SkImageInfo info = SkImageInfo::MakeN32Premul(3, 2);
    std::array<uint32_t, 8> src{};
    std::array<uint32_t, 8> dst{};
    dst.fill(0xCDCDCDCDU);
    const std::array<uint32_t, 8> untouched = dst;
    const SkImageInfo invalid_color = info.makeColorType(static_cast<SkColorType>(kLastEnum_SkColorType + 1));
    const SkImageInfo invalid_alpha = info.makeAlphaType(static_cast<SkAlphaType>(kLastEnum_SkAlphaType + 1));
    const SkImageInfo unknown = info.makeColorType(kUnknown_SkColorType);
    for (const SkImageInfo& bad : {invalid_color, invalid_alpha, unknown}) {
        if (SkConvertPixels(bad, dst.data(), 12, info, src.data(), 12)) return false;
        if (SkConvertPixels(info, dst.data(), 12, bad, src.data(), 12)) return false;
    }
    if (SkConvertPixels(info.makeWH(2, 2), dst.data(), 12, info, src.data(), 12)) return false;
    if (SkConvertPixels(info, dst.data(), 12, info, nullptr, 12)) return false;
    if (SkConvertPixels(info, nullptr, 12, info, src.data(), 12)) return false;
    for (const size_t stride : {size_t{0}, size_t{8}, size_t{13}, std::numeric_limits<size_t>::max() - 3}) {
        if (SkConvertPixels(info, dst.data(), stride, info, src.data(), 12)) return false;
        if (SkConvertPixels(info, dst.data(), 12, info, src.data(), stride)) return false;
    }
    if (dst != untouched) return false;
    const SkImageInfo row = info.makeWH(3, 1);
    if (!SkConvertPixels(row, dst.data(), 0, row, src.data(), 0)) return false;
    if (!SkConvertPixels(row, src.data(), 0, row, src.data(), 0)) return false;
    SkRectMemcpy(nullptr, 0, nullptr, 0, 0, 3);
    SkRectMemcpy(nullptr, 0, nullptr, 0, 4, 0);
    return true;
}
#endif
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) {
        const bool success = benchmark();
        return success ? 0 : 1;
    }
    std::unique_ptr<std::FILE, CloseFile> file;
    if (argc == 2) {
        file.reset(std::fopen(argv[1], "wb"));
        if (!file) return 2;
    }
    Record record{.output = file.get()};
    if (!formats(record) || !rgba_rows(record) || !exact_extents()) return 1;
#ifndef STX_SKIA_REFERENCE
    if (!rejected_inputs()) return 1;
#endif
    if (file && std::fflush(file.get()) != 0) return 2;
    std::printf("pixel conversions: %zu cases, %zu bytes, hash=%llu PASS\n",
                record.cases, record.bytes, static_cast<unsigned long long>(record.hash));
    return 0;
}
