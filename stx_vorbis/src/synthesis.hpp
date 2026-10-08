#pragma once
#include "setup.hpp"
namespace stx_vorbis::detail {
// All spans are disjoint except real/imaginary are updated in place. Coefficients
// are positive-angle twiddles. Each dispatch processes one complete FFT stage;
// size is a multiple of 2*half, half is a power of two, both are positive.
// real/imaginary hold size elements; cosine/sine hold half elements. No FMA/reassociation is permitted by the build.
using Butterfly = void (*)(double*, double*, const double*, const double*, std::size_t, std::size_t);
void scalar_butterfly(double* real, double* imaginary, const double* cosine, const double* sine,
                      std::size_t half, std::size_t size) noexcept;
[[nodiscard]] Butterfly select_butterfly(Synthesis requested);
void inverse_mdct(const Transform& plan, std::span<const double> spectrum, std::span<double> time,
                  std::span<double> real, std::span<double> imaginary, Butterfly butterfly) noexcept;
} // namespace stx_vorbis::detail
