#include "text_scan.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace stx_rtf2md::detail {

namespace {

std::uint32_t byte_at(std::string_view text, std::size_t offset) noexcept {
    const unsigned char byte = static_cast<unsigned char>(text[offset]);
    return static_cast<std::uint32_t>(byte);
}

bool continuation(std::uint32_t byte) noexcept {
    return (byte & 0xC0U) == 0x80U;
}

bool within(std::uint32_t scalar, std::uint32_t first, std::uint32_t last) noexcept {
    return scalar >= first && scalar <= last;
}

}

ScalarRead read_scalar(std::string_view text, std::size_t offset, std::size_t end) noexcept {
    ScalarRead read{};
    const std::uint32_t lead = byte_at(text, offset);
    const std::size_t available = end - offset;
    if (lead < 0x80U) {
        read.valid = true;
        read.scalar = lead;
        read.length = 1;
        return read;
    }
    std::size_t length = 0;
    std::uint32_t scalar = 0;
    std::uint32_t minimum = 0;
    if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2;
        scalar = lead & 0x1FU;
        minimum = 0x80U;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3;
        scalar = lead & 0x0FU;
        minimum = 0x800U;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4;
        scalar = lead & 0x07U;
        minimum = 0x10000U;
    } else {
        return read;
    }
    if (available < length) {
        return read;
    }
    for (std::size_t index = 1; index < length; ++index) {
        const std::uint32_t byte = byte_at(text, offset + index);
        if (!continuation(byte)) {
            return read;
        }
        scalar = (scalar << 6U) | (byte & 0x3FU);
    }
    const bool surrogate = scalar >= 0xD800U && scalar <= 0xDFFFU;
    if (scalar < minimum || surrogate || scalar > 0x10FFFFU) {
        return read;
    }
    read.valid = true;
    read.scalar = scalar;
    read.length = length;
    return read;
}

void append_scalar(std::string& text, std::uint32_t scalar) {
    if (scalar < 0x80U) {
        text.push_back(static_cast<char>(scalar));
    } else if (scalar < 0x800U) {
        text.push_back(static_cast<char>(0xC0U | (scalar >> 6U)));
        text.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    } else if (scalar < 0x10000U) {
        text.push_back(static_cast<char>(0xE0U | (scalar >> 12U)));
        text.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    } else {
        text.push_back(static_cast<char>(0xF0U | (scalar >> 18U)));
        text.push_back(static_cast<char>(0x80U | ((scalar >> 12U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    }
}

bool forbidden_scalar(std::uint32_t scalar, bool line_feed_admitted) noexcept {
    const bool admitted_control = scalar == 0x09U || (line_feed_admitted && scalar == 0x0AU);
    const bool control = (scalar < 0x20U && !admitted_control) || scalar == 0x7FU;
    const bool separator = scalar == 0x2028U || scalar == 0x2029U;
    return control || separator || scalar == 0xFEFFU;
}

bool is_ascii_punctuation(std::uint32_t scalar) noexcept {
    const bool first = within(scalar, 0x21U, 0x2FU);
    const bool second = within(scalar, 0x3AU, 0x40U);
    const bool third = within(scalar, 0x5BU, 0x60U);
    const bool fourth = within(scalar, 0x7BU, 0x7EU);
    return first || second || third || fourth;
}

bool is_unicode_whitespace(std::uint32_t scalar) noexcept {
    const bool ascii = scalar == 0x09U || scalar == 0x0AU || scalar == 0x0CU || scalar == 0x0DU || scalar == 0x20U;
    const bool separator = scalar == 0xA0U || scalar == 0x1680U || within(scalar, 0x2000U, 0x200AU)
        || scalar == 0x202FU || scalar == 0x205FU || scalar == 0x3000U;
    return ascii || separator;
}

bool is_unicode_punctuation(std::uint32_t scalar) noexcept {
    if (scalar < 0x80U) {
        return is_ascii_punctuation(scalar);
    }
    const bool latin = within(scalar, 0xA1U, 0xBFU) || scalar == 0xD7U || scalar == 0xF7U;
    const bool general = within(scalar, 0x2010U, 0x2027U) || within(scalar, 0x2030U, 0x205EU);
    const bool supplemental = within(scalar, 0x2E00U, 0x2E7FU);
    const bool cjk = within(scalar, 0x3001U, 0x303FU);
    const bool fullwidth = within(scalar, 0xFF01U, 0xFF0FU) || within(scalar, 0xFF1AU, 0xFF20U)
        || within(scalar, 0xFF3BU, 0xFF40U) || within(scalar, 0xFF5BU, 0xFF65U);
    return latin || general || supplemental || cjk || fullwidth;
}

std::uint32_t scalar_before(std::string_view text, std::size_t offset) noexcept {
    if (offset == 0) {
        return 0x0AU;
    }
    // Step back over at most three continuation bytes to the lead byte.
    std::size_t start = offset - 1U;
    std::size_t steps = 0;
    while (start > 0 && steps < 3U && continuation(byte_at(text, start))) {
        start -= 1U;
        steps += 1U;
    }
    const ScalarRead read = read_scalar(text, start, offset);
    if (!read.valid || start + read.length != offset) {
        return 0xFFFDU;
    }
    return read.scalar;
}

std::uint32_t scalar_at(std::string_view text, std::size_t offset) noexcept {
    if (offset >= text.size()) {
        return 0x0AU;
    }
    const ScalarRead read = read_scalar(text, offset, text.size());
    if (!read.valid) {
        return 0xFFFDU;
    }
    return read.scalar;
}

void EffortMeter::reset(std::uint64_t allowance) noexcept {
    used_ = 0;
    allowance_ = allowance;
    exhausted_ = false;
}

bool EffortMeter::charge(std::uint64_t amount) noexcept {
    if (exhausted_) {
        return false;
    }
    if (amount > allowance_ - used_) {
        used_ = allowance_;
        exhausted_ = true;
        return false;
    }
    used_ += amount;
    return true;
}

bool EffortMeter::exhausted() const noexcept {
    return exhausted_;
}

std::uint64_t EffortMeter::used() const noexcept {
    return used_;
}

}
