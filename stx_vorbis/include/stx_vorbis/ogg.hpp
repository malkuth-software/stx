#pragma once
#include "types.hpp"
#include <memory>
namespace stx_vorbis {
struct OggPacket final {
    std::span<const std::uint8_t> bytes{};
    std::uint64_t granule{0};
    std::uint64_t byte_offset{0};
    std::uint64_t index{0};
    std::uint32_t serial{0};
    std::uint32_t sequence{0};
    bool has_granule{false};
    bool beginning{false};
    bool ending{false};
    bool discontinuity{false};
    // Remaining page fragments permit bounded codec lookahead, without copying.
    // These two views expire on next_packet OR push (input compaction).
    std::span<const std::uint8_t> following_lacing{};
    std::span<const std::uint8_t> following_bytes{};
    bool page_has_granule{false};
    bool page_ending{false};
};
struct OggStreamEnd final {
    std::uint32_t serial{0};
    std::uint32_t sequence{0};
    std::uint64_t granule{0};
    bool has_granule{false};
};
// Codec-independent, CRC-checked Ogg framing, including multiplexed serials.
// push accepts a prefix; accepted bytes are never submitted again. Packet views
// remain valid until next_packet/reset/destruction. Calls belong to one thread.
class OggDemuxer final {
public:
    explicit OggDemuxer(Limits limits = {}, Recovery recovery = Recovery::strict,
                        std::pmr::memory_resource* memory = nullptr);
    ~OggDemuxer();
    OggDemuxer(const OggDemuxer&) = delete;
    OggDemuxer& operator=(const OggDemuxer&) = delete;
    [[nodiscard]] FeedResult push(std::span<const std::uint8_t> bytes, bool final = false) noexcept;
    [[nodiscard]] Status next_packet(OggPacket& packet) noexcept;
    // next_packet returns event for an empty EOS page; inspect this value once.
    [[nodiscard]] OggStreamEnd stream_end_event() const noexcept;
    [[nodiscard]] Diagnostic diagnostic() const noexcept;
    [[nodiscard]] MemoryReport memory_report() const noexcept;
    [[nodiscard]] std::uint64_t byte_offset() const noexcept;
    void reset() noexcept;
private:
    struct State;
    struct StateDeleter final {
        std::pmr::memory_resource* memory{nullptr};
        void operator()(State* state) const noexcept;
    };
    std::unique_ptr<State, StateDeleter> state_{nullptr, StateDeleter{}};
};
[[nodiscard]] std::uint32_t ogg_crc(std::span<const std::uint8_t> page) noexcept;
} // namespace stx_vorbis
