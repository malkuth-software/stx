#include "stx_vorbis/bit_reader.hpp"
#include "stx_vorbis/ogg.hpp"
#include "synthesis.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <vector>
namespace {
void check(const bool condition, const char* const message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
void test_bits() {
    const std::uint8_t bytes[]{0xab, 0xcd, 0xef};
    stx_vorbis::BitReader reader(bytes);
    std::uint32_t value = 0;
    check(reader.read(4, value) && value == 11, "bits nibble");
    check(reader.read(12, value) && value == 0xcda, "bits cross-byte");
    check(!reader.read(9, value) && reader.position() == 16, "bits failure atomicity");
    // Independent per-bit oracle exercises full unaligned loads and every short
    // tail, including zero-bit reads exactly at EOF. No padded input is granted.
    std::uint32_t seed = 0x679102abU;
    for (std::size_t length = 0; length <= 32; ++length) {
        std::vector<std::uint8_t> packet(length);
        for (std::size_t index = 0; index < length; ++index) {
            seed = seed * 1664525U + 1013904223U;
            packet[index] = static_cast<std::uint8_t>(seed >> 24);
        }
        for (std::size_t offset = 0; offset <= length * 8; ++offset) {
            for (unsigned int count = 0; count <= 33; ++count) {
                stx_vorbis::BitReader candidate(packet);
                check(candidate.skip(offset), "bit oracle offset");
                std::uint32_t actual = 0xa5a5a5a5U;
                const bool valid = count <= 32 && count <= length * 8 - offset;
                check(candidate.peek(count, actual) == valid, "bit oracle acceptance");
                check(candidate.position() == offset, "peek never advances");
                if (!valid) { check(actual == 0xa5a5a5a5U, "failed peek preserves output"); continue; }
                std::uint32_t expected = 0;
                for (unsigned int bit = 0; bit < count; ++bit) {
                    const std::size_t source = offset + bit;
                    const std::uint32_t digit = (packet[source / 8] >> (source % 8)) & 1U;
                    expected |= digit << bit;
                }
                check(actual == expected, "bit oracle value");
            }
        }
    }
}
void test_transform() {
    for (unsigned int size = 64; size <= 8192; size *= 2) {
        stx_vorbis::detail::Transform plan(std::pmr::new_delete_resource());
        stx_vorbis::detail::prepare_transform(plan, size);
        std::vector<double> spectrum(size / 2, 0);
        std::vector<double> time(size, 0);
        std::vector<double> accelerated(size, 0);
        std::vector<double> real(size * 4, 0);
        std::vector<double> imaginary(size * 4, 0);
        for (unsigned int index = 0; index < size / 2; ++index) spectrum[index] = std::sin(index * 0.17);
        stx_vorbis::detail::inverse_mdct(plan, spectrum, time, real, imaginary, stx_vorbis::detail::scalar_butterfly);
        const unsigned int reference_step = std::max(1U, size / 128);
        for (unsigned int sample = 0; sample < size; sample += reference_step) {
            double expected = 0;
            for (unsigned int bin = 0; bin < size / 2; ++bin) {
                const double angle = (2 * std::numbers::pi / size) * (sample + 0.5 + size / 4) * (bin + 0.5);
                expected += spectrum[bin] * std::cos(angle);
            }
            check(std::abs(expected - time[sample]) < 1e-9, "IMDCT definition");
        }
        const stx_vorbis::Synthesis choices[]{stx_vorbis::Synthesis::neon, stx_vorbis::Synthesis::sse2, stx_vorbis::Synthesis::avx2};
        for (const stx_vorbis::Synthesis choice : choices) {
            if (!stx_vorbis::synthesis_available(choice)) continue;
            stx_vorbis::detail::inverse_mdct(plan, spectrum, accelerated, real, imaginary, stx_vorbis::detail::select_butterfly(choice));
            for (unsigned int sample = 0; sample < size; ++sample)
                check(std::abs(time[sample] - accelerated[sample]) < 1e-12, "SIMD agreement");
        }
    }
}
void test_first_stage() {
    // Exact representations include signed zero; nontrivial coefficients guard
    // the operation order rather than relying on the normal (1, 0) twiddle.
    const double values[]{0.0, -0.0, 1.0, -1.0, 0.125, -32.5, 1e-100, 1e100};
    const double cosines[]{1.0, 0.625};
    const double sines[]{0.0, -0.375};
    const stx_vorbis::Synthesis choices[]{stx_vorbis::Synthesis::sse2, stx_vorbis::Synthesis::avx2};
    std::array<double, 18> expected_real{};
    std::array<double, 18> expected_imaginary{};
    std::array<double, 18> actual_real{};
    std::array<double, 18> actual_imaginary{};
    for (const stx_vorbis::Synthesis choice : choices) {
        if (!stx_vorbis::synthesis_available(choice)) continue;
        const stx_vorbis::detail::Butterfly butterfly = stx_vorbis::detail::select_butterfly(choice);
        for (std::size_t size = 2; size <= 18; size += 2) {
            for (std::size_t coefficient = 0; coefficient < 2; ++coefficient) {
                for (std::size_t index = 0; index < size; ++index) {
                    expected_real[index] = values[index % 8];
                    expected_imaginary[index] = values[(index + 3) % 8];
                    actual_real[index] = expected_real[index];
                    actual_imaginary[index] = expected_imaginary[index];
                }
                stx_vorbis::detail::scalar_butterfly(expected_real.data(), expected_imaginary.data(),
                    cosines + coefficient, sines + coefficient, 1, size);
                butterfly(actual_real.data(), actual_imaginary.data(), cosines + coefficient,
                          sines + coefficient, 1, size);
                for (std::size_t index = 0; index < size; ++index) {
                    check(std::bit_cast<std::uint64_t>(actual_real[index]) ==
                          std::bit_cast<std::uint64_t>(expected_real[index]), "first stage real bits");
                    check(std::bit_cast<std::uint64_t>(actual_imaginary[index]) ==
                          std::bit_cast<std::uint64_t>(expected_imaginary[index]), "first stage imaginary bits");
                }
            }
        }
    }
}
}
int main() { test_bits(); test_transform(); test_first_stage(); std::puts("core tests passed"); }
