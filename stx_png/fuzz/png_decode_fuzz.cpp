#include "stx_png/decoder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);
    const std::span<const std::byte> bytes = std::as_bytes(input);
    const stx_png::PngDecodeLimits limits{
        .encoded_bytes = 65536, .pixels = 4096, .filtered_bytes = 65536,
        .metadata_bytes = 16384, .inflate_work = {1000000}, .dimension = 64, .chunks = 128};
    const stx_png::PngDecodeResult inspected = stx_png::inspect_png_samples(bytes, limits);
    if (inspected.error != stx_png::PngDecodeError::none) return 0;
    std::unique_ptr<stx_zlib::InflateStream> workspace = std::make_unique<stx_zlib::InflateStream>();
    std::array<std::byte, 32> scratch{};
    const std::span<std::byte> bounded_scratch(scratch.data(), 1 + size % scratch.size());
    const stx_png::PngDecodeResult measured = stx_png::measure_png_samples(
        bytes, limits, *workspace, bounded_scratch);
    std::vector<std::byte> rows(inspected.layout.row_storage_bytes);
    std::vector<std::uint16_t> pixels(inspected.layout.rgba_samples);
    const std::size_t metadata_capacity = measured.error == stx_png::PngDecodeError::none
        ? measured.metadata_bytes : static_cast<std::size_t>(limits.metadata_bytes);
    std::vector<std::byte> metadata(metadata_capacity);
    std::vector<stx_png::PngMetadataRecord> records(inspected.layout.metadata_records);
    const stx_png::PngDecodeResult decoded = stx_png::decode_png_samples(bytes, limits,
        *workspace, rows, pixels, metadata, records);
    if (decoded.inflate_work.value > limits.inflate_work.value || decoded.metadata_bytes > limits.metadata_bytes) {
        std::abort();
    }
    if (measured.inflate_work.value > limits.inflate_work.value || measured.metadata_bytes > limits.metadata_bytes) {
        std::abort();
    }
    if (decoded.error == stx_png::PngDecodeError::none &&
        (measured.error != stx_png::PngDecodeError::none || measured.metadata_bytes != decoded.metadata_bytes)) {
        std::abort();
    }
    return 0;
}

// Repair only chunk CRCs after mutation so fuzzing reaches deflate, filters and
// metadata as well as the envelope. Truncated/invalid envelopes stay malformed.
#if !defined(STX_PNG_FUZZ_STANDALONE)
#include "stx_zlib/checksum.hpp"
extern "C" std::size_t LLVMFuzzerMutate(std::uint8_t*, std::size_t, std::size_t);
extern "C" std::size_t LLVMFuzzerCustomMutator(std::uint8_t* data, std::size_t size,
                                              std::size_t maximum, unsigned seed) {
    (void)seed;
    const std::size_t changed = LLVMFuzzerMutate(data, size, maximum);
    std::size_t offset = 8;
    while (offset <= changed && changed - offset >= 12) {
        const std::uint32_t length = (static_cast<std::uint32_t>(data[offset]) << 24U) |
            (static_cast<std::uint32_t>(data[offset + 1]) << 16U) |
            (static_cast<std::uint32_t>(data[offset + 2]) << 8U) | data[offset + 3];
        if (length > changed - offset - 12) break;
        const std::span<const std::uint8_t> checked(data + offset + 4, length + 4ULL);
        const std::span<const std::byte> bytes = std::as_bytes(checked);
        const std::uint32_t crc = stx_zlib::crc32(bytes);
        for (unsigned index = 0; index < 4; ++index) {
            data[offset + 8 + length + index] = static_cast<std::uint8_t>(crc >> (24 - index * 8));
        }
        offset += length + 12ULL;
    }
    return changed;
}
#else
int main() {
    const std::uint8_t empty = 0;
    const int result = LLVMFuzzerTestOneInput(&empty, 0);
    return result;
}
#endif
