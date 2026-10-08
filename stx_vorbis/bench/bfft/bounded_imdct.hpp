#pragma once

#include <bfft/bodft.h>
#include <array>
#include <cmath>
#include <memory_resource>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace stx_vorbis::experiment {
// One aligned allocation through the decoder's bounded resource. The resource
// outlives this owner. No handle survives release or replacement of its bytes.
class BodftStorage final {
public:
    BodftStorage(std::pmr::memory_resource& memory, const std::size_t bytes, const std::size_t alignment)
        : memory_(memory), bytes_(bytes), alignment_(alignment), data_(memory.allocate(bytes, alignment)) {}
    ~BodftStorage() { memory_.deallocate(data_, bytes_, alignment_); }
    BodftStorage(const BodftStorage&) = delete;
    BodftStorage& operator=(const BodftStorage&) = delete;
    void* data() noexcept { return data_; }
    std::size_t size() const noexcept { return bytes_; }
private:
    std::pmr::memory_resource& memory_;
    const std::size_t bytes_;
    const std::size_t alignment_;
    void* const data_;
};

inline unsigned int checked_block(const unsigned int block) {
    if (block < 64 || block > 8192 || (block & (block - 1)) != 0)
        throw std::invalid_argument("BODFT Vorbis block shape");
    return block;
}
inline bodft_storage_requirements transform_storage(const unsigned int block) {
    bodft_storage_requirements requirements{};
    const bfft_status status = bodft_query_storage(block / 2, BODFT_PRECISION_F64, &requirements);
    if (status != BFFT_OK) throw std::logic_error("BODFT storage query");
    return requirements;
}

// Immutable stream-lifetime coefficients. All owned bytes use the same resource
// as Setup. Construction failure releases every partially provisioned region.
class BfftPlan final {
public:
    BfftPlan(const unsigned int block, std::pmr::memory_resource& memory)
        : block_(checked_block(block)), requirements_(transform_storage(block_)),
          storage_(memory, requirements_.plan_bytes, requirements_.plan_alignment),
          rotations_(block_ / 4, bfft_complex{}, &memory) {
        const bfft_status status = bodft_prepare(block_ / 2, BODFT_PRECISION_F64,
            storage_.data(), storage_.size(), &plan_);
        if (status != BFFT_OK) throw std::logic_error("BODFT plan provision");
        const unsigned int quarter = block_ / 4;
        for (unsigned int index = 0; index < quarter; ++index) {
            const double angle = std::numbers::pi * (static_cast<double>(index) + 0.5) / block_;
            rotations_[index] = bfft_complex{std::cos(angle), std::sin(angle)};
        }
    }
    BfftPlan(const BfftPlan&) = delete;
    BfftPlan& operator=(const BfftPlan&) = delete;
    unsigned int block() const noexcept { return block_; }
    const bodft_prepared_plan* handle() const noexcept { return plan_; }
    const bodft_storage_requirements& requirements() const noexcept { return requirements_; }
    std::span<const bfft_complex> rotations() const noexcept { return rotations_; }
    std::size_t bytes() const noexcept {
        const std::size_t total = storage_.size() + rotations_.capacity() * sizeof(bfft_complex);
        return total;
    }
private:
    const unsigned int block_;
    const bodft_storage_requirements requirements_;
    BodftStorage storage_;
    std::pmr::vector<bfft_complex> rotations_;
    const bodft_prepared_plan* plan_{nullptr};
};

class BodftScratch final {
public:
    BodftScratch(const BfftPlan& plan, std::pmr::memory_resource& memory)
        : storage_(memory, plan.requirements().workspace_bytes, plan.requirements().workspace_alignment) {
        const bfft_status status = bodft_prepare_workspace(plan.handle(), storage_.data(), storage_.size(), &workspace_);
        if (status != BFFT_OK) throw std::logic_error("BODFT workspace provision");
    }
    BodftScratch(const BodftScratch&) = delete;
    BodftScratch& operator=(const BodftScratch&) = delete;
    bodft_workspace* handle() noexcept { return workspace_; }
    std::size_t bytes() const noexcept { return storage_.size(); }
private:
    BodftStorage storage_;
    bodft_workspace* workspace_{nullptr};
};

// Decoder-owned mutable storage, reused across packets and channels. Each
// block size gets BODFT scratch; conversion buffers share the largest extent.
// prepare is setup-only. Failed preparation invalidates scratch, never a plan.
class BfftWorkspace final {
public:
    explicit BfftWorkspace(std::pmr::memory_resource& memory)
        : memory_(memory), spectrum_(&memory), samples_(&memory), dct_(&memory) {}
    BfftWorkspace(const BfftWorkspace&) = delete;
    BfftWorkspace& operator=(const BfftWorkspace&) = delete;
    void prepare(const BfftPlan& small, const BfftPlan& large) {
        if (small.block() > large.block()) throw std::invalid_argument("BODFT block order");
        same_shape_ = small.block() == large.block();
        scratch_[0].reset();
        scratch_[1].reset();
        scratch_[0].emplace(small, memory_);
        if (!same_shape_) scratch_[1].emplace(large, memory_);
        spectrum_.resize(large.block() / 4);
        samples_.resize(large.block() / 2);
        dct_.resize(large.block() / 2);
    }
    std::size_t bytes() const noexcept {
        std::size_t total = spectrum_.capacity() * sizeof(bfft_complex)
            + (samples_.capacity() + dct_.capacity()) * sizeof(double);
        for (const std::optional<BodftScratch>& scratch : scratch_) {
            if (scratch.has_value()) total += (*scratch).bytes();
        }
        return total;
    }
    // Input N/2, output N, disjoint; borrows last only for this call. Every
    // allocation and implementation choice is outside the per-sample loops.
    void execute(const BfftPlan& plan, const unsigned int index,
                 const std::span<const double> input, const std::span<double> output) {
        const unsigned int block = plan.block();
        const unsigned int bins = block / 2;
        const unsigned int quarter = block / 4;
        if (index >= 2 || input.size() != bins || output.size() != block
            || spectrum_.size() < quarter || samples_.size() < bins || dct_.size() < bins)
            throw std::logic_error("BODFT IMDCT buffer shape");
        const unsigned int scratch_index = same_shape_ ? 0 : index;
        if (!scratch_[scratch_index].has_value()) throw std::logic_error("BODFT scratch unavailable");
        BodftScratch& scratch = *scratch_[scratch_index];
        const std::span<const bfft_complex> rotations = plan.rotations();
        for (unsigned int bin = 0; bin < quarter; ++bin) {
            const double low = input[bin];
            const double high = input[bins - 1 - bin];
            const bfft_complex rotation = rotations[bin];
            spectrum_[bin].re = low * rotation.re + high * rotation.im;
            spectrum_[bin].im = low * rotation.im - high * rotation.re;
        }
        const bfft_status status = bodft_inverse_prepared(plan.handle(), scratch.handle(),
            spectrum_.data(), quarter, samples_.data(), bins);
        if (status != BFFT_OK) throw std::logic_error("BODFT inverse execution");
        const double scale = static_cast<double>(quarter);
        for (unsigned int bin = 0; bin < quarter; ++bin) {
            dct_[2 * bin] = scale * samples_[bin];
            dct_[bins - 1 - 2 * bin] = -scale * samples_[quarter + bin];
        }
        for (unsigned int sample = 0; sample < quarter; ++sample) output[sample] = dct_[quarter + sample];
        for (unsigned int sample = 0; sample < bins; ++sample) output[quarter + sample] = -dct_[bins - 1 - sample];
        for (unsigned int sample = 0; sample < quarter; ++sample) output[3 * quarter + sample] = -dct_[sample];
    }
private:
    std::pmr::memory_resource& memory_;
    std::array<std::optional<BodftScratch>, 2> scratch_{};
    std::pmr::vector<bfft_complex> spectrum_;
    std::pmr::vector<double> samples_;
    std::pmr::vector<double> dct_;
    bool same_shape_{false};
};
} // namespace stx_vorbis::experiment
