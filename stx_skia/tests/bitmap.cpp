#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImage.h"
#include "include/core/SkPixelRef.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"

#include <array>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>

namespace {

template <typename T>
concept TemporaryBitmapInfo = requires(T&& value) { std::move(value).info(); };
template <typename T>
concept TemporaryBitmapPixmap = requires(T&& value) { std::move(value).pixmap(); };
// API item 8: peekPixels returns the view instead of filling an out-parameter.
template <typename T>
concept PeeksIntoOutParameter = requires(T& value, SkPixmap* pixmap) { value.peekPixels(pixmap); };
template <typename T>
concept PeeksAsOptional = requires(T& value) {
    requires std::same_as<decltype(value.peekPixels()), std::optional<SkPixmap>>;
};
#ifndef STX_SKIA_REFERENCE
static_assert(!TemporaryBitmapInfo<SkBitmap>);
static_assert(!TemporaryBitmapPixmap<SkBitmap>);
static_assert(PeeksAsOptional<const SkBitmap> && !PeeksIntoOutParameter<const SkBitmap>);
static_assert(PeeksAsOptional<SkCanvas> && !PeeksIntoOutParameter<SkCanvas>);
static_assert(PeeksAsOptional<const SkImage> && !PeeksIntoOutParameter<const SkImage>);
static_assert(PeeksAsOptional<SkSurface> && !PeeksIntoOutParameter<SkSurface>);
#else
static_assert(PeeksIntoOutParameter<const SkBitmap> && PeeksIntoOutParameter<SkSurface>);
#endif

struct ReleaseCount {
    int calls = 0;
    void* pixels = nullptr;
};

void CountRelease(void* pixels, void* context) {
    ReleaseCount& count = *static_cast<ReleaseCount*>(context);
    ++count.calls;
    count.pixels = pixels;
}

class RejectAllocation final : public SkBitmap::Allocator {
public:
    bool allocPixelRef(SkBitmap*) override { return false; }
};

bool ownership() {
    std::array<uint32_t, 16> pixels{};
    ReleaseCount count{};
    const SkImageInfo info = SkImageInfo::MakeN32Premul(4, 4);
    SkBitmap original{};
    if (!original.installPixels(info, pixels.data(), 4 * sizeof(uint32_t), CountRelease, &count)) {
        return false;
    }
    SkBitmap copy(original);
    SkBitmap moved(std::move(original));
    if (original.getPixels() != nullptr || !original.empty() || count.calls != 0) return false;
    if (copy.getPixels() != moved.getPixels() || copy.pixelRef() != moved.pixelRef()) return false;
    moved = std::move(moved);
    copy = copy;
    SkBitmap destination{};
    destination = std::move(moved);
    if (!moved.empty() || moved.pixelRef() != nullptr) return false;
    copy.reset();
    if (count.calls != 0) return false;
    destination.reset();
    if (count.calls != 1 || count.pixels != pixels.data()) return false;

    if (!destination.installPixels(info, pixels.data(), 4 * sizeof(uint32_t), CountRelease, &count)) {
        return false;
    }
    // setInfo accepts metadata borrowed from the bitmap it is resetting.
    if (!destination.setInfo(destination.info(), destination.rowBytes())) return false;
    if (destination.dimensions() != info.dimensions() || destination.getPixels() != nullptr ||
        count.calls != 2) return false;
    return true;
}

bool metadata() {
    SkBitmap bitmap{};
    const SkImageInfo rgb565 = SkImageInfo::Make(3, 2, kRGB_565_SkColorType, kPremul_SkAlphaType);
    if (!bitmap.setInfo(rgb565) || bitmap.alphaType() != kOpaque_SkAlphaType ||
        bitmap.rowBytes() != 6) return false;
    if (!bitmap.setAlphaType(kUnknown_SkAlphaType) || bitmap.alphaType() != kOpaque_SkAlphaType) return false;
    if (!bitmap.setAlphaType(kUnpremul_SkAlphaType) || bitmap.alphaType() != kOpaque_SkAlphaType) {
        return false;
    }
    sk_sp<SkColorSpace> colorSpace = SkColorSpace::MakeSRGB();
    bitmap.setColorSpace(colorSpace);
    if (bitmap.colorSpace() != colorSpace.get()) return false;
    SkBitmap copy(bitmap);
    copy.setColorSpace(nullptr);
    if (bitmap.colorSpace() == nullptr || copy.colorSpace() != nullptr) return false;
    const SkImageInfo bad = SkImageInfo::MakeN32Premul(-1, 2);
    if (bitmap.setInfo(bad) || !bitmap.empty() || !bitmap.isNull()) return false;
    const SkImageInfo valid = SkImageInfo::MakeN32Premul(2, 2);
    if (bitmap.setInfo(valid, std::numeric_limits<size_t>::max()) || !bitmap.empty()) return false;
    ReleaseCount failed{};
    std::array<uint32_t, 4> pixels{};
    if (bitmap.installPixels(valid, pixels.data(), 1, CountRelease, &failed) || failed.calls != 1) {
        return false;
    }
    if (!bitmap.installPixels(valid, nullptr, 8, CountRelease, &failed) || failed.calls != 2 ||
        bitmap.width() != 2 || bitmap.getPixels() != nullptr) return false;
    return true;
}

bool subset_and_transfer() {
    SkBitmap bitmap{};
    if (!bitmap.tryAllocN32Pixels(4, 4)) return false;
    bitmap.eraseColor(SK_ColorRED);
    SkBitmap subset{};
    if (!bitmap.extractSubset(&subset, SkIRect::MakeXYWH(1, 1, 2, 2))) return false;
    if (subset.getPixels() != bitmap.getAddr32(1, 1) ||
        subset.pixelRefOrigin() != SkIPoint{1, 1} || subset.rowBytes() != bitmap.rowBytes()) {
        return false;
    }
    subset.eraseColor(SK_ColorBLUE);
    if (bitmap.getColor(1, 1) != SK_ColorBLUE || bitmap.getColor(0, 0) != SK_ColorRED) return false;
    if (!subset.extractSubset(&subset, SkIRect::MakeXYWH(1, 1, 1, 1)) ||
        subset.pixelRefOrigin() != SkIPoint{2, 2}) return false;
    std::array<uint32_t, 4> destination{};
    const SkImageInfo info = SkImageInfo::MakeN32Premul(2, 2);
    const SkPixmap output(info, destination.data(), 8);
    if (!bitmap.readPixels(output, 1, 1)) return false;
    SkBitmap copy{};
    if (!copy.tryAllocN32Pixels(2, 2) || !copy.writePixels(output)) return false;
    if (copy.getColor(0, 0) != SK_ColorBLUE || copy.getColor(1, 1) != SK_ColorBLUE) return false;
    bitmap.reset();
    if (subset.getColor(0, 0) != SK_ColorBLUE) return false;
    return true;
}

bool alpha() {
    SkBitmap source{};
    if (!source.tryAllocN32Pixels(2, 3)) return false;
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const uint8_t coverage = static_cast<uint8_t>(40 * y + 15 * x);
            const SkIRect pixel = SkIRect::MakeXYWH(x, y, 1, 1);
            source.erase(SkColorSetARGB(coverage, 0, 0, 0), pixel);
        }
    }
    SkBitmap result{};
    if (!source.extractAlpha(&result)) return false;
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            if (*result.getAddr8(x, y) != 40 * y + 15 * x) return false;
        }
    }
    RejectAllocation reject{};
    void* retainedPixels = result.getPixels();
    SkIPoint offset{7, 9};
    if (source.extractAlpha(&result, nullptr, &reject, &offset) ||
        result.getPixels() != retainedPixels || offset != SkIPoint{7, 9}) return false;
    if (!source.extractAlpha(&source) || source.colorType() != kAlpha_8_SkColorType) return false;
    if (*source.getAddr8(1, 2) != 95) return false;
    return true;
}
}

int main() {
    if (!ownership()) return 1;
    if (!metadata()) return 2;
    if (!subset_and_transfer()) return 3;
    if (!alpha()) return 4;
    std::puts("bitmap ownership, metadata, shared subsets, transfers and alpha: PASS");
    return 0;
}
