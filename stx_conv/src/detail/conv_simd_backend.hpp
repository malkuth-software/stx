#pragma once
// The CONV kernels' SIMD backend: a small primitive vocabulary over one
// 128-bit lane group, with scalar, AArch64 NEON and x86-64 SSE2 versions.
// Every kernel in src/detail/conv_*_kernel.hpp is written once over this
// vocabulary (BFFT's bruun_simd_backend.hpp layout) and instantiated twice:
// with NativeOps for whole lane groups and with ScalarOps for the tail.
//
// Lanes are independent positions (lines, channels, points); no primitive
// moves data between lanes. The backend is chosen at compile time; define
// STX_CONV_FORCE_SCALAR to use ScalarOps throughout, for comparison.
//
// Numerical contract: add, sub, mul and div are the correctly rounded IEEE
// operations in every backend, and the library builds with
// -ffp-contract=off, so the backends agree bit for bit on finite operands.
// min and max are specified for finite, non-NaN operands only; the backends
// differ on NaN (std::fmin ignores it, vminq propagates it, _mm_min_ps
// returns its second operand) and may differ on the sign of a zero result.
#include <array>
#include <cmath>
#include <cstddef>
#if defined(__aarch64__)
#include <arm_neon.h>
#define STX_CONV_NEON 1
#elif defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define STX_CONV_SSE2 1
#endif
#if defined(__clang__) || defined(__GNUC__)
#define STX_CONV_INLINE inline __attribute__((always_inline))
#else
#define STX_CONV_INLINE inline
#endif

namespace stx_conv::kernels::detail {

// One lane.
template<class Scalar>
struct ScalarOps {
    using Value = Scalar;
    static constexpr std::size_t width = 1;

    // negative where sign < 0, otherwise positive.
    static Value choose_negative(Value sign, Value negative, Value positive) noexcept {
        const Value result = sign < Scalar(0) ? negative : positive;
        return result;
    }
    static Value load(const Scalar* source) noexcept { return *source; }
    static void store(Scalar* destination, Value value) noexcept { *destination = value; }
    static Value set(Scalar value) noexcept { return value; }
    static Value add(Value a, Value b) noexcept {
        const Value result = a + b;
        return result;
    }
    static Value sub(Value a, Value b) noexcept {
        const Value result = a - b;
        return result;
    }
    static Value mul(Value a, Value b) noexcept {
        const Value result = a * b;
        return result;
    }
    static Value div(Value a, Value b) noexcept {
        const Value result = a / b;
        return result;
    }
    static Value min(Value a, Value b) noexcept {
        const Value result = std::fmin(a, b);
        return result;
    }
    static Value max(Value a, Value b) noexcept {
        const Value result = std::fmax(a, b);
        return result;
    }
};

// A 128-bit lane group where the target has one; otherwise one lane.
template<class Scalar>
struct HardwareOps : ScalarOps<Scalar> {};

#if defined(STX_CONV_NEON)
template<>
struct HardwareOps<float> {
    using Value = float32x4_t;
    static constexpr std::size_t width = 4;

    static Value choose_negative(Value sign, Value negative, Value positive) noexcept {
        const uint32x4_t mask = vcltq_f32(sign, vdupq_n_f32(0.0f));
        const Value result = vbslq_f32(mask, negative, positive);
        return result;
    }
    static Value load(const float* source) noexcept {
        const Value result = vld1q_f32(source);
        return result;
    }
    static void store(float* destination, Value value) noexcept { vst1q_f32(destination, value); }
    static Value set(float value) noexcept {
        const Value result = vdupq_n_f32(value);
        return result;
    }
    static Value add(Value a, Value b) noexcept {
        const Value result = vaddq_f32(a, b);
        return result;
    }
    static Value sub(Value a, Value b) noexcept {
        const Value result = vsubq_f32(a, b);
        return result;
    }
    static Value mul(Value a, Value b) noexcept {
        const Value result = vmulq_f32(a, b);
        return result;
    }
    static Value div(Value a, Value b) noexcept {
        const Value result = vdivq_f32(a, b);
        return result;
    }
    static Value min(Value a, Value b) noexcept {
        const Value result = vminq_f32(a, b);
        return result;
    }
    static Value max(Value a, Value b) noexcept {
        const Value result = vmaxq_f32(a, b);
        return result;
    }
};

template<>
struct HardwareOps<double> {
    using Value = float64x2_t;
    static constexpr std::size_t width = 2;

    static Value choose_negative(Value sign, Value negative, Value positive) noexcept {
        const uint64x2_t mask = vcltq_f64(sign, vdupq_n_f64(0.0));
        const Value result = vbslq_f64(mask, negative, positive);
        return result;
    }
    static Value load(const double* source) noexcept {
        const Value result = vld1q_f64(source);
        return result;
    }
    static void store(double* destination, Value value) noexcept { vst1q_f64(destination, value); }
    static Value set(double value) noexcept {
        const Value result = vdupq_n_f64(value);
        return result;
    }
    static Value add(Value a, Value b) noexcept {
        const Value result = vaddq_f64(a, b);
        return result;
    }
    static Value sub(Value a, Value b) noexcept {
        const Value result = vsubq_f64(a, b);
        return result;
    }
    static Value mul(Value a, Value b) noexcept {
        const Value result = vmulq_f64(a, b);
        return result;
    }
    static Value div(Value a, Value b) noexcept {
        const Value result = vdivq_f64(a, b);
        return result;
    }
    static Value min(Value a, Value b) noexcept {
        const Value result = vminq_f64(a, b);
        return result;
    }
    static Value max(Value a, Value b) noexcept {
        const Value result = vmaxq_f64(a, b);
        return result;
    }
};
#elif defined(STX_CONV_SSE2)
template<>
struct HardwareOps<float> {
    using Value = __m128;
    static constexpr std::size_t width = 4;

    static Value choose_negative(Value sign, Value negative, Value positive) noexcept {
        const Value mask = _mm_cmplt_ps(sign, _mm_setzero_ps());
        const Value taken = _mm_and_ps(mask, negative);
        const Value kept = _mm_andnot_ps(mask, positive);
        const Value result = _mm_or_ps(taken, kept);
        return result;
    }
    static Value load(const float* source) noexcept {
        const Value result = _mm_loadu_ps(source);
        return result;
    }
    static void store(float* destination, Value value) noexcept { _mm_storeu_ps(destination, value); }
    static Value set(float value) noexcept {
        const Value result = _mm_set1_ps(value);
        return result;
    }
    static Value add(Value a, Value b) noexcept {
        const Value result = _mm_add_ps(a, b);
        return result;
    }
    static Value sub(Value a, Value b) noexcept {
        const Value result = _mm_sub_ps(a, b);
        return result;
    }
    static Value mul(Value a, Value b) noexcept {
        const Value result = _mm_mul_ps(a, b);
        return result;
    }
    static Value div(Value a, Value b) noexcept {
        const Value result = _mm_div_ps(a, b);
        return result;
    }
    static Value min(Value a, Value b) noexcept {
        const Value result = _mm_min_ps(a, b);
        return result;
    }
    static Value max(Value a, Value b) noexcept {
        const Value result = _mm_max_ps(a, b);
        return result;
    }
};

template<>
struct HardwareOps<double> {
    using Value = __m128d;
    static constexpr std::size_t width = 2;

    static Value choose_negative(Value sign, Value negative, Value positive) noexcept {
        const Value mask = _mm_cmplt_pd(sign, _mm_setzero_pd());
        const Value taken = _mm_and_pd(mask, negative);
        const Value kept = _mm_andnot_pd(mask, positive);
        const Value result = _mm_or_pd(taken, kept);
        return result;
    }
    static Value load(const double* source) noexcept {
        const Value result = _mm_loadu_pd(source);
        return result;
    }
    static void store(double* destination, Value value) noexcept { _mm_storeu_pd(destination, value); }
    static Value set(double value) noexcept {
        const Value result = _mm_set1_pd(value);
        return result;
    }
    static Value add(Value a, Value b) noexcept {
        const Value result = _mm_add_pd(a, b);
        return result;
    }
    static Value sub(Value a, Value b) noexcept {
        const Value result = _mm_sub_pd(a, b);
        return result;
    }
    static Value mul(Value a, Value b) noexcept {
        const Value result = _mm_mul_pd(a, b);
        return result;
    }
    static Value div(Value a, Value b) noexcept {
        const Value result = _mm_div_pd(a, b);
        return result;
    }
    static Value min(Value a, Value b) noexcept {
        const Value result = _mm_min_pd(a, b);
        return result;
    }
    static Value max(Value a, Value b) noexcept {
        const Value result = _mm_max_pd(a, b);
        return result;
    }
};
#endif

#if defined(STX_CONV_FORCE_SCALAR)
template<class Scalar>
using NativeOps = ScalarOps<Scalar>;
#else
template<class Scalar>
using NativeOps = HardwareOps<Scalar>;
#endif

// The backend's name for diagnostics: "neon-128", "sse2-128" or "scalar".
constexpr const char* native_backend_name() noexcept {
#if defined(STX_CONV_FORCE_SCALAR)
    return "scalar";
#elif defined(STX_CONV_NEON)
    return "neon-128";
#elif defined(STX_CONV_SSE2)
    return "sse2-128";
#else
    return "scalar";
#endif
}

} // namespace stx_conv::kernels::detail
