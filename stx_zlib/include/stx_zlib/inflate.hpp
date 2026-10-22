#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace stx_zlib {

struct ByteCount final { std::uint64_t value = 0; };
// Implementation-specific effort units, not CPU cycles or stable counts
// across buffer schedules/decoder versions. All configured limits are finite.
struct WorkCount final { std::uint64_t value = 0; };

enum class InflateFormat { zlib, raw_deflate };
enum class InflateStatus { need_input, need_output, stream_end, error };
enum class InflateError {
    none, invalid_argument, invalid_state, input_limit_exceeded,
    output_limit_exceeded, work_limit_exceeded, truncated_input,
    invalid_method, invalid_window, invalid_header_check, dictionary_required,
    invalid_block_type, stored_length_mismatch, invalid_code_counts,
    oversubscribed_tree, incomplete_tree, missing_end_of_block,
    invalid_code_length_repeat, invalid_huffman_code, invalid_literal_length,
    invalid_distance_code, distance_before_history, distance_exceeds_window,
    adler_mismatch
};

struct InflateLimits final {
    ByteCount input_bytes{};
    ByteCount output_bytes{};
    WorkCount work{};
};

struct InflateOptions final {
    InflateFormat format = InflateFormat::zlib;
    unsigned window_bits = 15;
    InflateLimits limits{};
};

struct InflateResult final {
    InflateStatus status = InflateStatus::error;
    InflateError error = InflateError::invalid_state;
    std::size_t input_consumed = 0;
    std::size_t output_produced = 0;
    ByteCount total_input{};
    ByteCount total_output{};
    WorkCount total_work{};
    std::uint32_t dictionary_id = 0;
};

// The caller owns this fixed workspace (less than 64 KiB), preferably off-stack.
// No allocation, callbacks, exceptions or retained input/output borrows. One
// execution context per stream. Input, output and workspace must be disjoint.
// A returned prefix is unverified until stream_end; discard it on any error.
// Errors freeze the decoder until reset. Invalid reset leaves old state intact.
// final_input describes the supplied extent: repeat it with any unconsumed tail.
// Empty input is not EOF. Exact stream end leaves subsequent bytes unconsumed.
class InflateStream final {
public:
    InflateStream() noexcept {}
    InflateStream(const InflateStream&) = delete;
    InflateStream& operator=(const InflateStream&) = delete;
    InflateStream(InflateStream&&) = delete;
    InflateStream& operator=(InflateStream&&) = delete;

    [[nodiscard]] InflateError reset(const InflateOptions& options) noexcept;
    [[nodiscard]] InflateResult process(std::span<const std::byte> input,
                                       std::span<std::byte> output,
                                       bool final_input) noexcept;

private:
    // Canonical trees: counts by bit length, then live symbols ordered by
    // length and symbol value. 288 is the largest RFC 1951 alphabet.
    struct Tree final {
        std::array<std::uint16_t, 16> counts{};
        std::array<std::uint16_t, 288> symbols{};
        // Nine low-order stream bits select a packed symbol/length. Zero
        // entries use canonical traversal (long codes or an invalid prefix).
        std::array<std::uint16_t, 512> lookup{};
        unsigned maximum_length = 0;
    };
    struct CodeCursor final {
        unsigned length = 1;
        unsigned code = 0;
        unsigned first = 0;
        unsigned index = 0;
    };
    struct Call final {
        std::span<const std::byte> input{};
        std::span<std::byte> output{};
        std::size_t read = 0;
        std::size_t written = 0;
        bool final_input = false;
        InflateStatus status = InflateStatus::need_input;
    };
    enum class Phase {
        idle, header, dictionary, block, stored_length, stored_complement,
        stored_bytes, dynamic_counts, code_lengths, expanded_lengths, repeat,
        literal, emit_literal, length_extra, distance, distance_extra, match,
        trailer, done, failed
    };

    bool fail(InflateError error) noexcept;
    bool charge(std::uint64_t amount) noexcept;
    bool bits(Call& call, unsigned count, unsigned& value) noexcept;
    bool symbol(Call& call, const Tree& tree, unsigned& value) noexcept;
    bool build_tree(Tree& tree, std::span<const std::uint8_t> lengths,
                    bool code_alphabet) noexcept;
    bool fixed_trees() noexcept;
    static constexpr Tree make_fixed_tree(bool distance_alphabet) noexcept;
    bool output_ready(Call& call) noexcept;
    void emit(Call& call, std::byte value) noexcept;
    void publish(Call& call, std::size_t count) noexcept;
    bool stored_bytes(Call& call) noexcept;
    bool copy_match(Call& call) noexcept;
    bool literal_run(Call& call) noexcept;
    void end_block() noexcept;
    bool step(Call& call) noexcept;

    InflateOptions options_{};
    Phase phase_ = Phase::idle;
    InflateError error_ = InflateError::none;
    ByteCount input_total_{};
    ByteCount output_total_{};
    WorkCount work_total_{};
    std::array<std::byte, 32768> history_{};
    std::size_t history_next_ = 0;
    std::size_t history_live_ = 0;
    std::size_t window_size_ = 32768;
    Tree literals_{};
    Tree distances_{};
    Tree code_lengths_{};
    // 286 literal/length + 30 distance lengths; 320 also holds fixed lengths.
    std::array<std::uint8_t, 320> lengths_{};
    std::array<std::uint8_t, 19> alphabet_lengths_{};
    CodeCursor code_{};
    std::uint32_t bit_buffer_ = 0;
    unsigned bit_count_ = 0;
    unsigned literal_count_ = 0;
    unsigned distance_count_ = 0;
    unsigned alphabet_count_ = 0;
    unsigned length_index_ = 0;
    unsigned repeat_symbol_ = 0;
    unsigned match_length_ = 0;
    unsigned match_distance_ = 0;
    unsigned extra_bits_ = 0;
    unsigned literal_byte_ = 0;
    unsigned stored_left_ = 0;
    bool final_block_ = false;
    std::uint32_t adler_low_ = 1;
    std::uint32_t adler_high_ = 0;
    std::uint32_t trailer_ = 0;
    unsigned trailer_bytes_ = 0;
    std::uint32_t dictionary_id_ = 0;
};

static_assert(sizeof(InflateStream) < 64U * 1024U);

} // namespace stx_zlib
