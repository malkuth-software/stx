#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace stx_zlib {

// The library's one implementation of each checksum (phase 4: checksum
// consolidation). Portable code only, with no platform intrinsics: identical
// results on every host. Read-only input; unaligned spans and any length
// (including empty) are valid. No allocation and no retained borrow.

// IEEE CRC-32: reflected polynomial 0xEDB88320, initial and final XOR
// 0xFFFFFFFF, as PNG and gzip use. Not CRC-32C. crc32({}) == 0 and
// crc32("123456789") == 0xCBF43926.
[[nodiscard]] std::uint32_t crc32(std::span<const std::byte> bytes) noexcept;
// Continues a CRC-32: crc32_update(crc32(a), b) == crc32(a followed by b),
// and crc32_update(0, b) == crc32(b).
[[nodiscard]] std::uint32_t crc32_update(std::uint32_t crc, std::span<const std::byte> bytes) noexcept;

// Adler-32 (RFC 1950): adler32({}) == 1 and adler32("123456789") == 0x091E01DE.
[[nodiscard]] std::uint32_t adler32(std::span<const std::byte> bytes) noexcept;
// Continues an Adler-32: adler32_update(adler32(a), b) == adler32(a followed
// by b), and adler32_update(1, b) == adler32(b).
[[nodiscard]] std::uint32_t adler32_update(std::uint32_t adler, std::span<const std::byte> bytes) noexcept;

} // namespace stx_zlib
