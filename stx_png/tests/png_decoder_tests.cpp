#include "stx_png/decoder.hpp"
#include "stx_png/encoder.hpp"
#include "stx_zlib/inflate.hpp"
#include "stx_zlib/checksum.hpp"
#include "png_dynamic_fixture.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

#if defined(STX_PNG_ORACLE)
extern "C" int stx_png_oracle(const unsigned char*, std::size_t,
    unsigned char*, std::size_t, std::uint32_t, std::uint32_t);
#endif

namespace {
using Bytes = std::vector<std::byte>;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void put32(Bytes& data, std::uint32_t value) {
    for (unsigned shift = 32; shift != 0; shift -= 8) {
        data.push_back(static_cast<std::byte>((value >> (shift - 8)) & 255U));
    }
}
void chunk(Bytes& data, const char* name, const Bytes& payload) {
    put32(data, static_cast<std::uint32_t>(payload.size()));
    const std::size_t begin = data.size();
    for (unsigned index = 0; index < 4; ++index) data.push_back(static_cast<std::byte>(name[index]));
    data.insert(data.end(), payload.begin(), payload.end());
    const std::span<const std::byte> checked(data.data() + begin, payload.size() + 4);
    put32(data, stx_zlib::crc32(checked));
}
Bytes stored(const Bytes& raw) {
    Bytes data{std::byte{0x78}, std::byte{0x01}};
    std::size_t offset = 0;
    do {
        const std::size_t count = std::min<std::size_t>(65535, raw.size() - offset);
        const bool final = count == raw.size() - offset;
        data.push_back(final ? std::byte{1} : std::byte{0});
        data.push_back(static_cast<std::byte>(count & 255));
        data.push_back(static_cast<std::byte>(count >> 8));
        data.push_back(static_cast<std::byte>((~count) & 255));
        data.push_back(static_cast<std::byte>(((~count) >> 8) & 255));
        data.insert(data.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
    } while (offset < raw.size());
    put32(data, stx_zlib::adler32(raw));
    return data;
}
struct Format final { unsigned color = 0; unsigned depth = 0; unsigned channels = 0; };
constexpr std::array<Format, 15> formats{{
    {0,1,1}, {0,2,1}, {0,4,1}, {0,8,1}, {0,16,1},
    {2,8,3}, {2,16,3}, {3,1,1}, {3,2,1}, {3,4,1}, {3,8,1},
    {4,8,2}, {4,16,2}, {6,8,4}, {6,16,4}}};
unsigned value_at(unsigned x, unsigned y, unsigned channel, const Format& format) {
    const unsigned value = (x * 73 + y * 139 + channel * 17) & ((1U << format.depth) - 1);
    return value;
}
unsigned reference_predictor(unsigned filter, unsigned a, unsigned b, unsigned c) {
    if (filter == 0) return 0;
    if (filter == 1) return a;
    if (filter == 2) return b;
    if (filter == 3) return (a + b) / 2;
    // Independent form of Paeth distances: b-c, a-c, a+b-2c.
    const int pa = std::abs(static_cast<int>(b) - static_cast<int>(c));
    const int pb = std::abs(static_cast<int>(a) - static_cast<int>(c));
    const int pc = std::abs(static_cast<int>(a) + static_cast<int>(b) - 2 * static_cast<int>(c));
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}
Bytes filtered(unsigned width, unsigned height, const Format& format, bool interlaced, unsigned filter) {
    constexpr std::array<unsigned, 7> sx{0,4,0,2,0,1,0};
    constexpr std::array<unsigned, 7> sy{0,0,4,0,2,0,1};
    constexpr std::array<unsigned, 7> dx{8,8,4,4,2,2,1};
    constexpr std::array<unsigned, 7> dy{8,8,8,4,4,2,2};
    Bytes raw{};
    const unsigned passes = interlaced ? 7 : 1;
    const unsigned stride = std::max(1U, format.channels * format.depth / 8);
    for (unsigned pass = 0; pass < passes; ++pass) {
        const unsigned start_x = interlaced ? sx[pass] : 0;
        const unsigned start_y = interlaced ? sy[pass] : 0;
        const unsigned step_x = interlaced ? dx[pass] : 1;
        const unsigned step_y = interlaced ? dy[pass] : 1;
        if (start_x >= width || start_y >= height) continue;
        const unsigned count = (width - start_x + step_x - 1) / step_x;
        const std::size_t row_bytes = (count * format.channels * format.depth + 7) / 8;
        Bytes previous(row_bytes, std::byte{0});
        for (unsigned y = start_y; y < height; y += step_y) {
            Bytes row(row_bytes, std::byte{0});
            std::size_t sample_index = 0;
            for (unsigned x = start_x; x < width; x += step_x) {
                for (unsigned channel = 0; channel < format.channels; ++channel) {
                    const unsigned value = value_at(x, y, channel, format);
                    if (format.depth == 16) {
                        row[sample_index * 2] = static_cast<std::byte>(value >> 8);
                        row[sample_index * 2 + 1] = static_cast<std::byte>(value & 255);
                    } else if (format.depth == 8) {
                        row[sample_index] = static_cast<std::byte>(value);
                    } else {
                        const std::size_t bit = sample_index * format.depth;
                        const unsigned shift = 8 - format.depth - static_cast<unsigned>(bit % 8);
                        row[bit / 8] |= static_cast<std::byte>(value << shift);
                    }
                    ++sample_index;
                }
            }
            raw.push_back(static_cast<std::byte>(filter));
            for (std::size_t index = 0; index < row_bytes; ++index) {
                const unsigned a = index < stride ? 0 : std::to_integer<unsigned>(row[index - stride]);
                const unsigned b = std::to_integer<unsigned>(previous[index]);
                const unsigned c = index < stride ? 0 : std::to_integer<unsigned>(previous[index - stride]);
                const unsigned predictor = reference_predictor(filter, a, b, c);
                raw.push_back(static_cast<std::byte>((std::to_integer<unsigned>(row[index]) - predictor) & 255U));
            }
            previous = row;
        }
    }
    return raw;
}
Bytes header(unsigned width, unsigned height, const Format& format, bool interlaced) {
    Bytes png{std::byte{137},std::byte{80},std::byte{78},std::byte{71},
              std::byte{13},std::byte{10},std::byte{26},std::byte{10}};
    Bytes ihdr{};
    put32(ihdr, width); put32(ihdr, height);
    ihdr.push_back(static_cast<std::byte>(format.depth));
    ihdr.push_back(static_cast<std::byte>(format.color));
    ihdr.push_back(std::byte{0}); ihdr.push_back(std::byte{0});
    ihdr.push_back(interlaced ? std::byte{1} : std::byte{0});
    chunk(png, "IHDR", ihdr);
    return png;
}
Bytes fixture(unsigned width, unsigned height, const Format& format, bool interlaced,
              unsigned filter, std::size_t split) {
    Bytes png = header(width, height, format, interlaced);
    if (format.color == 3) {
        Bytes palette{};
        Bytes alpha{};
        for (unsigned index = 0; index < (1U << format.depth); ++index) {
            palette.push_back(static_cast<std::byte>(index));
            palette.push_back(static_cast<std::byte>(255 - index));
            palette.push_back(static_cast<std::byte>((index * 17) & 255));
            alpha.push_back(static_cast<std::byte>(index));
        }
        chunk(png, "PLTE", palette); chunk(png, "tRNS", alpha);
    } else if (format.color == 0) {
        chunk(png, "tRNS", Bytes{std::byte{0}, std::byte{0}});
    } else if (format.color == 2) {
        chunk(png, "tRNS", Bytes{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{17}, std::byte{0}, std::byte{34}});
    }
    const Bytes raw = filtered(width, height, format, interlaced, filter);
    const Bytes compressed = stored(raw);
    for (std::size_t offset = 0; offset < compressed.size(); offset += split) {
        const std::size_t count = std::min(split, compressed.size() - offset);
        const Bytes payload(compressed.begin() + static_cast<std::ptrdiff_t>(offset),
                            compressed.begin() + static_cast<std::ptrdiff_t>(offset + count));
        chunk(png, "IDAT", payload);
    }
    chunk(png, "IDAT", {}); // Empty final IDAT must not become a spurious EOF.
    chunk(png, "IEND", {});
    return png;
}
struct Decoded final {
    stx_png::PngDecodeResult result{};
    std::vector<std::uint16_t> pixels{};
    Bytes metadata{};
    std::vector<stx_png::PngMetadataRecord> records{};
};
Decoded decode(const Bytes& png, const stx_png::PngDecodeLimits& limits = {}) {
    Decoded decoded{};
    decoded.result = stx_png::inspect_png_samples(png, limits);
    if (decoded.result.error != stx_png::PngDecodeError::none) return decoded;
    Bytes rows(decoded.result.layout.row_storage_bytes, std::byte{0});
    decoded.pixels.resize(decoded.result.layout.rgba_samples);
    decoded.records.resize(decoded.result.layout.metadata_records);
    decoded.metadata.resize(static_cast<std::size_t>(limits.metadata_bytes));
    std::unique_ptr<stx_zlib::InflateStream> workspace = std::make_unique<stx_zlib::InflateStream>();
    decoded.result = stx_png::decode_png_samples(png, limits, *workspace, rows,
        decoded.pixels, decoded.metadata, decoded.records);
    return decoded;
}
void verify_pixels(const Decoded& decoded, unsigned width, unsigned height, const Format& format) {
    require(decoded.result.error == stx_png::PngDecodeError::none, "valid fixture rejected");
    const unsigned scale = 65535 / ((1U << format.depth) - 1);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const unsigned first = value_at(x,y,0,format);
            std::array<unsigned,4> expected{first * scale, first * scale, first * scale, 65535};
            if (format.color == 3) {
                expected = {first * 257, (255 - first) * 257, ((first * 17) & 255) * 257, first * 257};
            }
            if (format.color == 2 || format.color == 6) {
                expected[1] = value_at(x,y,1,format) * scale;
                expected[2] = value_at(x,y,2,format) * scale;
            }
            if (format.color == 0 && first == 0) expected[3] = 0;
            if (format.color == 2 && first == 0 && value_at(x,y,1,format) == 17 && value_at(x,y,2,format) == 34) expected[3] = 0;
            if (format.color == 4 || format.color == 6) expected[3] = value_at(x,y,format.channels - 1,format) * scale;
            for (unsigned channel = 0; channel < 4; ++channel) {
                require(decoded.pixels[(y * width + x) * 4 + channel] == expected[channel], "sample mismatch");
            }
        }
    }
}
void compare_oracle(const Bytes& png, const Decoded& decoded) {
#if defined(STX_PNG_ORACLE)
    std::vector<unsigned char> output(decoded.pixels.size() * 2);
    const unsigned char* input = reinterpret_cast<const unsigned char*>(png.data());
    const int accepted = stx_png_oracle(input, png.size(), output.data(), output.size(),
        decoded.result.layout.width, decoded.result.layout.height);
    require(accepted != 0, "pinned libpng rejected matrix fixture");
    for (std::size_t index = 0; index < decoded.pixels.size(); ++index) {
        const unsigned expected = static_cast<unsigned>(output[index * 2]) * 256 + output[index * 2 + 1];
        require(decoded.pixels[index] == expected, "pinned libpng sample mismatch");
    }
#else
    (void)png;
    (void)decoded;
#endif
}
void matrix() {
    const stx_png::PngDecodeLimits limits{.metadata_bytes = 4096};
    constexpr std::array<unsigned, 17> widths{1,2,3,4,5,7,8,9,15,16,17,31,32,33,63,64,65};
    std::size_t cases = 0;
    for (const Format& format : formats) {
        for (bool interlaced : {false, true}) {
            for (unsigned width : widths) {
                for (unsigned height : {1U, 2U, 3U, 5U, 8U, 9U}) {
                    for (unsigned filter = 0; filter < 5; ++filter) {
                        const Bytes png = fixture(width, height, format, interlaced, filter, 37);
                        const Decoded decoded = decode(png, limits);
                        verify_pixels(decoded, width, height, format);
                        compare_oracle(png, decoded);
                        ++cases;
                    }
                }
            }
        }
    }
    for (const Format& format : formats) {
        if (format.color != 0 && format.color != 2) continue;
        Bytes png = header(9, 9, format, true);
        chunk(png, "IDAT", stored(filtered(9, 9, format, true, 4)));
        chunk(png, "IEND", {});
        const Decoded opaque = decode(png, limits);
        require(opaque.result.error == stx_png::PngDecodeError::none, "opaque selector failed");
        for (std::size_t index = 3; index < opaque.pixels.size(); index += 4) {
            require(opaque.pixels[index] == 65535, "opaque alpha changed");
        }
        compare_oracle(png, opaque);
    }
    const Bytes split = fixture(9, 9, formats[14], true, 4, 1);
    const Decoded decoded = decode(split, limits);
    verify_pixels(decoded, 9, 9, formats[14]);
    Bytes dynamic = header(257, 63, formats[13], false);
    for (std::byte value : stx_png_test::dynamic_idat) {
        chunk(dynamic, "IDAT", Bytes{value});
    }
    chunk(dynamic, "IEND", {});
    const Decoded dynamic_pixels = decode(dynamic, limits);
    verify_pixels(dynamic_pixels, 257, 63, formats[13]);
    compare_oracle(dynamic, dynamic_pixels);
    std::cout << "Format/filter/Adam7 matrix: " << cases << " cases, plus byte-split IDAT\n";
}
std::uint32_t reference_crc(std::span<const std::byte> bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (std::byte value : bytes) {
        crc ^= std::to_integer<std::uint32_t>(value);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const bool low_bit = (crc & 1U) != 0;
            crc >>= 1U;
            if (low_bit) crc ^= 0xedb88320U;
        }
    }
    crc ^= 0xffffffffU;
    return crc;
}
void checksum_boundaries() {
    Bytes storage(65551);
    std::uint32_t random = 1234567;
    for (std::byte& value : storage) {
        random = random * 1664525U + 1013904223U;
        value = static_cast<std::byte>(random >> 24U);
    }
    for (std::size_t offset = 0; offset < 16; ++offset) {
        for (std::size_t length = 0; length <= 257; ++length) {
            const std::span<const std::byte> bytes(storage.data() + offset, length);
            require(stx_zlib::crc32(bytes) == reference_crc(bytes), "bulk CRC alignment/tail mismatch");
        }
        const std::span<const std::byte> long_input(storage.data() + offset, 65535);
        require(stx_zlib::crc32(long_input) == reference_crc(long_input), "bulk CRC long input mismatch");
    }
    const Bytes check{std::byte{49},std::byte{50},std::byte{51},std::byte{52},std::byte{53},
                      std::byte{54},std::byte{55},std::byte{56},std::byte{57}};
    require(stx_zlib::crc32(check) == 0xcbf43926U, "IEEE CRC check vector mismatch");
}
void malformed_and_limits() {
    const Bytes valid = fixture(3, 2, formats[13], false, 0, 1000);
    for (std::size_t size = 0; size < valid.size(); ++size) {
        const Bytes cut(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(size));
        const Decoded decoded = decode(cut);
        require(decoded.result.error != stx_png::PngDecodeError::none, "truncation accepted");
    }
    Bytes corrupt = valid;
    corrupt[29] ^= std::byte{1};
    require(decode(corrupt).result.error == stx_png::PngDecodeError::crc_mismatch, "bad CRC accepted");
    corrupt = valid; corrupt.push_back(std::byte{0});
    require(decode(corrupt).result.error == stx_png::PngDecodeError::trailing_data, "trailing file byte accepted");
    stx_png::PngDecodeLimits limits{};
    const stx_png::PngDecodeResult layout = stx_png::inspect_png_samples(valid, limits);
    limits.filtered_bytes = layout.layout.filtered_bytes - 1;
    require(decode(valid, limits).result.error == stx_png::PngDecodeError::resource_limit, "filtered bound ignored");
    limits = {}; limits.pixels = 5;
    require(decode(valid, limits).result.error == stx_png::PngDecodeError::resource_limit, "pixel bound ignored");
    limits = {}; limits.inflate_work.value = 1;
    require(decode(valid, limits).result.inflate_error == stx_zlib::InflateError::work_limit_exceeded, "work bound ignored");
    limits = {}; limits.encoded_bytes = valid.size() - 1;
    require(decode(valid, limits).result.error == stx_png::PngDecodeError::resource_limit, "encoded bound ignored");
    limits = {}; limits.chunks = 1;
    require(decode(valid, limits).result.error == stx_png::PngDecodeError::resource_limit, "chunk bound ignored");
    for (unsigned mode = 0; mode < 6; ++mode) {
        Bytes png = header(1, 1, formats[13], false);
        Bytes raw{std::byte{0},std::byte{1},std::byte{2},std::byte{3},std::byte{4}};
        if (mode == 0) raw[0] = std::byte{5};
        if (mode == 1) raw.pop_back();
        if (mode == 2) raw.push_back(std::byte{0});
        Bytes stream = stored(raw);
        if (mode == 3) stream.push_back(std::byte{0});
        if (mode == 4) stream.back() ^= std::byte{1};
        if (mode == 5) stream.pop_back();
        chunk(png, "IDAT", stream); chunk(png, "IEND", {});
        require(decode(png).result.error != stx_png::PngDecodeError::none, "malformed image stream accepted");
    }
    Bytes png = header(1, 1, formats[10], false);
    chunk(png, "PLTE", Bytes{std::byte{0},std::byte{0},std::byte{0}});
    chunk(png, "IDAT", stored(Bytes{std::byte{0},std::byte{1}}));
    chunk(png, "IEND", {});
    require(decode(png).result.error == stx_png::PngDecodeError::invalid_palette_index, "palette overrun accepted");
}
Bytes text_image(const char* kind, const Bytes& field) {
    Bytes png = header(1, 1, formats[13], false);
    chunk(png, kind, field);
    chunk(png, "IDAT", stored(Bytes{std::byte{0},std::byte{1},std::byte{2},std::byte{3},std::byte{4}}));
    chunk(png, "IEND", {});
    return png;
}
Bytes international_field(const Bytes& content, const Bytes& translated, bool compressed,
                          unsigned method = 0) {
    Bytes field{std::byte{'K'}, std::byte{0}, compressed ? std::byte{1} : std::byte{0},
        static_cast<std::byte>(method), std::byte{0}};
    field.insert(field.end(), translated.begin(), translated.end());
    field.push_back(std::byte{0});
    if (compressed) {
        const Bytes stream = stored(content);
        field.insert(field.end(), stream.begin(), stream.end());
    } else {
        field.insert(field.end(), content.begin(), content.end());
    }
    return field;
}
void text_metadata() {
    const stx_png::PngDecodeLimits limits{.metadata_bytes=4096};
    const Bytes word{std::byte{'a'}};
    std::unique_ptr<stx_zlib::InflateStream> measured_workspace = std::make_unique<stx_zlib::InflateStream>();
    std::array<std::byte, 1> measured_scratch{};
    for (unsigned method = 0; method < 256; ++method) {
        const Bytes plain = text_image("iTXt", international_field(word, {}, false, method));
        require(decode(plain, limits).result.error == stx_png::PngDecodeError::none,
                "uncompressed iTXt compression method was not ignored");
        const Bytes compressed = text_image("iTXt", international_field(word, {}, true, method));
        const stx_png::PngDecodeError expected = method == 0
            ? stx_png::PngDecodeError::none : stx_png::PngDecodeError::invalid_metadata;
        require(decode(compressed, limits).result.error == expected, "compressed iTXt method validation failed");
    }
    const std::array<Bytes, 23> invalid{{
        {std::byte{0}}, {std::byte{0x80}}, {std::byte{0xbf}}, {std::byte{0xc0},std::byte{0x80}},
        {std::byte{0xc1},std::byte{0xbf}}, {std::byte{0xc2}}, {std::byte{0xc2},std::byte{0x7f}},
        {std::byte{0xc2},std::byte{0xc0}}, {std::byte{0xe0}}, {std::byte{0xe0},std::byte{0xa0}},
        {std::byte{0xe0},std::byte{0x9f},std::byte{0xbf}},
        {std::byte{0xed},std::byte{0xa0},std::byte{0x80}}, {std::byte{0xed},std::byte{0xbf},std::byte{0xbf}},
        {std::byte{0xef},std::byte{0xbf},std::byte{0x7f}}, {std::byte{0xf0}},
        {std::byte{0xf0},std::byte{0x90}}, {std::byte{0xf0},std::byte{0x90},std::byte{0x80}},
        {std::byte{0xf0},std::byte{0x8f},std::byte{0xbf},std::byte{0xbf}},
        {std::byte{0xf4},std::byte{0x90},std::byte{0x80},std::byte{0x80}},
        {std::byte{0xf5},std::byte{0x80},std::byte{0x80},std::byte{0x80}},
        {std::byte{0xf8},std::byte{0x88},std::byte{0x80},std::byte{0x80},std::byte{0x80}},
        {std::byte{0xfe}}, {std::byte{0xff}}}};
    for (const Bytes& malformed : invalid) {
        for (bool compressed : {false, true}) {
            const Bytes png = text_image("iTXt", international_field(malformed, {}, compressed));
            const stx_png::PngDecodeResult inspection = stx_png::inspect_png_samples(png, limits);
            const stx_png::PngDecodeError inspected_error = compressed
                ? stx_png::PngDecodeError::none : stx_png::PngDecodeError::invalid_metadata;
            require(inspection.error == inspected_error, "text validation occurred at the wrong boundary");
            require(decode(png, limits).result.error == stx_png::PngDecodeError::invalid_metadata,
                    "invalid iTXt text accepted");
            if (compressed) {
                const stx_png::PngDecodeResult measured = stx_png::measure_png_samples(
                    png, limits, *measured_workspace, measured_scratch);
                require(measured.error == stx_png::PngDecodeError::invalid_metadata,
                        "streamed metadata preflight accepted invalid UTF-8");
            }
            // A zero in a translated keyword is its delimiter, not UTF-8 data.
            if (malformed[0] == std::byte{0}) continue;
            const Bytes translated = text_image("iTXt", international_field(word, malformed, compressed));
            require(decode(translated, limits).result.error == stx_png::PngDecodeError::invalid_metadata,
                    "invalid translated keyword accepted");
        }
    }
    const Bytes split_scalar{std::byte{0xf0}, std::byte{0x9f}, std::byte{0x99}, std::byte{0x82}};
    const Bytes split_png = text_image("iTXt", international_field(split_scalar, {}, true));
    const stx_png::PngDecodeResult split_measurement = stx_png::measure_png_samples(
        split_png, limits, *measured_workspace, measured_scratch);
    require(split_measurement.error == stx_png::PngDecodeError::none &&
            split_measurement.metadata_bytes == split_scalar.size(), "split UTF-8 preflight failed");
    Bytes latin{};
    for (unsigned value = 1; value < 256; ++value) latin.push_back(static_cast<std::byte>(value));
    for (bool compressed : {false, true}) {
        Bytes field{std::byte{'K'}, std::byte{0}};
        if (compressed) field.push_back(std::byte{0});
        const std::size_t prefix = field.size();
        const Bytes content = compressed ? stored(latin) : latin;
        field.insert(field.end(), content.begin(), content.end());
        const char* kind = compressed ? "zTXt" : "tEXt";
        require(decode(text_image(kind, field), limits).result.error == stx_png::PngDecodeError::none,
                "nonzero Latin-1 text rejected");
        field.resize(prefix);
        const Bytes zero = compressed ? stored(Bytes{std::byte{0}}) : Bytes{std::byte{0}};
        field.insert(field.end(), zero.begin(), zero.end());
        require(decode(text_image(kind, field), limits).result.error == stx_png::PngDecodeError::invalid_metadata,
                "embedded Latin-1 NUL accepted");
        field.resize(prefix);
        if (compressed) {
            const Bytes empty = stored({});
            field.insert(field.end(), empty.begin(), empty.end());
        }
        require(decode(text_image(kind, field), limits).result.error == stx_png::PngDecodeError::none,
                "empty Latin-1 text rejected");
        require(decode(text_image("iTXt", international_field({}, {}, compressed)), limits).result.error ==
                stx_png::PngDecodeError::none, "empty UTF-8 text rejected");
    }
    // Encode every permitted Unicode scalar independently of the byte-class
    // validator, including noncharacters. U+0000 and surrogates are excluded.
    Bytes unicode{};
    unicode.reserve(4 * 0x110000);
    for (unsigned scalar = 1; scalar <= 0x10ffff; ++scalar) {
        if (scalar >= 0xd800 && scalar <= 0xdfff) continue;
        if (scalar <= 0x7f) {
            unicode.push_back(static_cast<std::byte>(scalar));
        } else if (scalar <= 0x7ff) {
            unicode.push_back(static_cast<std::byte>(0xc0U | (scalar >> 6)));
            unicode.push_back(static_cast<std::byte>(0x80U | (scalar & 63U)));
        } else if (scalar <= 0xffff) {
            unicode.push_back(static_cast<std::byte>(0xe0U | (scalar >> 12)));
            unicode.push_back(static_cast<std::byte>(0x80U | ((scalar >> 6) & 63U)));
            unicode.push_back(static_cast<std::byte>(0x80U | (scalar & 63U)));
        } else {
            unicode.push_back(static_cast<std::byte>(0xf0U | (scalar >> 18)));
            unicode.push_back(static_cast<std::byte>(0x80U | ((scalar >> 12) & 63U)));
            unicode.push_back(static_cast<std::byte>(0x80U | ((scalar >> 6) & 63U)));
            unicode.push_back(static_cast<std::byte>(0x80U | (scalar & 63U)));
        }
    }
    const stx_png::PngDecodeLimits large{.metadata_bytes=8ULL * 1024 * 1024};
    std::array<std::byte, 4096> unicode_scratch{};
    for (bool compressed : {false, true}) {
        const Bytes unicode_png = text_image("iTXt", international_field(unicode, {}, compressed));
        require(decode(unicode_png, large).result.error ==
                stx_png::PngDecodeError::none, "valid Unicode scalar rejected");
        if (compressed) {
            const stx_png::PngDecodeResult measured = stx_png::measure_png_samples(
                unicode_png, large, *measured_workspace, unicode_scratch);
            require(measured.error == stx_png::PngDecodeError::none &&
                    measured.metadata_bytes == unicode.size(), "Unicode preflight byte count changed");
        }
    }
    require(decode(text_image("iTXt", international_field({}, unicode, false)), large).result.error ==
            stx_png::PngDecodeError::none, "valid translated Unicode scalar rejected");
    std::cout << "PNG text: 256 methods, malformed UTF-8/NUL, and all nonzero Unicode scalars passed\n";
}
void metadata_and_storage() {
    Bytes png = header(1, 1, formats[13], false);
    const Bytes content(64, std::byte{65});
    const Bytes compressed = stored(content);
    Bytes field{std::byte{65},std::byte{0},std::byte{0}};
    field.insert(field.end(), compressed.begin(), compressed.end());
    chunk(png, "iCCP", field);
    chunk(png, "zTXt", field);
    Bytes international{std::byte{65},std::byte{0},std::byte{1},std::byte{0},std::byte{0},std::byte{0}};
    international.insert(international.end(), compressed.begin(), compressed.end());
    chunk(png, "iTXt", international);
    chunk(png, "IDAT", stored(Bytes{std::byte{0},std::byte{1},std::byte{2},std::byte{3},std::byte{4}}));
    chunk(png, "IEND", {});
    const Decoded decoded = decode(png);
    require(decoded.result.error == stx_png::PngDecodeError::none, "compressed metadata rejected");
    require(decoded.result.metadata_bytes == 192 && decoded.records.size() == 3, "metadata accounting mismatch");
    for (std::size_t index = 0; index < 192; ++index) require(decoded.metadata[index] == std::byte{65}, "metadata byte mismatch");
    std::unique_ptr<stx_zlib::InflateStream> measured_workspace = std::make_unique<stx_zlib::InflateStream>();
    std::array<std::byte, 1> measured_scratch{};
    const stx_png::PngDecodeResult measured = stx_png::measure_png_samples(
        png, {}, *measured_workspace, measured_scratch);
    require(measured.error == stx_png::PngDecodeError::none && measured.metadata_bytes == 192 &&
            measured.layout.metadata_records == 3, "exact metadata preflight failed");
    require(stx_png::measure_png_samples(png, {}, *measured_workspace, {}).error ==
            stx_png::PngDecodeError::invalid_argument, "empty metadata scratch accepted");
    const std::span<std::byte> aliased_scratch(png.data(), 1);
    require(stx_png::measure_png_samples(png, {}, *measured_workspace, aliased_scratch).error ==
            stx_png::PngDecodeError::invalid_argument, "aliased metadata scratch accepted");
    stx_png::PngDecodeLimits limits{};
    limits.inflate_work.value = decoded.result.inflate_work.value - 1;
    require(decode(png, limits).result.inflate_error == stx_zlib::InflateError::work_limit_exceeded, "metadata reset restored work budget");
    Bytes bomb = header(1,1,formats[13],false);
    // Python zlib 1.2.12, level 9: 4096 bytes of 'A'; oracle fixture only.
    const Bytes compact{std::byte{120},std::byte{218},std::byte{237},std::byte{193},std::byte{1},std::byte{13},std::byte{0},std::byte{0},std::byte{0},std::byte{194},std::byte{160},std::byte{108},std::byte{239},std::byte{95},std::byte{202},std::byte{30},std::byte{14},std::byte{40},std::byte{0},std::byte{0},std::byte{0},std::byte{224},std::byte{221},std::byte{0},std::byte{255},std::byte{173},std::byte{16},std::byte{61}};
    Bytes compressed_text{std::byte{65},std::byte{0},std::byte{0}};
    compressed_text.insert(compressed_text.end(), compact.begin(), compact.end());
    chunk(bomb, "zTXt", compressed_text);
    chunk(bomb, "zTXt", compressed_text);
    chunk(bomb, "IDAT", stored(Bytes{std::byte{0},std::byte{1},std::byte{2},std::byte{3},std::byte{4}}));
    chunk(bomb, "IEND", {});
    limits = {}; limits.metadata_bytes = 6000;
    require(decode(bomb, limits).result.inflate_error == stx_zlib::InflateError::output_limit_exceeded,
            "metadata reset restored output budget");
    const stx_png::PngDecodeResult measured_bomb = stx_png::measure_png_samples(
        bomb, limits, *measured_workspace, measured_scratch);
    require(measured_bomb.error == stx_png::PngDecodeError::inflate_failure &&
            measured_bomb.inflate_error == stx_zlib::InflateError::output_limit_exceeded,
            "metadata preflight restored output budget");
    Bytes policy_image = header(1, 1, formats[13], false);
    Bytes policy_field{std::byte{65}, std::byte{0}, std::byte{0}};
    const Bytes policy_stream = stored(Bytes(16, std::byte{65}));
    policy_field.insert(policy_field.end(), policy_stream.begin(), policy_stream.end());
    for (unsigned index = 0; index < 4; ++index) chunk(policy_image, "zTXt", policy_field);
    chunk(policy_image, "IDAT", stored(Bytes{std::byte{0},std::byte{1},std::byte{2},std::byte{3},std::byte{4}}));
    chunk(policy_image, "IEND", {});
    const Decoded policy_reference = decode(policy_image);
    require(policy_reference.result.error == stx_png::PngDecodeError::none, "policy fixture failed");
    // Byte output may legally overwrite an external settings object's unsigned
    // fields. The active request's policy must already be a private value copy.
    struct AliasedPolicy final {
        stx_png::PngDecodeLimits limits{};
        std::array<std::byte, 64> tail{};
    };
    AliasedPolicy policy{};
    policy.limits.inflate_work.value = policy_reference.result.inflate_work.value - 1;
    const std::span<AliasedPolicy> policy_objects(&policy, 1);
    const std::span<std::byte> policy_storage = std::as_writable_bytes(policy_objects);
    std::array<std::byte,10> policy_rows{};
    std::array<std::uint16_t,4> policy_pixels{};
    std::array<stx_png::PngMetadataRecord,4> policy_records{};
    std::unique_ptr<stx_zlib::InflateStream> policy_workspace = std::make_unique<stx_zlib::InflateStream>();
    const stx_png::PngDecodeResult policy_result = stx_png::decode_png_samples(policy_image, policy.limits,
        *policy_workspace, policy_rows, policy_pixels, policy_storage, policy_records);
    require(policy_result.inflate_error == stx_zlib::InflateError::work_limit_exceeded,
            "output overwrote active decode policy");

    const stx_png::PngDecodeResult inspection = stx_png::inspect_png_samples(png, {});
    Bytes rows(inspection.layout.row_storage_bytes, std::byte{0});
    std::array<std::uint16_t,4> pixels{91,91,91,91};
    std::array<stx_png::PngMetadataRecord,3> records{};
    Bytes output(measured.metadata_bytes, std::byte{0});
    std::unique_ptr<stx_zlib::InflateStream> workspace = std::make_unique<stx_zlib::InflateStream>();
    const stx_png::PngDecodeResult exact = stx_png::decode_png_samples(png, {}, *workspace, rows, pixels, output, records);
    require(exact.error == stx_png::PngDecodeError::none, "exact metadata buffer rejected");
    const stx_png::PngDecodeResult short_metadata = stx_png::decode_png_samples(png, {}, *workspace,
        rows, pixels, std::span<std::byte>(output).first(output.size() - 1), records);
    require(short_metadata.error == stx_png::PngDecodeError::insufficient_storage,
            "short metadata output accepted");
    const stx_png::PngDecodeResult short_rows = stx_png::decode_png_samples(png, {}, *workspace,
        std::span<std::byte>(rows).first(rows.size() - 1), pixels, output, records);
    require(short_rows.error == stx_png::PngDecodeError::insufficient_storage, "short rows accepted");
    const stx_png::PngDecodeResult overlap = stx_png::decode_png_samples(png, {}, *workspace, rows,
        pixels, rows, records);
    require(overlap.error == stx_png::PngDecodeError::invalid_argument, "alias accepted");
}
} // namespace
void write_corpus(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    unsigned index = 0;
    for (const Format& format : formats) {
        for (bool interlaced : {false, true}) {
            for (unsigned filter = 0; filter < 5; ++filter) {
                const Bytes png = fixture(17, 9, format, interlaced, filter, 37);
                const std::filesystem::path path = directory / ("matrix-" + std::to_string(index) + ".png");
                std::ofstream stream(path, std::ios::binary);
                stream.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
                require(static_cast<bool>(stream), "could not write corpus seed");
                ++index;
            }
        }
    }
    struct TextSeed final { const char* name = nullptr; Bytes png{}; };
    const Bytes valid{std::byte{'A'}, std::byte{0xe2}, std::byte{0x82}, std::byte{0xac}};
    const Bytes invalid{std::byte{0xed}, std::byte{0xa0}, std::byte{0x80}};
    const std::array<TextSeed, 6> text_seeds{{
        {.name="text-plain-utf8", .png=text_image("iTXt", international_field(valid, valid, false))},
        {.name="text-compressed-utf8", .png=text_image("iTXt", international_field(valid, {}, true))},
        {.name="text-ignored-method", .png=text_image("iTXt", international_field(valid, {}, false, 255))},
        {.name="text-invalid-plain", .png=text_image("iTXt", international_field(invalid, {}, false))},
        {.name="text-invalid-compressed", .png=text_image("iTXt", international_field(invalid, {}, true))},
        {.name="text-nul", .png=text_image("tEXt", Bytes{std::byte{'K'}, std::byte{0}, std::byte{'A'}, std::byte{0}})}
    }};
    for (const TextSeed& seed : text_seeds) {
        const std::filesystem::path path = directory / (std::string(seed.name) + ".png");
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(seed.png.data()), static_cast<std::streamsize>(seed.png.size()));
        require(static_cast<bool>(stream), "could not write text corpus seed");
    }
}
void write_benchmarks(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    for (unsigned format_index : {10U, 13U, 14U}) {
        const Format& format = formats[format_index];
        for (bool interlaced : {false, true}) {
            for (unsigned filter = 0; filter < 5; ++filter) {
                const Bytes png = fixture(256, 256, format, interlaced, filter, 65535);
                const std::string name = "color" + std::to_string(format.color) + "-depth" +
                    std::to_string(format.depth) + "-adam7-" + std::to_string(interlaced) +
                    "-filter" + std::to_string(filter) + ".png";
                const std::filesystem::path path = directory / name;
                std::ofstream stream(path, std::ios::binary);
                stream.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
                require(static_cast<bool>(stream), "could not write benchmark fixture");
            }
        }
    }
}
int main(int argc, char** argv) {
    try {
        matrix();
        checksum_boundaries();
        malformed_and_limits();
        metadata_and_storage();
        text_metadata();
        if (argc == 2) {
            const std::filesystem::path corpus(argv[1]);
            write_corpus(corpus);
            const std::filesystem::path workloads = corpus.parent_path() / "workloads";
            write_benchmarks(workloads);
        }
        std::cout << "Owned PNG sample decoder checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
