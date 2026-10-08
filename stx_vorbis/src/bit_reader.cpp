#include "stx_vorbis/bit_reader.hpp"
#include <limits>
#include <bit>
#include <cstring>
namespace stx_vorbis {
std::size_t BitReader::remaining() const noexcept {
    if (bytes_.size() > std::numeric_limits<std::size_t>::max() / 8) return 0;
    const std::size_t result = bytes_.size() * 8 - position_;
    return result;
}
bool BitReader::peek(const unsigned int count, std::uint32_t& value) const noexcept {
    if (count > 32 || count > remaining()) return false;
    if (count == 0) { value = 0; return true; }
    std::uint64_t accumulator = 0;
    const std::size_t start = position_ / 8;
    const unsigned int shift = static_cast<unsigned int>(position_ % 8);
    const unsigned int bytes = (shift + count + 7) / 8;
    // The full load is legal only inside the borrowed packet. memcpy avoids
    // alignment and aliasing assumptions; the short tail never overreads.
    if (std::endian::native == std::endian::little && bytes_.size() - start >= sizeof(accumulator)) {
        std::memcpy(&accumulator, bytes_.data() + start, sizeof(accumulator));
    } else {
        for (unsigned int index = 0; index < bytes; ++index)
            accumulator |= static_cast<std::uint64_t>(bytes_[start + index]) << (index * 8);
    }
    const std::uint64_t mask = (std::uint64_t{1} << count) - 1;
    value = static_cast<std::uint32_t>((accumulator >> shift) & mask);
    return true;
}
bool BitReader::read(const unsigned int count, std::uint32_t& value) noexcept {
    if (!peek(count, value)) return false;
    position_ += count;
    return true;
}
bool BitReader::read64(const unsigned int count, std::uint64_t& value) noexcept {
    if (count > 64 || count > remaining()) return false;
    const unsigned int low_count = count < 32 ? count : 32;
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    if (!read(low_count, low)) return false;
    if (!read(count - low_count, high)) return false;
    value = static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << low_count);
    return true;
}
bool BitReader::skip(const std::size_t count) noexcept {
    if (count > remaining()) return false;
    position_ += count;
    return true;
}
} // namespace stx_vorbis
