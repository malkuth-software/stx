#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace stx_png {

// Conversion of the decoder's straight-alpha RGBA16 samples to the four 8-bit
// channels a consumer stores.

// Memory order of the four 8-bit channels of one destination pixel.
enum class PixelOrder { rgba, bgra };
// Straight alpha keeps colour independent of coverage (interchange, PNG
// files); premultiplied scales every colour channel by alpha (compositing).
enum class PixelAlpha { straight, premultiplied };

struct PixelTarget final {
    PixelOrder order = PixelOrder::rgba;
    PixelAlpha alpha = PixelAlpha::premultiplied;
};

// True for the four declared order/alpha combinations.
[[nodiscard]] bool valid_pixel_target(PixelTarget target) noexcept;

// True when a destination of `pixel_bytes` live bytes holds a width x height
// image with `row_bytes` between rows: width and height nonzero,
// row_bytes >= width * 4, and at least row_bytes * (height - 1) + width * 4
// bytes. Checked by division, so no product can wrap.
[[nodiscard]] bool valid_pixel_destination(std::uint32_t width, std::uint32_t height,
    std::size_t pixel_bytes, std::size_t row_bytes) noexcept;

// Converts top-down straight-alpha RGBA16 samples to 8-bit pixels.
//
// Rounding rule: every output channel is the integer nearest the exact
// rational value, computed in unsigned 64-bit integers with no floating point.
//   straight colour and every alpha: (c * 255 + 32767) / 65535
//   premultiplied colour:            (c * a * 255 + 2147418112) / 4294836225
// where c and a are the 16-bit samples and 4294836225 = 65535 * 65535. Both
// denominators are odd and every numerator is an integer, so an exact half
// cannot occur and the rule needs no tie break. Eight-bit sources (stored as
// v * 257) convert back to exactly v; premultiplied 8-bit results equal
// round(v * alpha / 255).
//
// samples holds width * height * 4 live values; pixels satisfies
// valid_pixel_destination; bytes between rows are left unchanged. The two
// spans must be disjoint. Returns false, writing nothing, when the shape,
// target or disjointness is invalid.
[[nodiscard]] bool convert_rgba16_samples(std::span<const std::uint16_t> samples,
    std::uint32_t width, std::uint32_t height, PixelTarget target,
    std::span<std::byte> pixels, std::size_t row_bytes) noexcept;

} // namespace stx_png
