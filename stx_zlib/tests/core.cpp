#include "stx_zlib/checksum.hpp"
#include "stx_zlib/inflate.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "stx_zlib: %s\n", message);
    std::abort();
}
// Bitwise reference CRC-32 and per-byte Adler-32, independent of the
// table-driven and run-summed implementations under test.
std::uint32_t reference_crc(std::span<const std::byte> bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const std::byte item : bytes) {
        crc ^= std::to_integer<std::uint32_t>(item);
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xedb88320U : crc >> 1U;
    }
    return crc ^ 0xffffffffU;
}
std::uint32_t reference_adler(std::span<const std::byte> bytes) {
    std::uint32_t low = 1;
    std::uint32_t high = 0;
    for (const std::byte item : bytes) {
        low = (low + std::to_integer<std::uint32_t>(item)) % 65521U;
        high = (high + low) % 65521U;
    }
    return (high << 16U) | low;
}

void checksums() {
    const std::array<std::byte, 9> check{std::byte{'1'}, std::byte{'2'}, std::byte{'3'}, std::byte{'4'},
        std::byte{'5'}, std::byte{'6'}, std::byte{'7'}, std::byte{'8'}, std::byte{'9'}};
    require(stx_zlib::crc32(check) == 0xcbf43926U, "CRC-32 check value");
    require(stx_zlib::adler32(check) == 0x091e01deU, "Adler-32 check value");
    require(stx_zlib::crc32({}) == 0U && stx_zlib::adler32({}) == 1U, "empty checksums");
    // Lengths across the slicing and run boundaries, including all-0xFF
    // input, which maximizes the Adler sums.
    std::vector<std::byte> data(20000);
    for (std::size_t index = 0; index < data.size(); ++index) {
        data[index] = static_cast<std::byte>((index * 131U + (index >> 7U)) & 255U);
    }
    const std::vector<std::byte> ones(12000, std::byte{0xff});
    const std::array<const std::vector<std::byte>*, 2> sources{&data, &ones};
    for (const std::vector<std::byte>* source : sources) {
        for (const std::size_t length : {std::size_t{1}, std::size_t{7}, std::size_t{8}, std::size_t{9},
                                         std::size_t{5551}, std::size_t{5552}, std::size_t{5553},
                                         std::size_t{11104}, std::size_t{11105}, (*source).size()}) {
            const std::span<const std::byte> part((*source).data(), std::min(length, (*source).size()));
            require(stx_zlib::crc32(part) == reference_crc(part), "CRC-32 against the bitwise reference");
            require(stx_zlib::adler32(part) == reference_adler(part), "Adler-32 against the per-byte reference");
        }
    }
    // Continuation over every split of a 600-byte prefix.
    const std::span<const std::byte> whole(data.data(), 600);
    const std::uint32_t crc = stx_zlib::crc32(whole);
    const std::uint32_t adler = stx_zlib::adler32(whole);
    for (std::size_t split = 0; split <= whole.size(); ++split) {
        const std::span<const std::byte> head = whole.first(split);
        const std::span<const std::byte> tail = whole.subspan(split);
        require(stx_zlib::crc32_update(stx_zlib::crc32(head), tail) == crc, "CRC-32 continuation");
        require(stx_zlib::adler32_update(stx_zlib::adler32(head), tail) == adler, "Adler-32 continuation");
    }
    // A state outside Adler-32's residue range is reduced first, not overflowed.
    require(stx_zlib::adler32_update(0xffffffffU, ones) == stx_zlib::adler32_update(0x000e000eU, ones),
            "unreduced Adler state");
}
} // namespace

int main() {
    checksums();
    std::unique_ptr<stx_zlib::InflateStream> stream = std::make_unique<stx_zlib::InflateStream>();
    stx_zlib::InflateOptions options{};
    options.limits.input_bytes = stx_zlib::ByteCount{64};
    options.limits.output_bytes = stx_zlib::ByteCount{64};
    options.limits.work = stx_zlib::WorkCount{10000};
    require((*stream).reset(options) == stx_zlib::InflateError::none, "zlib reset failed");
    constexpr std::array<std::byte, 9> zlib_a{
        std::byte{0x78}, std::byte{0x9c}, std::byte{0x4b}, std::byte{0x04},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x62}, std::byte{0x00}, std::byte{0x62}};
    std::array<std::byte, 8> output{};
    const stx_zlib::InflateResult result = (*stream).process(zlib_a, output, true);
    require(result.status == stx_zlib::InflateStatus::stream_end, "zlib stream did not finish");
    require(result.input_consumed == zlib_a.size() && result.output_produced == 1,
            "zlib progress mismatch");
    require(output[0] == std::byte{'a'}, "zlib output mismatch");
    options.format = stx_zlib::InflateFormat::raw_deflate;
    require((*stream).reset(options) == stx_zlib::InflateError::none, "raw reset failed");
    constexpr std::array<std::byte, 3> raw_a{
        std::byte{0x4b}, std::byte{0x04}, std::byte{0x00}};
    const stx_zlib::InflateResult raw_result = (*stream).process(raw_a, output, true);
    require(raw_result.status == stx_zlib::InflateStatus::stream_end && output[0] == std::byte{'a'},
            "raw stream failed");
    return 0;
}
