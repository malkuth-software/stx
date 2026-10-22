#include <stx_zlib/deflate.hpp>
#include <stx_zlib/inflate.hpp>

#include <algorithm>
#include <array>
#include <memory>

int main() {
    std::array<std::byte, 65537> original{};
    for (std::size_t index = 0; index < original.size(); ++index) {
        original[index] = static_cast<std::byte>((index * 13 + index / 137) & 255);
    }
    std::array<std::byte, 131074> encoded{};
    std::array<std::byte, 65537> decoded{};
    std::unique_ptr<stx_zlib::DeflateStream> encoder_owner = std::make_unique<stx_zlib::DeflateStream>();
    std::unique_ptr<stx_zlib::InflateStream> decoder_owner = std::make_unique<stx_zlib::InflateStream>();
    stx_zlib::DeflateStream& encoder = *encoder_owner;
    stx_zlib::InflateStream& decoder = *decoder_owner;
    stx_zlib::DeflateOptions encoding{};
    encoding.input_limit = stx_zlib::ByteCount{original.size()};
    encoding.output_limit = stx_zlib::ByteCount{encoded.size()};
    encoding.work_limit = stx_zlib::WorkCount{1000000000};
    if (encoder.reset(encoding) != stx_zlib::DeflateError::none) return 1;
    const stx_zlib::DeflateResult compressed = encoder.process(original, encoded, stx_zlib::DeflateFlush::finish);
    if (compressed.status != stx_zlib::DeflateStatus::stream_end) return 2;
    if (compressed.input_consumed != original.size()) return 3;
    stx_zlib::InflateOptions decoding{};
    decoding.limits.input_bytes = stx_zlib::ByteCount{compressed.output_produced};
    decoding.limits.output_bytes = stx_zlib::ByteCount{decoded.size()};
    decoding.limits.work = stx_zlib::WorkCount{1000000000};
    if (decoder.reset(decoding) != stx_zlib::InflateError::none) return 4;
    const std::span<const std::byte> input = std::span<const std::byte>{encoded}.first(compressed.output_produced);
    const stx_zlib::InflateResult restored = decoder.process(input, decoded, true);
    if (restored.status != stx_zlib::InflateStatus::stream_end) return 5;
    if (restored.input_consumed != compressed.output_produced || restored.output_produced != original.size()) return 6;
    if (!std::equal(original.begin(), original.end(), decoded.begin())) return 7;
    return 0;
}
