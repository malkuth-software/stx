#pragma once
// Red/blue swap between RGBA8 and BGRA8 pixels. It includes no Skia header and the
// stx_skia_swizzle library links no Skia code, so a host can use it on its own.

#include <bit>
#include <cstddef>
#include <optional>
#include <span>

// Pixels are read as little-endian 32-bit words; every supported target is little-endian.
static_assert(std::endian::native == std::endian::little,
              "stx_skia swizzle kernels require a little-endian target");

namespace stx_skia {

// Whole 4-byte pixels in a row.
struct PixelCount {
    explicit constexpr PixelCount(std::size_t pixels) noexcept : value(pixels) {}
    std::size_t value;
};

// Rows.
struct RowCount {
    explicit constexpr RowCount(std::size_t rows) noexcept : value(rows) {}
    std::size_t value;
};

// Distance between the starts of consecutive rows, in bytes.
struct RowBytes {
    explicit constexpr RowBytes(std::size_t bytes) noexcept : value(bytes) {}
    std::size_t value;
};

enum class SwizzleResult {
    ok,
    size_not_whole_pixels,  // a span's size is not a multiple of 4
    size_mismatch,          // swap_red_blue: dst and src differ in size
    row_too_narrow,         // a stride is less than width x 4 bytes
    extent_overflow,        // a required extent does not fit in std::size_t
    buffer_too_short,       // a span is shorter than its required extent
    partial_overlap,        // the buffers overlap, but not as an exact in-place call
};

// Converts a stride given in pixels, as Android reports it, to bytes. Empty if the byte
// count does not fit in std::size_t.
[[nodiscard]] std::optional<RowBytes> row_bytes_for(PixelCount stride) noexcept;

// Swaps red and blue in each 4-byte pixel, leaving green and alpha in place. dst and src
// have the same size, a whole number of pixels, and are either the same span (in place) or
// disjoint. Nothing is written unless the result is ok.
[[nodiscard]] SwizzleResult swap_red_blue(std::span<std::byte> dst,
                                          std::span<const std::byte> src) noexcept;

// The same over width x height pixels of rows, as for one damage rectangle. Each buffer
// spans stride x (height - 1) + width x 4 bytes, and its stride is at least width x 4 bytes
// when there is more than one row. In place means the same start and the same stride; any
// other overlap is rejected. Validation finishes before any pixel is written, and bytes
// outside the rows' widths are never read or written.
[[nodiscard]] SwizzleResult swap_red_blue_rows(std::span<std::byte> dst, RowBytes dstStride,
                                               std::span<const std::byte> src,
                                               RowBytes srcStride, PixelCount width,
                                               RowCount height) noexcept;

}  // namespace stx_skia
