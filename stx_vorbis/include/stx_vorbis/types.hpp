#pragma once
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string_view>
#include <limits>

namespace stx_vorbis {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
enum class Status : std::uint8_t {
    ok, need_input, need_output, packet, pcm, event, end,
    invalid_argument, invalid_header, corrupt_page, sequence_gap, invalid_packet,
    truncated, resource_limit, allocation_failed, unsupported, io_error
};
enum class Recovery : std::uint8_t { strict, resynchronize };
enum class Synthesis : std::uint8_t { automatic, scalar, neon, sse2, avx2 };
struct Limits final {
    std::size_t memory_bytes{128 * 1024 * 1024};
    std::size_t input_bytes{256 * 1024};
    std::size_t packet_bytes{16 * 1024 * 1024};
    std::size_t metadata_bytes{1024 * 1024};
    std::size_t codebook_entries{1024 * 1024};
    std::size_t lookup_values{8 * 1024 * 1024};
    std::size_t resync_bytes{1024 * 1024};
    std::uint64_t packet_operations{32 * 1024 * 1024};
    std::uint32_t comments{16384};
    std::uint32_t logical_streams{16};
    std::uint32_t chained_streams{4096};
    std::uint32_t channels{255};
    std::uint32_t output_frames{4096};
};
struct Diagnostic final {
    Status code{Status::ok};
    std::uint64_t byte_offset{0};
    std::uint64_t packet_index{0};
    std::uint32_t serial{0};
    std::uint32_t page_sequence{0};
    std::size_t bit_offset{0}; // Reader-relative bit; zero when the failing check has no specific bit site.
    bool recoverable{false};
};
struct FeedResult final { Status status{Status::ok}; std::size_t accepted{0}; };
struct MemoryReport final {
    std::size_t setup_bytes{0};
    std::size_t workspace_bytes{0};
    std::size_t current_bytes{0};
    std::size_t peak_bytes{0};
};
[[nodiscard]] const char* status_name(Status status) noexcept;
[[nodiscard]] bool synthesis_available(Synthesis implementation) noexcept;
} // namespace stx_vorbis
