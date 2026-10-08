#pragma once

#include <bfft/bodft.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace stx_vorbis::experiment {
struct BodftDeleter final {
    void operator()(bodft_plan* plan) const noexcept { bodft_plan_destroy(plan); }
};

// Experimental owner: BODFT keeps mutable scratch in its plan. Each instance
// belongs to one decoder/benchmark and must not be shared between concurrent
// calls. BFFT allocations do not yet participate in the codec memory_resource.
class BfftImdct final {
public:
    explicit BfftImdct(const unsigned int block, const bool compact = false)
        : block_(block), compact_(compact), input_(block, 0.0), spectrum_(block / 2),
          cosine_(block / 2), sine_(block / 2), dct_(block / 2) {
        bodft_plan* created = nullptr;
        const unsigned int transform_size = compact ? block / 2 : block;
        const bfft_status status = bodft_plan_create(transform_size, &created);
        plan_.reset(created);
        if (status != BFFT_OK) throw std::runtime_error("BODFT plan creation failed");
        for (unsigned int index = 0; index < block / 2; ++index) {
            const double angle = std::numbers::pi * (static_cast<double>(index) + 0.5) / block;
            cosine_[index] = std::cos(angle);
            sine_[index] = std::sin(angle);
        }
    }

    BfftImdct(const BfftImdct&) = delete;
    BfftImdct& operator=(const BfftImdct&) = delete;

    // Disjoint input N/2 doubles and output N doubles, borrowed for this call.
    // No allocation during execute. Padded mode keeps the upper input half zero.
    void execute(const std::span<const double> input, const std::span<double> output) {
        if (input.size() != block_ / 2 || output.size() != block_)
            throw std::runtime_error("BODFT IMDCT shape mismatch");
        if (compact_) compact_dct(input);
        else padded_dct(input);
        const unsigned int bins = block_ / 2;
        // DCT-IV followed by the IMDCT's shift and signed reflection. The three
        // regions cover N output samples without per-element boundary branches.
        const unsigned int quarter = block_ / 4;
        for (unsigned int index = 0; index < quarter; ++index)
            output[index] = dct_[quarter + index];
        for (unsigned int index = 0; index < bins; ++index)
            output[quarter + index] = -dct_[bins - 1 - index];
        for (unsigned int index = 0; index < quarter; ++index)
            output[3 * quarter + index] = -dct_[index];
    }

private:
    void padded_dct(const std::span<const double> input) {
        std::copy(input.begin(), input.end(), input_.begin());
        const bfft_status status = bodft_forward(plan_.get(), input_.data(), spectrum_.data());
        if (status != BFFT_OK) throw std::runtime_error("BODFT execution failed");
        for (unsigned int index = 0; index < block_ / 2; ++index)
            dct_[index] = spectrum_[index].re * cosine_[index] + spectrum_[index].im * sine_[index];
    }

    void compact_dct(const std::span<const double> input) {
        const unsigned int bins = block_ / 2;
        const unsigned int quarter = block_ / 4;
        // M=N/2. Pair X[k] with X[M-1-k] and rotate by pi*(k+1/2)/(2M).
        // The M-point real inverse yields even DCT-IV indices, then the signed
        // reversed odd indices. Its 2/M normalization is undone by M/2 below.
        for (unsigned int index = 0; index < quarter; ++index) {
            const double low = input[index];
            const double high = input[bins - 1 - index];
            spectrum_[index].re = low * cosine_[index] + high * sine_[index];
            spectrum_[index].im = low * sine_[index] - high * cosine_[index];
        }
        const bfft_status status = bodft_inverse(plan_.get(), spectrum_.data(), input_.data());
        if (status != BFFT_OK) throw std::runtime_error("BODFT inverse execution failed");
        const double scale = static_cast<double>(quarter);
        for (unsigned int index = 0; index < quarter; ++index) {
            dct_[2 * index] = scale * input_[index];
            dct_[bins - 1 - 2 * index] = -scale * input_[quarter + index];
        }
    }

    unsigned int block_{};
    bool compact_{};
    std::unique_ptr<bodft_plan, BodftDeleter> plan_{};
    std::vector<double> input_;
    std::vector<bfft_complex> spectrum_;
    std::vector<double> cosine_;
    std::vector<double> sine_;
    std::vector<double> dct_;
};
} // namespace stx_vorbis::experiment
