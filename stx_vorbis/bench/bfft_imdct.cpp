#include "bfft_imdct.hpp"
#include "../src/synthesis.hpp"
#include <chrono>
#include <cstdio>
#include <limits>

namespace {
using Clock = std::chrono::steady_clock;

void check(const bool condition, const char* const message) {
    if (condition) return;
    std::fprintf(stderr, "%s\n", message);
    std::abort();
}

void fill_pattern(const std::span<double> input, const unsigned int pattern) {
    if (pattern == 0) {
        std::fill(input.begin(), input.end(), 0.0);
        return;
    }
    if (pattern == 1) {
        std::fill(input.begin(), input.end(), 0.0);
        input[input.size() / 2] = 1.0;
        return;
    }
    if (pattern == 2) {
        std::fill(input.begin(), input.end(), 1.0);
        return;
    }
    if (pattern == 3) {
        for (std::size_t index = 0; index < input.size(); ++index)
            input[index] = index % 2 == 0 ? 1.0 : -1.0;
        return;
    }
    for (std::size_t index = 0; index < input.size(); ++index)
        input[index] = std::sin(static_cast<double>(index) * 0.17);
}

void run(const unsigned int block) {
    stx_vorbis::detail::Transform original(std::pmr::new_delete_resource());
    stx_vorbis::detail::prepare_transform(original, block);
    stx_vorbis::experiment::BfftImdct candidate(block, true);
    const stx_vorbis::detail::Butterfly butterfly =
        stx_vorbis::detail::select_butterfly(stx_vorbis::Synthesis::automatic);
    std::vector<double> input(block / 2, 0.0);
    std::vector<double> expected(block, 0.0);
    std::vector<double> actual(block, 0.0);
    std::vector<double> real(block, 0.0);
    std::vector<double> imaginary(block, 0.0);
    double maximum = 0.0;
    for (unsigned int pattern = 0; pattern < 5; ++pattern) {
        fill_pattern(input, pattern);
        stx_vorbis::detail::inverse_mdct(original, input, expected, real, imaginary, butterfly);
        candidate.execute(input, actual);
        for (unsigned int index = 0; index < block; ++index) {
            const double error = std::abs(actual[index] - expected[index]);
            maximum = std::max(maximum, error);
            check(std::isfinite(actual[index]) && error < 1e-9, "BODFT/current IMDCT disagreement");
        }
        const unsigned int step = std::max(1U, block / 128);
        for (unsigned int sample = 0; sample < block; sample += step) {
            // Higher precision is confined to the independent test oracle.
            // Reduce the rational phase modulo 2*pi in integer arithmetic to
            // avoid large-angle libm reduction noise in this direct sum.
            long double reference = 0.0L;
            for (unsigned int bin = 0; bin < block / 2; ++bin) {
                const std::uint64_t numerator = std::uint64_t{2 * sample + 1 + block / 2} * (2 * bin + 1);
                const std::uint64_t phase = numerator % (4 * block);
                const long double angle = std::numbers::pi_v<long double> * phase / (2 * block);
                reference += static_cast<long double>(input[bin]) * std::cos(angle);
            }
            const double reference_sample = static_cast<double>(reference);
            if (std::abs(actual[sample] - reference_sample) >= 1e-9)
                std::fprintf(stderr, "block=%u pattern=%u sample=%u actual=%.17g baseline=%.17g oracle=%.17g\n",
                    block, pattern, sample, actual[sample], expected[sample], reference_sample);
            check(std::abs(actual[sample] - reference_sample) < 1e-9, "BODFT/direct cosine definition disagreement");
        }
    }
    constexpr unsigned int iterations = 1000;
    double probe = 0.0;
    for (unsigned int round = 0; round < 7; ++round) {
        for (unsigned int entry = 0; entry < 2; ++entry) {
            const unsigned int implementation = (round + entry) % 2;
            const Clock::time_point start = Clock::now();
            if (implementation == 0) {
                for (unsigned int iteration = 0; iteration < iterations; ++iteration) {
                    stx_vorbis::detail::inverse_mdct(original, input, actual, real, imaginary, butterfly);
                    probe += actual[iteration % block];
                }
            } else {
                for (unsigned int iteration = 0; iteration < iterations; ++iteration) {
                    candidate.execute(input, actual);
                    probe += actual[iteration % block];
                }
            }
            const Clock::time_point end = Clock::now();
            const double elapsed = std::chrono::duration<double, std::micro>(end - start).count();
            const double per_call = elapsed / iterations;
            std::printf("%u,%u,%s,%.9f,%.12g,%.12g\n", block, round,
                implementation == 0 ? "current" : "bodft", per_call, maximum, probe);
        }
    }
}
} // namespace

int main() {
    std::fprintf(stderr, "BFFT backend: %s\n", bodft_backend_name());
    std::puts("block,round,implementation,microseconds,max_baseline_error,probe");
    for (unsigned int block = 64; block <= 8192; block *= 2) run(block);
    return 0;
}
