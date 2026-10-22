#pragma once

#include "stx_zlib/inflate.hpp"

namespace stx_zlib {

enum class DeflateFormat { zlib, raw_deflate };
enum class DeflateStrategy { normal, filtered, huffman_only, rle, fixed };
enum class DeflateFlush { none, sync, finish };
enum class DeflateStatus { need_input, need_output, flush_complete, stream_end, error };
enum class DeflateError {
    none, invalid_argument, invalid_state, input_limit_exceeded,
    output_limit_exceeded, work_limit_exceeded, workspace_overflow
};
struct DeflateOptions final {
    DeflateFormat format = DeflateFormat::zlib;
    DeflateStrategy strategy = DeflateStrategy::normal;
    int level = 6;
    unsigned window_bits = 15;
    unsigned memory_level = 8;
    ByteCount input_limit{};
    ByteCount output_limit{};
    WorkCount work_limit{};
};
struct DeflateResult final {
    DeflateStatus status = DeflateStatus::error;
    DeflateError error = DeflateError::invalid_state;
    std::size_t input_consumed = 0;
    std::size_t output_produced = 0;
    ByteCount total_input{};
    ByteCount total_output{};
    WorkCount total_work{};
};

// Caller-owned bounded workspace; no allocation or retained input/output borrow.
// Input, output and this object must be disjoint. One execution context per
// stream. Invalid reset preserves the old stream. Other errors freeze it until
// reset; discard any emitted prefix on failure. Repeat flush with an unconsumed
// input tail. Once all input for sync/finish is accepted, drain with empty input
// until flush_complete/stream_end. Output spans never choose block boundaries.
class DeflateStream final {
public:
    DeflateStream() noexcept {}
    DeflateStream(const DeflateStream&) = delete;
    DeflateStream& operator=(const DeflateStream&) = delete;
    DeflateStream(DeflateStream&&) = delete;
    DeflateStream& operator=(DeflateStream&&) = delete;
    [[nodiscard]] DeflateError reset(const DeflateOptions& options) noexcept;
    [[nodiscard]] DeflateResult process(std::span<const std::byte> input,
        std::span<std::byte> output, DeflateFlush flush) noexcept;

private:
    static constexpr std::size_t block_capacity = 32768;
    struct Token final { std::uint16_t value = 0; std::uint16_t distance = 0; };
    struct Match final {
        unsigned length = 0;
        unsigned distance = 0;
        std::uint32_t work = 0;
        bool exhausted = false;
    };
    struct Prefix final { unsigned bucket = 0; unsigned tag = 0; };
    struct CodeRun final { unsigned symbol = 0; unsigned extra = 0; unsigned bits = 0; };
    struct Node final { std::uint64_t weight = 0; unsigned depth = 0; unsigned parent = 0; };
    struct WeightedSymbol final { std::uint32_t weight = 0; unsigned symbol = 0; };
    struct WeightOrder final {
        bool operator()(const WeightedSymbol& left, const WeightedSymbol& right) const noexcept;
    };
    enum class Phase { idle, active, syncing, finishing, done, failed };

    bool fail(DeflateError error) noexcept;
    bool charge(std::uint64_t amount) noexcept;
    void adler(std::span<const std::byte> bytes) noexcept;
    Prefix prefix_at(std::size_t position) const noexcept;
    void insert(std::size_t position) noexcept;
    void rebase_links(std::span<const std::uint32_t> source,
        std::span<std::uint32_t> destination, unsigned discarded, unsigned retained) noexcept;
    Match find_match(std::size_t position, std::size_t end, unsigned link,
        unsigned tag, unsigned minimum_length, std::uint64_t allowance) const noexcept;
    template<bool Checked>
    Match search_match(std::size_t position, std::size_t maximum, unsigned attempts,
        unsigned link, unsigned tag, unsigned minimum_length, std::uint64_t allowance) const noexcept;
    Match search_checked(std::size_t position, std::size_t maximum, unsigned attempts,
        unsigned link, unsigned tag, unsigned minimum_length, std::uint64_t allowance) const noexcept;
    bool tokenize() noexcept;
    void append_token(unsigned value, unsigned distance, std::size_t& count) noexcept;
    bool build_tree(std::span<const std::uint32_t> frequencies,
        std::span<std::uint8_t> lengths, std::span<std::uint16_t> codes,
        unsigned maximum) noexcept;
    bool prepare_dynamic() noexcept;
    bool write_bits(unsigned value, unsigned count) noexcept;
    bool align_bits() noexcept;
    bool emit_tokens(bool dynamic) noexcept;
    bool encode_block(bool final) noexcept;
    bool finish_boundary(DeflateFlush flush) noexcept;

    DeflateOptions options_{};
    Phase phase_ = Phase::idle;
    DeflateError error_ = DeflateError::none;
    ByteCount input_total_{};
    ByteCount output_total_{};
    WorkCount work_total_{};
    unsigned level_ = 6;
    unsigned hash_count_ = 32768;
    unsigned hash_shift_ = 5;
    unsigned chain_limit_ = 128;
    unsigned nice_length_ = 128;
    unsigned good_length_ = 8;
    unsigned lazy_limit_ = 16;
    std::size_t window_size_ = 32768;
    // Live prefix: history_live_ bytes followed by block_live_ input bytes.
    std::array<std::byte, 2 * block_capacity> data_{};
    std::size_t history_live_ = 0;
    std::size_t block_live_ = 0;
    bool hash_ready_ = false;
    // Links pack a 16-bit position and the first two bytes of its prefix.
    // Position 65535 is absent: a three-byte key can start at most at 65533.
    std::array<std::uint32_t, 65536> heads_{};
    std::array<std::uint32_t, 2 * block_capacity> previous_{};
    std::array<Token, block_capacity> tokens_{};
    std::size_t token_count_ = 0;
    std::array<std::uint32_t, 286> literal_frequency_{};
    std::array<std::uint32_t, 30> distance_frequency_{};
    std::array<std::uint8_t, 286> literal_length_{};
    std::array<std::uint8_t, 30> distance_length_{};
    std::array<std::uint16_t, 286> literal_code_{};
    std::array<std::uint16_t, 30> distance_code_{};
    std::array<std::uint8_t, 19> run_length_{};
    std::array<std::uint16_t, 19> run_code_{};
    std::array<CodeRun, 316> runs_{};
    unsigned run_count_ = 0;
    unsigned literal_count_ = 257;
    unsigned distance_count_ = 1;
    unsigned code_count_ = 4;
    std::uint64_t extra_cost_ = 0;
    std::uint64_t dynamic_cost_ = 0;
    // Reused Huffman construction storage; largest alphabet is 286 symbols.
    std::array<Node, 576> nodes_{};
    std::array<WeightedSymbol, 288> sorted_{};
    // Encoded bytes awaiting publication. At most one canonical block + flush.
    std::array<std::byte, 65536> pending_{};
    std::size_t pending_size_ = 0;
    std::size_t pending_read_ = 0;
    std::uint32_t bit_buffer_ = 0;
    unsigned bit_count_ = 0;
    std::uint32_t adler_low_ = 1;
    std::uint32_t adler_high_ = 0;
};

static_assert(sizeof(DeflateStream) <= 1024U * 1024U);

} // namespace stx_zlib
