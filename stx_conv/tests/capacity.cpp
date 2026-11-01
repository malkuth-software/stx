// stx_conv::kernels::capacity against its scalar instantiation (bit for bit)
// and against an independent 31-form threshold (to 16 epsilon of the total
// capacity), over 100,003 capacity sets per precision: random, tied, zero
// mass and full-capacity mass, across 121 binary exponents. Also checks short
// counts from an unaligned offset for tail overwrites, and the empty call.
#include <stx_conv/kernels.hpp>
#include "detail/conv_admission_kernel.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

// One subset form: (mass - sum of the masked capacities) / (5 - |mask|).
template<unsigned Mask, stx_conv::Precision Scalar>
Scalar subset_form(const std::array<Scalar, 5>& capacity, Scalar mass) noexcept {
    Scalar sum = Scalar(0);
    if constexpr ((Mask & 1u) != 0) { sum += capacity[0]; }
    if constexpr ((Mask & 2u) != 0) { sum += capacity[1]; }
    if constexpr ((Mask & 4u) != 0) { sum += capacity[2]; }
    if constexpr ((Mask & 8u) != 0) { sum += capacity[3]; }
    if constexpr ((Mask & 16u) != 0) { sum += capacity[4]; }
    const Scalar divisor = static_cast<Scalar>(5 - std::popcount(Mask));
    const Scalar result = (mass - sum) / divisor;
    return result;
}

// The maximum of the 31 proper-subset forms, by a fixed pairwise cascade.
template<stx_conv::Precision Scalar, unsigned... Mask>
Scalar subset_threshold(const std::array<Scalar, 5>& capacity, Scalar mass,
                        std::integer_sequence<unsigned, Mask...>) noexcept {
    std::array<Scalar, 64> forms{};
    ((forms[32 + Mask] = subset_form<Mask>(capacity, mass)), ...);
    // forms[63] stays zero; feasible nonnegative mass has a nonnegative threshold.
    for (std::size_t width = 16; width != 0; width /= 2) {
        for (std::size_t index = width; index < 2 * width; ++index) {
            forms[index] = std::max(forms[2 * index], forms[2 * index + 1]);
        }
    }
    return forms[1];
}

template<stx_conv::Precision Scalar>
void check_capacity() {
    constexpr std::size_t count = 100003;
    // One guard sample before and after each stream.
    std::array<std::vector<Scalar>, 5> storage{};
    std::array<const Scalar*, 5> inputs{};
    for (std::size_t slot = 0; slot < 5; ++slot) {
        storage[slot].assign(count + 2, Scalar(-1));
        inputs[slot] = storage[slot].data() + 1;
    }
    std::vector<Scalar> mass(count + 2, Scalar(-1));
    std::vector<Scalar> output(count + 2, Scalar(-1));
    std::mt19937 random(1907);
    std::uniform_real_distribution<double> draw(0, 1);
    for (std::size_t index = 1; index <= count; ++index) {
        const int exponent = static_cast<int>(index % 121) - 60;
        Scalar total = Scalar(0);
        for (std::size_t slot = 0; slot < 5; ++slot) {
            const Scalar tied = Scalar(slot % 3);
            const Scalar drawn = static_cast<Scalar>(draw(random));
            const Scalar value = index % 5 == 0 ? tied : drawn;
            storage[slot][index] = std::ldexp(value, exponent);
            total += storage[slot][index];
        }
        mass[index] = static_cast<Scalar>(draw(random)) * total;
        if (index % 7 == 0) { mass[index] = total; }
        if (index % 11 == 0) { mass[index] = Scalar(0); }
    }
    stx_conv::kernels::capacity<Scalar>(inputs.data(), mass.data() + 1, output.data() + 1, count);
    using ScalarBackend = stx_conv::kernels::detail::ScalarOps<Scalar>;
    double largest = 0.0;
    for (std::size_t index = 1; index <= count; ++index) {
        std::array<Scalar, 5> capacity{};
        Scalar total = Scalar(0);
        for (std::size_t slot = 0; slot < 5; ++slot) {
            capacity[slot] = storage[slot][index];
            total += capacity[slot];
        }
        const Scalar scalar = stx_conv::kernels::detail::capacity_threshold<ScalarBackend>(capacity, mass[index]);
        const Scalar oracle = subset_threshold(capacity, mass[index], std::make_integer_sequence<unsigned, 31>{});
        require(output[index] == scalar, "capacity: backend and scalar results differ");
        const double scale = std::max(static_cast<double>(total), std::numeric_limits<double>::min());
        const double error = std::abs(static_cast<double>(output[index]) - static_cast<double>(oracle)) / scale;
        largest = std::max(largest, error);
        require(error <= 16.0 * static_cast<double>(std::numeric_limits<Scalar>::epsilon()), "capacity: 31-form oracle differs");
    }
    require(output.front() == Scalar(-1) && output.back() == Scalar(-1), "capacity: guard samples overwritten");
    for (std::size_t short_count = 0; short_count <= 17; ++short_count) {
        std::fill(output.begin(), output.end(), Scalar(-1));
        stx_conv::kernels::capacity<Scalar>(inputs.data(), mass.data() + 1, output.data() + 1, short_count);
        require(output[short_count + 1] == Scalar(-1), "capacity: tail overwritten");
    }
    stx_conv::kernels::capacity<Scalar>(nullptr, nullptr, nullptr, 0);
    std::printf("capacity f%zu cases=%zu largest_scaled_error=%.17g\n", 8 * sizeof(Scalar), count, largest);
}

} // namespace

int main() {
    try {
        check_capacity<float>();
        check_capacity<double>();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
