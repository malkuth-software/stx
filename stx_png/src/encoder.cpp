#include "stx_png/encoder.hpp"

#include "stx_zlib/checksum.hpp"
#include "stx_zlib/deflate.hpp"
#include "restrict.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

namespace stx_png {
namespace {

constexpr std::array<std::uint8_t, 8> png_signature{
    0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};
// Compressed output grows in steps of this many bytes.
constexpr std::size_t output_step = 65536U;
// Largest PNG chunk length.
constexpr std::size_t chunk_limit = 0x7fffffffU;
// Matches the renderer core's registry ceiling so a capture never exceeds
// what the toolkit's own PNG validator will accept.
constexpr std::uint32_t dimension_limit = 16384U;

void append_u32_big_endian(std::vector<std::byte>& output, std::uint32_t value) {
    output.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::byte>(value & 0xffU));
}

void append_chunk(std::vector<std::byte>& output, const char (&type)[5],
                  std::span<const std::byte> data) {
    append_u32_big_endian(output, static_cast<std::uint32_t>(data.size()));
    const std::size_t crc_start = output.size();
    for (std::size_t index = 0; index < 4U; ++index) {
        output.push_back(static_cast<std::byte>(type[index]));
    }
    output.insert(output.end(), data.begin(), data.end());
    const std::span<const std::byte> crc_input(output.data() + crc_start,
                                               output.size() - crc_start);
    append_u32_big_endian(output, stx_zlib::crc32(crc_input));
}

} // namespace

namespace {

// One source row as straight 8-bit RGB or RGBA, in PNG channel order.
void gather_row(const PngEncodeInput& input, std::uint32_t row, std::span<std::byte> line) noexcept {
    const std::size_t red = input.order == PngChannelOrder::rgba ? 0U : 2U;
    const std::size_t blue = input.order == PngChannelOrder::rgba ? 2U : 0U;
    const std::byte* source = input.pixels.data() + static_cast<std::size_t>(input.row_bytes) * row;
    std::size_t at = 0;
    // An opaque source drops its fourth byte.
    if (input.alpha == PngSourceAlpha::opaque) {
        for (std::uint32_t column = 0; column < input.width; ++column) {
            const std::byte* pixel = source + static_cast<std::size_t>(column) * 4U;
            line[at] = pixel[red];
            line[at + 1U] = pixel[1];
            line[at + 2U] = pixel[blue];
            at += 3U;
        }
        return;
    }
    // A premultiplied source divides each colour channel by alpha, rounding
    // to nearest; a transparent pixel's colour is zero.
    if (input.alpha == PngSourceAlpha::premultiplied) {
        for (std::uint32_t column = 0; column < input.width; ++column) {
            const std::byte* pixel = source + static_cast<std::size_t>(column) * 4U;
            const unsigned alpha = std::to_integer<unsigned>(pixel[3]);
            std::array<std::byte, 3> colour{};
            if (alpha != 0U) {
                const std::array<std::byte, 3> stored{pixel[red], pixel[1], pixel[blue]};
                for (std::size_t channel = 0; channel < 3U; ++channel) {
                    const unsigned value = std::to_integer<unsigned>(stored[channel]);
                    const unsigned straight = std::min(255U, (value * 255U + alpha / 2U) / alpha);
                    colour[channel] = static_cast<std::byte>(straight);
                }
            }
            line[at] = colour[0];
            line[at + 1U] = colour[1];
            line[at + 2U] = colour[2];
            line[at + 3U] = pixel[3];
            at += 4U;
        }
        return;
    }
    for (std::uint32_t column = 0; column < input.width; ++column) {
        const std::byte* pixel = source + static_cast<std::size_t>(column) * 4U;
        line[at] = pixel[red];
        line[at + 1U] = pixel[1];
        line[at + 2U] = pixel[blue];
        line[at + 3U] = pixel[3];
        at += 4U;
    }
}

unsigned distance(int first, int second) noexcept {
    const int difference = first - second;
    const unsigned result = static_cast<unsigned>(difference < 0 ? -difference : difference);
    return result;
}

unsigned paeth_predictor(unsigned left, unsigned above, unsigned corner) noexcept {
    const int p = static_cast<int>(left) + static_cast<int>(above) - static_cast<int>(corner);
    const unsigned a = distance(p, static_cast<int>(left));
    const unsigned b = distance(p, static_cast<int>(above));
    const unsigned c = distance(p, static_cast<int>(corner));
    if (a <= b && a <= c) return left;
    if (b <= c) return above;
    return corner;
}

// One filtered byte: the source byte less its prediction, modulo 256.
std::byte filtered_byte(std::byte value, unsigned predicted) noexcept {
    const std::byte result = static_cast<std::byte>((std::to_integer<unsigned>(value) - predicted) & 255U);
    return result;
}

// Writes filter type `filter` of line (previous row `above`, zero for the
// first) into output, whose first byte is the filter type. Each filter has
// its own loops; the first pixel's bytes, [0, edge), have no left
// neighbour, so their left and corner bytes are zero.
// Restricted parameters: the output shares no byte with line or above
// (filter_row checks it).
void filter_row_bytes(unsigned filter, const std::byte* STX_PNG_RESTRICT line_data,
                      const std::byte* STX_PNG_RESTRICT above_data, std::size_t line_size, std::size_t above_size,
                      std::size_t stride, std::byte* STX_PNG_RESTRICT output_data, std::size_t output_size) noexcept {
    const std::span<const std::byte> line(line_data, line_size);
    const std::span<const std::byte> above(above_data, above_size);
    const std::span<std::byte> output(output_data, output_size);
    output[0] = static_cast<std::byte>(filter);
    const std::span<std::byte> filtered = output.subspan(1U);
    const std::size_t edge = std::min(stride, line.size());
    if (filter == 1U) {
        std::copy(line.begin(), line.begin() + static_cast<std::ptrdiff_t>(edge), filtered.begin());
        for (std::size_t index = edge; index < line.size(); ++index) {
            filtered[index] = filtered_byte(line[index], std::to_integer<unsigned>(line[index - stride]));
        }
        return;
    }
    if (filter == 2U) {
        for (std::size_t index = 0; index < line.size(); ++index) {
            filtered[index] = filtered_byte(line[index], std::to_integer<unsigned>(above[index]));
        }
        return;
    }
    if (filter == 3U) {
        for (std::size_t index = 0; index < edge; ++index) {
            filtered[index] = filtered_byte(line[index], std::to_integer<unsigned>(above[index]) / 2U);
        }
        for (std::size_t index = edge; index < line.size(); ++index) {
            const unsigned left = std::to_integer<unsigned>(line[index - stride]);
            const unsigned up = std::to_integer<unsigned>(above[index]);
            filtered[index] = filtered_byte(line[index], (left + up) / 2U);
        }
        return;
    }
    if (filter == 4U) {
        for (std::size_t index = 0; index < edge; ++index) {
            const unsigned up = std::to_integer<unsigned>(above[index]);
            filtered[index] = filtered_byte(line[index], paeth_predictor(0U, up, 0U));
        }
        for (std::size_t index = edge; index < line.size(); ++index) {
            const unsigned left = std::to_integer<unsigned>(line[index - stride]);
            const unsigned up = std::to_integer<unsigned>(above[index]);
            const unsigned corner = std::to_integer<unsigned>(above[index - stride]);
            filtered[index] = filtered_byte(line[index], paeth_predictor(left, up, corner));
        }
        return;
    }
    // Filter 0 (none) copies the row.
    std::copy(line.begin(), line.end(), filtered.begin());
}
// Overlap rule: output shares no byte with line or above (asserted in debug
// and sanitizer builds); the encoder gathers line and above into one vector
// and filters into a separate candidates vector.
void filter_row(unsigned filter, std::span<const std::byte> line, std::span<const std::byte> above,
                std::size_t stride, std::span<std::byte> output) noexcept {
    detail::require_disjoint(output, line);
    detail::require_disjoint(output, above);
    filter_row_bytes(filter, line.data(), above.data(), line.size(), above.size(), stride, output.data(),
                     output.size());
}

// Sum of |signed byte| over the filtered bytes (the filter type excluded).
std::uint64_t filter_cost(std::span<const std::byte> filtered) noexcept {
    std::uint64_t cost = 0;
    for (std::size_t index = 1; index < filtered.size(); ++index) {
        const unsigned value = std::to_integer<unsigned>(filtered[index]);
        const unsigned magnitude = value < 128U ? value : 256U - value;
        cost += magnitude;
    }
    return cost;
}

// Feeds all of input to the stream with flush, growing output by fixed
// steps; for finish, drains until stream end. Returns false on an encoder
// error or a stalled stream.
bool deflate_into(stx_zlib::DeflateStream& stream, std::span<const std::byte> input,
                  stx_zlib::DeflateFlush flush, std::vector<std::byte>& output,
                  std::size_t& produced) {
    std::size_t consumed = 0;
    for (;;) {
        if (output.size() - produced < output_step) output.resize(produced + output_step);
        const std::span<const std::byte> pending = input.subspan(consumed);
        const std::span<std::byte> space(output.data() + produced, output.size() - produced);
        const stx_zlib::DeflateResult step = stream.process(pending, space, flush);
        if (step.status == stx_zlib::DeflateStatus::error) return false;
        consumed += step.input_consumed;
        produced += step.output_produced;
        if (step.status == stx_zlib::DeflateStatus::stream_end) return true;
        const bool input_done = consumed == input.size();
        if (flush == stx_zlib::DeflateFlush::none && input_done &&
            step.status == stx_zlib::DeflateStatus::need_input) {
            return true;
        }
        if (step.input_consumed == 0U && step.output_produced == 0U &&
            step.status == stx_zlib::DeflateStatus::need_input) {
            return false;
        }
    }
}

} // namespace

bool encode_png(const PngEncodeInput& input, std::vector<std::byte>& output) {
    // Check the borrow before clearing its possible owner. Address differences
    // avoid overflowing an end-address calculation for either live range.
    bool aliased = false;
    if (!input.pixels.empty() && !output.empty()) {
        const std::uintptr_t source = reinterpret_cast<std::uintptr_t>(input.pixels.data());
        const std::uintptr_t destination = reinterpret_cast<std::uintptr_t>(output.data());
        aliased = source <= destination ? destination - source < input.pixels.size()
            : source - destination < output.size();
    }
    output.clear();
    if (aliased) return false;
    if (input.order != PngChannelOrder::rgba && input.order != PngChannelOrder::bgra) return false;
    if (input.alpha != PngSourceAlpha::straight && input.alpha != PngSourceAlpha::premultiplied &&
        input.alpha != PngSourceAlpha::opaque) {
        return false;
    }
    if (input.compression_level < 0 || input.compression_level > 9) return false;
    if (input.width == 0U || input.height == 0U || input.width > dimension_limit ||
        input.height > dimension_limit) {
        return false;
    }
    const std::uint64_t tight_row = static_cast<std::uint64_t>(input.width) * 4U;
    if (input.row_bytes < tight_row) return false;
    // Validate by division before multiplying an untrusted stride. The final
    // row needs pixel bytes only; a one-row image never uses its row stride.
    if (tight_row > input.pixels.size()) return false;
    const std::uint64_t preceding_rows = input.height - 1U;
    const std::uint64_t available = input.pixels.size() - tight_row;
    if (preceding_rows != 0 && input.row_bytes > available / preceding_rows) return false;

    // Dimensions are at most 16384, so these sizes fit easily in 64 bits.
    const std::size_t channels = input.alpha == PngSourceAlpha::opaque ? 3U : 4U;
    const std::size_t line_bytes = static_cast<std::size_t>(input.width) * channels;
    const std::size_t candidate_bytes = line_bytes + 1U;
    const std::uint64_t raw_size = static_cast<std::uint64_t>(candidate_bytes) * input.height;

    try {
        // Two source lines (current and previous, zero before the first row)
        // and one candidate row per filter type.
        std::vector<std::byte> lines(2U * line_bytes, std::byte{0});
        std::vector<std::byte> candidates(5U * candidate_bytes);
        std::span<std::byte> line(lines.data(), line_bytes);
        std::span<std::byte> above(lines.data() + line_bytes, line_bytes);

        std::unique_ptr<stx_zlib::DeflateStream> stream = std::make_unique<stx_zlib::DeflateStream>();
        stx_zlib::DeflateOptions options{};
        options.level = input.compression_level;
        options.input_limit.value = raw_size;
        // Stored blocks bound the output: five header bytes per 32 KiB block
        // beyond the raw bytes, plus the zlib header and trailer.
        options.output_limit.value = raw_size + (raw_size / 16384U + 1U) * 5U + 64U;
        // Generous and finite: 2^16 effort units per input byte.
        options.work_limit.value = (raw_size + 1U) * 65536U;
        if ((*stream).reset(options) != stx_zlib::DeflateError::none) return false;

        std::vector<std::byte> zlib;
        std::size_t produced = 0;
        for (std::uint32_t row = 0; row < input.height; ++row) {
            gather_row(input, row, line);
            unsigned chosen = 0U;
            if (input.compression_level == 0) {
                filter_row(0U, line, above, channels, std::span<std::byte>(candidates.data(), candidate_bytes));
            } else {
                std::uint64_t best = std::numeric_limits<std::uint64_t>::max();
                for (unsigned filter = 0; filter < 5U; ++filter) {
                    const std::span<std::byte> candidate(candidates.data() + filter * candidate_bytes,
                                                         candidate_bytes);
                    filter_row(filter, line, above, channels, candidate);
                    const std::uint64_t cost = filter_cost(candidate);
                    if (cost < best) {
                        best = cost;
                        chosen = filter;
                    }
                }
            }
            const std::span<const std::byte> filtered(candidates.data() + chosen * candidate_bytes,
                                                      candidate_bytes);
            if (!deflate_into(*stream, filtered, stx_zlib::DeflateFlush::none, zlib, produced)) return false;
            std::swap(line, above);
        }
        if (!deflate_into(*stream, {}, stx_zlib::DeflateFlush::finish, zlib, produced)) return false;
        zlib.resize(produced);
        if (zlib.size() > chunk_limit) return false;

        output.reserve(png_signature.size() + 25U + 13U + zlib.size() + 12U + 12U);
        for (const std::uint8_t value : png_signature) output.push_back(std::byte{value});
        std::array<std::byte, 13> header{};
        header[0] = static_cast<std::byte>((input.width >> 24U) & 0xffU);
        header[1] = static_cast<std::byte>((input.width >> 16U) & 0xffU);
        header[2] = static_cast<std::byte>((input.width >> 8U) & 0xffU);
        header[3] = static_cast<std::byte>(input.width & 0xffU);
        header[4] = static_cast<std::byte>((input.height >> 24U) & 0xffU);
        header[5] = static_cast<std::byte>((input.height >> 16U) & 0xffU);
        header[6] = static_cast<std::byte>((input.height >> 8U) & 0xffU);
        header[7] = static_cast<std::byte>(input.height & 0xffU);
        header[8] = std::byte{8};                                   // bit depth
        header[9] = std::byte{channels == 3U ? std::uint8_t{2} : std::uint8_t{6}}; // colour type
        header[10] = std::byte{0};                                  // compression
        header[11] = std::byte{0};                                  // filter method
        header[12] = std::byte{0};                                  // no interlace
        append_chunk(output, "IHDR", header);
        const std::array<std::byte, 1> srgb{std::byte{0}}; // perceptual intent
        append_chunk(output, "sRGB", srgb);
        append_chunk(output, "IDAT", zlib);
        append_chunk(output, "IEND", {});
    } catch (const std::bad_alloc&) {
        output.clear();
        return false;
    } catch (const std::length_error&) {
        output.clear();
        return false;
    }
    return true;
}

} // namespace stx_png
