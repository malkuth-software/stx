// Installed stx_png consumer: encode an image, inspect and decode it, convert
// the samples back to 8-bit, and refuse malformed input, using only the
// installed headers and the exported stx_png::stx_png target.
#include <stx_png/decoder.hpp>
#include <stx_png/encoder.hpp>
#include <stx_png/pixels.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "stx_png: %s\n", message);
    std::abort();
}
} // namespace

int main() {
    constexpr std::uint32_t width = 37;
    constexpr std::uint32_t height = 23;
    std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4U);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        pixels[index] = static_cast<std::byte>((index * 29U + index / 97U) & 255U);
    }
    stx_png::PngEncodeInput input{};
    input.width = width;
    input.height = height;
    input.row_bytes = static_cast<std::uint64_t>(width) * 4U;
    input.pixels = pixels;
    std::vector<std::byte> png{};
    require(stx_png::encode_png(input, png), "encode refused a valid image");

    const stx_png::PngDecodeResult inspected = stx_png::inspect_png_samples(png, {});
    require(inspected.error == stx_png::PngDecodeError::none, "inspection refused the encoded file");
    require(inspected.layout.width == width && inspected.layout.height == height, "dimensions changed");
    std::unique_ptr<stx_zlib::InflateStream> workspace = std::make_unique<stx_zlib::InflateStream>();
    std::vector<std::byte> rows(inspected.layout.row_storage_bytes);
    std::vector<std::uint16_t> samples(inspected.layout.rgba_samples);
    std::vector<std::byte> scratch(256);
    std::vector<stx_png::PngMetadataRecord> records(inspected.layout.metadata_records);
    const stx_png::PngDecodeResult decoded = stx_png::decode_png_samples_discarding_metadata(
        png, {}, *workspace, rows, samples, scratch, records);
    require(decoded.error == stx_png::PngDecodeError::none, "decode refused the encoded file");

    std::vector<std::byte> restored(pixels.size());
    const stx_png::PixelTarget straight{stx_png::PixelOrder::rgba, stx_png::PixelAlpha::straight};
    require(stx_png::convert_rgba16_samples(samples, width, height, straight, restored, width * 4U),
            "conversion refused");
    require(restored == pixels, "round trip changed pixels");

    std::vector<std::byte> broken = png;
    broken[20] ^= std::byte{1};
    require(stx_png::inspect_png_samples(broken, {}).error == stx_png::PngDecodeError::crc_mismatch,
            "corrupt header accepted");
    return 0;
}
