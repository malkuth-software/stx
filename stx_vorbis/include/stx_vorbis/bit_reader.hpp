#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace stx_vorbis {
// Vorbis least-significant-bit packing. A failed read does not advance.
class BitReader final {
public:
    explicit BitReader(std::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}
    [[nodiscard]] bool read(unsigned int count, std::uint32_t& value) noexcept;
    [[nodiscard]] bool read64(unsigned int count, std::uint64_t& value) noexcept;
    [[nodiscard]] bool peek(unsigned int count, std::uint32_t& value) const noexcept;
    [[nodiscard]] bool skip(std::size_t count) noexcept;
    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    [[nodiscard]] std::size_t remaining() const noexcept;
private:
    std::span<const std::uint8_t> bytes_{};
    std::size_t position_{0};
};
} // namespace stx_vorbis
