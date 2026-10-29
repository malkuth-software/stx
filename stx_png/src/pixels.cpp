#include "stx_png/pixels.hpp"

#include "restrict.hpp"

namespace stx_png {
namespace {

constexpr std::uint64_t sample_maximum = 65535U;
constexpr std::uint64_t straight_half = sample_maximum / 2U;
constexpr std::uint64_t product_maximum = sample_maximum * sample_maximum;
constexpr std::uint64_t product_half = product_maximum / 2U;
static_assert(straight_half == 32767U && product_maximum == 4294836225U &&
              product_half == 2147418112U);

std::byte straight_channel(std::uint64_t sample) noexcept {
    const std::uint64_t value = (sample * 255U + straight_half) / sample_maximum;
    return static_cast<std::byte>(value);
}
std::byte premultiplied_channel(std::uint64_t sample, std::uint64_t alpha) noexcept {
    const std::uint64_t value = (sample * alpha * 255U + product_half) / product_maximum;
    return static_cast<std::byte>(value);
}

// One kernel per target, chosen before the loop. Red, green and blue land at
// the offsets the order names; alpha is always the fourth byte.
// Restricted parameters: no byte is reached through both (convert_rows
// checks it).
template<PixelOrder Order, PixelAlpha Alpha>
void convert_samples(const std::uint16_t* STX_PNG_RESTRICT sample_data, std::size_t sample_count,
                     std::uint32_t width, std::uint32_t height, std::byte* STX_PNG_RESTRICT pixel_data,
                     std::size_t pixel_count, std::size_t row_bytes) noexcept {
    const std::span<const std::uint16_t> samples(sample_data, sample_count);
    const std::span<std::byte> pixels(pixel_data, pixel_count);
    constexpr std::size_t red = Order == PixelOrder::rgba ? 0U : 2U;
    constexpr std::size_t blue = Order == PixelOrder::rgba ? 2U : 0U;
    std::size_t source = 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        std::size_t destination = static_cast<std::size_t>(y) * row_bytes;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint64_t r = samples[source];
            const std::uint64_t g = samples[source + 1U];
            const std::uint64_t b = samples[source + 2U];
            const std::uint64_t a = samples[source + 3U];
            if constexpr (Alpha == PixelAlpha::straight) {
                pixels[destination + red] = straight_channel(r);
                pixels[destination + 1U] = straight_channel(g);
                pixels[destination + blue] = straight_channel(b);
            } else {
                pixels[destination + red] = premultiplied_channel(r, a);
                pixels[destination + 1U] = premultiplied_channel(g, a);
                pixels[destination + blue] = premultiplied_channel(b, a);
            }
            pixels[destination + 3U] = straight_channel(a);
            source += 4U;
            destination += 4U;
        }
    }
}

// Overlap rule: samples and pixels share no byte, which convert_rgba16_samples
// checks before choosing a kernel (and asserted here in debug and sanitizer
// builds).
template<PixelOrder Order, PixelAlpha Alpha>
void convert_rows(std::span<const std::uint16_t> samples, std::uint32_t width,
                  std::uint32_t height, std::span<std::byte> pixels,
                  std::size_t row_bytes) noexcept {
    detail::require_disjoint(samples, pixels);
    convert_samples<Order, Alpha>(samples.data(), samples.size(), width, height, pixels.data(), pixels.size(),
                                  row_bytes);
}

bool overlaps(std::span<const std::byte> left, std::span<const std::byte> right) noexcept {
    if (left.empty() || right.empty()) return false;
    const std::uintptr_t a = reinterpret_cast<std::uintptr_t>(left.data());
    const std::uintptr_t b = reinterpret_cast<std::uintptr_t>(right.data());
    const bool overlap = a <= b ? b - a < left.size() : a - b < right.size();
    return overlap;
}

} // namespace

bool valid_pixel_target(PixelTarget target) noexcept {
    const bool order = target.order == PixelOrder::rgba || target.order == PixelOrder::bgra;
    const bool alpha = target.alpha == PixelAlpha::straight ||
                       target.alpha == PixelAlpha::premultiplied;
    const bool valid = order && alpha;
    return valid;
}

bool valid_pixel_destination(std::uint32_t width, std::uint32_t height,
                             std::size_t pixel_bytes, std::size_t row_bytes) noexcept {
    if (width == 0U || height == 0U) return false;
    // width * 4 < 2^34 cannot wrap in 64 bits; comparing in 64 bits avoids
    // assuming the width of size_t.
    const std::uint64_t tight_row = static_cast<std::uint64_t>(width) * 4U;
    const std::uint64_t live = pixel_bytes;
    const std::uint64_t stride = row_bytes;
    if (stride < tight_row || live < tight_row) return false;
    const std::uint64_t preceding_rows = static_cast<std::uint64_t>(height) - 1U;
    const std::uint64_t available = live - tight_row;
    const bool valid = preceding_rows == 0U || stride <= available / preceding_rows;
    return valid;
}

bool convert_rgba16_samples(std::span<const std::uint16_t> samples,
    std::uint32_t width, std::uint32_t height, PixelTarget target,
    std::span<std::byte> pixels, std::size_t row_bytes) noexcept {
    if (!valid_pixel_target(target) || !valid_pixel_destination(width, height, pixels.size(), row_bytes)) {
        return false;
    }
    const std::uint64_t pixel_count = static_cast<std::uint64_t>(width) * height;
    if (pixel_count > samples.size() / 4U) return false;
    if (overlaps(std::as_bytes(samples), pixels)) return false;
    if (target.order == PixelOrder::rgba && target.alpha == PixelAlpha::straight) {
        convert_rows<PixelOrder::rgba, PixelAlpha::straight>(samples, width, height, pixels, row_bytes);
    } else if (target.order == PixelOrder::rgba) {
        convert_rows<PixelOrder::rgba, PixelAlpha::premultiplied>(samples, width, height, pixels, row_bytes);
    } else if (target.alpha == PixelAlpha::straight) {
        convert_rows<PixelOrder::bgra, PixelAlpha::straight>(samples, width, height, pixels, row_bytes);
    } else {
        convert_rows<PixelOrder::bgra, PixelAlpha::premultiplied>(samples, width, height, pixels, row_bytes);
    }
    return true;
}

} // namespace stx_png
