#include "synthesis.hpp"
#include <cmath>
#include <numbers>
#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#endif
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif
namespace stx_vorbis {
bool synthesis_available(const Synthesis implementation) noexcept {
    if (implementation == Synthesis::automatic || implementation == Synthesis::scalar) return true;
#if defined(__aarch64__) || defined(_M_ARM64)
    if (implementation == Synthesis::neon) return true;
#endif
#if defined(__x86_64__) || defined(_M_X64)
    if (implementation == Synthesis::sse2) return true;
#if defined(__clang__) || defined(__GNUC__)
    if (implementation == Synthesis::avx2) return __builtin_cpu_supports("avx2");
#endif
#endif
    return false;
}
namespace detail {
void prepare_transform(Transform& transform, const unsigned int block) {
    transform.block = block;
    transform.fft_size = block;
    const unsigned int size = transform.fft_size;
    const unsigned int bits = ilog(size - 1);
    transform.permutation.resize(size);
    for (unsigned int index = 0; index < size; ++index) {
        unsigned int source = index;
        unsigned int destination = 0;
        for (unsigned int bit = 0; bit < bits; ++bit) { destination = 2 * destination + (source & 1U); source >>= 1; }
        transform.permutation[index] = destination;
    }
    // Stage-major twiddles: each butterfly kernel reads contiguous coefficients.
    transform.cosine.resize(size - 1); transform.sine.resize(size - 1);
    for (unsigned int width = 2; width <= size; width *= 2) {
        const unsigned int half = width / 2;
        for (unsigned int index = 0; index < half; ++index) {
            const double angle = 2 * std::numbers::pi * index / width;
            transform.cosine[half - 1 + index] = std::cos(angle);
            transform.sine[half - 1 + index] = std::sin(angle);
        }
    }
    transform.pre_cosine.resize(block / 2); transform.pre_sine.resize(block / 2);
    transform.post_cosine.resize(block); transform.post_sine.resize(block);
    for (unsigned int index = 0; index < block / 2; ++index) {
        const double angle = std::numbers::pi * index / block;
        transform.pre_cosine[index] = std::cos(angle); transform.pre_sine[index] = std::sin(angle);
    }
    for (unsigned int index = 0; index < block; ++index) {
        const double angle = std::numbers::pi * (index + block / 4 + 0.5) / block;
        transform.post_cosine[index] = std::cos(angle); transform.post_sine[index] = std::sin(angle);
    }
    prepare_window(transform, block);
}
void prepare_window(Transform& transform, const unsigned int block) {
    transform.block = block;
    transform.window.resize(block / 2);
    for (unsigned int index = 0; index < block / 2; ++index) {
        const double inner = std::sin(std::numbers::pi * (static_cast<double>(index) + 0.5) / block);
        transform.window[index] = std::sin(0.5 * std::numbers::pi * inner * inner);
    }
}
void scalar_butterfly(double* const real, double* const imaginary, const double* const cosine,
                      const double* const sine, const std::size_t half, const std::size_t size) noexcept {
    for (std::size_t base = 0; base < size; base += 2 * half) {
        for (std::size_t index = 0; index < half; ++index) {
            const std::size_t left = base + index;
            const std::size_t right = left + half;
            const double c = cosine[index]; const double s = sine[index];
            const double product_real = real[right] * c - imaginary[right] * s;
            const double product_imaginary = real[right] * s + imaginary[right] * c;
            const double left_real = real[left]; const double left_imaginary = imaginary[left];
            real[left] = left_real + product_real; imaginary[left] = left_imaginary + product_imaginary;
            real[right] = left_real - product_real; imaginary[right] = left_imaginary - product_imaginary;
        }
    }
}
#if defined(__aarch64__) || defined(_M_ARM64)
void neon_butterfly(double* const real, double* const imaginary, const double* const cosine,
                    const double* const sine, const std::size_t half, const std::size_t size) noexcept {
    if (half < 2) { scalar_butterfly(real, imaginary, cosine, sine, half, size); return; }
    for (std::size_t base = 0; base < size; base += 2 * half) {
        for (std::size_t index = 0; index < half; index += 2) {
            const std::size_t left = base + index;
            const std::size_t right = left + half;
            const float64x2_t c = vld1q_f64(cosine + index); const float64x2_t s = vld1q_f64(sine + index);
            const float64x2_t rr = vld1q_f64(real + right); const float64x2_t ri = vld1q_f64(imaginary + right);
            const float64x2_t pr = vsubq_f64(vmulq_f64(rr, c), vmulq_f64(ri, s));
            const float64x2_t pi = vaddq_f64(vmulq_f64(rr, s), vmulq_f64(ri, c));
            const float64x2_t lr = vld1q_f64(real + left); const float64x2_t li = vld1q_f64(imaginary + left);
            vst1q_f64(real + left, vaddq_f64(lr, pr)); vst1q_f64(imaginary + left, vaddq_f64(li, pi));
            vst1q_f64(real + right, vsubq_f64(lr, pr)); vst1q_f64(imaginary + right, vsubq_f64(li, pi));
        }
    }
}
#endif
#if defined(__x86_64__) || defined(_M_X64)
void sse2_butterfly(double* const real, double* const imaginary, const double* const cosine,
                    const double* const sine, const std::size_t half, const std::size_t size) noexcept {
    if (half < 2) { scalar_butterfly(real, imaginary, cosine, sine, half, size); return; }
    for (std::size_t base = 0; base < size; base += 2 * half) {
        for (std::size_t index = 0; index < half; index += 2) {
            const std::size_t left = base + index;
            const std::size_t right = left + half;
            const __m128d c = _mm_loadu_pd(cosine + index); const __m128d s = _mm_loadu_pd(sine + index);
            const __m128d rr = _mm_loadu_pd(real + right); const __m128d ri = _mm_loadu_pd(imaginary + right);
            const __m128d pr = _mm_sub_pd(_mm_mul_pd(rr, c), _mm_mul_pd(ri, s));
            const __m128d pi = _mm_add_pd(_mm_mul_pd(rr, s), _mm_mul_pd(ri, c));
            const __m128d lr = _mm_loadu_pd(real + left); const __m128d li = _mm_loadu_pd(imaginary + left);
            _mm_storeu_pd(real + left, _mm_add_pd(lr, pr)); _mm_storeu_pd(imaginary + left, _mm_add_pd(li, pi));
            _mm_storeu_pd(real + right, _mm_sub_pd(lr, pr)); _mm_storeu_pd(imaginary + right, _mm_sub_pd(li, pi));
        }
    }
}
#if defined(__clang__) || defined(__GNUC__)
// Four independent adjacent groups share the first-stage coefficient. Pack
// their left/right values in registers; storage stays in its original order.
// The stage contract supplies an even size and disjoint real/imaginary arrays.
__attribute__((target("avx2")))
void avx2_first_stage(double* const real, double* const imaginary,
                      const double* const cosine, const double* const sine,
                      const std::size_t size) noexcept {
    const std::size_t paired = size - size % 8;
    const __m256d c = _mm256_set1_pd(cosine[0]);
    const __m256d s = _mm256_set1_pd(sine[0]);
    for (std::size_t base = 0; base < paired; base += 8) {
        const __m256d real_first = _mm256_loadu_pd(real + base);
        const __m256d real_second = _mm256_loadu_pd(real + base + 4);
        const __m256d imaginary_first = _mm256_loadu_pd(imaginary + base);
        const __m256d imaginary_second = _mm256_loadu_pd(imaginary + base + 4);
        const __m256d lr = _mm256_unpacklo_pd(real_first, real_second);
        const __m256d rr = _mm256_unpackhi_pd(real_first, real_second);
        const __m256d li = _mm256_unpacklo_pd(imaginary_first, imaginary_second);
        const __m256d ri = _mm256_unpackhi_pd(imaginary_first, imaginary_second);
        const __m256d pr = _mm256_sub_pd(_mm256_mul_pd(rr, c), _mm256_mul_pd(ri, s));
        const __m256d pi = _mm256_add_pd(_mm256_mul_pd(rr, s), _mm256_mul_pd(ri, c));
        const __m256d real_sum = _mm256_add_pd(lr, pr);
        const __m256d real_difference = _mm256_sub_pd(lr, pr);
        const __m256d imaginary_sum = _mm256_add_pd(li, pi);
        const __m256d imaginary_difference = _mm256_sub_pd(li, pi);
        _mm256_storeu_pd(real + base, _mm256_unpacklo_pd(real_sum, real_difference));
        _mm256_storeu_pd(real + base + 4, _mm256_unpackhi_pd(real_sum, real_difference));
        _mm256_storeu_pd(imaginary + base, _mm256_unpacklo_pd(imaginary_sum, imaginary_difference));
        _mm256_storeu_pd(imaginary + base + 4, _mm256_unpackhi_pd(imaginary_sum, imaginary_difference));
    }
    if (paired != size) {
        sse2_butterfly(real + paired, imaginary + paired, cosine, sine, 1, size - paired);
    }
}
__attribute__((target("avx2")))
void avx2_butterfly(double* const real, double* const imaginary, const double* const cosine,
                    const double* const sine, const std::size_t half, const std::size_t size) noexcept {
    if (half == 1) { avx2_first_stage(real, imaginary, cosine, sine, size); return; }
    if (half < 4) { sse2_butterfly(real, imaginary, cosine, sine, half, size); return; }
    for (std::size_t base = 0; base < size; base += 2 * half) {
        for (std::size_t index = 0; index < half; index += 4) {
            const std::size_t left = base + index;
            const std::size_t right = left + half;
            const __m256d c = _mm256_loadu_pd(cosine + index); const __m256d s = _mm256_loadu_pd(sine + index);
            const __m256d rr = _mm256_loadu_pd(real + right); const __m256d ri = _mm256_loadu_pd(imaginary + right);
            const __m256d pr = _mm256_sub_pd(_mm256_mul_pd(rr, c), _mm256_mul_pd(ri, s));
            const __m256d pi = _mm256_add_pd(_mm256_mul_pd(rr, s), _mm256_mul_pd(ri, c));
            const __m256d lr = _mm256_loadu_pd(real + left); const __m256d li = _mm256_loadu_pd(imaginary + left);
            _mm256_storeu_pd(real + left, _mm256_add_pd(lr, pr)); _mm256_storeu_pd(imaginary + left, _mm256_add_pd(li, pi));
            _mm256_storeu_pd(real + right, _mm256_sub_pd(lr, pr)); _mm256_storeu_pd(imaginary + right, _mm256_sub_pd(li, pi));
        }
    }
}
#endif
#endif
Butterfly select_butterfly(const Synthesis requested) {
    require(synthesis_available(requested), Status::unsupported);
#if defined(__aarch64__) || defined(_M_ARM64)
    if (requested == Synthesis::automatic || requested == Synthesis::neon) return neon_butterfly;
#endif
#if defined(__x86_64__) || defined(_M_X64)
#if defined(__clang__) || defined(__GNUC__)
    if ((requested == Synthesis::automatic && synthesis_available(Synthesis::avx2)) || requested == Synthesis::avx2)
        return avx2_butterfly;
#endif
    if (requested == Synthesis::automatic || requested == Synthesis::sse2) return sse2_butterfly;
#endif
    return scalar_butterfly;
}
void inverse_mdct(const Transform& plan, const std::span<const double> spectrum, const std::span<double> time,
                  const std::span<double> real, const std::span<double> imaginary, const Butterfly butterfly) noexcept {
    const unsigned int size = plan.fft_size;
    std::fill_n(real.begin(), size, 0.0); std::fill_n(imaginary.begin(), size, 0.0);
    const unsigned int bins = plan.block / 2;
    for (unsigned int index = 0; index < bins; ++index) {
        const unsigned int destination = plan.permutation[index];
        real[destination] = spectrum[index] * plan.pre_cosine[index];
        imaginary[destination] = spectrum[index] * plan.pre_sine[index];
    }
    for (unsigned int width = 2; width <= size; width *= 2) {
        const unsigned int half = width / 2;
        const double* const cosine = plan.cosine.data() + half - 1;
        const double* const sine = plan.sine.data() + half - 1;
        butterfly(real.data(), imaginary.data(), cosine, sine, half, size);
    }
    // Expand cos(pi/M*(t+1/2+M/2)*(k+1/2)), M=N/2, into input
    // modulation, an N-point positive FFT, and output modulation. No transpose.
    // Two contiguous regions replace a runtime remainder for every sample.
    const unsigned int offset = plan.block / 4;
    const unsigned int split = size - offset;
    for (unsigned int index = 0; index < split; ++index) {
        const unsigned int source = index + offset;
        time[index] = real[source] * plan.post_cosine[index] - imaginary[source] * plan.post_sine[index];
    }
    for (unsigned int index = split; index < size; ++index) {
        const unsigned int source = index - split;
        time[index] = real[source] * plan.post_cosine[index] - imaginary[source] * plan.post_sine[index];
    }
}
} // namespace detail
} // namespace stx_vorbis
