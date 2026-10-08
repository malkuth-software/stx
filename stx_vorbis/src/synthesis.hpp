#pragma once
#include "setup.hpp"
namespace stx_vorbis::detail {
// All spans are disjoint except real/imaginary are updated in place. Coefficients
// are positive-angle twiddles. No FMA/reassociation is permitted by the build.
using Butterfly = void (*)(double*, double*, const double*, const double*, std::size_t);
void scalar_butterfly(double* real, double* imaginary, const double* cosine, const double* sine,
                      std::size_t half) noexcept;
[[nodiscard]] Butterfly select_butterfly(Synthesis requested);
void inverse_mdct(const Transform& plan, std::span<const double> spectrum, std::span<double> time,
                  std::span<double> real, std::span<double> imaginary, Butterfly butterfly) noexcept;
} // namespace stx_vorbis::detail
