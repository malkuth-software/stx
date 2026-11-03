#pragma once
// The CONV kernels' SIMD backend: a small primitive vocabulary over one
// 128-bit lane group, with scalar, AArch64 NEON and x86-64 SSE2 versions.
// Every kernel in src/detail/conv_*_kernel.hpp is written once over this
// vocabulary (BFFT's bruun_simd_backend.hpp layout) and instantiated twice:
// with NativeOps for whole lane groups and with ScalarOps for the tail.
//
// Lanes are independent positions (lines, channels, points). One primitive
// moves data between lanes: mul_lane<Lane>(a, weights) multiplies every lane
// of a by lane Lane of weights, the same correctly rounded product as
// mul(a, set(weight)), so a weight computed in a pixel lane can weight
// channel lanes without a store and a broadcast load. The backend is chosen at compile time; define
// STX_CONV_FORCE_SCALAR to use ScalarOps throughout, for comparison.
//
// Numerical contract: add, sub, mul and div are the correctly rounded IEEE
// operations in every backend, and the library builds with
// -ffp-contract=off, so the backends agree bit for bit on finite operands.
// min and max are specified for finite, non-NaN operands only; the backends
// differ on NaN (std::fmin ignores it, vminq propagates it, _mm_min_ps
// returns its second operand) and may differ on the sign of a zero result.
// keep_or_zero yields the value or +0 in every lane, bit for bit.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

// The instruction-set variant this translation unit compiles, as an inline
// namespace so that each variant's inline functions and template instances
// have their own symbols: src/lanes.cpp is compiled once per SIMD level
// (scalar, v128 and, on x86-64, avx2), and no variant's code can stand in for
// another's at link time. Everything else compiles v128.
#if !defined(STX_CONV_ISA)
#define STX_CONV_ISA v128
#endif

namespace stx_conv::kernels::detail {
inline namespace STX_CONV_ISA {

// A hint that the cache line holding `address` will be read soon. It reads
// nothing and changes no result; address must still point into (or one past)
// a live object, as for any pointer formed by a kernel.
STX_CONV_INLINE void prefetch_read(const void* address) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    __builtin_prefetch(address, 0, 3);
#else
    static_cast<void>(address);
#endif
}

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
    // value in every lane when keep, otherwise +0.
    static Value keep_or_zero(bool keep, Value value) noexcept {
        const Value result = keep ? value : Scalar(0);
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
    // a times lane Lane of weights; one lane, so Lane is 0.
    template<std::size_t Lane>
    static Value mul_lane(Value a, Value weights) noexcept {
        static_assert(Lane == 0, "one lane");
        const Value result = a * weights;
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
    static Value keep_or_zero(bool keep, Value value) noexcept {
        const uint32x4_t mask = vdupq_n_u32(std::uint32_t(0) - static_cast<std::uint32_t>(keep));
        const uint32x4_t kept = vandq_u32(mask, vreinterpretq_u32_f32(value));
        const Value result = vreinterpretq_f32_u32(kept);
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
    template<std::size_t Lane>
    static Value mul_lane(Value a, Value weights) noexcept {
        const Value result = vmulq_laneq_f32(a, weights, Lane);
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
    static Value keep_or_zero(bool keep, Value value) noexcept {
        const uint64x2_t mask = vdupq_n_u64(std::uint64_t(0) - static_cast<std::uint64_t>(keep));
        const uint64x2_t kept = vandq_u64(mask, vreinterpretq_u64_f64(value));
        const Value result = vreinterpretq_f64_u64(kept);
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
    template<std::size_t Lane>
    static Value mul_lane(Value a, Value weights) noexcept {
        const Value result = vmulq_laneq_f64(a, weights, Lane);
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
    static Value keep_or_zero(bool keep, Value value) noexcept {
        const Value mask = _mm_castsi128_ps(_mm_set1_epi32(-static_cast<int>(keep)));
        const Value result = _mm_and_ps(mask, value);
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
    template<std::size_t Lane>
    static Value mul_lane(Value a, Value weights) noexcept {
        const Value weight = _mm_shuffle_ps(weights, weights, _MM_SHUFFLE(Lane, Lane, Lane, Lane));
        const Value result = _mm_mul_ps(a, weight);
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
    static Value keep_or_zero(bool keep, Value value) noexcept {
        const Value mask = _mm_castsi128_pd(_mm_set1_epi64x(-static_cast<long long>(keep)));
        const Value result = _mm_and_pd(mask, value);
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
    template<std::size_t Lane>
    static Value mul_lane(Value a, Value weights) noexcept {
        const Value weight = _mm_shuffle_pd(weights, weights, Lane == 0 ? 0 : 3);
        const Value result = _mm_mul_pd(a, weight);
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

#if defined(STX_CONV_AVX2_LANES)
// 256-bit AVX2 lane groups for the lane-loop kernels (admission, bubble, jet,
// mapping, raw current, synthesis), compiled only in the avx2 variant of
// src/lanes.cpp, whose definitions carry the avx2 target. The tensor and warp
// kernels keep 128-bit channel lanes. Same operations as HardwareOps, lane for
// lane, so the results are the same bits.
template<class Scalar>
struct Avx2Ops;
template<>
struct Avx2Ops<float> {
    using Value = __m256;
    static constexpr std::size_t width = 8;
    static Value choose_negative(Value sign, Value negative, Value positive) noexcept {
        const Value mask = _mm256_cmp_ps(sign,  _mm256_setzero_ps(), _CMP_LT_OQ);
        const Value result = _mm256_blendv_ps(positive, negative, mask);
        return result;
    }
    static Value keep_or_zero(bool keep, Value value) noexcept {
        const Value mask = _mm256_castsi256_ps(_mm256_set1_epi32(-static_cast<int>(keep)));
        const Value result = _mm256_and_ps(mask, value);
        return result;
    }
    static Value load(const float* source) noexcept {
        const Value result = _mm256_loadu_ps(source);
        return result;
    }
    static void store(float* destination, Value value) noexcept { _mm256_storeu_ps(destination, value); }
    static Value set(float value) noexcept {
        const Value result = _mm256_set1_ps(value);
        return result;
    }
    static Value add(Value a, Value b) noexcept {
        const Value result = _mm256_add_ps(a, b);
        return result;
    }
    static Value sub(Value a, Value b) noexcept {
        const Value result = _mm256_sub_ps(a, b);
        return result;
    }
    static Value mul(Value a, Value b) noexcept {
        const Value result = _mm256_mul_ps(a, b);
        return result;
    }
    static Value div(Value a, Value b) noexcept {
        const Value result = _mm256_div_ps(a, b);
        return result;
    }
    static Value min(Value a, Value b) noexcept {
        const Value result = _mm256_min_ps(a, b);
        return result;
    }
    static Value max(Value a, Value b) noexcept {
        const Value result = _mm256_max_ps(a, b);
        return result;
    }
};
template<>
struct Avx2Ops<double> {
    using Value = __m256d;
    static constexpr std::size_t width = 4;
    static Value choose_negative(Value sign, Value negative, Value positive) noexcept {
        const Value mask = _mm256_cmp_pd(sign,  _mm256_setzero_pd(), _CMP_LT_OQ);
        const Value result = _mm256_blendv_pd(positive, negative, mask);
        return result;
    }
    static Value keep_or_zero(bool keep, Value value) noexcept {
        const Value mask = _mm256_castsi256_pd(_mm256_set1_epi64x(-static_cast<long long>(keep)));
        const Value result = _mm256_and_pd(mask, value);
        return result;
    }
    static Value load(const double* source) noexcept {
        const Value result = _mm256_loadu_pd(source);
        return result;
    }
    static void store(double* destination, Value value) noexcept { _mm256_storeu_pd(destination, value); }
    static Value set(double value) noexcept {
        const Value result = _mm256_set1_pd(value);
        return result;
    }
    static Value add(Value a, Value b) noexcept {
        const Value result = _mm256_add_pd(a, b);
        return result;
    }
    static Value sub(Value a, Value b) noexcept {
        const Value result = _mm256_sub_pd(a, b);
        return result;
    }
    static Value mul(Value a, Value b) noexcept {
        const Value result = _mm256_mul_pd(a, b);
        return result;
    }
    static Value div(Value a, Value b) noexcept {
        const Value result = _mm256_div_pd(a, b);
        return result;
    }
    static Value min(Value a, Value b) noexcept {
        const Value result = _mm256_min_pd(a, b);
        return result;
    }
    static Value max(Value a, Value b) noexcept {
        const Value result = _mm256_max_pd(a, b);
        return result;
    }
};
template<class Scalar>
using LaneOps = Avx2Ops<Scalar>;
#elif defined(STX_CONV_SCALAR_LANES)
// The scalar variant of src/lanes.cpp: one lane throughout.
template<class Scalar>
using LaneOps = ScalarOps<Scalar>;
#else
// The lane-loop kernels' lane group: the 128-bit backend.
template<class Scalar>
using LaneOps = NativeOps<Scalar>;
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

} // inline namespace STX_CONV_ISA
} // namespace stx_conv::kernels::detail
