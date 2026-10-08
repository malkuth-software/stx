#include "stx_vorbis/bit_reader.hpp"
#include "stx_vorbis/ogg.hpp"
#include "synthesis.hpp"
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
}
int main() { test_bits(); test_transform(); std::puts("core tests passed"); }
