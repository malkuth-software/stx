#pragma once

// Private helpers shared by the model check and the markdown reader and
// writer: UTF-8 decoding and encoding, the dialect's character classes and
// an effort meter. Not installed.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace stx_rtf2md::detail {

struct ScalarRead final {
    bool valid = false;
    std::uint32_t scalar = 0;
    std::size_t length = 0;
};

// Decodes the scalar at offset within [offset, end); end is at most
// text.size() and greater than offset. Rejects overlong forms, surrogates,
// values above U+10FFFF and sequences cut by end.
[[nodiscard]] ScalarRead read_scalar(std::string_view text, std::size_t offset, std::size_t end) noexcept;

// Appends the UTF-8 form of a valid scalar.
void append_scalar(std::string& text, std::uint32_t scalar);

// The model forbids C0 controls other than tab (and line feed in code
// blocks), DEL, U+2028, U+2029 and U+FEFF.
[[nodiscard]] bool forbidden_scalar(std::uint32_t scalar, bool line_feed_admitted) noexcept;

[[nodiscard]] bool is_ascii_punctuation(std::uint32_t scalar) noexcept;

// Unicode whitespace for flanking: tab, line feed, form feed, carriage
// return, space and the Zs characters.
[[nodiscard]] bool is_unicode_whitespace(std::uint32_t scalar) noexcept;

// Punctuation for flanking: ASCII punctuation, Latin-1 punctuation and
// symbols, and the General Punctuation, Supplemental Punctuation, CJK
// Symbols and Punctuation and fullwidth ASCII punctuation blocks. This is a
// stated subset of Unicode's P and S categories; reader and writer use the
// same one, so the writer's emphasis check stays exact.
[[nodiscard]] bool is_unicode_punctuation(std::uint32_t scalar) noexcept;

// The scalar ending just before offset, or U+000A (treated as whitespace)
// at the start of text. Invalid bytes read as U+FFFD.
[[nodiscard]] std::uint32_t scalar_before(std::string_view text, std::size_t offset) noexcept;

// The scalar starting at offset, or U+000A at the end of text.
[[nodiscard]] std::uint32_t scalar_at(std::string_view text, std::size_t offset) noexcept;

// Counts effort units against a fixed allowance. Once exhausted it stays
// exhausted; callers stop at the next check.
class EffortMeter final {
public:
    void reset(std::uint64_t allowance) noexcept;
    // Charges amount; returns false once the allowance is exceeded.
    [[nodiscard]] bool charge(std::uint64_t amount) noexcept;
    [[nodiscard]] bool exhausted() const noexcept;
    [[nodiscard]] std::uint64_t used() const noexcept;

private:
    std::uint64_t used_ = 0;
    std::uint64_t allowance_ = 0;
    bool exhausted_ = false;
};

}
