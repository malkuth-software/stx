#include "stx_skia/swizzle.hpp"

// Only standard headers: this library links no Skia code. Skia's SkOpts::RGBA_to_BGRA is the
// reference for results and speed, not a dependency.
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>

static_assert(std::endian::native == std::endian::little,
              "the red/blue mask below assumes little-endian pixel words");

namespace stx_skia {
namespace {

constexpr std::size_t kPixelBytes = 4;
constexpr std::size_t kMaxSize = std::numeric_limits<std::size_t>::max();
// A little-endian word holds the bytes R, G, B, A (or B, G, R, A) from low to high.
constexpr std::uint32_t kGreenAlpha = 0xFF00FF00u;
constexpr std::uint32_t kRedBlue = 0x00FF00FFu;

std::uint32_t swap_pixel(std::uint32_t pixel) {
    const std::uint32_t kept = pixel & kGreenAlpha;
    const std::uint32_t moved = std::rotl(pixel & kRedBlue, 16);
    const std::uint32_t swapped = kept | moved;
    return swapped;
}

// The kernels take raw addresses of count pixels that the callers have validated. The in-place
// form has one address, so the compiler sees no possible overlap; the disjoint form is chosen
// only after overlap is excluded. Words move through std::memcpy, so any alignment is allowed.
void swap_in_place(std::byte* pixels, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        std::byte* const at = pixels + i * kPixelBytes;
        std::uint32_t pixel = 0;
        std::memcpy(&pixel, at, kPixelBytes);
        const std::uint32_t swapped = swap_pixel(pixel);
        std::memcpy(at, &swapped, kPixelBytes);
    }
}

void swap_disjoint(std::byte* dst, const std::byte* src, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t pixel = 0;
        std::memcpy(&pixel, src + i * kPixelBytes, kPixelBytes);
        const std::uint32_t swapped = swap_pixel(pixel);
        std::memcpy(dst + i * kPixelBytes, &swapped, kPixelBytes);
    }
}

// Whether [a, a + aSize) and [b, b + bSize) share a byte. std::less orders unrelated
// pointers totally, where the built-in comparison does not.
bool ranges_intersect(const std::byte* a, std::size_t aSize, const std::byte* b,
                      std::size_t bSize) {
    if (aSize == 0 || bSize == 0) return false;
    const std::less<const std::byte*> before{};
    const bool aStartsBeforeBEnds = before(a, b + bSize);
    const bool bStartsBeforeAEnds = before(b, a + aSize);
    return aStartsBeforeBEnds && bStartsBeforeAEnds;
}

struct RowExtent {
    SwizzleResult result = SwizzleResult::ok;
    std::size_t bytes = 0;
};

// Validates one buffer of height rows, rowBytes wide, stride apart, and returns the bytes
// they span: stride x (height - 1) + rowBytes.
RowExtent row_extent(std::size_t spanBytes, RowBytes stride, std::size_t rowBytes,
                     RowCount height) {
    if (height.value == 0) return RowExtent{SwizzleResult::ok, 0};
    if (height.value > 1 && stride.value < rowBytes) {
        return RowExtent{SwizzleResult::row_too_narrow, 0};
    }
    const std::size_t lastRow = height.value - 1;
    if (lastRow != 0 && stride.value > kMaxSize / lastRow) {
        return RowExtent{SwizzleResult::extent_overflow, 0};
    }
    const std::size_t lastRowStart = stride.value * lastRow;
    if (rowBytes > kMaxSize - lastRowStart) return RowExtent{SwizzleResult::extent_overflow, 0};
    const std::size_t extent = lastRowStart + rowBytes;
    if (spanBytes < extent) return RowExtent{SwizzleResult::buffer_too_short, 0};
    return RowExtent{SwizzleResult::ok, extent};
}

}  // namespace

std::optional<RowBytes> row_bytes_for(PixelCount stride) noexcept {
    if (stride.value > kMaxSize / kPixelBytes) return std::nullopt;
    const RowBytes bytes(stride.value * kPixelBytes);
    return bytes;
}

SwizzleResult swap_red_blue(std::span<std::byte> dst, std::span<const std::byte> src) noexcept {
    if (dst.size() % kPixelBytes != 0 || src.size() % kPixelBytes != 0) {
        return SwizzleResult::size_not_whole_pixels;
    }
    if (dst.size() != src.size()) return SwizzleResult::size_mismatch;
    const std::size_t count = dst.size() / kPixelBytes;
    if (dst.data() == src.data()) {
        swap_in_place(dst.data(), count);
        return SwizzleResult::ok;
    }
    if (ranges_intersect(dst.data(), dst.size(), src.data(), src.size())) {
        return SwizzleResult::partial_overlap;
    }
    swap_disjoint(dst.data(), src.data(), count);
    return SwizzleResult::ok;
}

SwizzleResult swap_red_blue_rows(std::span<std::byte> dst, RowBytes dstStride,
                                 std::span<const std::byte> src, RowBytes srcStride,
                                 PixelCount width, RowCount height) noexcept {
    if (width.value > kMaxSize / kPixelBytes) return SwizzleResult::extent_overflow;
    const std::size_t rowBytes = width.value * kPixelBytes;
    const RowExtent dstExtent = row_extent(dst.size(), dstStride, rowBytes, height);
    if (dstExtent.result != SwizzleResult::ok) return dstExtent.result;
    const RowExtent srcExtent = row_extent(src.size(), srcStride, rowBytes, height);
    if (srcExtent.result != SwizzleResult::ok) return srcExtent.result;
    if (width.value == 0 || height.value == 0) return SwizzleResult::ok;

    const bool inPlace = dst.data() == src.data() && dstStride.value == srcStride.value;
    if (!inPlace &&
        ranges_intersect(dst.data(), dstExtent.bytes, src.data(), srcExtent.bytes)) {
        return SwizzleResult::partial_overlap;
    }
    for (std::size_t row = 0; row < height.value; ++row) {
        std::byte* const dstRow = dst.data() + row * dstStride.value;
        if (inPlace) {
            swap_in_place(dstRow, width.value);
        } else {
            const std::byte* const srcRow = src.data() + row * srcStride.value;
            swap_disjoint(dstRow, srcRow, width.value);
        }
    }
    return SwizzleResult::ok;
}

}  // namespace stx_skia
