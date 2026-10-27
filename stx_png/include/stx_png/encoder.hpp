#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace stx_png {

// Channel order of the 32-bit source pixels. Both orders are read as four
// 8-bit channels per pixel, top row first.
enum class PngChannelOrder : std::uint8_t { rgba, bgra };

// How the source's fourth channel is read.
enum class PngSourceAlpha : std::uint8_t {
    // Straight alpha: written as RGBA (colour type 6).
    straight,
    // Premultiplied alpha: unpremultiplied, then written as RGBA. Each colour
    // channel becomes min(255, (c * 255 + a / 2) / a), the nearest value with
    // ties rounding up, and 0 where a = 0.
    premultiplied,
    // Ignored: an opaque truecolor PNG (colour type 2) is written.
    opaque
};

// Default compression level, chosen from measured size and time; see
// notes:results/2026-10-21-png-encoder/README.md.
inline constexpr int default_png_compression_level = 6;

struct PngEncodeInput final {
    std::uint32_t width{};
    std::uint32_t height{};
    // Bytes between successive rows; must be at least width * 4. Trailing
    // padding of the final row is not required.
    std::uint64_t row_bytes{};
    std::span<const std::byte> pixels{};
    PngChannelOrder order{PngChannelOrder::rgba};
    PngSourceAlpha alpha{PngSourceAlpha::straight};
    // stx_zlib level, 0 (stored) to 9. Level 0 also writes every row with
    // filter type 0; other levels choose a filter per row.
    int compression_level{default_png_compression_level};
};

// Renderer-neutral PNG encoder used by every host: 8-bit truecolor(+alpha),
// non-interlaced, an sRGB chunk, and one IDAT compressed by stx_zlib.
//
// Filter selection (levels 1-9): each row is filtered with all five PNG
// filters and the one with the smallest sum of |signed byte| (bytes read as
// -128..127) is written; a tie takes the lower filter type. This is the
// classic minimum-sum-of-absolute-differences heuristic.
//
// Output is a deterministic function of the pixels and settings: the same
// bytes on every host, independent of how the encoder's buffers are fed.
// Pixel input must not overlap the live output storage. Returns false on
// invalid input, overlap, overflow, an IDAT over 2^31 - 1 bytes or an
// encoder failure; output is then left empty.
[[nodiscard]] bool encode_png(const PngEncodeInput& input,
                              std::vector<std::byte>& output);

// Chunk CRCs and the zlib Adler-32 come from stx_zlib/checksum.hpp, the one
// implementation of each.

} // namespace stx_png
