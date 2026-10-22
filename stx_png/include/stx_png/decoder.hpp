#pragma once

#include "stx_zlib/inflate.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace stx_png {

enum class PngDecodeError {
    none, invalid_argument, resource_limit, invalid_signature, truncated_chunk,
    invalid_chunk_type, crc_mismatch, invalid_header, invalid_order,
    invalid_palette, invalid_transparency, invalid_metadata, unknown_critical,
    missing_image, trailing_data, insufficient_storage, inflate_failure,
    invalid_filtered_size, invalid_filter, invalid_palette_index
};

// Provisional experiment limits, not a new production registry policy.
struct PngDecodeLimits final {
    std::uint64_t encoded_bytes = 32ULL * 1024 * 1024;
    std::uint64_t pixels = 4096ULL * 4096;
    std::uint64_t filtered_bytes = 129ULL * 1024 * 1024;
    std::uint64_t metadata_bytes = 4ULL * 1024 * 1024;
    stx_zlib::WorkCount inflate_work{512ULL * 1024 * 1024};
    std::uint32_t dimension = 4096;
    std::uint32_t chunks = 4096;
};

struct PngSampleLayout final {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    unsigned depth = 0;
    unsigned color = 0;
    bool interlaced = false;
    std::size_t rgba_samples = 0;
    std::size_t row_storage_bytes = 0;
    std::uint64_t filtered_bytes = 0;
    std::size_t metadata_records = 0;
};

// Offsets refer to the original encoded span, or the separate decoded metadata
// storage. No borrow is retained. A compressed field excludes its keyword and
// other uncompressed prefix, which remains available through encoded_offset.
struct PngMetadataRecord final {
    std::uint32_t type = 0;
    std::size_t encoded_offset = 0;
    std::size_t encoded_length = 0;
    bool compressed = false;
    std::size_t decoded_offset = 0;
    std::size_t decoded_length = 0;
};

struct PngDecodeResult final {
    PngDecodeError error = PngDecodeError::none;
    stx_zlib::InflateError inflate_error = stx_zlib::InflateError::none;
    PngSampleLayout layout{};
    std::size_t metadata_bytes = 0;
    stx_zlib::WorkCount inflate_work{};
};

// Structural inspection does not validate compressed contents or palette indices.
[[nodiscard]] PngDecodeResult inspect_png_samples(
    std::span<const std::byte> encoded, PngDecodeLimits limits) noexcept;

// Optional preflight for exact decoded metadata capacity. Structural parsing
// runs first; then each compressed metadata field is validated into reusable
// caller scratch, including its checksum and text encoding. Scratch must be
// nonempty and disjoint from encoded bytes and workspace. The call retains no
// borrow and may overwrite scratch and workspace. metadata_bytes is exact on
// success and counts only complete compressed fields on failure; scratch then
// contains an unverified prefix. inflate_work charges this preflight only.
// IDAT and palette indices are validated later by decode, which does not
// trust this result.
[[nodiscard]] PngDecodeResult measure_png_samples(
    std::span<const std::byte> encoded, PngDecodeLimits limits,
    stx_zlib::InflateStream& workspace, std::span<std::byte> scratch) noexcept;

// Private lossless sample seam: native uint16 RGBA, top row first, straight alpha,
// no colour conversion. All spans and workspace must be mutually disjoint.
// Limits are copied at entry, so output cannot mutate the active policy.
// No allocation or retained borrow. One execution context per workspace.
// On any failure, discard all output (possibly partial). Structural validation
// and row/sample/record capacity checks precede mutation; compressed metadata
// exhaustion can follow partial output. Text encoding is checked without
// normalization; ICC profiles and language tags are not semantically validated.
[[nodiscard]] PngDecodeResult decode_png_samples(
    std::span<const std::byte> encoded, PngDecodeLimits limits,
    stx_zlib::InflateStream& workspace, std::span<std::byte> rows,
    std::span<std::uint16_t> rgba, std::span<std::byte> metadata,
    std::span<PngMetadataRecord> records) noexcept;

// As decode_png_samples, for a consumer that needs the pixels but not the
// decoded metadata. Each compressed field (iCCP, zTXt, compressed iTXt) is
// inflated once through the nonempty, reusable scratch, with the same
// checksum, text and quota checks, and then discarded, so no storage
// proportional to the metadata limit is needed. Records describe every
// ancillary chunk; their decoded_offset and decoded_length are zero, and
// metadata_bytes reports the validated decoded total. The image and the
// metadata share one inflate work quota. Scratch joins the disjoint storage.
[[nodiscard]] PngDecodeResult decode_png_samples_discarding_metadata(
    std::span<const std::byte> encoded, PngDecodeLimits limits,
    stx_zlib::InflateStream& workspace, std::span<std::byte> rows,
    std::span<std::uint16_t> rgba, std::span<std::byte> scratch,
    std::span<PngMetadataRecord> records) noexcept;

} // namespace stx_png
