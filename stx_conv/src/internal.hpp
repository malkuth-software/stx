#pragma once
#include <stx_conv/conv.hpp>

namespace stx_conv::detail {
template<Precision Scalar> inline constexpr Scalar sample_limit = Scalar(1.0e10);
template<> inline constexpr double sample_limit<double> = 1.0e100;
template<Precision Scalar>
std::size_t checked_product(std::size_t left, std::size_t right);
void validate_length(std::size_t length);
bool fits(std::size_t count, std::size_t stride, std::size_t lanes, std::size_t capacity) noexcept;
template<Precision Scalar>
bool overlaps(std::span<const Scalar> source, std::span<Scalar> destination) noexcept;
template<Precision Scalar>
Status validate_samples(BasicLineInput<Scalar> source, std::size_t count, std::size_t lanes) noexcept;
template<Precision Scalar>
struct Kernel {
    static Status compile_unchecked(BasicLineInput<Scalar> source, BasicProfile<Scalar>& profile, BasicLineWorkspace<Scalar>& work) noexcept;
    static Status compile_checked(BasicLineInput<Scalar> source, BasicProfile<Scalar>& profile, BasicLineWorkspace<Scalar>& work) noexcept;
    static void evaluate_unchecked(const BasicProfile<Scalar>& profile, const BasicLinePlan<Scalar>& plan, BasicLineOutput<Scalar> output) noexcept;
    static Status evaluate_checked(const BasicProfile<Scalar>& profile, const BasicLinePlan<Scalar>& plan, BasicLineOutput<Scalar> output) noexcept;
    static Status resize_checked(const BasicImagePlan<Scalar>& plan, BasicImageInput<Scalar> source, BasicImageOutput<Scalar> output,
                                 BasicImageWorkspace<Scalar>& work) noexcept;
};
} // namespace stx_conv::detail
