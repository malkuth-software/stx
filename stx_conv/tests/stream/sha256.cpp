#include "sha256.hpp"
#include <bit>

namespace stx_conv_tests {
namespace {

constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};
constexpr std::array<std::uint32_t, 8> initial_state{
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

} // namespace

Sha256::Sha256() noexcept {
    reset();
}

void Sha256::reset() noexcept {
    state = initial_state;
    block.fill(0);
    block_length = 0;
    message_bytes = 0;
}

void Sha256::compress_block() noexcept {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t word = 0; word < 16; ++word) {
        const std::uint32_t b0 = block[4 * word];
        const std::uint32_t b1 = block[4 * word + 1];
        const std::uint32_t b2 = block[4 * word + 2];
        const std::uint32_t b3 = block[4 * word + 3];
        schedule[word] = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
    }
    for (std::size_t word = 16; word < 64; ++word) {
        const std::uint32_t w15 = schedule[word - 15];
        const std::uint32_t w2 = schedule[word - 2];
        const std::uint32_t s0 = std::rotr(w15, 7) ^ std::rotr(w15, 18) ^ (w15 >> 3);
        const std::uint32_t s1 = std::rotr(w2, 17) ^ std::rotr(w2, 19) ^ (w2 >> 10);
        schedule[word] = schedule[word - 16] + s0 + schedule[word - 7] + s1;
    }
    std::array<std::uint32_t, 8> v = state;
    for (std::size_t round = 0; round < 64; ++round) {
        const std::uint32_t sum1 = std::rotr(v[4], 6) ^ std::rotr(v[4], 11) ^ std::rotr(v[4], 25);
        const std::uint32_t choice = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const std::uint32_t first = v[7] + sum1 + choice + round_constants[round] + schedule[round];
        const std::uint32_t sum0 = std::rotr(v[0], 2) ^ std::rotr(v[0], 13) ^ std::rotr(v[0], 22);
        const std::uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        const std::uint32_t second = sum0 + majority;
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + first;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = first + second;
    }
    for (std::size_t word = 0; word < 8; ++word) { state[word] += v[word]; }
}

void Sha256::update(std::span<const std::byte> bytes) noexcept {
    for (const std::byte value : bytes) {
        block[block_length] = static_cast<std::uint8_t>(value);
        ++block_length;
        if (block_length == block.size()) {
            compress_block();
            block_length = 0;
        }
    }
    message_bytes += bytes.size();
}

std::string Sha256::finish_hex() {
    const std::uint64_t message_bits = message_bytes * 8;
    block[block_length] = 0x80u;
    ++block_length;
    if (block_length > 56) {
        while (block_length < block.size()) {
            block[block_length] = 0;
            ++block_length;
        }
        compress_block();
        block_length = 0;
    }
    while (block_length < 56) {
        block[block_length] = 0;
        ++block_length;
    }
    for (std::size_t byte = 0; byte < 8; ++byte) {
        block[56 + byte] = static_cast<std::uint8_t>(message_bits >> (56 - 8 * byte));
    }
    compress_block();
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const std::uint32_t word : state) {
        for (std::size_t nibble = 8; nibble != 0; --nibble) {
            const std::uint32_t digit = (word >> (4 * (nibble - 1))) & 0xfu;
            result.push_back(digits[digit]);
        }
    }
    reset();
    return result;
}

} // namespace stx_conv_tests
