#include "stx_zlib/checksum.hpp"

#include <algorithm>
#include <array>

namespace stx_zlib {
namespace {

// Eight byte positions, 256 values each: table[k] advances a CRC by one byte
// followed by k zero bytes, so eight table reads consume eight bytes at once
// (slicing by eight). The tables encode only the reflected IEEE polynomial;
// they are not CRC-32C instruction tables.
using CrcTables = std::array<std::array<std::uint32_t, 256>, 8>;
constexpr CrcTables make_crc_tables() noexcept {
    CrcTables tables{};
    for (std::uint32_t index = 0; index < 256U; ++index) {
        std::uint32_t value = index;
        for (unsigned bit = 0; bit < 8; ++bit) {
            value = (value & 1U) != 0U ? 0xedb88320U ^ (value >> 1U) : value >> 1U;
        }
        tables[0][index] = value;
    }
    for (std::size_t position = 1; position < tables.size(); ++position) {
        for (std::size_t index = 0; index < 256; ++index) {
            const std::uint32_t previous = tables[position - 1][index];
            tables[position][index] = tables[0][previous & 255U] ^ (previous >> 8U);
        }
    }
    return tables;
}
constexpr CrcTables crc_tables = make_crc_tables();

constexpr std::uint32_t adler_modulus = 65521U;
// The largest run whose sums, with any previous residues, fit in uint32:
// (m - 1) + n (m - 1) + 255 n (n + 1) / 2 < 2^32 for n = 5552.
constexpr std::size_t adler_run = 5552U;

} // namespace

std::uint32_t crc32_update(std::uint32_t crc, std::span<const std::byte> bytes) noexcept {
    std::uint32_t state = crc ^ 0xffffffffU;
    const std::size_t interior = bytes.size() - bytes.size() % 8U;
    std::size_t offset = 0;
    for (; offset < interior; offset += 8U) {
        // Explicit little-endian assembly: unaligned input and no read beyond
        // the live span.
        const std::uint32_t first = std::to_integer<std::uint32_t>(bytes[offset]) |
            (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
            (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
            (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
        const std::uint32_t combined = state ^ first;
        state = crc_tables[7][combined & 255U] ^ crc_tables[6][(combined >> 8U) & 255U] ^
                crc_tables[5][(combined >> 16U) & 255U] ^ crc_tables[4][combined >> 24U] ^
                crc_tables[3][std::to_integer<unsigned>(bytes[offset + 4U])] ^
                crc_tables[2][std::to_integer<unsigned>(bytes[offset + 5U])] ^
                crc_tables[1][std::to_integer<unsigned>(bytes[offset + 6U])] ^
                crc_tables[0][std::to_integer<unsigned>(bytes[offset + 7U])];
    }
    for (; offset < bytes.size(); ++offset) {
        const unsigned value = std::to_integer<unsigned>(bytes[offset]);
        state = crc_tables[0][(state ^ value) & 255U] ^ (state >> 8U);
    }
    const std::uint32_t result = state ^ 0xffffffffU;
    return result;
}

std::uint32_t crc32(std::span<const std::byte> bytes) noexcept {
    const std::uint32_t result = crc32_update(0U, bytes);
    return result;
}

std::uint32_t adler32_update(std::uint32_t adler, std::span<const std::byte> bytes) noexcept {
    // A state with a residue at or above the modulus is not an Adler-32
    // value; reducing it first keeps the run bound valid for any argument.
    std::uint32_t low = (adler & 0xffffU) % adler_modulus;
    std::uint32_t high = (adler >> 16U) % adler_modulus;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t count = std::min(adler_run, bytes.size() - offset);
        const std::uint32_t length = static_cast<std::uint32_t>(count);
        // Independent sums (plain and position-weighted) instead of a serial
        // per-byte recurrence: the same integer result, and loops a compiler
        // may vectorize at common widths.
        std::uint32_t sum = 0;
        std::uint32_t weighted = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const std::uint32_t value = std::to_integer<std::uint32_t>(bytes[offset + index]);
            sum += value;
            weighted += (length - static_cast<std::uint32_t>(index)) * value;
        }
        const std::uint32_t next_high = high + length * low + weighted;
        low = (low + sum) % adler_modulus;
        high = next_high % adler_modulus;
        offset += count;
    }
    const std::uint32_t result = (high << 16U) | low;
    return result;
}

std::uint32_t adler32(std::span<const std::byte> bytes) noexcept {
    const std::uint32_t result = adler32_update(1U, bytes);
    return result;
}

} // namespace stx_zlib
