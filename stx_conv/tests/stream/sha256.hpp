#pragma once
// FIPS 180-4 SHA-256 for test digests. Test-only; not part of the library.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace stx_conv_tests {

class Sha256 final {
public:
    Sha256() noexcept;
    // Appends bytes; the borrow ends with the call.
    void update(std::span<const std::byte> bytes) noexcept;
    // Pads, finishes and returns 64 lowercase hexadecimal digits. The object
    // is then reset to an empty message.
    [[nodiscard]] std::string finish_hex();

private:
    void reset() noexcept;
    void compress_block() noexcept;

    std::array<std::uint32_t, 8> state{};
    std::array<std::uint8_t, 64> block{};
    std::size_t block_length = 0;
    std::uint64_t message_bytes = 0;
};

} // namespace stx_conv_tests
