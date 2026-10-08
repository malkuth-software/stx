#pragma once
#include "ogg.hpp"
#include <string_view>
namespace stx_vorbis {
struct StreamInfo final {
    std::uint32_t chain_index{0};
    std::uint32_t serial{0};
    std::uint32_t sample_rate{0};
    std::uint32_t channels{0};
    std::uint32_t small_block{0};
    std::uint32_t large_block{0};
    std::uint32_t codebooks{0};
    std::uint32_t floors{0};
    std::uint32_t residues{0};
    std::uint32_t mappings{0};
    std::uint32_t modes{0};
    std::uint64_t granule_origin{0}; // Positive broadcast offset; sample positions remain stream-relative.
    std::uint64_t emitted_frames{0}; // Actual frames consumed, excluding recovery gaps.
    std::uint64_t next_sample{0}; // Stream-relative coordinate of the next PCM frame.
    std::uint64_t total_frames{0};
    bool total_known{false};
    bool timeline_discontinuous{false};
    bool position_known{true};
    MemoryReport memory{};
};
enum class EventKind : std::uint8_t { stream_begin, stream_end, diagnostic };
struct Event final { EventKind kind{EventKind::diagnostic}; StreamInfo stream{}; Diagnostic diagnostic{}; };
struct PcmView final {
    // Channel-major borrowed storage: channel c starts at samples+c*stride.
    // Only [0,frames) of each channel is live. Vorbis channel order is preserved.
    const float* samples{nullptr};
    std::size_t stride{0};
    std::uint32_t channels{0};
    std::uint32_t frames{0};
    std::uint32_t sample_rate{0};
    std::uint32_t chain_index{0};
    std::uint64_t first_sample{0};
    [[nodiscard]] std::span<const float> channel(std::uint32_t index) const noexcept;
};
class Decoder final {
public:
    explicit Decoder(Limits limits = {}, Recovery recovery = Recovery::strict,
                     Synthesis synthesis = Synthesis::automatic, std::pmr::memory_resource* memory = nullptr);
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    [[nodiscard]] FeedResult push(std::span<const std::uint8_t> bytes, bool final = false) noexcept;
    // Returns the same pcm/event until consumed/acknowledged; does not accumulate
    // output behind it. A fatal status remains sticky until reset.
    [[nodiscard]] Status advance() noexcept;
    [[nodiscard]] PcmView output() const noexcept;
    [[nodiscard]] Status consume(std::uint32_t frames) noexcept;
    [[nodiscard]] Event event() const noexcept;
    void acknowledge_event() noexcept;
    [[nodiscard]] StreamInfo info() const noexcept;
    [[nodiscard]] Diagnostic diagnostic() const noexcept;
    // Metadata views survive PCM consumption. After acknowledging stream_end,
    // the next advance may replace them while parsing the next chain. reset also invalidates them.
    [[nodiscard]] std::string_view vendor() const noexcept;
    [[nodiscard]] std::size_t comment_count() const noexcept;
    [[nodiscard]] std::string_view comment(std::size_t index) const noexcept;
    [[nodiscard]] std::string_view tag(std::string_view key, std::size_t occurrence = 0) const noexcept;
    [[nodiscard]] std::uint64_t byte_offset() const noexcept;
    // Drop transport/overlap state while retaining validated setup (push seeking).
    // Recovery must be resynchronize; a later granule reanchors the PCM timeline.
    [[nodiscard]] Status discontinuity() noexcept;
    void reset() noexcept;
private:
    struct State;
    struct StateDeleter final {
        std::pmr::memory_resource* memory{nullptr};
        void operator()(State* state) const noexcept;
    };
    std::unique_ptr<State, StateDeleter> state_{nullptr, StateDeleter{}};
};
// Conversion fills caller storage without consuming the view. Input/output must
// not overlap. int16 rounds to nearest, ties away from zero, then saturates.
[[nodiscard]] Status copy_interleaved(PcmView source, std::span<float> destination) noexcept;
[[nodiscard]] Status copy_interleaved(PcmView source, std::span<std::int16_t> destination) noexcept;
[[nodiscard]] Status copy_planar(PcmView source, std::span<std::span<float>> destination) noexcept;
[[nodiscard]] Status copy_planar(PcmView source, std::span<std::span<std::int16_t>> destination) noexcept;
} // namespace stx_vorbis
