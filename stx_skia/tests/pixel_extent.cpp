#include "include/core/SkData.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/private/SkMalloc.h"
#include "src/core/SkAutoPixmapStorage.h"
#include "src/core/SkReadPixelsRec.h"
#include "src/core/SkSafeMath.h"
#include "src/core/SkWritePixelsRec.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace {
bool arithmetic() {
    constexpr size_t maximum = std::numeric_limits<size_t>::max();
    std::array<size_t, 3 * sizeof(size_t) * 8 + 2> values{};
    size_t count = 0;
    values[count++] = 0;
    values[count++] = maximum;
    for (unsigned int bit = 0; bit < sizeof(size_t) * 8; ++bit) {
        const size_t power = size_t{1} << bit;
        values[count++] = power - 1;
        values[count++] = power;
        values[count++] = power + 1;
    }
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < count; ++j) {
            const size_t x = values[i];
            const size_t y = values[j];
            SkSafeMath product;
            const size_t value = product.mul(x, y);
            const bool fits = y == 0 || x <= maximum / y;
            if (product.ok() != fits || value != x * y) return false;
            const size_t saturated = SkSafeMath::Mul(x, y);
            if (saturated != (fits ? value : maximum)) return false;
            SkSafeMath sum;
            const size_t added = sum.add(x, y);
            const bool sum_fits = x <= maximum - y;
            if (sum.ok() != sum_fits || added != x + y) return false;
            if (SkSafeMath::Add(x, y) != (sum_fits ? added : maximum)) return false;
            SkSafeMath difference;
            const size_t subtracted = difference.sub(x, y);
            if (difference.ok() != (x >= y) || subtracted != x - y) return false;
        }
    }
    SkSafeMath sticky;
    sticky.add(maximum, 1);
    if (sticky.add(1, 2) != 3 || sticky.ok()) return false;
    if (SkSafeMath::Align4(maximum) != 0) return false;
    for (size_t alignment = 1; alignment <= 32; ++alignment) {
        for (size_t value = 0; value < 100; ++value) {
            SkSafeMath math;
            const size_t aligned = math.alignUpNonPow2(value, alignment);
            const size_t expected = ((value + alignment - 1) / alignment) * alignment;
            if (!math.ok() || aligned != expected) return false;
            if ((alignment & (alignment - 1)) == 0 && math.alignUp(value, alignment) != expected) return false;
        }
    }
    SkSafeMath zero_alignment;
    if (zero_alignment.alignUpNonPow2(7, 0) != 0 || zero_alignment.ok()) return false;
    SkSafeMath lcm;
    if (lcm.lcm(21, 6) != 42 || !lcm.ok()) return false;
    const std::array<int, 7> integers = {std::numeric_limits<int>::min(), -100, -1, 0, 1, 100,
                                       std::numeric_limits<int>::max()};
    for (const int a : integers) {
        for (const int b : integers) {
            const int64_t wide_a = a;
            const int64_t wide_b = b;
            const std::array<int64_t, 3> expected = {wide_a + wide_b, wide_a - wide_b, wide_a * wide_b};
            std::array<SkSafeMath, 3> checks{};
            const std::array<int, 3> actual = {checks[0].addInt(a, b), checks[1].subInt(a, b), checks[2].mulInt(a, b)};
            for (size_t i = 0; i < expected.size(); ++i) {
                const bool fits = expected[i] >= std::numeric_limits<int>::min() && expected[i] <= std::numeric_limits<int>::max();
                if (checks[i].ok() != fits || (fits && actual[i] != expected[i])) return false;
            }
            const bool divides = b != 0 && !(a == std::numeric_limits<int>::min() && b == -1);
            SkSafeMath division;
            SkSafeMath modulo;
            const int quotient = division.divInt(a, b);
            const int remainder = modulo.modInt(a, b);
            if (division.ok() != divides || modulo.ok() != divides) return false;
            if (divides && (quotient != a / b || remainder != a % b)) return false;
        }
    }
    return true;
}

bool image_extents() {
    for (int type = 0; type <= kLastEnum_SkColorType; ++type) {
        const SkColorType color = static_cast<SkColorType>(type);
        const int pixel_bytes = SkColorTypeBytesPerPixel(color);
        for (int width = 0; width <= 17; ++width) {
            for (int height = 0; height <= 9; ++height) {
                const SkImageInfo info = SkImageInfo::Make(width, height, color, kPremul_SkAlphaType);
                const size_t row_bytes = static_cast<size_t>(width * pixel_bytes);
                if (info.minRowBytes() != row_bytes || !info.validRowBytes(row_bytes)) return false;
                for (size_t padding = 0; padding <= 8; padding += 4) {
                    const size_t stride = row_bytes + padding;
                    const size_t expected = height == 0 ? 0 : static_cast<size_t>(height - 1) * stride + row_bytes;
                    if (info.computeByteSize(stride) != expected) return false;
                }
                if (info != info.makeDimensions(info.dimensions())) return false;
                const SkImageInfo changed = info.makeAlphaType(kOpaque_SkAlphaType);
                if (changed.width() != width || changed.height() != height || changed.alphaType() != kOpaque_SkAlphaType) return false;
            }
        }
    }
    const SkImageInfo srgb = SkImageInfo::MakeS32(7, 5, kPremul_SkAlphaType);
    sk_sp<SkColorSpace> space = SkColorSpace::MakeSRGB();
    const SkImageInfo explicit_srgb = SkImageInfo::MakeN32Premul(7, 5, space);
    if (srgb != explicit_srgb || !srgb.gammaCloseToSRGB()) return false;
    const SkColorInfo changed_alpha = srgb.colorInfo().makeAlphaType(kOpaque_SkAlphaType);
    if (!changed_alpha.isOpaque() || changed_alpha.colorSpace() != srgb.colorSpace()) return false;
    const SkColorInfo changed_type = changed_alpha.makeColorType(kAlpha_8_SkColorType);
    if (changed_type.bytesPerPixel() != 1 || changed_type.shiftPerPixel() != 0) return false;
    const SkImageInfo linear = srgb.makeColorSpace(SkColorSpace::MakeSRGBLinear());
    if (linear == srgb || linear.gammaCloseToSRGB()) return false;
    const SkImageInfo large = SkImageInfo::MakeN32Premul(std::numeric_limits<int>::max(), 3);
    if (!SkImageInfo::ByteSizeOverflowed(large.computeMinByteSize())) return false;
    const SkImageInfo one = SkImageInfo::MakeA8(std::numeric_limits<int>::max(), 1);
    if (one.computeMinByteSize() != static_cast<size_t>(std::numeric_limits<int>::max())) return false;
    const SkImageInfo two_rows = one.makeWH(one.width(), 2);
    if (!SkImageInfo::ByteSizeOverflowed(two_rows.computeMinByteSize())) return false;
#ifndef STX_SKIA_REFERENCE
    SkAlphaType unchanged = kUnpremul_SkAlphaType;
    if (SkColorTypeValidateAlphaType(static_cast<SkColorType>(kLastEnum_SkColorType + 1),
                                     kPremul_SkAlphaType, &unchanged)) return false;
    if (unchanged != kUnpremul_SkAlphaType) return false;
#endif
    return true;
}

bool clipped_records() {
    std::array<std::byte, 128> pixels{};
    const SkImageInfo info = SkImageInfo::MakeN32Premul(5, 4);
    for (int x = -7; x <= 9; ++x) {
        for (int y = -6; y <= 7; ++y) {
            SkReadPixelsRec read(info, pixels.data(), 24, x, y);
            SkWritePixelsRec write(info, pixels.data(), 24, x, y);
            const int left = x < 0 ? 0 : x;
            const int top = y < 0 ? 0 : y;
            const int right = x + 5 > 7 ? 7 : x + 5;
            const int bottom = y + 4 > 5 ? 5 : y + 4;
            const bool overlap = left < right && top < bottom;
            if (read.trim(7, 5) != overlap || write.trim(7, 5) != overlap) return false;
            if (overlap) {
                const size_t dx = static_cast<size_t>(x < 0 ? -x : 0);
                const size_t dy = static_cast<size_t>(y < 0 ? -y : 0);
                const void* expected = pixels.data() + dx * 4 + dy * 24;
                if (read.fPixels != expected || write.fPixels != expected) return false;
                if (read.fInfo.width() != right - left || read.fInfo.height() != bottom - top) return false;
                if (read.fInfo != write.fInfo || read.fX != left || read.fY != top) return false;
                if (write.fX != left || write.fY != top || read.fRowBytes != 24 || write.fRowBytes != 24) return false;
            } else {
                if (read.fPixels != pixels.data() || write.fPixels != pixels.data()) return false;
                if (read.fInfo != info || write.fInfo != info || read.fX != x || read.fY != y) return false;
                if (write.fX != x || write.fY != y) return false;
            }
        }
    }
    SkReadPixelsRec extreme(info, pixels.data(), 24, std::numeric_limits<int>::min(), 0);
    if (extreme.trim(7, 5) || extreme.fPixels != pixels.data() || extreme.fInfo != info) return false;
    SkWritePixelsRec overflow(info, pixels.data(), std::numeric_limits<size_t>::max(), -1, -1);
    if (overflow.trim(7, 5) || overflow.fPixels != pixels.data() || overflow.fX != -1) return false;
    return true;
}

bool owned_storage() {
    const SkImageInfo info = SkImageInfo::MakeN32Premul(7, 5, SkColorSpace::MakeSRGB());
    std::array<std::byte, 7 * 5 * 4> borrowed{};
    SkAutoPixmapStorage storage;
    if (!storage.tryAlloc(info)) return false;
    std::memset(storage.writable_addr(), 0x5A, storage.computeByteSize());
    const void* pixels = storage.addr();
    SkAutoPixmapStorage moved(std::move(storage));
    if (storage.addr() != nullptr || !storage.isEmpty() || moved.addr() != pixels) return false;
    sk_sp<SkData> data = moved.detachPixelsAsData();
    if (!data || (*data).data() != pixels || moved.addr() != nullptr || !moved.isEmpty()) return false;
    for (size_t index = 0; index < (*data).size(); ++index) {
        if ((*data).bytes()[index] != 0x5A) return false;
    }
    if (!storage.tryAlloc(info)) return false;
    void* detached = storage.detachPixels();
    const bool detached_ok = detached != nullptr && storage.addr() == nullptr && storage.isEmpty();
    sk_free(detached);
    if (!detached_ok) return false;
#ifndef STX_SKIA_REFERENCE
    if (!storage.tryAlloc(info) || !moved.tryAlloc(info)) return false;
    pixels = storage.addr();
    moved = std::move(storage);
    if (moved.addr() != pixels || storage.addr() != nullptr || !storage.isEmpty()) return false;
    moved = std::move(moved);
    if (moved.addr() != pixels || moved.info() != info) return false;
    if (!moved.tryAlloc(moved.info()) || moved.info() != info) return false;
    const SkImageInfo too_large = SkImageInfo::MakeN32Premul(std::numeric_limits<int>::max(), 3);
    if (moved.tryAlloc(too_large) || moved.addr() != nullptr || !moved.isEmpty() || moved.rowBytes() != 0) return false;
    const SkImageInfo negative = SkImageInfo::MakeN32Premul(-1, 4);
    if (moved.tryAlloc(negative) || !moved.isEmpty()) return false;
    const SkImageInfo invalid_color = SkImageInfo::Make(3, 4,
        static_cast<SkColorType>(kLastEnum_SkColorType + 1), kPremul_SkAlphaType);
    if (moved.tryAlloc(invalid_color) || !moved.isEmpty()) return false;
    const SkImageInfo invalid_alpha = SkImageInfo::Make(3, 4, kRGBA_8888_SkColorType,
        static_cast<SkAlphaType>(kLastEnum_SkAlphaType + 1));
    if (moved.tryAlloc(invalid_alpha) || !moved.isEmpty()) return false;
    storage.reset(info, borrowed.data(), 28);
    if (!moved.tryAlloc(info)) return false;
    moved = std::move(storage);
    if (moved.addr() != borrowed.data() || storage.addr() != nullptr || !storage.isEmpty()) return false;
    if (moved.detachPixelsAsData() || moved.addr() != borrowed.data()) return false;
    moved.reset();
#endif
    return true;
}
}

int main() {
    if (!arithmetic() || !image_extents() || !clipped_records() || !owned_storage()) return 1;
    std::puts("checked extents, clipped transfers and owned pixmap storage: PASS");
    return 0;
}
