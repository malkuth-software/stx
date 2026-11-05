#include "include/core/SkColorSpace.h"
#include "include/core/SkPixmap.h"
#include "src/core/SkConvertPixels.h"
#include "src/core/SkMask.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#ifndef STX_SKIA_REFERENCE
template <typename T>
concept CanBorrowPixmapInfo = requires(T&& view) { std::forward<T>(view).info(); };
static_assert(CanBorrowPixmapInfo<SkPixmap&>);
static_assert(CanBorrowPixmapInfo<const SkPixmap&>);
static_assert(!CanBorrowPixmapInfo<SkPixmap>);
#endif

namespace {
struct CloseFile {
    void operator()(std::FILE* file) const {
        if (file != nullptr) std::fclose(file);
    }
};
struct Record {
    std::FILE* file = nullptr;
    uint64_t hash = 1469598103934665603ULL;
    size_t bytes = 0;
    bool append(const void* values, size_t length) {
        const uint8_t* data = static_cast<const uint8_t*>(values);
        for (size_t i = 0; i < length; ++i) {
            hash ^= data[i];
            hash *= 1099511628211ULL;
        }
        if (file != nullptr && std::fwrite(values, 1, length, file) != length) return false;
        bytes += length;
        return true;
    }
};

bool color_and_fill(Record& record) {
    constexpr int width = 13;
    constexpr int height = 7;
    std::array<float, width * height * 4> source{};
    const std::array<SkAlphaType, 3> alphas = {kOpaque_SkAlphaType, kPremul_SkAlphaType, kUnpremul_SkAlphaType};
    for (int type = 1; type <= kLastEnum_SkColorType; ++type) {
        const SkColorType format = static_cast<SkColorType>(type);
        for (const SkAlphaType alpha : alphas) {
            for (size_t i = 0; i < source.size() / 4; ++i) {
                source[4 * i] = static_cast<float>(i % 9) / 8.0f;
                source[4 * i + 1] = static_cast<float>((i * 3) % 9) / 8.0f;
                source[4 * i + 2] = static_cast<float>((i * 7) % 9) / 8.0f;
                source[4 * i + 3] = alpha == kOpaque_SkAlphaType ? 1.0f : static_cast<float>(i % 5) / 4.0f;
            }
            const SkImageInfo input = SkImageInfo::Make(width, height, kRGBA_F32_SkColorType, kUnpremul_SkAlphaType);
            const SkImageInfo info = SkImageInfo::Make(width, height, format, alpha);
            const size_t stride = info.minRowBytes() + 3 * static_cast<size_t>(info.bytesPerPixel());
            const size_t extent = info.computeByteSize(stride);
            std::unique_ptr<uint8_t[]> pixels(new uint8_t[extent]);
            std::memset(pixels.get(), 0xCC, extent);
            SkPixmap view(info, pixels.get(), stride);
            if (!SkConvertPixels(info, pixels.get(), stride, input, source.data(), input.minRowBytes())) return false;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const float a = view.getAlphaf(x, y);
                    if (!record.append(&a, sizeof(a))) return false;
                    // Upstream does not implement color getters for these XR formats.
                    if (format != kBGR_101010x_XR_SkColorType && format != kBGRA_10101010_XR_SkColorType) {
                        const SkColor packed = view.getColor(x, y);
                        const SkColor4f color = view.getColor4f(x, y);
                        if (!record.append(&packed, sizeof(packed))) return false;
                        // RGBA1010102 float channel order is an intentional correction;
                        // its independent expected-value test is below.
                        if (format != kRGBA_1010102_SkColorType && !record.append(&color, sizeof(color))) return false;
                    }
                    const size_t offset = static_cast<size_t>(y) * stride + static_cast<size_t>(x) * info.bytesPerPixel();
                    if (view.addr(x, y) != pixels.get() + offset) return false;
                }
            }
            const uint8_t opaque = view.computeIsOpaque() ? 1 : 0;
            if (!record.append(&opaque, sizeof(opaque))) return false;
            const std::array<SkIRect, 4> clips = {
                SkIRect::MakeXYWH(-2, -1, 7, 5), SkIRect::MakeXYWH(4, 3, 20, 20),
                SkIRect::MakeWH(width, height), SkIRect::MakeXYWH(20, 20, 2, 2)};
            for (const SkIRect& clip : clips) {
                const SkColor4f fill = {0.25f, 0.5f, 0.75f, 0.5f};
                const bool changed = view.erase(fill, &clip);
                const bool intersects = clip.fLeft < width && clip.fTop < height && clip.fRight > 0 && clip.fBottom > 0;
                if (changed != intersects) return false;
                for (int y = 0; y + 1 < height; ++y) {
                    for (size_t x = info.minRowBytes(); x < stride; ++x) {
                        if (pixels[static_cast<size_t>(y) * stride + x] != 0xCC) return false;
                    }
                }
                if (!record.append(pixels.get(), extent)) return false;
            }
            if (!view.erase(SK_ColorWHITE) || !view.computeIsOpaque()) return false;
            if (!record.append(pixels.get(), extent)) return false;
        }
    }
    return true;
}

bool subsets_and_reads(Record& record) {
    constexpr int width = 9;
    constexpr int height = 6;
    constexpr size_t stride = 44;
    const SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    alignas(uint32_t) std::array<uint8_t, (height - 1) * stride + width * 4> pixels{};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t offset = static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
            pixels[offset] = static_cast<uint8_t>(x * 20);
            pixels[offset + 1] = static_cast<uint8_t>(y * 30);
            pixels[offset + 2] = 73;
            pixels[offset + 3] = 255;
        }
    }
    SkPixmap view(info, pixels.data(), stride);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const SkColor expected = SkColorSetARGB(255, x * 20, y * 30, 73);
            if (view.getColor(x, y) != expected || view.getAlphaf(x, y) != 1.0f) return false;
        }
    }
    for (int y = -4; y <= height + 1; ++y) {
        for (int x = -5; x <= width + 1; ++x) {
            const SkIRect requested = SkIRect::MakeXYWH(x, y, 5, 4);
            SkPixmap subset;
            const bool intersects = x < width && y < height && x + 5 > 0 && y + 4 > 0;
            if (view.extractSubset(&subset, requested) != intersects) return false;
            const int left = x < 0 ? 0 : x;
            const int top = y < 0 ? 0 : y;
            const int right = x + 5 > width ? width : x + 5;
            const int bottom = y + 4 > height ? height : y + 4;
            if (intersects) {
                if (subset.width() != right - left || subset.height() != bottom - top || subset.rowBytes() != stride) return false;
                if (subset.addr() != view.addr(left, top)) return false;
            } else if (!subset.isEmpty() || subset.addr() != nullptr) return false;
            alignas(uint32_t) std::array<uint8_t, 4 * 24> destination{};
            destination.fill(0xCD);
            const SkImageInfo dst_info = info.makeWH(5, 4);
            if (view.readPixels(dst_info, destination.data(), 24, x, y) != intersects) return false;
            for (int dy = 0; dy < 4; ++dy) {
                for (int dx = 0; dx < 5; ++dx) {
                    const int sx = x + dx;
                    const int sy = y + dy;
                    const bool copied = sx >= 0 && sx < width && sy >= 0 && sy < height;
                    for (int channel = 0; channel < 4; ++channel) {
                        const size_t target = static_cast<size_t>(dy * 24 + dx * 4 + channel);
                        if (copied) {
                            const size_t original = static_cast<size_t>(sy) * stride + static_cast<size_t>(sx * 4 + channel);
                            if (destination[target] != pixels[original]) return false;
                        } else if (destination[target] != 0xCD) return false;
                    }
                }
                for (int pad = 20; pad < 24; ++pad) {
                    if (destination[static_cast<size_t>(dy * 24 + pad)] != 0xCD) return false;
                }
            }
            if (!record.append(destination.data(), destination.size())) return false;
        }
    }
    // The result may be the source view itself; snapshot metadata before reset.
    SkPixmap in_place = view;
    if (!in_place.extractSubset(&in_place, SkIRect::MakeXYWH(2, 1, 4, 3))) return false;
    if (in_place.addr() != view.addr(2, 1) || in_place.width() != 4 || in_place.height() != 3) return false;
    SkPixmap metadata(info, nullptr, stride);
    SkPixmap subset;
    if (!metadata.extractSubset(&subset, SkIRect::MakeXYWH(1, 1, 2, 2)) || subset.addr() != nullptr) return false;
    sk_sp<SkColorSpace> space = SkColorSpace::MakeSRGB();
    view.setColorSpace(space);
    sk_sp<SkColorSpace> retained = view.refColorSpace();
    if (view.colorSpace() != space.get() || retained.get() != space.get()) return false;
    return true;
}

bool scaling(Record& record) {
    const std::array<SkSamplingOptions, 5> samplings = {
        SkSamplingOptions(SkFilterMode::kNearest), SkSamplingOptions(SkFilterMode::kLinear),
        SkSamplingOptions(SkCubicResampler::Mitchell()), SkSamplingOptions(SkCubicResampler::CatmullRom()),
        SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear)};
    for (const SkAlphaType alpha : {kPremul_SkAlphaType, kUnpremul_SkAlphaType}) {
        const SkImageInfo info = SkImageInfo::Make(9, 7, kRGBA_8888_SkColorType, alpha);
        std::array<uint32_t, 9 * 7> pixels{};
        SkPixmap source(info, pixels.data(), 9 * 4);
        if (!source.erase(SK_ColorBLUE)) return false;
        if (!source.erase(0x8055AA00U, SkIRect::MakeXYWH(2, 1, 4, 5))) return false;
        for (const SkSamplingOptions& sampling : samplings) {
            for (const int width : {3, 9, 17}) {
                const int height = width == 9 ? 7 : width + 2;
                const SkImageInfo dst_info = info.makeWH(width, height);
                const size_t row_bytes = dst_info.minRowBytes() + 12;
                const size_t extent = dst_info.computeByteSize(row_bytes);
                std::unique_ptr<uint8_t[]> output(new uint8_t[extent]);
                std::memset(output.get(), 0xCE, extent);
                SkPixmap destination(dst_info, output.get(), row_bytes);
                if (!source.scalePixels(destination, sampling)) return false;
                for (int y = 0; y + 1 < height; ++y) {
                    for (size_t x = dst_info.minRowBytes(); x < row_bytes; ++x) {
                        if (output[static_cast<size_t>(y) * row_bytes + x] != 0xCE) return false;
                    }
                }
                if (!record.append(output.get(), extent)) return false;
            }
        }
    }
    return true;
}

bool masks_and_failures() {
    std::array<uint8_t, 15> pixels{};
    const SkMask mask(pixels.data(), SkIRect::MakeXYWH(-7, 10, 5, 3), 5, SkMask::kA8_Format);
    SkPixmap view;
    if (!view.reset(mask) || view.width() != 5 || view.height() != 3 || view.addr() != pixels.data()) return false;
    const SkMask other(pixels.data(), mask.fBounds, 1, SkMask::kBW_Format);
    if (view.reset(other) || !view.isEmpty() || view.addr() != nullptr) return false;
#ifndef STX_SKIA_REFERENCE
    for (const SkColorType format : {kRGBA_1010102_SkColorType, kBGRA_1010102_SkColorType}) {
        const std::array<uint32_t, 2> packed = {3U << 30 | 1023U, 3U << 30 | (1023U << 20)};
        const SkImageInfo packed_info = SkImageInfo::Make(2, 1, format, kUnpremul_SkAlphaType);
        const SkPixmap packed_view(packed_info, packed.data(), 8);
        const SkColor4f first = packed_view.getColor4f(0, 0);
        const SkColor4f second = packed_view.getColor4f(1, 0);
        const bool rgba = format == kRGBA_1010102_SkColorType;
        if (first.fR != (rgba ? 1.0f : 0.0f) || first.fB != (rgba ? 0.0f : 1.0f)) return false;
        if (second.fR != first.fB || second.fB != first.fR || first.fG != 0.0f || first.fA != 1.0f) return false;
    }
    if (view.addr(0, 0) != nullptr || view.computeIsOpaque() || view.erase(SK_ColorRED)) return false;
    const SkImageInfo info = SkImageInfo::MakeA8(5, 3);
    SkPixmap missing(info, nullptr, 5);
    if (missing.computeIsOpaque() || missing.erase(SK_ColorRED)) return false;
    if (missing.readPixels(info, pixels.data(), 5)) return false;
    const SkPixmap empty(info.makeWH(0, 3), nullptr, 0);
    if (!empty.computeIsOpaque() || empty.erase(SK_ColorRED)) return false;
    if (missing.extractSubset(nullptr, SkIRect::MakeWH(2, 2))) return false;
    SkPixmap invalid(info, pixels.data(), 2);
    if (invalid.erase(SK_ColorRED) || invalid.computeIsOpaque()) return false;
    const SkPixmap invalid_format(info.makeColorType(static_cast<SkColorType>(kLastEnum_SkColorType + 1)), pixels.data(), 5);
    if (invalid_format.erase(SK_ColorRED) || invalid_format.computeIsOpaque()) return false;
#endif
    return true;
}
}

int main(int argc, char** argv) {
    std::unique_ptr<std::FILE, CloseFile> file;
    if (argc == 2) {
        file.reset(std::fopen(argv[1], "wb"));
        if (!file) return 2;
    }
    Record record{.file = file.get()};
    if (!color_and_fill(record)) { std::puts("color/fill failed"); return 1; }
    if (!subsets_and_reads(record)) { std::puts("subset/read failed"); return 1; }
    if (!scaling(record)) { std::puts("scaling failed"); return 1; }
    if (!masks_and_failures()) { std::puts("mask/failure failed"); return 1; }
    if (file && std::fflush(file.get()) != 0) return 2;
    std::printf("pixmap: %zu bytes hash=%llu PASS\n", record.bytes, static_cast<unsigned long long>(record.hash));
    return 0;
}
