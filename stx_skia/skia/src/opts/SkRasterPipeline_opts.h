/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRasterPipeline_opts_DEFINED
#define SkRasterPipeline_opts_DEFINED

#include "include/core/SkSpan.h"
#include "include/core/SkTypes.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkTemplates.h"
#include "modules/skcms/skcms.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpContextTypes.h"
#include "src/core/SkUtils.h"  // unaligned_{load,store}
#include "src/shaders/SkPerlinNoiseShaderType.h"

#include <array>
#include <cstdint>
#include <type_traits>

// Every function in this file should be marked static and inline using SI.
#if defined(__clang__) || defined(__GNUC__)
    #define SI __attribute__((always_inline)) static inline
#else
    #define SI static inline
#endif

#if defined(__clang__)
    #define SK_UNROLL _Pragma("unroll")
#else
    #define SK_UNROLL
#endif

// Why does RasterPipeline have its own SIMD wrapper and is not using SkVx? SkVx is designed
// for keeping things simple, e.g. so you can put vectors in classes. SkVx has a very simple,
// predictable, memory layout - they are equivalent to a struct with an array of n values.
// Unfortunately, because of that, they will not pass in registers. A core design principle of
// SkRP is to have the 8 parameters passed into a stage be actual hardware registers (for
// optimal performance).
#if defined(__clang__)
    template <int N, typename T> using Vec = T __attribute__((ext_vector_type(N)));
#elif defined(__GNUC__)
    // Unfortunately, GCC does not allow us to omit the struct. This will not compile:
    //   template <int N, typename T> using Vec = T __attribute__((vector_size(N*sizeof(T))));
    template <int N, typename T> struct VecHelper {
        typedef T __attribute__((vector_size(N * sizeof(T)))) V;
    };
    template <int N, typename T> using Vec = typename VecHelper<N, T>::V;
#endif

template <typename Dst, typename Src>
SI Dst widen_cast(const Src& src) {
    static_assert(sizeof(Dst) > sizeof(Src));
    static_assert(std::is_trivially_copyable<Dst>::value);
    static_assert(std::is_trivially_copyable<Src>::value);
    Dst dst;
    memcpy(&dst, &src, sizeof(Src));
    return dst;
}

using NoCtx = const void*;

// Reinterprets a value's bits as another type of the same size: lane vectors, intrinsic vector
// types, and on the scalar path, same-size integers. It takes the value, not a reference; the
// reference-taking sk_bit_cast and std::bit_cast change code generation in the AVX-512 and
// AVX2 kernels.
template <typename To, typename From>
SI To bit_cast_lanes(From v) {
    static_assert(sizeof(To) == sizeof(From));
    To bits = __builtin_bit_cast(To, v);
    return bits;
}

// A stage kernel receives its context as its first parameter, as exactly the pointer type that
// SkRasterPipelineOpContext lists for its op and precision. The stage wrappers below take the
// kernel as a pointer of that type, so a kernel declaring another type does not compile.

template <SkRasterPipelineOp op, bool kHighp>
using StageContextPointer = SkRasterPipelineStageContextPointer<SkRasterPipelineOpContext<op>,
                                                                 kHighp>;

// The program's context for one stage, convertible only to the table's pointer type. It keeps
// the form of the untyped
// adapter it replaces, a temporary converted at the kernel call, because that form compiles to
// the same code; a helper function returning the pointer changes register allocation in
// perlin_noise and bicubic_clamp_8888.
template <SkRasterPipelineOp op, bool kHighp>
struct StageContext {
    const SkRasterPipelineStage* fStage;

    operator StageContextPointer<op, kHighp>() const {
        StageContextPointer<op, kHighp> context =
                static_cast<StageContextPointer<op, kHighp>>((*fStage).ctx);
        return context;
    }
};

#if defined(SKRP_CPU_SCALAR) || defined(SKRP_CPU_NEON) || defined(SKRP_CPU_AVX2) || \
        defined(SKRP_CPU_ML4) || defined(SKRP_CPU_AVX) || defined(SKRP_CPU_SSE41) || \
        defined(SKRP_CPU_SSE2)
    // Honor the existing setting
#elif !defined(__clang__) && !defined(__GNUC__)
    #define SKRP_CPU_SCALAR
#elif defined(SK_ARM_HAS_NEON)
    #define SKRP_CPU_NEON
#elif SK_CPU_X64_LEVEL >= SK_CPU_X64_LEVEL_ML4
    #define SKRP_CPU_ML4
#elif SK_CPU_X64_LEVEL >= SK_CPU_X64_LEVEL_AVX2
    #define SKRP_CPU_AVX2
#elif SK_CPU_X64_LEVEL >= SK_CPU_X64_LEVEL_AVX
    #define SKRP_CPU_AVX
#elif SK_CPU_X64_LEVEL >= SK_CPU_X64_LEVEL_SSE41
    #define SKRP_CPU_SSE41
#elif SK_CPU_X64_LEVEL >= SK_CPU_X64_LEVEL_SSE2
    #define SKRP_CPU_SSE2
#elif SK_CPU_LSX_LEVEL >= SK_CPU_LSX_LEVEL_LASX
    #define SKRP_CPU_LASX
#elif SK_CPU_LSX_LEVEL >= SK_CPU_LSX_LEVEL_LSX
    #define SKRP_CPU_LSX
#else
    #define SKRP_CPU_SCALAR
#endif

#if defined(SKRP_CPU_SCALAR)
    #include <math.h>
#elif defined(SKRP_CPU_NEON)
    #include <arm_neon.h>
#elif defined(SKRP_CPU_LASX)
    #include <lasxintrin.h>
    #include <lsxintrin.h>
#elif defined(SKRP_CPU_LSX)
    #include <lsxintrin.h>
#else
    #include <immintrin.h>
#endif

// Notes:
// * rcp_fast and rcp_precise both produce a reciprocal, but rcp_fast is an estimate with at least
//   12 bits of precision while rcp_precise should be accurate for float size. For ARM rcp_precise
//   requires 2 Newton-Raphson refinement steps because its estimate has 8 bit precision, and for
//   Intel this requires one additional step because its estimate has 12 bit precision.
//
// * Don't call rcp_approx or rsqrt_approx directly; only use rcp_fast and rsqrt.

namespace SK_OPTS_NS {
#if defined(SKRP_CPU_SCALAR)
    // This path should lead to portable scalar code.
    using F   = float   ;
    using I32 =  int32_t;
    using U64 = uint64_t;
    using U32 = uint32_t;
    using U16 = uint16_t;
    using U8  = uint8_t ;

    SI F   min(F a, F b)     { return fminf(a,b); }
    SI I32 min(I32 a, I32 b) { return a < b ? a : b; }
    SI U32 min(U32 a, U32 b) { return a < b ? a : b; }
    SI F   max(F a, F b)     { return fmaxf(a,b); }
    SI I32 max(I32 a, I32 b) { return a > b ? a : b; }
    SI U32 max(U32 a, U32 b) { return a > b ? a : b; }

    SI F   mad(F f, F m, F a)   { return a+f*m; }
    SI F   nmad(F f, F m, F a)  { return a-f*m; }
    SI F   abs_  (F v)          { return fabsf(v); }
    SI I32 abs_  (I32 v)        { return v < 0 ? -v : v; }
    SI F   floor_(F v)          { return floorf(v); }
    SI F    ceil_(F v)          { return ceilf(v); }
    SI F   rcp_approx(F v)      { return 1.0f / v; }  // use rcp_fast instead
    SI F   rsqrt_approx(F v)    { return 1.0f / sqrtf(v); }
    SI F   sqrt_ (F v)          { return sqrtf(v); }
    SI F   rcp_precise (F v)    { return 1.0f / v; }

    SI I32 iround(F v)          { return static_cast<I32>((v + 0.5f)); }
    SI U32 round(F v)           { return static_cast<U32>((v + 0.5f)); }
    SI U16 pack(U32 v)          { return static_cast<U16>(v); }
    SI U8  pack(U16 v)          { return  static_cast<U8>(v); }

    SI F   if_then_else(I32 c, F   t, F   e) { return c ? t : e; }
    SI I32 if_then_else(I32 c, I32 t, I32 e) { return c ? t : e; }

    SI bool any(I32 c)                 { return c != 0; }
    SI bool all(I32 c)                 { return c != 0; }

    template <typename T>
    SI T gather(const T* p, U32 ix) { return p[ix]; }
    template <typename T>
    SI T gather_unaligned(const T* p, U32 ix) { return gather<T>(p, ix); }

    SI void scatter_masked(I32 src, int* dst, U32 ix, I32 mask) {
        dst[ix] = mask ? src : dst[ix];
    }

    SI void load2(const uint16_t* ptr, U16* r, U16* g) {
        *r = ptr[0];
        *g = ptr[1];
    }
    SI void store2(uint16_t* ptr, U16 r, U16 g) {
        ptr[0] = r;
        ptr[1] = g;
    }
    SI void load4(const uint16_t* ptr, U16* r, U16* g, U16* b, U16* a) {
        *r = ptr[0];
        *g = ptr[1];
        *b = ptr[2];
        *a = ptr[3];
    }
    SI void store4(uint16_t* ptr, U16 r, U16 g, U16 b, U16 a) {
        ptr[0] = r;
        ptr[1] = g;
        ptr[2] = b;
        ptr[3] = a;
    }

    SI void load4(const float* ptr, F* r, F* g, F* b, F* a) {
        *r = ptr[0];
        *g = ptr[1];
        *b = ptr[2];
        *a = ptr[3];
    }
    SI void store4(float* ptr, F r, F g, F b, F a) {
        ptr[0] = r;
        ptr[1] = g;
        ptr[2] = b;
        ptr[3] = a;
    }

#elif defined(SKRP_CPU_NEON)
    template <typename T> using V = Vec<4, T>;
    using F   = V<float   >;
    using I32 = V< int32_t>;
    using U64 = V<uint64_t>;
    using U32 = V<uint32_t>;
    using U16 = V<uint16_t>;
    using U8  = V<uint8_t >;

    // We polyfill a few routines that Clang doesn't build into ext_vector_types.
    SI F   min(F a, F b)     { return vminq_f32(a,b); }
    SI I32 min(I32 a, I32 b) { return vminq_s32(a,b); }
    SI U32 min(U32 a, U32 b) { return vminq_u32(a,b); }
    SI F   max(F a, F b)     { return vmaxq_f32(a,b); }
    SI I32 max(I32 a, I32 b) { return vmaxq_s32(a,b); }
    SI U32 max(U32 a, U32 b) { return vmaxq_u32(a,b); }

    SI F   abs_  (F v)       { return vabsq_f32(v); }
    SI I32 abs_  (I32 v)     { return vabsq_s32(v); }
    SI F   rcp_approx(F v)   { float32x4_t e = vrecpeq_f32(v);  return vrecpsq_f32 (v,e  ) * e; }
    SI F   rcp_precise(F v)  { F e = rcp_approx(v);   return vrecpsq_f32 (v,e  ) * e; }
    SI F   rsqrt_approx(F v) { float32x4_t e = vrsqrteq_f32(v); return vrsqrtsq_f32(v,e*e) * e; }

    SI U16 pack(U32 v)       { return __builtin_convertvector(v, U16); }
    SI U8  pack(U16 v)       { return __builtin_convertvector(v,  U8); }

    SI F   if_then_else(I32 c, F   t, F   e) { return vbslq_f32(bit_cast_lanes<U32>(c),t,e); }
    SI I32 if_then_else(I32 c, I32 t, I32 e) { return vbslq_s32(bit_cast_lanes<U32>(c),t,e); }

    #if defined(SK_CPU_ARM64)
        SI bool any(I32 c) { return vmaxvq_u32(bit_cast_lanes<U32>(c)) != 0; }
        SI bool all(I32 c) { return vminvq_u32(bit_cast_lanes<U32>(c)) != 0; }

        SI F     mad(F f, F m, F a) { return vfmaq_f32(a,f,m); }
        SI F    nmad(F f, F m, F a) { return vfmsq_f32(a,f,m); }
        SI F  floor_(F v)           { return vrndmq_f32(v); }
        SI F   ceil_(F v)           { return vrndpq_f32(v); }
        SI F   sqrt_(F v)           { return vsqrtq_f32(v); }
        SI I32 iround(F v)          { return vcvtnq_s32_f32(v); }
        SI U32 round(F v)           { return vcvtnq_u32_f32(v); }
    #else
        SI bool any(I32 c) { return c[0] | c[1] | c[2] | c[3]; }
        SI bool all(I32 c) { return c[0] & c[1] & c[2] & c[3]; }

        SI F mad(F f, F m, F a)  { return vmlaq_f32(a,f,m); }
        SI F nmad(F f, F m, F a) { return vmlsq_f32(a,f,m); }

        SI F floor_(F v) {
            F roundtrip = vcvtq_f32_s32(vcvtq_s32_f32(v));
            return roundtrip - if_then_else(roundtrip > v, F() + 1, F());
        }

        SI F ceil_(F v) {
            F roundtrip = vcvtq_f32_s32(vcvtq_s32_f32(v));
            return roundtrip + if_then_else(roundtrip < v, F() + 1, F());
        }

        SI F sqrt_(F v) {
            float32x4_t e = vrsqrteq_f32(v);  // Estimate and two refinement steps for e = rsqrt(v).
            e *= vrsqrtsq_f32(v,e*e);
            e *= vrsqrtsq_f32(v,e*e);
            return v*e;                // sqrt(v) == v*rsqrt(v).
        }

        SI I32 iround(F v) {
            return vcvtq_s32_f32(v + 0.5f);
        }

        SI U32 round(F v) {
            return vcvtq_u32_f32(v + 0.5f);
        }
    #endif

    template <typename T>
    SI V<T> gather(const T* ptr, U32 ix) {
        // The compiler assumes ptr is aligned, which caused crashes on some
        // arm32 chips because a register was marked as "aligned to 32 bits"
        // incorrectly. skbug.com/409859319
        SkASSERTF(reinterpret_cast<uintptr_t>(ptr) % alignof(T) == 0,
                 "Should use gather_unaligned");
        return V<T>{ptr[ix[0]], ptr[ix[1]], ptr[ix[2]], ptr[ix[3]]};
    }
    template <typename T>
    SI V<T> gather_unaligned(const T* ptr, U32 ix) {
        // This tells the compiler ptr might not be aligned appropriately, so
        // it generates better assembly.
        typedef T __attribute__ ((aligned (1))) unaligned_ptr;
        const unaligned_ptr* uptr = static_cast<const unaligned_ptr*>(ptr);
        return V<T>{uptr[ix[0]], uptr[ix[1]], uptr[ix[2]], uptr[ix[3]]};
    }

    SI void scatter_masked(I32 src, int* dst, U32 ix, I32 mask) {
        I32 before = gather(dst, ix);
        I32 after = if_then_else(mask, src, before);
        dst[ix[0]] = after[0];
        dst[ix[1]] = after[1];
        dst[ix[2]] = after[2];
        dst[ix[3]] = after[3];
    }
    SI void load2(const uint16_t* ptr, U16* r, U16* g) {
        uint16x4x2_t rg = vld2_u16(ptr);
        *r = rg.val[0];
        *g = rg.val[1];
    }
    SI void store2(uint16_t* ptr, U16 r, U16 g) {
        vst2_u16(ptr, (uint16x4x2_t{{r,g}}));
    }
    SI void load4(const uint16_t* ptr, U16* r, U16* g, U16* b, U16* a) {
        uint16x4x4_t rgba = vld4_u16(ptr);
        *r = rgba.val[0];
        *g = rgba.val[1];
        *b = rgba.val[2];
        *a = rgba.val[3];
    }

    SI void store4(uint16_t* ptr, U16 r, U16 g, U16 b, U16 a) {
        vst4_u16(ptr, (uint16x4x4_t{{r,g,b,a}}));
    }
    SI void load4(const float* ptr, F* r, F* g, F* b, F* a) {
        float32x4x4_t rgba = vld4q_f32(ptr);
        *r = rgba.val[0];
        *g = rgba.val[1];
        *b = rgba.val[2];
        *a = rgba.val[3];
    }
    SI void store4(float* ptr, F r, F g, F b, F a) {
        vst4q_f32(ptr, (float32x4x4_t{{r,g,b,a}}));
    }

#elif defined(SKRP_CPU_ML4)
    template <typename T> using V = Vec<16, T>;
    using F   = V<float   >;
    using I32 = V< int32_t>;
    using U64 = V<uint64_t>;
    using U32 = V<uint32_t>;
    using U16 = V<uint16_t>;
    using U8  = V<uint8_t >;

    SI F   mad(F f, F m, F a) { return _mm512_fmadd_ps(f, m, a); }
    SI F  nmad(F f, F m, F a) { return _mm512_fnmadd_ps(f, m, a); }
    SI F   min(F a, F b)     { return _mm512_min_ps(a,b);    }
    SI I32 min(I32 a, I32 b) { return bit_cast_lanes<I32>(_mm512_min_epi32(bit_cast_lanes<__m512i>(a),bit_cast_lanes<__m512i>(b))); }
    SI U32 min(U32 a, U32 b) { return bit_cast_lanes<U32>(_mm512_min_epu32(bit_cast_lanes<__m512i>(a),bit_cast_lanes<__m512i>(b))); }
    SI F   max(F a, F b)     { return _mm512_max_ps(a,b);    }
    SI I32 max(I32 a, I32 b) { return bit_cast_lanes<I32>(_mm512_max_epi32(bit_cast_lanes<__m512i>(a),bit_cast_lanes<__m512i>(b))); }
    SI U32 max(U32 a, U32 b) { return bit_cast_lanes<U32>(_mm512_max_epu32(bit_cast_lanes<__m512i>(a),bit_cast_lanes<__m512i>(b))); }
    SI F   abs_  (F v)   { return _mm512_and_ps(v, _mm512_sub_ps(_mm512_setzero(), v)); }
    SI I32 abs_  (I32 v) { return bit_cast_lanes<I32>(_mm512_abs_epi32(bit_cast_lanes<__m512i>(v)));   }
    SI F   floor_(F v)   { return _mm512_floor_ps(v);    }
    SI F   ceil_(F v)    { return _mm512_ceil_ps(v);     }
    SI F   rcp_approx(F v) { return _mm512_rcp14_ps  (v);  }
    SI F   rsqrt_approx (F v)   { return _mm512_rsqrt14_ps(v);  }
    SI F   sqrt_ (F v)   { return _mm512_sqrt_ps (v);    }
    SI F rcp_precise (F v) {
        F e = rcp_approx(v);
        return _mm512_fnmadd_ps(v, e, _mm512_set1_ps(2.0f)) * e;
    }
    SI I32 iround(F v)         { return bit_cast_lanes<I32>(_mm512_cvtps_epi32(v)); }
    SI U32 round(F v)          { return bit_cast_lanes<U32>(_mm512_cvtps_epi32(v)); }
    SI U16 pack(U32 v) {
        __m256i rst = _mm256_packus_epi32(_mm512_castsi512_si256(bit_cast_lanes<__m512i>(v)),
                                          _mm512_extracti64x4_epi64(bit_cast_lanes<__m512i>(v), 1));
        return bit_cast_lanes<U16>(_mm256_permutex_epi64(rst, 216));
    }
    SI U8 pack(U16 v) {
        __m256i rst = _mm256_packus_epi16(bit_cast_lanes<__m256i>(v), bit_cast_lanes<__m256i>(v));
        return bit_cast_lanes<U8>(_mm256_castsi256_si128(_mm256_permute4x64_epi64(rst, 8)));
    }
    SI F if_then_else(I32 c, F t, F e) {
        __m512i mask = _mm512_set1_epi32(0x80000000);
        __m512i aa = _mm512_and_si512(bit_cast_lanes<__m512i>(c), mask);
        return _mm512_mask_blend_ps(_mm512_test_epi32_mask(aa, aa),e,t);
    }
    SI I32 if_then_else(I32 c, I32 t, I32 e) {
        __m512i mask = _mm512_set1_epi32(0x80000000);
        __m512i aa = _mm512_and_si512(bit_cast_lanes<__m512i>(c), mask);
        return bit_cast_lanes<I32>(_mm512_mask_blend_epi32(_mm512_test_epi32_mask(aa, aa),bit_cast_lanes<__m512i>(e),bit_cast_lanes<__m512i>(t)));
    }
    SI bool any(I32 c) {
        __mmask16 mask32 = _mm512_test_epi32_mask(bit_cast_lanes<__m512i>(c), bit_cast_lanes<__m512i>(c));
        return mask32 != 0;
    }
    SI bool all(I32 c) {
        __mmask16 mask32 = _mm512_test_epi32_mask(bit_cast_lanes<__m512i>(c), bit_cast_lanes<__m512i>(c));
        return mask32 == 0xffff;
    }
    template <typename T>
    SI V<T> gather(const T* p, U32 ix) {
        return V<T>{ p[ix[ 0]], p[ix[ 1]], p[ix[ 2]], p[ix[ 3]],
                     p[ix[ 4]], p[ix[ 5]], p[ix[ 6]], p[ix[ 7]],
                     p[ix[ 8]], p[ix[ 9]], p[ix[10]], p[ix[11]],
                     p[ix[12]], p[ix[13]], p[ix[14]], p[ix[15]] };
    }
    SI F   gather(const float* p, U32 ix) { return _mm512_i32gather_ps(bit_cast_lanes<__m512i>(ix), p, 4); }
    SI U32 gather(const uint32_t* p, U32 ix) {
        return bit_cast_lanes<U32>(_mm512_i32gather_epi32(bit_cast_lanes<__m512i>(ix), p, 4)); }
    SI U64 gather(const uint64_t* p, U32 ix) {
        __m512i parts[] = {
            _mm512_i32gather_epi64(_mm512_castsi512_si256(bit_cast_lanes<__m512i>(ix)), p, 8),
            _mm512_i32gather_epi64(_mm512_extracti32x8_epi32(bit_cast_lanes<__m512i>(ix), 1), p, 8),
        };
        return sk_bit_cast<U64>(parts);
    }
    template <typename T>
    SI V<T> gather_unaligned(const T* p, U32 ix) {
        return gather(p, ix);
    }

    template <typename V, typename S>
    SI void scatter_masked(V src, S* dst, U32 ix, I32 mask) {
        V before = gather(dst, ix);
        V after = if_then_else(mask, src, before);
        dst[ix[0]] = after[0];
        dst[ix[1]] = after[1];
        dst[ix[2]] = after[2];
        dst[ix[3]] = after[3];
        dst[ix[4]] = after[4];
        dst[ix[5]] = after[5];
        dst[ix[6]] = after[6];
        dst[ix[7]] = after[7];
        dst[ix[8]] = after[8];
        dst[ix[9]] = after[9];
        dst[ix[10]] = after[10];
        dst[ix[11]] = after[11];
        dst[ix[12]] = after[12];
        dst[ix[13]] = after[13];
        dst[ix[14]] = after[14];
        dst[ix[15]] = after[15];
    }

    // The compiler often scalarizes integer division into scalar idiv loops because
    // AVX512 and below don't have vectorized integer division. We can force vectorization by
    // performing the division in double precision. A 64-bit double has 53 bits of mantissa,
    // which is enough to represent any 32-bit integer exactly.
    SI void div_fn(I32* dst, I32* src) {
        // Load using 256-bit registers to avoid 512-bit performance issues
        __m256i d_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(dst) + 0);
        __m256i d_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(dst) + 1);
        __m256i s_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src) + 0);
        __m256i s_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src) + 1);

        // Split lower 256-bit into two 128-bit registers (each holding 4 lanes)
        __m128i d0 = _mm256_castsi256_si128(d_lo);
        __m128i d1 = _mm256_extractf128_si256(d_lo, 1);
        __m128i s0 = _mm256_castsi256_si128(s_lo);
        __m128i s1 = _mm256_extractf128_si256(s_lo, 1);

        // Split upper 256-bit into two 128-bit registers (each holding 4 lanes)
        __m128i d2 = _mm256_castsi256_si128(d_hi);
        __m128i d3 = _mm256_extractf128_si256(d_hi, 1);
        __m128i s2 = _mm256_castsi256_si128(s_hi);
        __m128i s3 = _mm256_extractf128_si256(s_hi, 1);

        // Execute four independent 256-bit divisions (F64 vector width 4) (which can be pipelined)
        // Double-precision division natively handles dividing by zero (producing +/-Inf/NaN)
        // and INT_MIN / -1 overflow (producing +2147483648.0) without any hardware crash.
        // Upon truncation back to I32, both saturate to INT_MIN (0x80000000).
        __m256d fd0 = _mm256_cvtepi32_pd(d0);    // convert to double
        __m256d fs0 = _mm256_cvtepi32_pd(s0);
        __m256d fr0 = _mm256_div_pd(fd0, fs0);   // divide
        __m128i r0  = _mm256_cvttpd_epi32(fr0);  // convert to int (truncates toward zero)

        __m256d fd1 = _mm256_cvtepi32_pd(d1);
        __m256d fs1 = _mm256_cvtepi32_pd(s1);
        __m256d fr1 = _mm256_div_pd(fd1, fs1);
        __m128i r1  = _mm256_cvttpd_epi32(fr1);

        __m256d fd2 = _mm256_cvtepi32_pd(d2);
        __m256d fs2 = _mm256_cvtepi32_pd(s2);
        __m256d fr2 = _mm256_div_pd(fd2, fs2);
        __m128i r2  = _mm256_cvttpd_epi32(fr2);

        __m256d fd3 = _mm256_cvtepi32_pd(d3);
        __m256d fs3 = _mm256_cvtepi32_pd(s3);
        __m256d fr3 = _mm256_div_pd(fd3, fs3);
        __m128i r3  = _mm256_cvttpd_epi32(fr3);

        // Recombine the four 128-bit blocks into two 256-bit registers and store
        __m256i r_lo = _mm256_insertf128_si256(_mm256_castsi128_si256(r0), r1, 1);
        __m256i r_hi = _mm256_insertf128_si256(_mm256_castsi128_si256(r2), r3, 1);

        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst) + 0, r_lo);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst) + 1, r_hi);
    }
    #define SKRP_SUPPORTS_OPTIMIZED_DIV_INT

    SI void div_fn(U32* dst, U32* src) {
        // Load using 256-bit registers to avoid 512-bit performance issues.
        __m256i d_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(dst) + 0);
        __m256i d_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(dst) + 1);
        __m256i s_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src) + 0);
        __m256i s_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src) + 1);

        // Split lower 256-bit into two 128-bit registers (each holding 4 lanes)
        __m128i d0 = _mm256_castsi256_si128(d_lo);
        __m128i d1 = _mm256_extractf128_si256(d_lo, 1);
        __m128i s0 = _mm256_castsi256_si128(s_lo);
        __m128i s1 = _mm256_extractf128_si256(s_lo, 1);

        // Split upper 256-bit into two 128-bit registers (each holding 4 lanes)
        __m128i d2 = _mm256_castsi256_si128(d_hi);
        __m128i d3 = _mm256_extractf128_si256(d_hi, 1);
        __m128i s2 = _mm256_castsi256_si128(s_hi);
        __m128i s3 = _mm256_extractf128_si256(s_hi, 1);

        // Convert double, divide, and convert back to U32.
        // Double-precision division natively handles dividing by zero (producing +Inf/NaN).
        // Upon truncation back to U32, both saturate to INT_MIN (0x80000000).
        __m256d fd0 = _mm256_cvtepu32_pd(d0);   // convert to double
        __m256d fs0 = _mm256_cvtepu32_pd(s0);
        __m256d fr0 = _mm256_div_pd(fd0, fs0);  // divide
        __m128i r0  = _mm256_cvttpd_epu32(fr0); // convert to uint (truncates toward zero)

        __m256d fd1 = _mm256_cvtepu32_pd(d1);
        __m256d fs1 = _mm256_cvtepu32_pd(s1);
        __m256d fr1 = _mm256_div_pd(fd1, fs1);
        __m128i r1  = _mm256_cvttpd_epu32(fr1);

        __m256d fd2 = _mm256_cvtepu32_pd(d2);
        __m256d fs2 = _mm256_cvtepu32_pd(s2);
        __m256d fr2 = _mm256_div_pd(fd2, fs2);
        __m128i r2  = _mm256_cvttpd_epu32(fr2);

        __m256d fd3 = _mm256_cvtepu32_pd(d3);
        __m256d fs3 = _mm256_cvtepu32_pd(s3);
        __m256d fr3 = _mm256_div_pd(fd3, fs3);
        __m128i r3  = _mm256_cvttpd_epu32(fr3);

        // Recombine the four 128-bit blocks into two 256-bit registers and store
        __m256i r_lo = _mm256_insertf128_si256(_mm256_castsi128_si256(r0), r1, 1);
        __m256i r_hi = _mm256_insertf128_si256(_mm256_castsi128_si256(r2), r3, 1);

        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst) + 0, r_lo);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst) + 1, r_hi);
    }
    #define SKRP_SUPPORTS_OPTIMIZED_DIV_UINT

    SI void load2(const uint16_t* ptr, U16* r, U16* g) {
        __m256i _01234567 = _mm256_loadu_si256((reinterpret_cast<const __m256i*>(ptr)) + 0);
        __m256i _89abcdef = _mm256_loadu_si256((reinterpret_cast<const __m256i*>(ptr)) + 1);

        *r = bit_cast_lanes<U16>(_mm256_permute4x64_epi64(_mm256_packs_epi32(_mm256_srai_epi32(_mm256_slli_epi32
            (_01234567, 16), 16), _mm256_srai_epi32(_mm256_slli_epi32(_89abcdef, 16), 16)), 216));
        *g = bit_cast_lanes<U16>(_mm256_permute4x64_epi64(_mm256_packs_epi32(_mm256_srai_epi32(_01234567, 16),
                             _mm256_srai_epi32(_89abcdef, 16)), 216));
    }
    SI void store2(uint16_t* ptr, U16 r, U16 g) {
        __m256i _01234567 = _mm256_unpacklo_epi16(bit_cast_lanes<__m256i>(r), bit_cast_lanes<__m256i>(g));
        __m256i _89abcdef = _mm256_unpackhi_epi16(bit_cast_lanes<__m256i>(r), bit_cast_lanes<__m256i>(g));
        __m512i combinedVector = _mm512_inserti64x4(_mm512_castsi256_si512(_01234567),
                    _89abcdef, 1);
        __m512i aa = _mm512_permutexvar_epi64(_mm512_setr_epi64(0,1,4,5,2,3,6,7), combinedVector);
        _01234567 = _mm512_castsi512_si256(aa);
        _89abcdef = _mm512_extracti64x4_epi64(aa, 1);

        _mm256_storeu_si256(reinterpret_cast<__m256i*>(ptr) + 0, _01234567);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(ptr) + 1, _89abcdef);
    }

    SI void load4(const uint16_t* ptr, U16* r, U16* g, U16* b, U16* a) {
        __m256i _0123 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr));
        __m256i _4567 = _mm256_loadu_si256((reinterpret_cast<const __m256i*>(ptr)) + 1);
        __m256i _89ab = _mm256_loadu_si256((reinterpret_cast<const __m256i*>(ptr)) + 2);
        __m256i _cdef = _mm256_loadu_si256((reinterpret_cast<const __m256i*>(ptr)) + 3);

        __m256i a0 = _mm256_unpacklo_epi16(_0123, _4567);
        __m256i a1 = _mm256_unpackhi_epi16(_0123, _4567);
        __m256i b0 = _mm256_unpacklo_epi16(a0, a1);
        __m256i b1 = _mm256_unpackhi_epi16(a0, a1);
        __m256i a2 = _mm256_unpacklo_epi16(_89ab, _cdef);
        __m256i a3 = _mm256_unpackhi_epi16(_89ab, _cdef);
        __m256i b2 = _mm256_unpacklo_epi16(a2, a3);
        __m256i b3 = _mm256_unpackhi_epi16(a2, a3);
        __m256i rr = _mm256_unpacklo_epi64(b0, b2);
        __m256i gg = _mm256_unpackhi_epi64(b0, b2);
        __m256i bb = _mm256_unpacklo_epi64(b1, b3);
        __m256i aa = _mm256_unpackhi_epi64(b1, b3);

        *r = bit_cast_lanes<U16>(_mm256_permutexvar_epi32(_mm256_setr_epi32(0,4,1,5,2,6,3,7), rr));
        *g = bit_cast_lanes<U16>(_mm256_permutexvar_epi32(_mm256_setr_epi32(0,4,1,5,2,6,3,7), gg));
        *b = bit_cast_lanes<U16>(_mm256_permutexvar_epi32(_mm256_setr_epi32(0,4,1,5,2,6,3,7), bb));
        *a = bit_cast_lanes<U16>(_mm256_permutexvar_epi32(_mm256_setr_epi32(0,4,1,5,2,6,3,7), aa));
    }
    SI void store4(uint16_t* ptr, U16 r, U16 g, U16 b, U16 a) {
        __m256i rg012389ab = _mm256_unpacklo_epi16(bit_cast_lanes<__m256i>(r), bit_cast_lanes<__m256i>(g));
        __m256i rg4567cdef = _mm256_unpackhi_epi16(bit_cast_lanes<__m256i>(r), bit_cast_lanes<__m256i>(g));
        __m256i ba012389ab = _mm256_unpacklo_epi16(bit_cast_lanes<__m256i>(b), bit_cast_lanes<__m256i>(a));
        __m256i ba4567cdef = _mm256_unpackhi_epi16(bit_cast_lanes<__m256i>(b), bit_cast_lanes<__m256i>(a));

        __m256i _0189 = _mm256_unpacklo_epi32(rg012389ab, ba012389ab);
        __m256i _23ab = _mm256_unpackhi_epi32(rg012389ab, ba012389ab);
        __m256i _45cd = _mm256_unpacklo_epi32(rg4567cdef, ba4567cdef);
        __m256i _67ef = _mm256_unpackhi_epi32(rg4567cdef, ba4567cdef);

        __m256i _ab23 = _mm256_permutex_epi64(_23ab, 78),
             _0123 = _mm256_blend_epi32(_0189, _ab23, 0xf0),
             _89ab = _mm256_permutex_epi64(_mm256_blend_epi32(_0189, _ab23, 0x0f), 78),
             _ef67 = _mm256_permutex_epi64(_67ef, 78),
             _4567 = _mm256_blend_epi32(_45cd, _ef67, 0xf0),
             _cdef = _mm256_permutex_epi64(_mm256_blend_epi32(_45cd, _ef67, 0x0f), 78);

        _mm256_storeu_si256(reinterpret_cast<__m256i*>(ptr), _0123);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(ptr) + 1, _4567);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(ptr) + 2, _89ab);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(ptr) + 3, _cdef);
    }

    SI void load4(const float* ptr, F* r, F* g, F* b, F* a) {
        F _048c;
        F _159d;
        F _26ae;
        F _37bf;

        _048c = _mm512_castps128_ps512(_mm_loadu_ps(ptr)         );
        _048c = _mm512_insertf32x4(_048c, _mm_loadu_ps(ptr+16), 1);
        _048c = _mm512_insertf32x4(_048c, _mm_loadu_ps(ptr+32), 2);
        _048c = _mm512_insertf32x4(_048c, _mm_loadu_ps(ptr+48), 3);
        _159d = _mm512_castps128_ps512(_mm_loadu_ps(ptr+4)       );
        _159d = _mm512_insertf32x4(_159d, _mm_loadu_ps(ptr+20), 1);
        _159d = _mm512_insertf32x4(_159d, _mm_loadu_ps(ptr+36), 2);
        _159d = _mm512_insertf32x4(_159d, _mm_loadu_ps(ptr+52), 3);
        _26ae = _mm512_castps128_ps512(_mm_loadu_ps(ptr+8)       );
        _26ae = _mm512_insertf32x4(_26ae, _mm_loadu_ps(ptr+24), 1);
        _26ae = _mm512_insertf32x4(_26ae, _mm_loadu_ps(ptr+40), 2);
        _26ae = _mm512_insertf32x4(_26ae, _mm_loadu_ps(ptr+56), 3);
        _37bf = _mm512_castps128_ps512(_mm_loadu_ps(ptr+12)      );
        _37bf = _mm512_insertf32x4(_37bf, _mm_loadu_ps(ptr+28), 1);
        _37bf = _mm512_insertf32x4(_37bf, _mm_loadu_ps(ptr+44), 2);
        _37bf = _mm512_insertf32x4(_37bf, _mm_loadu_ps(ptr+60), 3);

        F rg02468acf = _mm512_unpacklo_ps(_048c, _26ae);
        F ba02468acf = _mm512_unpackhi_ps(_048c, _26ae);
        F rg13579bde = _mm512_unpacklo_ps(_159d, _37bf);
        F ba13579bde = _mm512_unpackhi_ps(_159d, _37bf);

        *r = bit_cast_lanes<F>(_mm512_unpacklo_ps(rg02468acf, rg13579bde));
        *g = bit_cast_lanes<F>(_mm512_unpackhi_ps(rg02468acf, rg13579bde));
        *b = bit_cast_lanes<F>(_mm512_unpacklo_ps(ba02468acf, ba13579bde));
        *a = bit_cast_lanes<F>(_mm512_unpackhi_ps(ba02468acf, ba13579bde));
    }

    SI void store4(float* ptr, F r, F g, F b, F a) {
        F rg014589cd = _mm512_unpacklo_ps(r, g);
        F rg2367abef = _mm512_unpackhi_ps(r, g);
        F ba014589cd = _mm512_unpacklo_ps(b, a);
        F ba2367abef = _mm512_unpackhi_ps(b, a);

        F _048c = bit_cast_lanes<F>(_mm512_unpacklo_pd(bit_cast_lanes<__m512d>(rg014589cd), bit_cast_lanes<__m512d>(ba014589cd)));
        F _26ae = bit_cast_lanes<F>(_mm512_unpacklo_pd(bit_cast_lanes<__m512d>(rg2367abef), bit_cast_lanes<__m512d>(ba2367abef)));
        F _159d = bit_cast_lanes<F>(_mm512_unpackhi_pd(bit_cast_lanes<__m512d>(rg014589cd), bit_cast_lanes<__m512d>(ba014589cd)));
        F _37bf = bit_cast_lanes<F>(_mm512_unpackhi_pd(bit_cast_lanes<__m512d>(rg2367abef), bit_cast_lanes<__m512d>(ba2367abef)));

        F _ae26 = bit_cast_lanes<F>(_mm512_permutexvar_pd(_mm512_setr_epi64(4,5,6,7,0,1,2,3), bit_cast_lanes<__m512d>(_26ae)));
        F _bf37 = bit_cast_lanes<F>(_mm512_permutexvar_pd(_mm512_setr_epi64(4,5,6,7,0,1,2,3), bit_cast_lanes<__m512d>(_37bf)));
        F _8c04 = bit_cast_lanes<F>(_mm512_permutexvar_pd(_mm512_setr_epi64(4,5,6,7,0,1,2,3), bit_cast_lanes<__m512d>(_048c)));
        F _9d15 = bit_cast_lanes<F>(_mm512_permutexvar_pd(_mm512_setr_epi64(4,5,6,7,0,1,2,3), bit_cast_lanes<__m512d>(_159d)));

        __m512i index = _mm512_setr_epi32(4,5,6,7,0,1,2,3,12,13,14,15,8,9,10,11);
        F _0426 = bit_cast_lanes<F>(_mm512_permutex2var_pd(bit_cast_lanes<__m512d>(_048c), _mm512_setr_epi64(0,1,2,3,12,13,14,15),
                    bit_cast_lanes<__m512d>(_ae26)));
        F _1537 = bit_cast_lanes<F>(_mm512_permutex2var_pd(bit_cast_lanes<__m512d>(_159d), _mm512_setr_epi64(0,1,2,3,12,13,14,15),
                    bit_cast_lanes<__m512d>(_bf37)));
        F _5173 = _mm512_permutexvar_ps(index, _1537);
        F _0123 = bit_cast_lanes<F>(_mm512_permutex2var_pd(bit_cast_lanes<__m512d>(_0426), _mm512_setr_epi64(0,1,10,11,4,5,14,15),
                    bit_cast_lanes<__m512d>(_5173)));

        F _5476 = bit_cast_lanes<F>(_mm512_permutex2var_pd(bit_cast_lanes<__m512d>(_5173), _mm512_setr_epi64(0,1,10,11,4,5,14,15),
                    bit_cast_lanes<__m512d>(_0426)));
        F _4567 = _mm512_permutexvar_ps(index, _5476);
        F _8cae = bit_cast_lanes<F>(_mm512_permutex2var_pd(bit_cast_lanes<__m512d>(_8c04), _mm512_setr_epi64(0,1,2,3,12,13,14,15),
                    bit_cast_lanes<__m512d>(_26ae)));
        F _9dbf = bit_cast_lanes<F>(_mm512_permutex2var_pd(bit_cast_lanes<__m512d>(_9d15), _mm512_setr_epi64(0,1,2,3,12,13,14,15),
                    bit_cast_lanes<__m512d>(_37bf)));
        F _d9fb = _mm512_permutexvar_ps(index, _9dbf);
        F _89ab = bit_cast_lanes<F>(_mm512_permutex2var_pd(bit_cast_lanes<__m512d>(_8cae), _mm512_setr_epi64(0,1,10,11,4,5,14,15),
                    bit_cast_lanes<__m512d>(_d9fb)));
        F _dcfe = bit_cast_lanes<F>(_mm512_permutex2var_pd(bit_cast_lanes<__m512d>(_d9fb), _mm512_setr_epi64(0,1,10,11,4,5,14,15),
                    bit_cast_lanes<__m512d>(_8cae)));
        F _cdef = _mm512_permutexvar_ps(index, _dcfe);

        _mm512_storeu_ps(ptr+0, _0123);
        _mm512_storeu_ps(ptr+16, _4567);
        _mm512_storeu_ps(ptr+32, _89ab);
        _mm512_storeu_ps(ptr+48, _cdef);
    }

#elif defined(SKRP_CPU_AVX2)
    // These are __m256 and __m256i, but friendlier and strongly-typed.
    template <typename T> using V = Vec<8, T>;
    using F   = V<float   >;
    using I32 = V< int32_t>;
    using U64 = V<uint64_t>;
    using U32 = V<uint32_t>;
    using U16 = V<uint16_t>;
    using U8  = V<uint8_t >;

    SI F   mad(F f, F m, F a) { return _mm256_fmadd_ps(f, m, a); }
    SI F  nmad(F f, F m, F a) { return _mm256_fnmadd_ps(f, m, a); }

    SI F   min(F a, F b)     { return _mm256_min_ps(a,b);    }
    SI I32 min(I32 a, I32 b) { return bit_cast_lanes<I32>(_mm256_min_epi32(bit_cast_lanes<__m256i>(a),bit_cast_lanes<__m256i>(b))); }
    SI U32 min(U32 a, U32 b) { return bit_cast_lanes<U32>(_mm256_min_epu32(bit_cast_lanes<__m256i>(a),bit_cast_lanes<__m256i>(b))); }
    SI F   max(F a, F b)     { return _mm256_max_ps(a,b);    }
    SI I32 max(I32 a, I32 b) { return bit_cast_lanes<I32>(_mm256_max_epi32(bit_cast_lanes<__m256i>(a),bit_cast_lanes<__m256i>(b))); }
    SI U32 max(U32 a, U32 b) { return bit_cast_lanes<U32>(_mm256_max_epu32(bit_cast_lanes<__m256i>(a),bit_cast_lanes<__m256i>(b))); }

    SI F   abs_  (F v)       { return _mm256_and_ps(v, 0-v); }
    SI I32 abs_  (I32 v)     { return bit_cast_lanes<I32>(_mm256_abs_epi32(bit_cast_lanes<__m256i>(v))); }
    SI F   floor_(F v)       { return _mm256_floor_ps(v);    }
    SI F   ceil_(F v)        { return _mm256_ceil_ps(v);     }
    SI F   rcp_approx(F v)   { return _mm256_rcp_ps  (v);    }  // use rcp_fast instead
    SI F   rsqrt_approx(F v) { return _mm256_rsqrt_ps(v);    }
    SI F   sqrt_ (F v)       { return _mm256_sqrt_ps (v);    }
    SI F   rcp_precise (F v) {
        F e = rcp_approx(v);
        return _mm256_fnmadd_ps(v, e, _mm256_set1_ps(2.0f)) * e;
    }

    SI I32 iround(F v)         { return bit_cast_lanes<I32>(_mm256_cvtps_epi32(v)); }
    SI U32 round(F v)          { return bit_cast_lanes<U32>(_mm256_cvtps_epi32(v)); }
    SI U16 pack(U32 v) {
        return bit_cast_lanes<U16>(_mm_packus_epi32(_mm256_extractf128_si256(bit_cast_lanes<__m256i>(v), 0),
                                     _mm256_extractf128_si256(bit_cast_lanes<__m256i>(v), 1)));
    }
    SI U8 pack(U16 v) {
        __m128i r = _mm_packus_epi16(bit_cast_lanes<__m128i>(v),bit_cast_lanes<__m128i>(v));
        return sk_unaligned_load<U8>(&r);
    }

    SI F   if_then_else(I32 c, F   t, F   e) { return _mm256_blendv_ps(e, t, bit_cast_lanes<__m256>(c)); }
    SI I32 if_then_else(I32 c, I32 t, I32 e) {
        return bit_cast_lanes<I32>(_mm256_blendv_ps(bit_cast_lanes<__m256>(e), bit_cast_lanes<__m256>(t), bit_cast_lanes<__m256>(c)));
    }

    // NOTE: This version of 'all' only works with mask values (true == all bits set)
    SI bool any(I32 c) { return !_mm256_testz_si256(bit_cast_lanes<__m256i>(c), _mm256_set1_epi32(-1)); }
    SI bool all(I32 c) { return  _mm256_testc_si256(bit_cast_lanes<__m256i>(c), _mm256_set1_epi32(-1)); }

    template <typename T>
    SI V<T> gather(const T* p, U32 ix) {
        return V<T>{ p[ix[0]], p[ix[1]], p[ix[2]], p[ix[3]],
                     p[ix[4]], p[ix[5]], p[ix[6]], p[ix[7]], };
    }
    SI F   gather(const float*    p, U32 ix) { return _mm256_i32gather_ps(p, bit_cast_lanes<__m256i>(ix), 4); }
    SI U32 gather(const uint32_t* p, U32 ix) {
        return bit_cast_lanes<U32>(_mm256_i32gather_epi32(reinterpret_cast<const int*>(p), bit_cast_lanes<__m256i>(ix), 4));
    }
    SI U64 gather(const uint64_t* p, U32 ix) {
        __m256i parts[] = {
                _mm256_i32gather_epi64(
                        reinterpret_cast<const long long int*>(p), _mm256_extracti128_si256(bit_cast_lanes<__m256i>(ix), 0), 8),
                _mm256_i32gather_epi64(
                        reinterpret_cast<const long long int*>(p), _mm256_extracti128_si256(bit_cast_lanes<__m256i>(ix), 1), 8),
        };
        return sk_bit_cast<U64>(parts);
    }
    template <typename T>
    SI V<T> gather_unaligned(const T* p, U32 ix) {
        return gather(p, ix);
    }

    SI void scatter_masked(I32 src, int* dst, U32 ix, I32 mask) {
        I32 before = gather(dst, ix);
        I32 after = if_then_else(mask, src, before);
        dst[ix[0]] = after[0];
        dst[ix[1]] = after[1];
        dst[ix[2]] = after[2];
        dst[ix[3]] = after[3];
        dst[ix[4]] = after[4];
        dst[ix[5]] = after[5];
        dst[ix[6]] = after[6];
        dst[ix[7]] = after[7];
    }

    // The compiler often scalarizes integer division into scalar idiv loops because
    // AVX512 and below don't have vectorized integer division. We can force vectorization by
    // performing the division in double precision. A 64-bit double has 53 bits of mantissa,
    // which is enough to represent any 32-bit integer exactly.
    SI void div_fn(I32* dst, I32* src) {
        // Load using 128-bit registers to avoid 256-bit extraction overhead
        __m128i d0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst) + 0);
        __m128i d1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst) + 1);
        __m128i s0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src) + 0);
        __m128i s1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src) + 1);

        // Convert lower 4 lanes to double, divide, and convert back to integer.
        // Double-precision division natively handles dividing by zero (producing +/-Inf/NaN)
        // and INT_MIN / -1 overflow (producing +2147483648.0) without any hardware crash.
        // Upon truncation back to I32, both saturate to INT_MIN (0x80000000).
        __m256d fd0 = _mm256_cvtepi32_pd(d0);
        __m256d fs0 = _mm256_cvtepi32_pd(s0);
        __m256d fr0 = _mm256_div_pd(fd0, fs0);
        __m128i r0  = _mm256_cvttpd_epi32(fr0);  // (truncates toward zero)
        // Same for upper 4 lanes.
        __m256d fd1 = _mm256_cvtepi32_pd(d1);
        __m256d fs1 = _mm256_cvtepi32_pd(s1);
        __m256d fr1 = _mm256_div_pd(fd1, fs1);
        __m128i r1  = _mm256_cvttpd_epi32(fr1);
        // Recombine lanes
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst) + 0, r0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst) + 1, r1);
    }
    #define SKRP_SUPPORTS_OPTIMIZED_DIV_INT

    SI void div_fn(U32* dst, U32* src) {
        // Load using 128-bit registers to avoid 256-bit extraction overhead
        __m128i d0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst) + 0);
        __m128i d1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst) + 1);
        __m128i s0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src) + 0);
        __m128i s1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src) + 1);

        // AVX2 (and below) lack a way to turn unsigned integers to doubles directly so we
        // clamp to INT_MAX before converting to doubles.
        __m128i max_safe = _mm_set1_epi32(0x7FFFFFFF);
        __m128i d0_safe  = _mm_min_epu32(d0, max_safe);
        __m128i d1_safe  = _mm_min_epu32(d1, max_safe);
        __m128i s0_safe  = _mm_min_epu32(s0, max_safe);
        __m128i s1_safe  = _mm_min_epu32(s1, max_safe);

        // Convert lower 4 lanes to double, divide, and convert back to U32.
        // Double-precision division natively handles dividing by zero (producing +Inf/NaN).
        // Upon truncation back to U32, both saturate to INT_MIN (0x80000000).
        __m256d fd0  = _mm256_cvtepi32_pd(d0_safe);
        __m256d fs0  = _mm256_cvtepi32_pd(s0_safe);
        __m256d fr0  = _mm256_div_pd(fd0, fs0);
        __m128i r0   = _mm256_cvttpd_epi32(fr0);  // (truncates toward zero)
        // Same for upper 4 lanes.
        __m256d fd1  = _mm256_cvtepi32_pd(d1_safe);
        __m256d fs1  = _mm256_cvtepi32_pd(s1_safe);
        __m256d fr1  = _mm256_div_pd(fd1, fs1);
        __m128i r1   = _mm256_cvttpd_epi32(fr1);
        // Recombine lanes
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst) + 0, r0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst) + 1, r1);
    }
    #define SKRP_SUPPORTS_OPTIMIZED_DIV_UINT

    SI void load2(const uint16_t* ptr, U16* r, U16* g) {
        __m128i _0123 = _mm_loadu_si128((reinterpret_cast<const __m128i*>(ptr)) + 0);
        __m128i _4567 = _mm_loadu_si128((reinterpret_cast<const __m128i*>(ptr)) + 1);
        *r = bit_cast_lanes<U16>(_mm_packs_epi32(_mm_srai_epi32(_mm_slli_epi32(_0123, 16), 16),
                                  _mm_srai_epi32(_mm_slli_epi32(_4567, 16), 16)));
        *g = bit_cast_lanes<U16>(_mm_packs_epi32(_mm_srai_epi32(_0123, 16),
                                  _mm_srai_epi32(_4567, 16)));
    }
    SI void store2(uint16_t* ptr, U16 r, U16 g) {
        __m128i _0123 = _mm_unpacklo_epi16(bit_cast_lanes<__m128i>(r), bit_cast_lanes<__m128i>(g));
        __m128i _4567 = _mm_unpackhi_epi16(bit_cast_lanes<__m128i>(r), bit_cast_lanes<__m128i>(g));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr) + 0, _0123);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr) + 1, _4567);
    }

    SI void load4(const uint16_t* ptr, U16* r, U16* g, U16* b, U16* a) {
        __m128i _01 = _mm_loadu_si128((reinterpret_cast<const __m128i*>(ptr)) + 0);
        __m128i _23 = _mm_loadu_si128((reinterpret_cast<const __m128i*>(ptr)) + 1);
        __m128i _45 = _mm_loadu_si128((reinterpret_cast<const __m128i*>(ptr)) + 2);
        __m128i _67 = _mm_loadu_si128((reinterpret_cast<const __m128i*>(ptr)) + 3);

        __m128i _02 = _mm_unpacklo_epi16(_01, _23);  // r0 r2 g0 g2 b0 b2 a0 a2
        __m128i _13 = _mm_unpackhi_epi16(_01, _23);  // r1 r3 g1 g3 b1 b3 a1 a3
        __m128i _46 = _mm_unpacklo_epi16(_45, _67);
        __m128i _57 = _mm_unpackhi_epi16(_45, _67);

        __m128i rg0123 = _mm_unpacklo_epi16(_02, _13);  // r0 r1 r2 r3 g0 g1 g2 g3
        __m128i ba0123 = _mm_unpackhi_epi16(_02, _13);  // b0 b1 b2 b3 a0 a1 a2 a3
        __m128i rg4567 = _mm_unpacklo_epi16(_46, _57);
        __m128i ba4567 = _mm_unpackhi_epi16(_46, _57);

        *r = bit_cast_lanes<U16>(_mm_unpacklo_epi64(rg0123, rg4567));
        *g = bit_cast_lanes<U16>(_mm_unpackhi_epi64(rg0123, rg4567));
        *b = bit_cast_lanes<U16>(_mm_unpacklo_epi64(ba0123, ba4567));
        *a = bit_cast_lanes<U16>(_mm_unpackhi_epi64(ba0123, ba4567));
    }
    SI void store4(uint16_t* ptr, U16 r, U16 g, U16 b, U16 a) {
        __m128i rg0123 = _mm_unpacklo_epi16(bit_cast_lanes<__m128i>(r), bit_cast_lanes<__m128i>(g));  // r0 g0 r1 g1 r2 g2 r3 g3
        __m128i rg4567 = _mm_unpackhi_epi16(bit_cast_lanes<__m128i>(r), bit_cast_lanes<__m128i>(g));  // r4 g4 r5 g5 r6 g6 r7 g7
        __m128i ba0123 = _mm_unpacklo_epi16(bit_cast_lanes<__m128i>(b), bit_cast_lanes<__m128i>(a));
        __m128i ba4567 = _mm_unpackhi_epi16(bit_cast_lanes<__m128i>(b), bit_cast_lanes<__m128i>(a));

        __m128i _01 = _mm_unpacklo_epi32(rg0123, ba0123);
        __m128i _23 = _mm_unpackhi_epi32(rg0123, ba0123);
        __m128i _45 = _mm_unpacklo_epi32(rg4567, ba4567);
        __m128i _67 = _mm_unpackhi_epi32(rg4567, ba4567);

        _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr) + 0, _01);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr) + 1, _23);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr) + 2, _45);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr) + 3, _67);
    }

    SI void load4(const float* ptr, F* r, F* g, F* b, F* a) {
        F _04 = _mm256_castps128_ps256(_mm_loadu_ps(ptr+ 0));
        F _15 = _mm256_castps128_ps256(_mm_loadu_ps(ptr+ 4));
        F _26 = _mm256_castps128_ps256(_mm_loadu_ps(ptr+ 8));
        F _37 = _mm256_castps128_ps256(_mm_loadu_ps(ptr+12));
        _04 = _mm256_insertf128_ps(_04, _mm_loadu_ps(ptr+16), 1);
        _15 = _mm256_insertf128_ps(_15, _mm_loadu_ps(ptr+20), 1);
        _26 = _mm256_insertf128_ps(_26, _mm_loadu_ps(ptr+24), 1);
        _37 = _mm256_insertf128_ps(_37, _mm_loadu_ps(ptr+28), 1);

        F rg0145 = _mm256_unpacklo_ps(_04,_15);  // r0 r1 g0 g1 | r4 r5 g4 g5
        F ba0145 = _mm256_unpackhi_ps(_04,_15);
        F rg2367 = _mm256_unpacklo_ps(_26,_37);
        F ba2367 = _mm256_unpackhi_ps(_26,_37);

        *r = bit_cast_lanes<F>(_mm256_unpacklo_pd(bit_cast_lanes<__m256d>(rg0145), bit_cast_lanes<__m256d>(rg2367)));
        *g = bit_cast_lanes<F>(_mm256_unpackhi_pd(bit_cast_lanes<__m256d>(rg0145), bit_cast_lanes<__m256d>(rg2367)));
        *b = bit_cast_lanes<F>(_mm256_unpacklo_pd(bit_cast_lanes<__m256d>(ba0145), bit_cast_lanes<__m256d>(ba2367)));
        *a = bit_cast_lanes<F>(_mm256_unpackhi_pd(bit_cast_lanes<__m256d>(ba0145), bit_cast_lanes<__m256d>(ba2367)));
    }
    SI void store4(float* ptr, F r, F g, F b, F a) {
        F rg0145 = _mm256_unpacklo_ps(r, g);  // r0 g0 r1 g1 | r4 g4 r5 g5
        F rg2367 = _mm256_unpackhi_ps(r, g);  // r2 ...      | r6 ...
        F ba0145 = _mm256_unpacklo_ps(b, a);  // b0 a0 b1 a1 | b4 a4 b5 a5
        F ba2367 = _mm256_unpackhi_ps(b, a);  // b2 ...      | b6 ...

        F _04 = bit_cast_lanes<F>(_mm256_unpacklo_pd(bit_cast_lanes<__m256d>(rg0145), bit_cast_lanes<__m256d>(ba0145)));  // r0 g0 b0 a0 | r4 g4 b4 a4
        F _15 = bit_cast_lanes<F>(_mm256_unpackhi_pd(bit_cast_lanes<__m256d>(rg0145), bit_cast_lanes<__m256d>(ba0145)));  // r1 ...      | r5 ...
        F _26 = bit_cast_lanes<F>(_mm256_unpacklo_pd(bit_cast_lanes<__m256d>(rg2367), bit_cast_lanes<__m256d>(ba2367)));  // r2 ...      | r6 ...
        F _37 = bit_cast_lanes<F>(_mm256_unpackhi_pd(bit_cast_lanes<__m256d>(rg2367), bit_cast_lanes<__m256d>(ba2367)));// r3 ...      | r7 ...

        F _01 = _mm256_permute2f128_ps(_04, _15, 32),  // 32 == 0010 0000 == lo, lo
          _23 = _mm256_permute2f128_ps(_26, _37, 32),
          _45 = _mm256_permute2f128_ps(_04, _15, 49),  // 49 == 0011 0001 == hi, hi
          _67 = _mm256_permute2f128_ps(_26, _37, 49);
        _mm256_storeu_ps(ptr+ 0, _01);
        _mm256_storeu_ps(ptr+ 8, _23);
        _mm256_storeu_ps(ptr+16, _45);
        _mm256_storeu_ps(ptr+24, _67);
    }

#elif defined(SKRP_CPU_SSE2) || defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
    template <typename T> using V = Vec<4, T>;
    using F   = V<float   >;
    using I32 = V< int32_t>;
    using U64 = V<uint64_t>;
    using U32 = V<uint32_t>;
    using U16 = V<uint16_t>;
    using U8  = V<uint8_t >;

    SI F if_then_else(I32 c, F t, F e) {
        return _mm_or_ps(_mm_and_ps(bit_cast_lanes<__m128>(c), t), _mm_andnot_ps(bit_cast_lanes<__m128>(c), e));
    }
    SI I32 if_then_else(I32 c, I32 t, I32 e) {
        return bit_cast_lanes<I32>(_mm_or_ps(_mm_and_ps(bit_cast_lanes<__m128>(c), bit_cast_lanes<__m128>(t)),
                              _mm_andnot_ps(bit_cast_lanes<__m128>(c), bit_cast_lanes<__m128>(e))));
    }

    SI F   min(F a, F b)     { return _mm_min_ps(a,b); }
    SI F   max(F a, F b)     { return _mm_max_ps(a,b); }
#if defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
    SI I32 min(I32 a, I32 b) { return bit_cast_lanes<I32>(_mm_min_epi32(bit_cast_lanes<__m128i>(a),bit_cast_lanes<__m128i>(b))); }
    SI U32 min(U32 a, U32 b) { return bit_cast_lanes<U32>(_mm_min_epu32(bit_cast_lanes<__m128i>(a),bit_cast_lanes<__m128i>(b))); }
    SI I32 max(I32 a, I32 b) { return bit_cast_lanes<I32>(_mm_max_epi32(bit_cast_lanes<__m128i>(a),bit_cast_lanes<__m128i>(b))); }
    SI U32 max(U32 a, U32 b) { return bit_cast_lanes<U32>(_mm_max_epu32(bit_cast_lanes<__m128i>(a),bit_cast_lanes<__m128i>(b))); }
#else
    SI I32 min(I32 a, I32 b) { return if_then_else(a < b, a, b); }
    SI I32 max(I32 a, I32 b) { return if_then_else(a > b, a, b); }
    SI U32 min(U32 a, U32 b) {
        return sk_bit_cast<U32>(if_then_else(a < b, sk_bit_cast<I32>(a), sk_bit_cast<I32>(b)));
    }
    SI U32 max(U32 a, U32 b) {
        return sk_bit_cast<U32>(if_then_else(a > b, sk_bit_cast<I32>(a), sk_bit_cast<I32>(b)));
    }
#endif

    SI F   mad(F f, F m, F a)  { return a+f*m;              }
    SI F  nmad(F f, F m, F a)  { return a-f*m;              }
    SI F   abs_(F v)           { return _mm_and_ps(v, 0-v); }
#if defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
    SI I32 abs_(I32 v)         { return bit_cast_lanes<I32>(_mm_abs_epi32(bit_cast_lanes<__m128i>(v))); }
#else
    SI I32 abs_(I32 v)         { return max(v, -v); }
#endif
    SI F   rcp_approx(F v)     { return _mm_rcp_ps  (v);    }  // use rcp_fast instead
    SI F   rcp_precise (F v)   { F e = rcp_approx(v); return e * (2.0f - v * e); }
    SI F   rsqrt_approx(F v)   { return _mm_rsqrt_ps(v);    }
    SI F    sqrt_(F v)         { return _mm_sqrt_ps (v);    }

    // The compiler often scalarizes integer division into scalar idiv loops because
    // AVX512 and below don't have vectorized integer division. We can force vectorization by
    // performing the division in double precision. A 64-bit double has 53 bits of mantissa,
    // which is enough to represent any 32-bit integer exactly.
    SI void div_fn(I32* dst, I32* src) {
        __m128i d = bit_cast_lanes<__m128i>(*dst);
        __m128i s = bit_cast_lanes<__m128i>(*src);
        // Convert top 2 lanes to double, divide, and convert back to integer.
        // Double-precision division natively handles dividing by zero (producing +/-Inf/NaN)
        // and INT_MIN / -1 overflow (producing +2147483648.0) without any hardware crash.
        // Upon truncation back to I32, both saturate to INT_MIN (0x80000000).
        __m128d fd_lo = _mm_cvtepi32_pd(d);
        __m128d fs_lo = _mm_cvtepi32_pd(s);
        __m128d fr_lo = _mm_div_pd(fd_lo, fs_lo);
        __m128i r_lo  = _mm_cvttpd_epi32(fr_lo);
        // Same for upper 2 lanes.
        __m128d fd_hi = _mm_cvtepi32_pd(_mm_srli_si128(d, 8));
        __m128d fs_hi = _mm_cvtepi32_pd(_mm_srli_si128(s, 8));
        __m128d fr_hi = _mm_div_pd(fd_hi, fs_hi);
        __m128i r_hi  = _mm_cvttpd_epi32(fr_hi);
        // Recombine lanes
        *dst = bit_cast_lanes<I32>(_mm_unpacklo_epi64(r_lo, r_hi)); // Combine halves
    }
    #define SKRP_SUPPORTS_OPTIMIZED_DIV_INT

    SI void div_fn(U32* dst, U32* src) {
        __m128i d_raw = bit_cast_lanes<__m128i>(*dst);
        __m128i s_raw = bit_cast_lanes<__m128i>(*src);

        // AVX2 (and below) lack a way to turn unsigned integers to doubles directly so we
        // clamp to INT_MAX before converting to doubles.
#if defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
        __m128i max_safe = _mm_set1_epi32(0x7FFFFFFF);
        __m128i d = _mm_min_epu32(d_raw, max_safe);
        __m128i s = _mm_min_epu32(s_raw, max_safe);
#else   // SSE2/SSSE3 clamp fallback
        __m128i max_safe = _mm_set1_epi32(0x7FFFFFFF);
        __m128i d_mask   = _mm_srai_epi32(d_raw, 31);
        __m128i d        = _mm_or_si128(_mm_andnot_si128(d_mask, d_raw),
                                        _mm_and_si128(d_mask, max_safe));
        __m128i s_mask   = _mm_srai_epi32(s_raw, 31);
        __m128i s        = _mm_or_si128(_mm_andnot_si128(s_mask, s_raw),
                                        _mm_and_si128(s_mask, max_safe));
#endif
        // Convert lower 2 lanes to double, divide, and convert back to U32.
        // Double-precision division natively handles dividing by zero (producing +Inf/NaN).
        // Upon truncation back to U32, both saturate to INT_MIN (0x80000000).
        __m128d fd_lo = _mm_cvtepi32_pd(d);
        __m128d fs_lo = _mm_cvtepi32_pd(s);
        __m128d fr_lo = _mm_div_pd(fd_lo, fs_lo);
        __m128i r_lo  = _mm_cvttpd_epi32(fr_lo);
        // Same for upper 2 lanes.
        __m128d fd_hi = _mm_cvtepi32_pd(_mm_srli_si128(d, 8));
        __m128d fs_hi = _mm_cvtepi32_pd(_mm_srli_si128(s, 8));
        __m128d fr_hi = _mm_div_pd(fd_hi, fs_hi);
        __m128i r_hi  = _mm_cvttpd_epi32(fr_hi);
        // Recombine lanes
        *dst = bit_cast_lanes<U32>(_mm_unpacklo_epi64(r_lo, r_hi));
    }
    #define SKRP_SUPPORTS_OPTIMIZED_DIV_UINT

    SI I32 iround(F v)         { return bit_cast_lanes<I32>(_mm_cvtps_epi32(v)); }
    SI U32 round(F v)          { return bit_cast_lanes<U32>(_mm_cvtps_epi32(v)); }

    SI U16 pack(U32 v) {
    #if defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
        __m128i p = _mm_packus_epi32(bit_cast_lanes<__m128i>(v),bit_cast_lanes<__m128i>(v));
    #else
        // Sign extend so that _mm_packs_epi32() does the pack we want.
        __m128i p = _mm_srai_epi32(_mm_slli_epi32(bit_cast_lanes<__m128i>(v), 16), 16);
        p = _mm_packs_epi32(p,p);
    #endif
        return sk_unaligned_load<U16>(&p);  // We have two copies.  Return (the lower) one.
    }
    SI U8 pack(U16 v) {
        __m128i r = widen_cast<__m128i>(v);
        r = _mm_packus_epi16(r,r);
        return sk_unaligned_load<U8>(&r);
    }

    // NOTE: This only checks the top bit of each lane, and is incorrect with non-mask values.
    SI bool any(I32 c) { return _mm_movemask_ps(sk_bit_cast<F>(c)) != 0b0000; }
    SI bool all(I32 c) { return _mm_movemask_ps(sk_bit_cast<F>(c)) == 0b1111; }

    SI F floor_(F v) {
    #if defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
        return _mm_floor_ps(v);
    #else
        F roundtrip = _mm_cvtepi32_ps(_mm_cvttps_epi32(v));
        return roundtrip - if_then_else(roundtrip > v, F() + 1, F() + 0);
    #endif
    }

    SI F ceil_(F v) {
    #if defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
        return _mm_ceil_ps(v);
    #else
        F roundtrip = _mm_cvtepi32_ps(_mm_cvttps_epi32(v));
        return roundtrip + if_then_else(roundtrip < v, F() + 1, F() + 0);
    #endif
    }

    template <typename T>
    SI V<T> gather(const T* p, U32 ix) {
        return V<T>{p[ix[0]], p[ix[1]], p[ix[2]], p[ix[3]]};
    }
    template <typename T>
    SI V<T> gather_unaligned(const T* p, U32 ix) {
        return gather(p, ix);
    }
    SI void scatter_masked(I32 src, int* dst, U32 ix, I32 mask) {
        I32 before = gather(dst, ix);
        I32 after = if_then_else(mask, src, before);
        dst[ix[0]] = after[0];
        dst[ix[1]] = after[1];
        dst[ix[2]] = after[2];
        dst[ix[3]] = after[3];
    }
    SI void load2(const uint16_t* ptr, U16* r, U16* g) {
        __m128i _01 = _mm_loadu_si128((reinterpret_cast<const __m128i*>(ptr)) + 0); // r0 g0 r1 g1 r2 g2 r3 g3
        __m128i rg01_23 = _mm_shufflelo_epi16(_01, 0xD8);            // r0 r1 g0 g1 r2 g2 r3 g3
        __m128i rg      = _mm_shufflehi_epi16(rg01_23, 0xD8);        // r0 r1 g0 g1 r2 r3 g2 g3

        __m128i R = _mm_shuffle_epi32(rg, 0x88);  // r0 r1 r2 r3 r0 r1 r2 r3
        __m128i G = _mm_shuffle_epi32(rg, 0xDD);  // g0 g1 g2 g3 g0 g1 g2 g3
        *r = sk_unaligned_load<U16>(&R);
        *g = sk_unaligned_load<U16>(&G);
    }
    SI void store2(uint16_t* ptr, U16 r, U16 g) {
        __m128i rg = _mm_unpacklo_epi16(widen_cast<__m128i>(r), widen_cast<__m128i>(g));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr) + 0, rg);
    }

    SI void load4(const uint16_t* ptr, U16* r, U16* g, U16* b, U16* a) {
        __m128i _01 = _mm_loadu_si128((reinterpret_cast<const __m128i*>(ptr)) + 0);  // r0 g0 b0 a0 r1 g1 b1 a1
        __m128i _23 = _mm_loadu_si128((reinterpret_cast<const __m128i*>(ptr)) + 1); // r2 g2 b2 a2 r3 g3 b3 a3

        __m128i _02 = _mm_unpacklo_epi16(_01, _23);  // r0 r2 g0 g2 b0 b2 a0 a2
        __m128i _13 = _mm_unpackhi_epi16(_01, _23);  // r1 r3 g1 g3 b1 b3 a1 a3

        __m128i rg = _mm_unpacklo_epi16(_02, _13);  // r0 r1 r2 r3 g0 g1 g2 g3
        __m128i ba = _mm_unpackhi_epi16(_02, _13);  // b0 b1 b2 b3 a0 a1 a2 a3

        *r = sk_unaligned_load<U16>(reinterpret_cast<uint16_t*>(&rg) + 0);
        *g = sk_unaligned_load<U16>(reinterpret_cast<uint16_t*>(&rg) + 4);
        *b = sk_unaligned_load<U16>(reinterpret_cast<uint16_t*>(&ba) + 0);
        *a = sk_unaligned_load<U16>(reinterpret_cast<uint16_t*>(&ba) + 4);
    }

    SI void store4(uint16_t* ptr, U16 r, U16 g, U16 b, U16 a) {
        __m128i rg = _mm_unpacklo_epi16(widen_cast<__m128i>(r), widen_cast<__m128i>(g));
        __m128i ba = _mm_unpacklo_epi16(widen_cast<__m128i>(b), widen_cast<__m128i>(a));

        _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr) + 0, _mm_unpacklo_epi32(rg, ba));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ptr) + 1, _mm_unpackhi_epi32(rg, ba));
    }

    SI void load4(const float* ptr, F* r, F* g, F* b, F* a) {
        F _0 = _mm_loadu_ps(ptr + 0);
        F _1 = _mm_loadu_ps(ptr + 4);
        F _2 = _mm_loadu_ps(ptr + 8);
        F _3 = _mm_loadu_ps(ptr +12);
        _MM_TRANSPOSE4_PS(_0,_1,_2,_3);
        *r = _0;
        *g = _1;
        *b = _2;
        *a = _3;
    }

    SI void store4(float* ptr, F r, F g, F b, F a) {
        _MM_TRANSPOSE4_PS(r,g,b,a);
        _mm_storeu_ps(ptr + 0, r);
        _mm_storeu_ps(ptr + 4, g);
        _mm_storeu_ps(ptr + 8, b);
        _mm_storeu_ps(ptr +12, a);
    }

#elif defined(SKRP_CPU_LASX)
    // These are __m256 and __m256i, but friendlier and strongly-typed.
    template <typename T> using V = Vec<8, T>;
    using F   = V<float   >;
    using I32 = V<int32_t>;
    using U64 = V<uint64_t>;
    using U32 = V<uint32_t>;
    using U16 = V<uint16_t>;
    using U8  = V<uint8_t >;

    SI __m128i emulate_lasx_d_xr2vr_l(__m256i a) {
        v4i64 tmp = a;
        v2i64 al = {tmp[0], tmp[1]};
        return (__m128i)al;
    }

    SI __m128i emulate_lasx_d_xr2vr_h(__m256i a) {
        v4i64 tmp = a;
        v2i64 ah = {tmp[2], tmp[3]};
        return (__m128i)ah;
    }

    SI F if_then_else(I32 c, F t, F e) {
        return sk_bit_cast<Vec<8,float>>(__lasx_xvbitsel_v(sk_bit_cast<__m256i>(e),
                                                           sk_bit_cast<__m256i>(t),
                                                           sk_bit_cast<__m256i>(c)));
    }

    SI I32 if_then_else(I32 c, I32 t, I32 e) {
        return sk_bit_cast<Vec<8,int32_t>>(__lasx_xvbitsel_v(sk_bit_cast<__m256i>(e),
                                                             sk_bit_cast<__m256i>(t),
                                                             sk_bit_cast<__m256i>(c)));
    }

    SI F   min(F a, F b)        { return __lasx_xvfmin_s(a,b); }
    SI F   max(F a, F b)        { return __lasx_xvfmax_s(a,b); }
    SI I32 min(I32 a, I32 b)    { return __lasx_xvmin_w(a,b);  }
    SI U32 min(U32 a, U32 b)    { return __lasx_xvmin_wu(a,b); }
    SI I32 max(I32 a, I32 b)    { return __lasx_xvmax_w(a,b);  }
    SI U32 max(U32 a, U32 b)    { return __lasx_xvmax_wu(a,b); }

    SI F   mad(F f, F m, F a)   { return __lasx_xvfmadd_s(f, m, a);      }
    SI F   nmad(F f, F m, F a)  { return __lasx_xvfmadd_s(-f, m, a);    }
    SI F   abs_  (F v)          { return (F)__lasx_xvand_v((I32)v, (I32)(0-v));     }
    SI I32 abs_(I32 v)          { return max(v, -v);                     }
    SI F   rcp_approx(F v)      { return __lasx_xvfrecip_s(v);           }
    SI F   rcp_precise (F v)    { F e = rcp_approx(v); return e * nmad(v, e, F() + 2.0f); }
    SI F   rsqrt_approx (F v)   { return __lasx_xvfrsqrt_s(v);           }
    SI F    sqrt_(F v)          { return __lasx_xvfsqrt_s(v);            }

    SI U32 iround(F v) {
        F t = F() + 0.5f;
        return __lasx_xvftintrz_w_s(v + t);
    }

    SI U32 round(F v) {
        F t = F() + 0.5f;
        return __lasx_xvftintrz_w_s(v + t);
    }

    SI U16 pack(U32 v) {
        return __lsx_vpickev_h(__lsx_vsat_wu(emulate_lasx_d_xr2vr_h(v), 15),
                               __lsx_vsat_wu(emulate_lasx_d_xr2vr_l(v), 15));
    }

    SI U8 pack(U16 v) {
        __m128i tmp = __lsx_vsat_hu(v, 7);
        __m128i r = __lsx_vpickev_b(tmp, tmp);
        return sk_unaligned_load<U8>(&r);
    }

    SI bool any(I32 c){
        v8i32 retv = (v8i32)__lasx_xvmskltz_w(__lasx_xvslt_wu(__lasx_xvldi(0), c));
        return (retv[0] | retv[4]) != 0b0000;
    }

    SI bool all(I32 c){
        v8i32 retv = (v8i32)__lasx_xvmskltz_w(__lasx_xvslt_wu(__lasx_xvldi(0), c));
        return (retv[0] & retv[4]) == 0b1111;
    }

    SI F floor_(F v) {
        return __lasx_xvfrintrm_s(v);
    }

    SI F ceil_(F v) {
        return __lasx_xvfrintrp_s(v);
    }

    template <typename T>
    SI V<T> gather(const T* p, U32 ix) {
        return V<T>{ p[ix[0]], p[ix[1]], p[ix[2]], p[ix[3]],
                     p[ix[4]], p[ix[5]], p[ix[6]], p[ix[7]], };
    }
    template <typename T>
    SI V<T> gather_unaligned(const T* p, U32 ix) {
        return gather(p, ix);
    }

    template <typename V, typename S>
    SI void scatter_masked(V src, S* dst, U32 ix, I32 mask) {
        V before = gather(dst, ix);
        V after = if_then_else(mask, src, before);
        dst[ix[0]] = after[0];
        dst[ix[1]] = after[1];
        dst[ix[2]] = after[2];
        dst[ix[3]] = after[3];
        dst[ix[4]] = after[4];
        dst[ix[5]] = after[5];
        dst[ix[6]] = after[6];
        dst[ix[7]] = after[7];
    }

    SI void load2(const uint16_t* ptr, U16* r, U16* g) {
        U16 _0123 = __lsx_vld(ptr, 0),
            _4567 = __lsx_vld(ptr, 16);
        *r = __lsx_vpickev_h(__lsx_vsat_w(__lsx_vsrai_w(__lsx_vslli_w(_4567, 16), 16), 15),
                             __lsx_vsat_w(__lsx_vsrai_w(__lsx_vslli_w(_0123, 16), 16), 15));
        *g = __lsx_vpickev_h(__lsx_vsat_w(__lsx_vsrai_w(_4567, 16), 15),
                             __lsx_vsat_w(__lsx_vsrai_w(_0123, 16), 15));
    }
    SI void store2(uint16_t* ptr, U16 r, U16 g) {
        __m128i _0123 = __lsx_vilvl_h(g, r),
             _4567 = __lsx_vilvh_h(g, r);
        __lsx_vst(_0123, ptr, 0);
        __lsx_vst(_4567, ptr, 16);
    }

    SI void load4(const uint16_t* ptr, U16* r, U16* g, U16* b, U16* a) {
        __m128i _01 = __lsx_vld(ptr, 0),
                _23 = __lsx_vld(ptr, 16),
                _45 = __lsx_vld(ptr, 32),
                _67 = __lsx_vld(ptr, 48);

        __m128i _02 = __lsx_vilvl_h(_23, _01),     // r0 r2 g0 g2 b0 b2 a0 a2
             _13 = __lsx_vilvh_h(_23, _01),     // r1 r3 g1 g3 b1 b3 a1 a3
             _46 = __lsx_vilvl_h(_67, _45),
             _57 = __lsx_vilvh_h(_67, _45);

        __m128i rg0123 = __lsx_vilvl_h(_13, _02),  // r0 r1 r2 r3 g0 g1 g2 g3
             ba0123 = __lsx_vilvh_h(_13, _02),  // b0 b1 b2 b3 a0 a1 a2 a3
             rg4567 = __lsx_vilvl_h(_57, _46),
             ba4567 = __lsx_vilvh_h(_57, _46);

        *r = __lsx_vilvl_d(rg4567, rg0123);
        *g = __lsx_vilvh_d(rg4567, rg0123);
        *b = __lsx_vilvl_d(ba4567, ba0123);
        *a = __lsx_vilvh_d(ba4567, ba0123);
    }

    SI void store4(uint16_t* ptr, U16 r, U16 g, U16 b, U16 a) {
        __m128i rg0123 = __lsx_vilvl_h(g, r),      // r0 g0 r1 g1 r2 g2 r3 g3
             rg4567 = __lsx_vilvh_h(g, r),      // r4 g4 r5 g5 r6 g6 r7 g7
             ba0123 = __lsx_vilvl_h(a, b),
             ba4567 = __lsx_vilvh_h(a, b);

        __m128i _01 =__lsx_vilvl_w(ba0123, rg0123),
             _23 =__lsx_vilvh_w(ba0123, rg0123),
             _45 =__lsx_vilvl_w(ba4567, rg4567),
             _67 =__lsx_vilvh_w(ba4567, rg4567);

        __lsx_vst(_01, ptr, 0);
        __lsx_vst(_23, ptr, 16);
        __lsx_vst(_45, ptr, 32);
        __lsx_vst(_67, ptr, 48);
    }

    SI void load4(const float* ptr, F* r, F* g, F* b, F* a) {
        F _04 = (F)__lasx_xvpermi_q(__lasx_xvld(ptr, 0), __lasx_xvld(ptr, 64), 0x02);
        F _15 = (F)__lasx_xvpermi_q(__lasx_xvld(ptr, 16), __lasx_xvld(ptr, 80), 0x02);
        F _26 = (F)__lasx_xvpermi_q(__lasx_xvld(ptr, 32), __lasx_xvld(ptr, 96), 0x02);
        F _37 = (F)__lasx_xvpermi_q(__lasx_xvld(ptr, 48), __lasx_xvld(ptr, 112), 0x02);

        F rg0145 = (F)__lasx_xvilvl_w((__m256i)_15, (__m256i)_04),  // r0 r1 g0 g1 | r4 r5 g4 g5
          ba0145 = (F)__lasx_xvilvh_w((__m256i)_15, (__m256i)_04),
          rg2367 = (F)__lasx_xvilvl_w((__m256i)_37, (__m256i)_26),
          ba2367 = (F)__lasx_xvilvh_w((__m256i)_37, (__m256i)_26);

        *r = (F)__lasx_xvilvl_d((__m256i)rg2367, (__m256i)rg0145);
        *g = (F)__lasx_xvilvh_d((__m256i)rg2367, (__m256i)rg0145);
        *b = (F)__lasx_xvilvl_d((__m256i)ba2367, (__m256i)ba0145);
        *a = (F)__lasx_xvilvh_d((__m256i)ba2367, (__m256i)ba0145);
    }
    SI void store4(float* ptr, F r, F g, F b, F a) {
        F rg0145 = (F)__lasx_xvilvl_w((__m256i)g, (__m256i)r),         // r0 g0 r1 g1 | r4 g4 r5 g5
          rg2367 = (F)__lasx_xvilvh_w((__m256i)g, (__m256i)r),         // r2 ...      | r6 ...
          ba0145 = (F)__lasx_xvilvl_w((__m256i)a, (__m256i)b),         // b0 a0 b1 a1 | b4 a4 b5 a5
          ba2367 = (F)__lasx_xvilvh_w((__m256i)a, (__m256i)b);         // b2 ...      | b6 ...

        F _04 = (F)__lasx_xvilvl_d((__m256i)ba0145, (__m256i)rg0145),  // r0 g0 b0 a0 | r4 g4 b4 a4
          _15 = (F)__lasx_xvilvh_d((__m256i)ba0145, (__m256i)rg0145),  // r1 ...      | r5 ...
          _26 = (F)__lasx_xvilvl_d((__m256i)ba2367, (__m256i)rg2367),  // r2 ...      | r6 ...
          _37 = (F)__lasx_xvilvh_d((__m256i)ba2367, (__m256i)rg2367);  // r3 ...      | r7 ...

        F _01 = (F)__lasx_xvpermi_q((__m256i)_04, (__m256i)_15, 0x02),
          _23 = (F)__lasx_xvpermi_q((__m256i)_26, (__m256i)_37, 0x02),
          _45 = (F)__lasx_xvpermi_q((__m256i)_04, (__m256i)_15, 0x13),
          _67 = (F)__lasx_xvpermi_q((__m256i)_26, (__m256i)_37, 0x13);
        __lasx_xvst(_01, ptr, 0);
        __lasx_xvst(_23, ptr, 32);
        __lasx_xvst(_45, ptr, 64);
        __lasx_xvst(_67, ptr, 96);
    }

#elif defined(SKRP_CPU_LSX)
    template <typename T> using V = Vec<4, T>;
    using F   = V<float   >;
    using I32 = V<int32_t >;
    using U64 = V<uint64_t>;
    using U32 = V<uint32_t>;
    using U16 = V<uint16_t>;
    using U8  = V<uint8_t >;

    #define _LSX_TRANSPOSE4_S(row0, row1, row2, row3)                          \
    do {                                                                       \
        __m128 __t0 = (__m128)__lsx_vilvl_w ((__m128i)row1, (__m128i)row0);    \
        __m128 __t1 = (__m128)__lsx_vilvl_w ((__m128i)row3, (__m128i)row2);    \
        __m128 __t2 = (__m128)__lsx_vilvh_w ((__m128i)row1, (__m128i)row0);    \
        __m128 __t3 = (__m128)__lsx_vilvh_w ((__m128i)row3, (__m128i)row2);    \
        (row0) = (__m128)__lsx_vilvl_d ((__m128i)__t1, (__m128i)__t0);         \
        (row1) = (__m128)__lsx_vilvh_d ((__m128i)__t1, (__m128i)__t0);         \
        (row2) = (__m128)__lsx_vilvl_d ((__m128i)__t3, (__m128i)__t2);         \
        (row3) = (__m128)__lsx_vilvh_d ((__m128i)__t3, (__m128i)__t2);         \
    } while (0)

    SI F if_then_else(I32 c, F t, F e) {
        return sk_bit_cast<Vec<4,float>>(__lsx_vbitsel_v(sk_bit_cast<__m128i>(e),
                                                         sk_bit_cast<__m128i>(t),
                                                         sk_bit_cast<__m128i>(c)));
    }

    SI I32 if_then_else(I32 c, I32 t, I32 e) {
        return sk_bit_cast<Vec<4,int32_t>>(__lsx_vbitsel_v(sk_bit_cast<__m128i>(e),
                                                           sk_bit_cast<__m128i>(t),
                                                           sk_bit_cast<__m128i>(c)));
    }

    SI F   min(F a, F b)        { return __lsx_vfmin_s(a,b);     }
    SI F   max(F a, F b)        { return __lsx_vfmax_s(a,b);     }
    SI I32 min(I32 a, I32 b)    { return __lsx_vmin_w(a,b);      }
    SI U32 min(U32 a, U32 b)    { return __lsx_vmin_wu(a,b);     }
    SI I32 max(I32 a, I32 b)    { return __lsx_vmax_w(a,b);      }
    SI U32 max(U32 a, U32 b)    { return __lsx_vmax_wu(a,b);     }

    SI F   mad(F f, F m, F a)   { return __lsx_vfmadd_s(f, m, a);        }
    SI F   nmad(F f, F m, F a)  { return __lsx_vfmadd_s(-f, m, a);      }
    SI F   abs_(F v)            { return (F)__lsx_vand_v((I32)v, (I32)(0-v));       }
    SI I32 abs_(I32 v)          { return max(v, -v);                     }
    SI F   rcp_approx (F v)     { return __lsx_vfrecip_s(v);             }
    SI F   rcp_precise (F v)    { F e = rcp_approx(v); return e * nmad(v, e, F() + 2.0f); }
    SI F   rsqrt_approx (F v)   { return __lsx_vfrsqrt_s(v);             }
    SI F    sqrt_(F v)          { return __lsx_vfsqrt_s (v);             }

    SI U32 iround(F v) {
        F t = F() + 0.5f;
        return __lsx_vftintrz_w_s(v + t); }

    SI U32 round(F v) {
        F t = F() + 0.5f;
        return __lsx_vftintrz_w_s(v + t); }

    SI U16 pack(U32 v) {
        __m128i tmp = __lsx_vsat_wu(v, 15);
        __m128i p =  __lsx_vpickev_h(tmp, tmp);
        return sk_unaligned_load<U16>(&p);  // We have two copies.  Return (the lower) one.
    }

    SI U8 pack(U16 v) {
        __m128i r = widen_cast<__m128i>(v);
        __m128i tmp = __lsx_vsat_hu(r, 7);
        r =  __lsx_vpickev_b(tmp, tmp);
        return sk_unaligned_load<U8>(&r);
    }

    SI bool any(I32 c){
        v4i32 retv = (v4i32)__lsx_vmskltz_w(__lsx_vslt_wu(__lsx_vldi(0), c));
        return retv[0] != 0b0000;
    }

    SI bool all(I32 c){
        v4i32 retv = (v4i32)__lsx_vmskltz_w(__lsx_vslt_wu(__lsx_vldi(0), c));
        return retv[0] == 0b1111;
    }

    SI F floor_(F v) {
        return __lsx_vfrintrm_s(v);
    }

    SI F ceil_(F v) {
        return __lsx_vfrintrp_s(v);
    }

    template <typename T>
    SI V<T> gather(const T* p, U32 ix) {
        return V<T>{p[ix[0]], p[ix[1]], p[ix[2]], p[ix[3]]};
    }
    // Using 'int*' prevents data from passing through floating-point registers.
    SI F   gather(const int*    p, int ix0, int ix1, int ix2, int ix3) {
       F ret = {0.0};
       ret = (F)__lsx_vinsgr2vr_w(ret, p[ix0], 0);
       ret = (F)__lsx_vinsgr2vr_w(ret, p[ix1], 1);
       ret = (F)__lsx_vinsgr2vr_w(ret, p[ix2], 2);
       ret = (F)__lsx_vinsgr2vr_w(ret, p[ix3], 3);
       return ret;
    }
    template <typename T>
    SI V<T> gather_unaligned(const T* p, U32 ix) {
        return gather(p, ix);
    }

    template <typename V, typename S>
    SI void scatter_masked(V src, S* dst, U32 ix, I32 mask) {
        V before = gather(dst, ix);
        V after = if_then_else(mask, src, before);
        dst[ix[0]] = after[0];
        dst[ix[1]] = after[1];
        dst[ix[2]] = after[2];
        dst[ix[3]] = after[3];
    }

    SI void load2(const uint16_t* ptr, U16* r, U16* g) {
        __m128i _01 = __lsx_vld(ptr, 0);                  // r0 g0 r1 g1 r2 g2 r3 g3
        __m128i rg     = __lsx_vshuf4i_h(_01, 0xD8);         // r0 r1 g0 g1 r2 r3 g2 g3

        __m128i R = __lsx_vshuf4i_w(rg, 0x88);               // r0 r1 r2 r3 r0 r1 r2 r3
        __m128i G = __lsx_vshuf4i_w(rg, 0xDD);               // g0 g1 g2 g3 g0 g1 g2 g3
        *r = sk_unaligned_load<U16>(&R);
        *g = sk_unaligned_load<U16>(&G);
    }

    SI void store2(uint16_t* ptr, U16 r, U16 g) {
        U32 rg = __lsx_vilvl_h(widen_cast<__m128i>(g), widen_cast<__m128i>(r));
        __lsx_vst(rg, ptr, 0);
    }

    SI void load4(const uint16_t* ptr, U16* r, U16* g, U16* b, U16* a) {
        __m128i _01 = __lsx_vld(ptr,  0),    // r0 g0 b0 a0 r1 g1 b1 a1
                _23 = __lsx_vld(ptr,  16);   // r2 g2 b2 a2 r3 g3 b3 a3

        __m128i _02 = __lsx_vilvl_h(_23, _01),  // r0 r2 g0 g2 b0 b2 a0 a2
             _13 = __lsx_vilvh_h(_23, _01);  // r1 r3 g1 g3 b1 b3 a1 a3

        __m128i rg = __lsx_vilvl_h(_13, _02),   // r0 r1 r2 r3 g0 g1 g2 g3
             ba = __lsx_vilvh_h(_13, _02);   // b0 b1 b2 b3 a0 a1 a2 a3

        *r = sk_unaligned_load<U16>((uint16_t*)&rg + 0);
        *g = sk_unaligned_load<U16>((uint16_t*)&rg + 4);
        *b = sk_unaligned_load<U16>((uint16_t*)&ba + 0);
        *a = sk_unaligned_load<U16>((uint16_t*)&ba + 4);
    }

    SI void store4(uint16_t* ptr, U16 r, U16 g, U16 b, U16 a) {
        __m128i rg = __lsx_vilvl_h(widen_cast<__m128i>(g), widen_cast<__m128i>(r)),
             ba = __lsx_vilvl_h(widen_cast<__m128i>(a), widen_cast<__m128i>(b));

        __lsx_vst(__lsx_vilvl_w(ba, rg), ptr, 0);
        __lsx_vst(__lsx_vilvh_w(ba, rg), ptr, 16);
    }

    SI void load4(const float* ptr, F* r, F* g, F* b, F* a) {
        F _0 = (F)__lsx_vld(ptr, 0),
          _1 = (F)__lsx_vld(ptr, 16),
          _2 = (F)__lsx_vld(ptr, 32),
          _3 = (F)__lsx_vld(ptr, 48);
        _LSX_TRANSPOSE4_S(_0,_1,_2,_3);
        *r = _0;
        *g = _1;
        *b = _2;
        *a = _3;
    }

    SI void store4(float* ptr, F r, F g, F b, F a) {
        _LSX_TRANSPOSE4_S(r,g,b,a);
        __lsx_vst(r, ptr, 0);
        __lsx_vst(g, ptr, 16);
        __lsx_vst(b, ptr, 32);
        __lsx_vst(a, ptr, 48);
    }

#endif

// Helpers to do scalar -> vector promotion on GCC (clang does this automatically)
// We need to subtract (not add) zero to keep float conversion zero-cost. See:
// https://stackoverflow.com/q/48255293
//
// The GCC implementation should be usable everywhere, but Mac clang (only) complains that the
// expressions make these functions not constexpr.
//
// Further: We can't use the subtract-zero version in scalar mode. There, the subtraction will
// really happen (at least at low optimization levels), which can alter the bit pattern of NaNs.
// Because F_() is used when copying uniforms (even integer uniforms), this can corrupt values.
// The vector subtraction of zero doesn't appear to ever alter NaN bit patterns.
#if defined(__clang__) || defined(SKRP_CPU_SCALAR)
SI constexpr F F_(float x) { return x; }
SI constexpr I32 I32_(int32_t x) { return x; }
SI constexpr U32 U32_(uint32_t x) { return x; }
#else
SI constexpr F F_(float x) { return x - F(); }
SI constexpr I32 I32_(int32_t x) { return x + I32(); }
SI constexpr U32 U32_(uint32_t x) { return x + U32(); }
#endif

// Extremely helpful literals:
static constexpr F F0 = F_(0.0f),
                   F1 = F_(1.0f);

#if !defined(SKRP_CPU_SCALAR)
    SI F min(F a, float b) { return min(a, F_(b)); }
    SI F min(float a, F b) { return min(F_(a), b); }
    SI F max(F a, float b) { return max(a, F_(b)); }
    SI F max(float a, F b) { return max(F_(a), b); }

    SI F mad(F f, F m, float a) { return mad(f, m, F_(a)); }
    SI F mad(F f, float m, F a) { return mad(f, F_(m), a); }
    SI F mad(F f, float m, float a) { return mad(f, F_(m), F_(a)); }
    SI F mad(float f, F m, F a) { return mad(F_(f), m, a); }
    SI F mad(float f, F m, float a) { return mad(F_(f), m, F_(a)); }
    SI F mad(float f, float m, F a) { return mad(F_(f), F_(m), a); }

    SI F nmad(F f, F m, float a) { return nmad(f, m, F_(a)); }
    SI F nmad(F f, float m, F a) { return nmad(f, F_(m), a); }
    SI F nmad(F f, float m, float a) { return nmad(f, F_(m), F_(a)); }
    SI F nmad(float f, F m, F a) { return nmad(F_(f), m, a); }
    SI F nmad(float f, F m, float a) { return nmad(F_(f), m, F_(a)); }
    SI F nmad(float f, float m, F a) { return nmad(F_(f), F_(m), a); }
#endif

// We need to be a careful with casts.
// (F)x means cast x to float in the portable path, but bit_cast x to float in the others.
// These named casts and bit_cast() are always what they seem to be.
#if defined(SKRP_CPU_SCALAR)
    SI F   cast  (U32 v) { return   static_cast<F>(v); }
    SI F   cast64(U64 v) { return   static_cast<F>(v); }
    SI U32 trunc_(F   v) { return static_cast<U32>(v); }
    SI U32 expand(U16 v) { return static_cast<U32>(v); }
    SI U32 expand(U8  v) { return static_cast<U32>(v); }
#else
    SI F   cast  (U32 v) { return      __builtin_convertvector(bit_cast_lanes<I32>(v),   F); }
    SI F   cast64(U64 v) { return      __builtin_convertvector(     v,   F); }
    SI U32 trunc_(F   v) { return bit_cast_lanes<U32>(__builtin_convertvector(     v, I32)); }
    SI U32 expand(U16 v) { return      __builtin_convertvector(     v, U32); }
    SI U32 expand(U8  v) { return      __builtin_convertvector(     v, U32); }
#endif

#if !defined(SKRP_CPU_SCALAR)
SI F if_then_else(I32 c, F     t, float e) { return if_then_else(c,    t , F_(e)); }
SI F if_then_else(I32 c, float t, F     e) { return if_then_else(c, F_(t),    e ); }
SI F if_then_else(I32 c, float t, float e) { return if_then_else(c, F_(t), F_(e)); }
#endif

SI F fract(F v) { return v - floor_(v); }

// See http://www.machinedlearnings.com/2011/06/fast-approximate-logarithm-exponential.html
SI F approx_log2(F x) {
    // e - 127 is a fair approximation of log2(x) in its own right...
    F e = cast(sk_bit_cast<U32>(x)) * (1.0f / (1<<23));

    // ... but using the mantissa to refine its error is _much_ better.
    F m = sk_bit_cast<F>((sk_bit_cast<U32>(x) & 0x007fffff) | 0x3f000000);

    return nmad(m, 1.498030302f, e - 124.225514990f) - 1.725879990f / (0.3520887068f + m);
}

SI F approx_log(F x) {
    const float ln2 = 0.69314718f;
    return ln2 * approx_log2(x);
}

SI F approx_pow2(F x) {
    constexpr float kInfinityBits = 0x7f800000;

    F f = fract(x);
    F approx = nmad(f, 1.490129070f, x + 121.274057500f);
      approx += 27.728023300f / (4.84252568f - f);
      approx *= 1.0f * (1<<23);
      approx  = min(max(approx, F0), F_(kInfinityBits));  // guard against underflow/overflow

    return sk_bit_cast<F>(round(approx));
}

SI F approx_exp(F x) {
    const float log2_e = 1.4426950408889634074f;
    return approx_pow2(log2_e * x);
}

SI F approx_powf(F x, F y) {
    return if_then_else((x == 0)|(x == 1), x
                                         , approx_pow2(approx_log2(x) * y));
}
#if !defined(SKRP_CPU_SCALAR)
SI F approx_powf(F x, float y) { return approx_powf(x, F_(y)); }
#endif

SI F from_half(U16 h) {
#if defined(SKRP_CPU_NEON) && defined(SK_CPU_ARM64)
    return vcvt_f32_f16(bit_cast_lanes<float16x4_t>(h));

#elif defined(SKRP_CPU_ML4)
    return _mm512_cvtph_ps(bit_cast_lanes<__m256i>(h));

#elif defined(SKRP_CPU_AVX2)
    return _mm256_cvtph_ps(bit_cast_lanes<__m128i>(h));

#else
    // Remember, a half is 1-5-10 (sign-exponent-mantissa) with 15 exponent bias.
    U32 sem = expand(h);
    U32 s   = sem & 0x8000;
    U32 em = sem ^ s;

    // Convert to 1-8-23 float with 127 bias, flushing denorm halfs (including zero) to zero.
    I32 denorm = bit_cast_lanes<I32>(em) < 0x0400;      // I32 comparison is often quicker, and always safe here.
    return if_then_else(denorm, F0
                              , sk_bit_cast<F>( (s<<16) + (em<<13) + ((127-15)<<23) ));
#endif
}

SI U16 to_half(F f) {
#if defined(SKRP_CPU_NEON) && defined(SK_CPU_ARM64)
    return bit_cast_lanes<U16>(vcvt_f16_f32(f));

#elif defined(SKRP_CPU_ML4)
    return bit_cast_lanes<U16>(_mm512_cvtps_ph(f, _MM_FROUND_CUR_DIRECTION));

#elif defined(SKRP_CPU_AVX2)
    return bit_cast_lanes<U16>(_mm256_cvtps_ph(f, _MM_FROUND_CUR_DIRECTION));

#else
    // Remember, a float is 1-8-23 (sign-exponent-mantissa) with 127 exponent bias.
    U32 sem = sk_bit_cast<U32>(f);
    U32 s   = sem & 0x80000000;
    U32 em = sem ^ s;

    // Convert to 1-5-10 half with 15 bias, flushing denorm halfs (including zero) to zero.
    I32 denorm = bit_cast_lanes<I32>(em) < 0x38800000;  // I32 comparison is often quicker, and always safe here.
    return pack(bit_cast_lanes<U32>(if_then_else(denorm, I32_(0)
                                        , bit_cast_lanes<I32>(((s>>16) + (em>>13) - ((127-15)<<10))))));
#endif
}

// Native CPU contexts use flat addresses. A tail's temporary base is a biased
// address token, not an array pointer: keep biasing and cancellation in uintptr_t
// arithmetic, then form the actual scratch pointer only at the stage access.
static void patch_memory_contexts(SkSpan<SkRasterPipelineContexts::MemoryCtxPatch> memoryCtxPatches,
                                  const size_t dx, const size_t dy, size_t tail) {
    for (SkRasterPipelineContexts::MemoryCtxPatch& patch : memoryCtxPatches) {
        SkRasterPipelineContexts::MemoryCtx& context = *patch.info.context;
        const size_t pixelOffset = dy * context.stride + dx;
        const uintptr_t byteOffset = patch.info.bytesPerPixel * pixelOffset;
        if (patch.info.load) {
            const uintptr_t base = reinterpret_cast<uintptr_t>(context.pixels);
            const uintptr_t address = base + byteOffset;
            const void* pixels = reinterpret_cast<const void*>(address);
            const size_t bytes = patch.info.bytesPerPixel * tail;
            memcpy(patch.scratch, pixels, bytes);
        }
        SkASSERT(patch.backup == nullptr);
        const uintptr_t scratchAddress = reinterpret_cast<uintptr_t>(patch.scratch);
        const uintptr_t biasedAddress = scratchAddress - byteOffset;
        patch.backup = context.pixels;
        context.pixels = reinterpret_cast<void*>(biasedAddress);
    }
}

static void restore_memory_contexts(
        SkSpan<SkRasterPipelineContexts::MemoryCtxPatch> memoryCtxPatches,
        const size_t dx, const size_t dy, size_t tail) {
    for (SkRasterPipelineContexts::MemoryCtxPatch& patch : memoryCtxPatches) {
        SkRasterPipelineContexts::MemoryCtx& context = *patch.info.context;
        SkASSERT(patch.backup != nullptr);
        context.pixels = patch.backup;
        patch.backup = nullptr;
        if (patch.info.store) {
            const size_t pixelOffset = dy * context.stride + dx;
            const uintptr_t byteOffset = patch.info.bytesPerPixel * pixelOffset;
            const uintptr_t base = reinterpret_cast<uintptr_t>(context.pixels);
            const uintptr_t address = base + byteOffset;
            void* pixels = reinterpret_cast<void*>(address);
            const size_t bytes = patch.info.bytesPerPixel * tail;
            memcpy(pixels, patch.scratch, bytes);
        }
    }
}

#if defined(SKRP_CPU_SCALAR) || defined(SKRP_CPU_SSE2)
    // In scalar and SSE2 mode, we always use precise math so we can have more predictable results.
    // Chrome will use the SSE2 implementation when --disable-skia-runtime-opts is set. (b/40042946)
    SI F rcp_fast(F v) { return rcp_precise(v); }
    SI F rsqrt(F v)    { return rcp_precise(sqrt_(v)); }
#else
    SI F rcp_fast(F v) { return rcp_approx(v); }
    SI F rsqrt(F v)    { return rsqrt_approx(v); }
#endif

// Our fundamental vector depth is our pixel stride.
static constexpr size_t N = sizeof(F) / sizeof(float);

// We're finally going to get to what a Stage function looks like!

// Any custom ABI to use for all (non-externally-facing) stage functions?
// Also decide here whether to use narrow (compromise) or wide (ideal) stages.
#if defined(SK_CPU_ARM32) && defined(SKRP_CPU_NEON)
    // This lets us pass vectors more efficiently on 32-bit ARM.
    // We can still only pass 16 floats, so best as 4x {r,g,b,a}.
    #define ABI __attribute__((pcs("aapcs-vfp")))
    #define SKRP_NARROW_STAGES 1
#elif defined(_MSC_VER)
    // Even if not vectorized, this lets us pass {r,g,b,a} as registers,
    // instead of {b,a} on the stack.  Narrow stages work best for __vectorcall.
    #define ABI __vectorcall
    #define SKRP_NARROW_STAGES 1
#elif defined(__x86_64__) || defined(SK_CPU_ARM64) || defined(SK_CPU_LOONGARCH)
    // These platforms are ideal for wider stages, and their default ABI is ideal.
    #define ABI
    #define SKRP_NARROW_STAGES 0
#else
    // 32-bit or unknown... shunt them down the narrow path.
    // Odds are these have few registers and are better off there.
    #define ABI
    #define SKRP_NARROW_STAGES 1
#endif

#if SKRP_NARROW_STAGES
    struct Params {
        size_t dx, dy;
        F dr,dg,db,da;
    };
    using Stage = void(ABI*)(Params*, SkRasterPipelineStage* program, F r, F g, F b, F a);
#else
    using Stage = void(ABI*)(SkRasterPipelineStage* program, const size_t dx, const size_t dy,
                             F,F,F,F, F,F,F,F);
#endif

static void start_pipeline(size_t dx, size_t dy,
                           size_t xlimit, size_t ylimit,
                           SkRasterPipelineStage* program,
                           SkSpan<SkRasterPipelineContexts::MemoryCtxPatch> memoryCtxPatches) {
    Stage start = reinterpret_cast<Stage>((*program).fn);
    const size_t x0 = dx;
    for (; dy < ylimit; dy++) {
    #if SKRP_NARROW_STAGES
        Params params = { x0,dy, F0,F0,F0,F0 };
        while (params.dx + N <= xlimit) {
            start(&params,program, F0,F0,F0,F0);
            params.dx += N;
        }
        if (size_t tail = xlimit - params.dx) {
            patch_memory_contexts(memoryCtxPatches, params.dx, dy, tail);
            start(&params,program, F0,F0,F0,F0);
            restore_memory_contexts(memoryCtxPatches, params.dx, dy, tail);
        }
    #else
        dx = x0;
        while (dx + N <= xlimit) {
            start(program,dx,dy, F0,F0,F0,F0, F0,F0,F0,F0);
            dx += N;
        }
        if (size_t tail = xlimit - dx) {
            patch_memory_contexts(memoryCtxPatches, dx, dy, tail);
            start(program,dx,dy, F0,F0,F0,F0, F0,F0,F0,F0);
            restore_memory_contexts(memoryCtxPatches, dx, dy, tail);
        }
    #endif
    }
}

// Stage tables list each op's stage in SkRasterPipelineOp order, so an op's value indexes its
// stage; SkOpts.cpp copies them into its dispatch tables.
template <typename Entry, size_t kCount>
consteval bool stages_in_op_order(const std::array<Entry, kCount>& entries) {
    for (size_t i = 0; i < kCount; ++i) {
        if (static_cast<size_t>(entries[i].op) != i) {
            return false;
        }
    }
    return true;
}

// A highp stage kernel: its op's context, the coordinates of its first pixel, and the source and
// destination registers, which it may modify.
template <SkRasterPipelineOp op>
using HighpKernel = void (*)(StageContextPointer<op, true>, const size_t, const size_t,
                             F&, F&, F&, F&, F&, F&, F&, F&);

// A highp stage runs its kernel, advances the program by one stage and calls the next one,
// letting the optimizer decide whether that call is a tail call.
#if SKRP_NARROW_STAGES
    template <SkRasterPipelineOp op, HighpKernel<op> kernel>
    static void ABI highp_stage(Params* params, SkRasterPipelineStage* program,
                                F r, F g, F b, F a) {
        kernel(StageContext<op, true>{program}, (*params).dx, (*params).dy,
               r, g, b, a, (*params).dr, (*params).dg, (*params).db, (*params).da);
        ++program;
        Stage fn = reinterpret_cast<Stage>((*program).fn);
        return fn(params, program, r, g, b, a);
    }
#else
    template <SkRasterPipelineOp op, HighpKernel<op> kernel>
    static void ABI highp_stage(SkRasterPipelineStage* program, const size_t dx, const size_t dy,
                                F r, F g, F b, F a, F dr, F dg, F db, F da) {
        kernel(StageContext<op, true>{program}, dx, dy, r, g, b, a, dr, dg, db, da);
        ++program;
        Stage fn = reinterpret_cast<Stage>((*program).fn);
        return fn(program, dx, dy, r, g, b, a, dr, dg, db, da);
    }
#endif

// just_return() is a simple no-op stage that only exists to end the chain,
// returning back up to start_pipeline(), and from there to the caller.
#if SKRP_NARROW_STAGES
    static void ABI just_return(Params*, SkRasterPipelineStage*, F,F,F,F) {}
#else
    static void ABI just_return(SkRasterPipelineStage*, size_t,size_t,
                                F,F,F,F, F,F,F,F) {}
#endif

// We could start defining normal Stages now.  But first, some helper functions.

template <typename V, typename T>
SI V load(const T* src) {
    return sk_unaligned_load<V>(src);
}

template <typename V, typename T>
SI void store(T* dst, V v) {
    sk_unaligned_store(dst, v);
}

SI F from_byte(U8 b) {
    return cast(expand(b)) * (1/255.0f);
}
SI F from_short(U16 s) {
    return cast(expand(s)) * (1/65535.0f);
}
SI void from_565(U16 _565, F* r, F* g, F* b) {
    U32 wide = expand(_565);
    *r = cast(wide & (31<<11)) * (1.0f / (31<<11));
    *g = cast(wide & (63<< 5)) * (1.0f / (63<< 5));
    *b = cast(wide & (31<< 0)) * (1.0f / (31<< 0));
}
SI void from_4444(U16 _4444, F* r, F* g, F* b, F* a) {
    U32 wide = expand(_4444);
    *r = cast(wide & (15<<12)) * (1.0f / (15<<12));
    *g = cast(wide & (15<< 8)) * (1.0f / (15<< 8));
    *b = cast(wide & (15<< 4)) * (1.0f / (15<< 4));
    *a = cast(wide & (15<< 0)) * (1.0f / (15<< 0));
}
SI void from_8888(U32 _8888, F* r, F* g, F* b, F* a) {
    *r = cast((_8888      ) & 0xff) * (1/255.0f);
    *g = cast((_8888 >>  8) & 0xff) * (1/255.0f);
    *b = cast((_8888 >> 16) & 0xff) * (1/255.0f);
    *a = cast((_8888 >> 24)       ) * (1/255.0f);
}
SI void from_88(U16 _88, F* r, F* g) {
    U32 wide = expand(_88);
    *r = cast((wide      ) & 0xff) * (1/255.0f);
    *g = cast((wide >>  8) & 0xff) * (1/255.0f);
}
SI void from_1010102(U32 rgba, F* r, F* g, F* b, F* a) {
    *r = cast((rgba      ) & 0x3ff) * (1/1023.0f);
    *g = cast((rgba >> 10) & 0x3ff) * (1/1023.0f);
    *b = cast((rgba >> 20) & 0x3ff) * (1/1023.0f);
    *a = cast((rgba >> 30)        ) * (1/   3.0f);
}
SI void from_1010102_xr(U32 rgba, F* r, F* g, F* b, F* a) {
    // Match https://developer.apple.com/documentation/metal/mtlpixelformat/bgr10_xr?language=objc
    // i.e. "float = (xr10_value - 384) / 510.0f", but with the modification that we store 2 bits
    // of alpha with a regular unorm encoding.
    *r = (cast((rgba      ) & 0x3ff) - 384.f) * (1/510.f);
    *g = (cast((rgba >> 10) & 0x3ff) - 384.f) * (1/510.f);
    *b = (cast((rgba >> 20) & 0x3ff) - 384.f) * (1/510.f);
    *a = (cast((rgba >> 30)        )        ) * (1/3.f); // A in 1010102_xr is *not* extended range
}
SI void from_10101010_xr(U64 _10x6, F* r, F* g, F* b, F* a) {
    // From https://developer.apple.com/documentation/metal/mtlpixelformat/bgra10_xr?language=objc
    // the linear transformation is the same as 1010102_xr, except the integer encoding is shifted
    // to have 6 low bits of padding.
    *r = (cast64((_10x6 >> ( 0+6)) & 0x3ff) - 384.f) * (1/510.f);
    *g = (cast64((_10x6 >> (16+6)) & 0x3ff) - 384.f) * (1/510.f);
    *b = (cast64((_10x6 >> (32+6)) & 0x3ff) - 384.f) * (1/510.f);
    *a = (cast64((_10x6 >> (48+6)) & 0x3ff) - 384.f) * (1/510.f);
}
SI void from_10x6(U64 _10x6, F* r, F* g, F* b, F* a) {
    *r = cast64((_10x6 >> ( 0+6)) & 0x3ff) * (1/1023.0f);
    *g = cast64((_10x6 >> (16+6)) & 0x3ff) * (1/1023.0f);
    *b = cast64((_10x6 >> (32+6)) & 0x3ff) * (1/1023.0f);
    *a = cast64((_10x6 >> (48+6)) & 0x3ff) * (1/1023.0f);
}
SI void from_1616(U32 _1616, F* r, F* g) {
    *r = cast((_1616      ) & 0xffff) * (1/65535.0f);
    *g = cast((_1616 >> 16) & 0xffff) * (1/65535.0f);
}
SI void from_16161616(U64 _16161616, F* r, F* g, F* b, F* a) {
    *r = cast64((_16161616      ) & 0xffff) * (1/65535.0f);
    *g = cast64((_16161616 >> 16) & 0xffff) * (1/65535.0f);
    *b = cast64((_16161616 >> 32) & 0xffff) * (1/65535.0f);
    *a = cast64((_16161616 >> 48) & 0xffff) * (1/65535.0f);
}

// Used by load_ and store_ stages to get to the right (dx,dy) starting point of contiguous memory.
template <typename T>
SI T* ptr_at_xy(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy) {
    const size_t elementOffset = dy * (*ctx).stride + dx;
    const uintptr_t byteOffset = elementOffset * sizeof(T);
    const uintptr_t base = reinterpret_cast<uintptr_t>((*ctx).pixels);
    const uintptr_t address = base + byteOffset;
    T* pixels = reinterpret_cast<T*>(address);
    return pixels;
}

// clamp v to [0,limit).
SI F clamp(F v, F limit) {
    F inclusive = sk_bit_cast<F>(sk_bit_cast<U32>(limit) - 1);  // Exclusive -> inclusive.
    return min(max(0.0f, v), inclusive);
}

// clamp to (0,limit).
SI F clamp_ex(F v, float limit) {
    const F inclusiveZ = F_(std::numeric_limits<float>::min());
    const F inclusiveL = sk_bit_cast<F>( sk_bit_cast<U32>(F_(limit)) - 1 );
    return min(max(inclusiveZ, v), inclusiveL);
}

// Polynomial approximation of degree 5 for sin(x * 2 * pi) in the range [-1/4, 1/4]
// Adapted from https://github.com/google/swiftshader/blob/master/docs/Sin-Cos-Optimization.pdf
SI F sin5q_(F x) {
    // A * x + B * x^3 + C * x^5
    // Exact at x = 0, 1/12, 1/6, 1/4, and their negatives,
    // which correspond to x * 2 * pi = 0, pi/6, pi/3, pi/2
    constexpr float A = 6.28230858f;
    constexpr float B = -41.1693687f;
    constexpr float C = 74.4388885f;
    F x2 = x * x;
    return x * mad(mad(x2, C, B), x2, A);
}

SI F sin_(F x) {
    constexpr float one_over_pi2 = 1 / (2 * SK_FloatPI);
    x = mad(x, -one_over_pi2, 0.25f);
    x = 0.25f - abs_(x - floor_(x + 0.5f));
    return sin5q_(x);
}

SI F cos_(F x) {
    constexpr float one_over_pi2 = 1 / (2 * SK_FloatPI);
    x *= one_over_pi2;
    x = 0.25f - abs_(x - floor_(x + 0.5f));
    return sin5q_(x);
}

/*  "GENERATING ACCURATE VALUES FOR THE TANGENT FUNCTION"
     https://mae.ufl.edu/~uhk/ACCURATE-TANGENT.pdf

    approx = x + (1/3)x^3 + (2/15)x^5 + (17/315)x^7 + (62/2835)x^9

    Some simplifications:
    1. tan(x) is periodic, -PI/2 < x < PI/2
    2. tan(x) is odd, so tan(-x) = -tan(x)
    3. Our polynomial approximation is best near zero, so we use the following identity
                    tan(x) + tan(y)
       tan(x + y) = -----------------
                   1 - tan(x)*tan(y)
       tan(PI/4) = 1

       So for x > PI/8, we do the following refactor:
       x' = x - PI/4

                1 + tan(x')
       tan(x) = ------------
                1 - tan(x')
 */
SI F tan_(F x) {
    constexpr float Pi = SK_FloatPI;
    // periodic between -pi/2 ... pi/2
    // shift to 0...Pi, scale 1/Pi to get into 0...1, then fract, scale-up, shift-back
    x = mad(fract(mad(x, 1/Pi, 0.5f)), Pi, -Pi/2);

    I32 neg = (x < 0.0f);
    x = if_then_else(neg, -x, x);

    // minimize total error by shifting if x > pi/8
    I32 use_quotient = (x > (Pi/8));
    x = if_then_else(use_quotient, x - (Pi/4), x);

    // 9th order poly = 4th order(x^2) * x
    const float c4 = 62 / 2835.0f;
    const float c3 = 17 / 315.0f;
    const float c2 = 2 / 15.0f;
    const float c1 = 1 / 3.0f;
    const float c0 = 1.0f;
    F x2 = x * x;
    x *= mad(x2, mad(x2, mad(x2, mad(x2, c4, c3), c2), c1), c0);
    x = if_then_else(use_quotient, (1+x)/(1-x), x);
    x = if_then_else(neg, -x, x);
    return x;
}

/*  Use 4th order polynomial approximation from https://arachnoid.com/polysolve/
        with 129 values of x,atan(x) for x:[0...1]
    This only works for 0 <= x <= 1
 */
SI F approx_atan_unit(F x) {
    // y =   0.14130025741326729 x⁴
    //     - 0.34312835980675116 x³
    //     - 0.016172900528248768 x²
    //     + 1.00376969762003850 x
    //     - 0.00014758242182738969
    const float c4 =  0.14130025741326729f;
    const float c3 = -0.34312835980675116f;
    const float c2 = -0.016172900528248768f;
    const float c1 =  1.0037696976200385f;
    const float c0 = -0.00014758242182738969f;
    return mad(x, mad(x, mad(x, mad(x, c4, c3), c2), c1), c0);
}

// Use identity atan(x) = pi/2 - atan(1/x) for x > 1
SI F atan_(F x) {
    I32 neg = (x < 0.0f);
    x = if_then_else(neg, -x, x);
    I32 flip = (x > 1.0f);
    x = if_then_else(flip, 1/x, x);
    x = approx_atan_unit(x);
    x = if_then_else(flip, SK_FloatPI/2 - x, x);
    x = if_then_else(neg, -x, x);
    return x;
}

// Handbook of Mathematical Functions, by Milton Abramowitz and Irene Stegun:
// https://books.google.com/books/content?id=ZboM5tOFWtsC&pg=PA81&img=1&zoom=3&hl=en&bul=1&sig=ACfU3U2M75tG_iGVOS92eQspr14LTq02Nw&ci=0%2C15%2C999%2C1279&edge=0
// http://screen/8YGJxUGFQ49bVX6
SI F asin_(F x) {
    I32 neg = (x < 0.0f);
    x = if_then_else(neg, -x, x);
    const float c3 = -0.0187293f;
    const float c2 = 0.0742610f;
    const float c1 = -0.2121144f;
    const float c0 = 1.5707288f;
    F poly = mad(x, mad(x, mad(x, c3, c2), c1), c0);
    x = nmad(sqrt_(1 - x), poly, SK_FloatPI/2);
    x = if_then_else(neg, -x, x);
    return x;
}

SI F acos_(F x) {
    return SK_FloatPI/2 - asin_(x);
}

/*  Use identity atan(x) = pi/2 - atan(1/x) for x > 1
    By swapping y,x to ensure the ratio is <= 1, we can safely call atan_unit()
    which avoids a 2nd divide instruction if we had instead called atan().
 */
SI F atan2_(F y0, F x0) {
    I32 flip = (abs_(y0) > abs_(x0));
    F   y = if_then_else(flip, x0, y0);
    F   x = if_then_else(flip, y0, x0);
    F   arg = y/x;

    I32 neg = (arg < 0.0f);
    arg = if_then_else(neg, -arg, arg);

    F r = approx_atan_unit(arg);
    r = if_then_else(flip, SK_FloatPI/2 - r, r);
    r = if_then_else(neg, -r, r);

    // handle quadrant distinctions
    r = if_then_else((y0 >= 0) & (x0  < 0), r + SK_FloatPI, r);
    r = if_then_else((y0  < 0) & (x0 <= 0), r - SK_FloatPI, r);
    // Note: we don't try to handle 0,0 or infinities
    return r;
}

// Used by gather_ stages to calculate the base pointer and a vector of indices to load.
template <typename T>
SI U32 ix_and_ptr(T** ptr, const SkRasterPipelineContexts::GatherCtx* ctx, F x, F y) {
    // We use exclusive clamp so that our min value is > 0 because ULP subtraction using U32 would
    // produce a NaN if applied to +0.f.
    x = clamp_ex(x, (*ctx).width );
    y = clamp_ex(y, (*ctx).height);
    x = sk_bit_cast<F>(sk_bit_cast<U32>(x) - static_cast<uint32_t>((*ctx).roundDownAtInteger));
    y = sk_bit_cast<F>(sk_bit_cast<U32>(y) - static_cast<uint32_t>((*ctx).roundDownAtInteger));
    *ptr = static_cast<const T*>((*ctx).pixels);
    return trunc_(y)*(*ctx).stride + trunc_(x);
}

// We often have a nominally [0,1] float value we need to scale and convert to an integer,
// whether for a table lookup or to pack back down into bytes for storage. The floating point
// value is mapped to an integer using the equation "v * scale + bias".
//
// In practice, especially when dealing with interesting color spaces, that notionally
// [0,1] float may be out of [0,1] range.  Unorms cannot represent that, so we must clamp to
// [0,maxI] after the bias and scale has been applied to `v`. This allows callers that explicitly
// support negative float values (extended range) to still pack to a unorm.
//
// In most cases bias is 0 and the max value equals `scale`, but you can adjust the expected input
// by tweaking `maxI` relative to `scale`.
SI U32 to_unorm(F v, float scale, float bias, int maxI) {
    // Any time we use round() we probably want to use to_unorm().
    return round(min(max(0.0f, mad(v, scale, bias)), static_cast<float>( maxI)));
}
SI U32 to_unorm(F v, int scale) {
    return to_unorm(v, static_cast<float>( scale), /*bias=*/0.f, /*maxI=*/scale);
}

SI I32 cond_to_mask(I32 cond) {
#if defined(SKRP_CPU_SCALAR)
    // In scalar mode, conditions are bools (0 or 1), but we want to store and operate on masks
    // (eg, using bitwise operations to select values).
    return if_then_else(cond, I32(~0), I32(0));
#else
    // In SIMD mode, our various instruction sets already represent conditions as masks.
    return cond;
#endif
}

#if defined(SKRP_CPU_SCALAR)
// In scalar mode, `data` only contains a single lane.
SI uint32_t select_lane(uint32_t data, int /*lane*/) { return data; }
SI  int32_t select_lane( int32_t data, int /*lane*/) { return data; }
#else
// In SIMD mode, `data` contains a vector of lanes.
SI uint32_t select_lane(U32 data, int lane) { return data[lane]; }
SI  int32_t select_lane(I32 data, int lane) { return data[lane]; }
#endif

// Now finally, normal Stages!

SI void seed_shader_k(NoCtx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    static constexpr float iota[] = {
        0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f,
        8.5f, 9.5f,10.5f,11.5f,12.5f,13.5f,14.5f,15.5f,
    };
    static_assert(std::size(iota) >= SkRasterPipelineContexts::kMaxStride_highp);

    // It's important for speed to explicitly cast(dx) and cast(dy),
    // which has the effect of splatting them to vectors before converting to floats.
    // On Intel this breaks a data dependency on previous loop iterations' registers.
    r = cast(U32_(dx)) + sk_unaligned_load<F>(iota);
    g = cast(U32_(dy)) + 0.5f;
    b = F1;  // This is w=1 for matrix multiplies by the device coords.
    a = F0;
}

SI void dither_k(const float* rate, const size_t dx, const size_t dy,
                 F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    // Get [(dx,dy), (dx+1,dy), (dx+2,dy), ...] loaded up in integer vectors.
    uint32_t iota[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    static_assert(std::size(iota) >= SkRasterPipelineContexts::kMaxStride_highp);

    U32 X = U32_(dx) + sk_unaligned_load<U32>(iota);
    U32 Y = U32_(dy);

    // We're doing 8x8 ordered dithering, see https://en.wikipedia.org/wiki/Ordered_dithering.
    // In this case n=8 and we're using the matrix that looks like 1/64 x [ 0 48 12 60 ... ].

    // We only need X and X^Y from here on, so it's easier to just think of that as "Y".
    Y ^= X;

    // We'll mix the bottom 3 bits of each of X and Y to make 6 bits,
    // for 2^6 == 64 == 8x8 matrix values.  If X=abc and Y=def, we make fcebda.
    U32 M = (Y & 1) << 5 | (X & 1) << 4
          | (Y & 2) << 2 | (X & 2) << 1
          | (Y & 4) >> 1 | (X & 4) >> 2;

    // Scale that dither to [0,1), then (-0.5,+0.5), here using 63/128 = 0.4921875 as 0.5-epsilon.
    // We want to make sure our dither is less than 0.5 in either direction to keep exact values
    // like 0 and 1 unchanged after rounding.
    F dither = mad(cast(M), 2/128.0f, -63/128.0f);

    r = mad(dither, *rate, r);
    g = mad(dither, *rate, g);
    b = mad(dither, *rate, b);

    r = max(0.0f, min(r, a));
    g = max(0.0f, min(g, a));
    b = max(0.0f, min(b, a));
}

// load 4 floats from memory, and splat them into r,g,b,a
SI void uniform_color_k(const SkRasterPipelineContexts::UniformColorCtx* c,
                        const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = F_((*c).r);
    g = F_((*c).g);
    b = F_((*c).b);
    a = F_((*c).a);
}
SI void unbounded_uniform_color_k(const SkRasterPipelineContexts::UniformColorCtx* c,
                                  const size_t dx, const size_t dy,
                                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = F_((*c).r);
    g = F_((*c).g);
    b = F_((*c).b);
    a = F_((*c).a);
}
// load 4 floats from memory, and splat them into dr,dg,db,da
SI void uniform_color_dst_k(const SkRasterPipelineContexts::UniformColorCtx* c,
                            const size_t dx, const size_t dy,
                            F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    dr = F_((*c).r);
    dg = F_((*c).g);
    db = F_((*c).b);
    da = F_((*c).a);
}

// splats opaque-black into r,g,b,a
SI void black_color_k(NoCtx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = g = b = F0;
    a = F1;
}

SI void white_color_k(NoCtx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = g = b = a = F1;
}

// load registers r,g,b,a from context (mirrors store_src)
SI void load_src_k(const float* ptr, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = sk_unaligned_load<F>(ptr + 0*N);
    g = sk_unaligned_load<F>(ptr + 1*N);
    b = sk_unaligned_load<F>(ptr + 2*N);
    a = sk_unaligned_load<F>(ptr + 3*N);
}

// store registers r,g,b,a into context (mirrors load_src)
SI void store_src_k(float* ptr, const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    sk_unaligned_store(ptr + 0*N, r);
    sk_unaligned_store(ptr + 1*N, g);
    sk_unaligned_store(ptr + 2*N, b);
    sk_unaligned_store(ptr + 3*N, a);
}
// store registers r,g into context
SI void store_src_rg_k(float* ptr, const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    sk_unaligned_store(ptr + 0*N, r);
    sk_unaligned_store(ptr + 1*N, g);
}
// load registers r,g from context
SI void load_src_rg_k(const float* ptr, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = sk_unaligned_load<F>(ptr + 0*N);
    g = sk_unaligned_load<F>(ptr + 1*N);
}
// store register a into context
SI void store_src_a_k(float* ptr, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    sk_unaligned_store(ptr, a);
}

// load registers dr,dg,db,da from context (mirrors store_dst)
SI void load_dst_k(const float* ptr, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    dr = sk_unaligned_load<F>(ptr + 0*N);
    dg = sk_unaligned_load<F>(ptr + 1*N);
    db = sk_unaligned_load<F>(ptr + 2*N);
    da = sk_unaligned_load<F>(ptr + 3*N);
}

// store registers dr,dg,db,da into context (mirrors load_dst)
SI void store_dst_k(float* ptr, const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    sk_unaligned_store(ptr + 0*N, dr);
    sk_unaligned_store(ptr + 1*N, dg);
    sk_unaligned_store(ptr + 2*N, db);
    sk_unaligned_store(ptr + 3*N, da);
}

// Most blend modes apply the same logic to each channel. Each such mode's stage is
// blend_each_channel_k<mode_channel>.
template <F (&channel)(F, F, F, F)>
SI void blend_each_channel_k(NoCtx, const size_t dx, const size_t dy,
                             F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = channel(r,dr,a,da);
    g = channel(g,dg,a,da);
    b = channel(b,db,a,da);
    a = channel(a,da,a,da);
}

SI F inv(F x) { return 1.0f - x; }
SI F two(F x) { return x + x; }

SI F clear_channel(F s, F d, F sa, F da) { return F0; }
SI F srcatop_channel(F s, F d, F sa, F da) { return mad(s, da, d*inv(sa)); }
SI F dstatop_channel(F s, F d, F sa, F da) { return mad(d, sa, s*inv(da)); }
SI F srcin_channel(F s, F d, F sa, F da) { return s * da; }
SI F dstin_channel(F s, F d, F sa, F da) { return d * sa; }
SI F srcout_channel(F s, F d, F sa, F da) { return s * inv(da); }
SI F dstout_channel(F s, F d, F sa, F da) { return d * inv(sa); }
SI F srcover_channel(F s, F d, F sa, F da) { return mad(d, inv(sa), s); }
SI F dstover_channel(F s, F d, F sa, F da) { return mad(s, inv(da), d); }

SI F modulate_channel(F s, F d, F sa, F da) { return s*d; }
SI F multiply_channel(F s, F d, F sa, F da) { return mad(s, d, mad(s, inv(da), d*inv(sa))); }
SI F plus__channel(F s, F d, F sa, F da) { return min(s + d, 1.0f); }  // We can clamp to either 1 or sa.
SI F screen_channel(F s, F d, F sa, F da) { return nmad(s, d, s + d); }
SI F xor__channel(F s, F d, F sa, F da) { return mad(s, inv(da), d*inv(sa)); }

// Most other blend modes apply the same logic to colors, and srcover to alpha. Each such mode's
// stage is blend_colors_k<mode_channel>.
template <F (&channel)(F, F, F, F)>
SI void blend_colors_k(NoCtx, const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = channel(r,dr,a,da);
    g = channel(g,dg,a,da);
    b = channel(b,db,a,da);
    a = mad(da, inv(a), a);
}

SI F darken_channel(F s, F d, F sa, F da) { return s + d -     max(s*da, d*sa) ; }
SI F lighten_channel(F s, F d, F sa, F da) { return s + d -     min(s*da, d*sa) ; }
SI F difference_channel(F s, F d, F sa, F da) { return s + d - two(min(s*da, d*sa)); }
SI F exclusion_channel(F s, F d, F sa, F da) { return s + d - two(s*d); }

SI F colorburn_channel(F s, F d, F sa, F da) {
    return if_then_else(d == da,    d +    s*inv(da),
           if_then_else(s ==  0, /* s + */ d*inv(sa),
                                sa*(da - min(da, (da-d)*sa*rcp_fast(s))) + s*inv(da) + d*inv(sa)));
}
SI F colordodge_channel(F s, F d, F sa, F da) {
    return if_then_else(d ==  0, /* d + */ s*inv(da),
           if_then_else(s == sa,    s +    d*inv(sa),
                                 sa*min(da, (d*sa)*rcp_fast(sa - s)) + s*inv(da) + d*inv(sa)));
}
SI F hardlight_channel(F s, F d, F sa, F da) {
    return s*inv(da) + d*inv(sa)
         + if_then_else(two(s) <= sa, two(s*d), sa*da - two((da-d)*(sa-s)));
}
SI F overlay_channel(F s, F d, F sa, F da) {
    return s*inv(da) + d*inv(sa)
         + if_then_else(two(d) <= da, two(s*d), sa*da - two((da-d)*(sa-s)));
}

SI F softlight_channel(F s, F d, F sa, F da) {
    F m  = if_then_else(da > 0, d / da, 0.0f);
    F s2 = two(s);
    F m4 = two(two(m));

    // The logic forks three ways:
    //    1. dark src?
    //    2. light src, dark dst?
    //    3. light src, light dst?
    F darkSrc = d*(sa + (s2 - sa)*(1.0f - m));  // Used in case 1.
    F darkDst = (m4*m4 + m4)*(m - 1.0f) + 7.0f*m;  // Used in case 2.
    F liteDst = sqrt_(m) - m;
    F liteSrc = d*sa + da*(s2 - sa) * if_then_else(two(two(d)) <= da, darkDst, liteDst); // 2 or 3?
    return s*inv(da) + d*inv(sa) + if_then_else(s2 <= sa, darkSrc, liteSrc);      // 1 or (2 or 3)?
}

// We're basing our implemenation of non-separable blend modes on
//   https://www.w3.org/TR/compositing-1/#blendingnonseparable.
// and
//   https://www.khronos.org/registry/OpenGL/specs/es/3.2/es_spec_3.2.pdf
// They're equivalent, but ES' math has been better simplified.
//
// Anything extra we add beyond that is to make the math work with premul inputs.

SI F sat(F r, F g, F b) { return max(r, max(g,b)) - min(r, min(g,b)); }
SI F lum(F r, F g, F b) { return mad(r, 0.30f, mad(g, 0.59f, b*0.11f)); }

SI void set_sat(F* r, F* g, F* b, F s) {
    F mn  = min(*r, min(*g,*b));
    F mx  = max(*r, max(*g,*b));
    F sat = mx - mn;

    // Map min channel to 0, max channel to s, and scale the middle proportionally.
    s = if_then_else(sat == 0.0f, 0.0f, s * rcp_fast(sat));
    *r = (*r - mn) * s;
    *g = (*g - mn) * s;
    *b = (*b - mn) * s;
}
SI void set_lum(F* r, F* g, F* b, F l) {
    F diff = l - lum(*r, *g, *b);
    *r += diff;
    *g += diff;
    *b += diff;
}
SI F clip_channel(F c, F l, I32 clip_low, I32 clip_high, F mn_scale, F mx_scale) {
    c = if_then_else(clip_low,  mad(mn_scale, c - l, l), c);
    c = if_then_else(clip_high, mad(mx_scale, c - l, l), c);
    c = max(c, 0.0f);  // Sometimes without this we may dip just a little negative.
    return c;
}
SI void clip_color(F* r, F* g, F* b, F a) {
    F   mn        = min(*r, min(*g, *b));
    F   mx        = max(*r, max(*g, *b));
    F   l         = lum(*r, *g, *b);
    F   mn_scale  = (    l) * rcp_fast(l - mn);
    F   mx_scale  = (a - l) * rcp_fast(mx - l);
    I32 clip_low  = cond_to_mask(mn < 0 && l != mn);
    I32 clip_high = cond_to_mask(mx > a && l != mx);

    *r = clip_channel(*r, l, clip_low, clip_high, mn_scale, mx_scale);
    *g = clip_channel(*g, l, clip_low, clip_high, mn_scale, mx_scale);
    *b = clip_channel(*b, l, clip_low, clip_high, mn_scale, mx_scale);
}

SI void hue_k(NoCtx, const size_t dx, const size_t dy,
              F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F R = r*a;
    F G = g*a;
    F B = b*a;

    set_sat(&R, &G, &B, sat(dr,dg,db)*a);
    set_lum(&R, &G, &B, lum(dr,dg,db)*a);
    clip_color(&R,&G,&B, a*da);

    r = mad(r, inv(da), mad(dr, inv(a), R));
    g = mad(g, inv(da), mad(dg, inv(a), G));
    b = mad(b, inv(da), mad(db, inv(a), B));
    a = a + nmad(a, da, da);
}
SI void saturation_k(NoCtx, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F R = dr*a;
    F G = dg*a;
    F B = db*a;

    set_sat(&R, &G, &B, sat( r, g, b)*da);
    set_lum(&R, &G, &B, lum(dr,dg,db)* a);  // (This is not redundant.)
    clip_color(&R,&G,&B, a*da);

    r = mad(r, inv(da), mad(dr, inv(a), R));
    g = mad(g, inv(da), mad(dg, inv(a), G));
    b = mad(b, inv(da), mad(db, inv(a), B));
    a = a + nmad(a, da, da);
}
SI void color_k(NoCtx, const size_t dx, const size_t dy,
                F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F R = r*da;
    F G = g*da;
    F B = b*da;

    set_lum(&R, &G, &B, lum(dr,dg,db)*a);
    clip_color(&R,&G,&B, a*da);

    r = mad(r, inv(da), mad(dr, inv(a), R));
    g = mad(g, inv(da), mad(dg, inv(a), G));
    b = mad(b, inv(da), mad(db, inv(a), B));
    a = a + nmad(a, da, da);
}
SI void luminosity_k(NoCtx, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F R = dr*a;
    F G = dg*a;
    F B = db*a;

    set_lum(&R, &G, &B, lum(r,g,b)*da);
    clip_color(&R,&G,&B, a*da);

    r = mad(r, inv(da), mad(dr, inv(a), R));
    g = mad(g, inv(da), mad(dg, inv(a), G));
    b = mad(b, inv(da), mad(db, inv(a), B));
    a = a + nmad(a, da, da);
}

SI void srcover_rgba_8888_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                            const size_t dx, const size_t dy,
                            F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 dst = load<U32>(ptr);
    dr = cast((dst      ) & 0xff);
    dg = cast((dst >>  8) & 0xff);
    db = cast((dst >> 16) & 0xff);
    da = cast((dst >> 24)       );
    // {dr,dg,db,da} are in [0,255]
    // { r, g, b, a} are in [0,  1] (but may be out of gamut)

    r = mad(dr, inv(a), r*255.0f);
    g = mad(dg, inv(a), g*255.0f);
    b = mad(db, inv(a), b*255.0f);
    a = mad(da, inv(a), a*255.0f);
    // { r, g, b, a} are now in [0,255]  (but may be out of gamut)

    // to_unorm() clamps back to gamut.  Scaling by 1 since we're already 255-based.
    dst = to_unorm(r, /*scale=*/1, /*bias=*/0.f, /*maxI=*/255)
        | to_unorm(g, /*scale=*/1, /*bias=*/0.f, /*maxI=*/255) <<  8
        | to_unorm(b, /*scale=*/1, /*bias=*/0.f, /*maxI=*/255) << 16
        | to_unorm(a, /*scale=*/1, /*bias=*/0.f, /*maxI=*/255) << 24;
    store(ptr, dst);
}

SI F clamp_01_(F v) { return min(max(0.0f, v), 1.0f); }

SI void clamp_01_k(NoCtx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = clamp_01_(r);
    g = clamp_01_(g);
    b = clamp_01_(b);
    a = clamp_01_(a);
}

SI void clamp_a_01_k(NoCtx, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    a = clamp_01_(a);
}

SI void clamp_gamut_k(NoCtx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    a = min(max(a, 0.0f), 1.0f);
    r = min(max(r, 0.0f), a);
    g = min(max(g, 0.0f), a);
    b = min(max(b, 0.0f), a);
}

SI void set_rgb_k(const float* rgb, const size_t dx, const size_t dy,
                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = F_(rgb[0]);
    g = F_(rgb[1]);
    b = F_(rgb[2]);
}

SI void unbounded_set_rgb_k(const float* rgb, const size_t dx, const size_t dy,
                            F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = F_(rgb[0]);
    g = F_(rgb[1]);
    b = F_(rgb[2]);
}

SI void swap_rb_k(NoCtx, const size_t dx, const size_t dy,
                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F tmp = r;
    r = b;
    b = tmp;
}
SI void swap_rb_dst_k(NoCtx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F tmp = dr;
    dr = db;
    db = tmp;
}

SI void move_src_dst_k(NoCtx, const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    dr = r;
    dg = g;
    db = b;
    da = a;
}
SI void move_dst_src_k(NoCtx, const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = dr;
    g = dg;
    b = db;
    a = da;
}
SI void swap_src_dst_k(NoCtx, const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    std::swap(r, dr);
    std::swap(g, dg);
    std::swap(b, db);
    std::swap(a, da);
}

SI void premul_k(NoCtx, const size_t dx, const size_t dy,
                 F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = r * a;
    g = g * a;
    b = b * a;
}
SI void premul_dst_k(NoCtx, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    dr = dr * da;
    dg = dg * da;
    db = db * da;
}
SI void unpremul_k(NoCtx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    float inf = sk_bit_cast<float>(0x7f800000);
    F scale = if_then_else(1.0f/a < inf, 1.0f/a, 0.0f);
    r *= scale;
    g *= scale;
    b *= scale;
}
SI void unpremul_polar_k(NoCtx, const size_t dx, const size_t dy,
                         F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    float inf = sk_bit_cast<float>(0x7f800000);
    F scale = if_then_else(1.0f/a < inf, 1.0f/a, 0.0f);
    g *= scale;
    b *= scale;
}

SI void force_opaque_k(NoCtx, const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {  a = F1; }
SI void force_opaque_dst_k(NoCtx, const size_t dx, const size_t dy,
                           F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { da = F1; }

SI void rgb_to_hsl_k(NoCtx, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F mx = max(r, max(g,b));
    F mn = min(r, min(g,b));
    F d = mx - mn;
    F d_rcp = 1.0f / d;

    F h = (1/6.0f) *
          if_then_else(mx == mn, 0.0f,
          if_then_else(mx ==  r, (g-b)*d_rcp + if_then_else(g < b, 6.0f, 0.0f),
          if_then_else(mx ==  g, (b-r)*d_rcp + 2.0f,
                                 (r-g)*d_rcp + 4.0f)));

    F l = (mx + mn) * 0.5f;
    F s = if_then_else(mx == mn, 0.0f,
                       d / if_then_else(l > 0.5f, 2.0f-mx-mn, mx+mn));

    r = h;
    g = s;
    b = l;
}
// One channel of hsl_to_rgb, from its hue offset, chroma c and lightness l.
SI F hsl_hue_to_rgb(F hue, F c, F l) {
    F q = clamp_01_(abs_(fract(hue) * 6.0f - 3.0f) - 1.0f);
    return (q - 0.5f) * c + l;
}

SI void hsl_to_rgb_k(NoCtx, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    // See GrRGBToHSLFilterEffect.fp

    F h = r;
    F s = g;
    F l = b;
    F c = (1.0f - abs_(2.0f * l - 1)) * s;

    r = hsl_hue_to_rgb(h + 0.0f/3.0f, c, l);
    g = hsl_hue_to_rgb(h + 2.0f/3.0f, c, l);
    b = hsl_hue_to_rgb(h + 1.0f/3.0f, c, l);
}

// Color conversion functions used in gradient interpolation, based on
// https://www.w3.org/TR/css-color-4/#color-conversion-code
SI void css_lab_to_xyz_k(NoCtx, const size_t dx, const size_t dy,
                         F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    constexpr float k = 24389 / 27.0f;
    constexpr float e = 216 / 24389.0f;

    F f[3];
    f[1] = (r + 16) * (1 / 116.0f);
    f[0] = (g * (1 / 500.0f)) + f[1];
    f[2] = f[1] - (b * (1 / 200.0f));

    F f_cubed[3] = { f[0]*f[0]*f[0], f[1]*f[1]*f[1], f[2]*f[2]*f[2] };

    F xyz[3] = {
        if_then_else(f_cubed[0] > e, f_cubed[0], (116 * f[0] - 16) * (1 / k)),
        if_then_else(r > k * e,      f_cubed[1], r * (1 / k)),
        if_then_else(f_cubed[2] > e, f_cubed[2], (116 * f[2] - 16) * (1 / k))
    };

    constexpr float D50[3] = { 0.3457f / 0.3585f, 1.0f, (1.0f - 0.3457f - 0.3585f) / 0.3585f };
    r = xyz[0]*D50[0];
    g = xyz[1]*D50[1];
    b = xyz[2]*D50[2];
}

SI void css_oklab_to_linear_srgb_k(NoCtx, const size_t dx, const size_t dy,
                                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F l_ = r + 0.3963377774f * g + 0.2158037573f * b;
    F m_ = r - 0.1055613458f * g - 0.0638541728f * b;
    F s_ = r - 0.0894841775f * g - 1.2914855480f * b;

    F l = l_*l_*l_;
    F m = m_*m_*m_;
    F s = s_*s_*s_;

    r = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

SI void css_oklab_gamut_map_to_linear_srgb_k(NoCtx, const size_t dx, const size_t dy,
                                             F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    // TODO(https://crbug.com/1508329): Add support for gamut mapping.
    // Return a greyscale value, so that accidental use is obvious.
    F l_ = r;
    F m_ = r;
    F s_ = r;

    F l = l_*l_*l_;
    F m = m_*m_*m_;
    F s = s_*s_*s_;

    r = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

// Skia stores all polar colors with hue in the first component, so this "LCH -> Lab" transform
// actually takes "HCL". This is also used to do the same polar transform for OkHCL to OkLAB.
// See similar comments & logic in SkGradientBaseShader.cpp.
SI void css_hcl_to_lab_k(NoCtx, const size_t dx, const size_t dy,
                         F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F H = r;
    F C = g;
    F L = b;

    F hueRadians = H * (SK_FloatPI / 180);

    r = L;
    g = C * cos_(hueRadians);
    b = C * sin_(hueRadians);
}

SI F mod_(F x, float y) {
    return nmad(y, floor_(x * (1 / y)), x);
}

struct RGB { F r, g, b; };

SI RGB css_hsl_to_srgb_(F h, F s, F l) {
    h = mod_(h, 360);

    s *= 0.01f;
    l *= 0.01f;

    F k[3] = {
        mod_(0 + h * (1 / 30.0f), 12),
        mod_(8 + h * (1 / 30.0f), 12),
        mod_(4 + h * (1 / 30.0f), 12)
    };
    F a  = s * min(l, 1 - l);
    return {
        l - a * max(-1.0f, min(min(k[0] - 3.0f, 9.0f - k[0]), 1.0f)),
        l - a * max(-1.0f, min(min(k[1] - 3.0f, 9.0f - k[1]), 1.0f)),
        l - a * max(-1.0f, min(min(k[2] - 3.0f, 9.0f - k[2]), 1.0f))
    };
}

SI void css_hsl_to_srgb_k(NoCtx, const size_t dx, const size_t dy,
                          F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    RGB rgb = css_hsl_to_srgb_(r, g, b);
    r = rgb.r;
    g = rgb.g;
    b = rgb.b;
}

SI void css_hwb_to_srgb_k(NoCtx, const size_t dx, const size_t dy,
                          F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    g *= 0.01f;
    b *= 0.01f;

    F gray = g / (g + b);

    RGB rgb = css_hsl_to_srgb_(r, F_(100.0f), F_(50.0f));
    rgb.r = rgb.r * (1 - g - b) + g;
    rgb.g = rgb.g * (1 - g - b) + g;
    rgb.b = rgb.b * (1 - g - b) + g;

    I32 isGray = (g + b) >= 1;

    r = if_then_else(isGray, gray, rgb.r);
    g = if_then_else(isGray, gray, rgb.g);
    b = if_then_else(isGray, gray, rgb.b);
}

// Derive alpha's coverage from rgb coverage and the values of src and dst alpha.
SI F alpha_coverage_from_rgb_coverage(F a, F da, F cr, F cg, F cb) {
    return if_then_else(a < da, min(cr, min(cg,cb))
                              , max(cr, max(cg,cb)));
}

SI void scale_1_float_k(const float* c, const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = r * *c;
    g = g * *c;
    b = b * *c;
    a = a * *c;
}
SI void scale_u8_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint8_t* ptr = ptr_at_xy<const uint8_t>(ctx, dx,dy);

    U8 scales = load<U8>(ptr);
    F c = from_byte(scales);

    r = r * c;
    g = g * c;
    b = b * c;
    a = a * c;
}
SI void scale_565_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx,dy);

    F cr;
    F cg;
    F cb;
    from_565(load<U16>(ptr), &cr, &cg, &cb);

    F ca = alpha_coverage_from_rgb_coverage(a,da, cr,cg,cb);

    r = r * cr;
    g = g * cg;
    b = b * cb;
    a = a * ca;
}

SI F lerp(F from, F to, F t) {
    return mad(to-from, t, from);
}

SI void lerp_1_float_k(const float* c, const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = lerp(dr, r, F_(*c));
    g = lerp(dg, g, F_(*c));
    b = lerp(db, b, F_(*c));
    a = lerp(da, a, F_(*c));
}
SI void scale_native_k(const float scales[], const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F c = sk_unaligned_load<F>(scales);
    r = r * c;
    g = g * c;
    b = b * c;
    a = a * c;
}
SI void lerp_native_k(const float scales[], const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F c = sk_unaligned_load<F>(scales);
    r = lerp(dr, r, c);
    g = lerp(dg, g, c);
    b = lerp(db, b, c);
    a = lerp(da, a, c);
}
SI void lerp_u8_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint8_t* ptr = ptr_at_xy<const uint8_t>(ctx, dx,dy);

    U8 scales = load<U8>(ptr);
    F c = from_byte(scales);

    r = lerp(dr, r, c);
    g = lerp(dg, g, c);
    b = lerp(db, b, c);
    a = lerp(da, a, c);
}
SI void lerp_565_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx,dy);

    F cr;
    F cg;
    F cb;
    from_565(load<U16>(ptr), &cr, &cg, &cb);

    F ca = alpha_coverage_from_rgb_coverage(a,da, cr,cg,cb);

    r = lerp(dr, r, cr);
    g = lerp(dg, g, cg);
    b = lerp(db, b, cb);
    a = lerp(da, a, ca);
}

SI void emboss_k(const SkRasterPipelineContexts::EmbossCtx* ctx, const size_t dx, const size_t dy,
                 F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint8_t* mptr = ptr_at_xy<const uint8_t>(&(*ctx).mul, dx,dy);
    const uint8_t* aptr = ptr_at_xy<const uint8_t>(&(*ctx).add, dx,dy);

    F mul = from_byte(load<U8>(mptr));
    F add = from_byte(load<U8>(aptr));

    r = mad(r, mul, add);
    g = mad(g, mul, add);
    b = mad(b, mul, add);
}

SI void byte_tables_k(const SkRasterPipelineContexts::TablesCtx* tables,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = from_byte(gather((*tables).r, to_unorm(r, 255)));
    g = from_byte(gather((*tables).g, to_unorm(g, 255)));
    b = from_byte(gather((*tables).b, to_unorm(b, 255)));
    a = from_byte(gather((*tables).a, to_unorm(a, 255)));
}

SI F strip_sign(F x, U32* sign) {
    U32 bits = sk_bit_cast<U32>(x);
    *sign = bits & 0x80000000;
    return sk_bit_cast<F>(bits ^ *sign);
}

SI F apply_sign(F x, U32 sign) {
    return sk_bit_cast<F>(sign | sk_bit_cast<U32>(x));
}

// The transfer functions below apply to each color channel independently; the sign is set
// aside and restored around the curve.
SI F parametric_channel(F v, const skcms_TransferFunction* ctx) {
    U32 sign;
    v = strip_sign(v, &sign);

    F r = if_then_else(v <= (*ctx).d, mad((*ctx).c, v, (*ctx).f)
                                  , approx_powf(mad((*ctx).a, v, (*ctx).b), (*ctx).g) + (*ctx).e);
    return apply_sign(r, sign);
}

SI void parametric_k(const skcms_TransferFunction* ctx, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = parametric_channel(r, ctx);
    g = parametric_channel(g, ctx);
    b = parametric_channel(b, ctx);
}

// gamma_channel and the bilerp_lerp helpers replaced lambdas and keep ordinary inlining, as
// those lambdas had: forcing it reorders gamma_'s loads and costs NEON bilerp_clamp_8888 a
// register spill. The other helpers that replaced lambdas compile to the same code with SI.
// gamma_ appends the whole sRGB-ish transfer function and uses only its exponent g. It read g
// through a float pointer to the structure before; it now names the member.
static inline F gamma_channel(F v, const skcms_TransferFunction* ctx) {
    U32 sign;
    v = strip_sign(v, &sign);
    return apply_sign(approx_powf(v, (*ctx).g), sign);
}

SI void gamma__k(const skcms_TransferFunction* ctx, const size_t dx, const size_t dy,
                 F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = gamma_channel(r, ctx);
    g = gamma_channel(g, ctx);
    b = gamma_channel(b, ctx);
}

SI F PQish_channel(F v, const skcms_TransferFunction* ctx) {
    U32 sign;
    v = strip_sign(v, &sign);

    F r = approx_powf(max(mad((*ctx).b, approx_powf(v, (*ctx).c), (*ctx).a), 0.0f)
                       / (mad((*ctx).e, approx_powf(v, (*ctx).c), (*ctx).d)),
                    (*ctx).f);

    return apply_sign(r, sign);
}

SI void PQish_k(const skcms_TransferFunction* ctx, const size_t dx, const size_t dy,
                F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = PQish_channel(r, ctx);
    g = PQish_channel(g, ctx);
    b = PQish_channel(b, ctx);
}

SI F HLGish_channel(F v, const skcms_TransferFunction* ctx) {
    U32 sign;
    v = strip_sign(v, &sign);

    const float R = (*ctx).a;
    const float G = (*ctx).b;
    const float a = (*ctx).c;
    const float b = (*ctx).d;
    const float c = (*ctx).e;
    const float K = (*ctx).f + 1.0f;

    F r = if_then_else(v*R <= 1, approx_powf(v*R, G)
                               , approx_exp((v-c)*a) + b);

    return K * apply_sign(r, sign);
}

SI void HLGish_k(const skcms_TransferFunction* ctx, const size_t dx, const size_t dy,
                 F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = HLGish_channel(r, ctx);
    g = HLGish_channel(g, ctx);
    b = HLGish_channel(b, ctx);
}

SI F HLGinvish_channel(F v, const skcms_TransferFunction* ctx) {
    U32 sign;
    v = strip_sign(v, &sign);

    const float R = (*ctx).a;
    const float G = (*ctx).b;
    const float a = (*ctx).c;
    const float b = (*ctx).d;
    const float c = (*ctx).e;
    const float K = (*ctx).f + 1.0f;

    v /= K;
    F r = if_then_else(v <= 1, R * approx_powf(v, G)
                             , a * approx_log(v - b) + c);

    return apply_sign(r, sign);
}

SI void HLGinvish_k(const skcms_TransferFunction* ctx, const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = HLGinvish_channel(r, ctx);
    g = HLGinvish_channel(g, ctx);
    b = HLGinvish_channel(b, ctx);
}

SI void ootf_k(const float* ctx, const size_t dx, const size_t dy,
               F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F Y = ctx[0] * r + ctx[1] * g + ctx[2] * b;

    U32 sign;
    Y = strip_sign(Y, &sign);
    F Y_to_gamma_minus_one = apply_sign(approx_powf(Y, ctx[3]), sign);
    r = r * Y_to_gamma_minus_one;
    g = g * Y_to_gamma_minus_one;
    b = b * Y_to_gamma_minus_one;
}

SI void load_a8_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint8_t* ptr = ptr_at_xy<const uint8_t>(ctx, dx,dy);

    r = g = b = F0;
    a = from_byte(load<U8>(ptr));
}
SI void load_a8_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint8_t* ptr = ptr_at_xy<const uint8_t>(ctx, dx,dy);

    dr = dg = db = F0;
    da = from_byte(load<U8>(ptr));
}
SI void gather_a8_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                    const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint8_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r,g);
    r = g = b = F0;
    a = from_byte(gather(ptr, ix));
}
SI void store_a8_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint8_t* ptr = ptr_at_xy<uint8_t>(ctx, dx,dy);

    U8 packed = pack(pack(to_unorm(a, 255)));
    store(ptr, packed);
}
SI void store_r8_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint8_t* ptr = ptr_at_xy<uint8_t>(ctx, dx,dy);

    U8 packed = pack(pack(to_unorm(r, 255)));
    store(ptr, packed);
}

SI void load_565_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx,dy);

    from_565(load<U16>(ptr), &r,&g,&b);
    a = F1;
}
SI void load_565_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                       const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx,dy);

    from_565(load<U16>(ptr), &dr,&dg,&db);
    da = F1;
}
SI void gather_565_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                     const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r,g);
    from_565(gather_unaligned(ptr, ix), &r,&g,&b);
    a = F1;
}
SI void store_565_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint16_t* ptr = ptr_at_xy<uint16_t>(ctx, dx,dy);

    U16 px = pack( to_unorm(r, 31) << 11
                 | to_unorm(g, 63) <<  5
                 | to_unorm(b, 31)      );
    store(ptr, px);
}

SI void load_4444_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx,dy);
    from_4444(load<U16>(ptr), &r,&g,&b,&a);
}
SI void load_4444_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                        const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx,dy);
    from_4444(load<U16>(ptr), &dr,&dg,&db,&da);
}
SI void gather_4444_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r,g);
    from_4444(gather_unaligned(ptr, ix), &r,&g,&b,&a);
}
SI void store_4444_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                     const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint16_t* ptr = ptr_at_xy<uint16_t>(ctx, dx,dy);
    U16 px = pack( to_unorm(r, 15) << 12
                 | to_unorm(g, 15) <<  8
                 | to_unorm(b, 15) <<  4
                 | to_unorm(a, 15)      );
    store(ptr, px);
}

SI void load_8888_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr = ptr_at_xy<const uint32_t>(ctx, dx,dy);
    from_8888(load<U32>(ptr), &r,&g,&b,&a);
}
SI void load_8888_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                        const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr = ptr_at_xy<const uint32_t>(ctx, dx,dy);
    from_8888(load<U32>(ptr), &dr,&dg,&db,&da);
}
SI void gather_8888_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r,g);
    from_8888(gather_unaligned(ptr, ix), &r,&g,&b,&a);
}
SI void store_8888_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                     const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 px = to_unorm(r, 255)
           | to_unorm(g, 255) <<  8
           | to_unorm(b, 255) << 16
           | to_unorm(a, 255) << 24;
    store(ptr, px);
}

SI void load_rg88_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx, dy);
    from_88(load<U16>(ptr), &r, &g);
    b = F0;
    a = F1;
}
SI void load_rg88_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                        const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx, dy);
    from_88(load<U16>(ptr), &dr, &dg);
    db = F0;
    da = F1;
}
SI void gather_rg88_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r, g);
    from_88(gather_unaligned(ptr, ix), &r, &g);
    b = F0;
    a = F1;
}
SI void store_rg88_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                     const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint16_t* ptr = ptr_at_xy<uint16_t>(ctx, dx, dy);
    U16 px = pack( to_unorm(r, 255) | to_unorm(g, 255) <<  8 );
    store(ptr, px);
}

SI void load_a16_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx,dy);
    r = g = b = F0;
    a = from_short(load<U16>(ptr));
}
SI void load_a16_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                       const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx, dy);
    dr = dg = db = F0;
    da = from_short(load<U16>(ptr));
}
SI void gather_a16_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                     const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r, g);
    r = g = b = F0;
    a = from_short(gather_unaligned(ptr, ix));
}
SI void store_a16_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint16_t* ptr = ptr_at_xy<uint16_t>(ctx, dx,dy);

    U16 px = pack(to_unorm(a, 65535));
    store(ptr, px);
}

SI void load_r16_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx, dy);
    g = b = F0;
    a = F1;
    r = from_short(load<U16>(ptr));
}
SI void load_r16_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                       const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx, dy);
    dg = db = F0;
    da = F1;
    dr = from_short(load<U16>(ptr));
}
SI void gather_r16_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                     const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r, g);
    g = b = F0;
    a = F1;
    r = from_short(gather_unaligned(ptr, ix));
}
SI void store_r16_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint16_t* ptr = ptr_at_xy<uint16_t>(ctx, dx, dy);

    U16 px = pack(to_unorm(r, 65535));
    store(ptr, px);
}

SI void load_rg1616_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr = ptr_at_xy<const uint32_t>(ctx, dx, dy);
    b = F0;
    a = F1;
    from_1616(load<U32>(ptr), &r,&g);
}
SI void load_rg1616_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                          const size_t dx, const size_t dy,
                          F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr = ptr_at_xy<const uint32_t>(ctx, dx, dy);
    from_1616(load<U32>(ptr), &dr, &dg);
    db = F0;
    da = F1;
}
SI void gather_rg1616_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                        const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r, g);
    from_1616(gather_unaligned(ptr, ix), &r, &g);
    b = F0;
    a = F1;
}
SI void store_rg1616_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                       const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 px = to_unorm(r, 65535)
           | to_unorm(g, 65535) <<  16;
    store(ptr, px);
}

SI void load_16161616_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                        const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint64_t* ptr = ptr_at_xy<const uint64_t>(ctx, dx, dy);
    from_16161616(load<U64>(ptr), &r,&g, &b, &a);
}
SI void load_16161616_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                            const size_t dx, const size_t dy,
                            F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint64_t* ptr = ptr_at_xy<const uint64_t>(ctx, dx, dy);
    from_16161616(load<U64>(ptr), &dr, &dg, &db, &da);
}
SI void gather_16161616_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                          const size_t dx, const size_t dy,
                          F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint64_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r, g);
    from_16161616(gather_unaligned(ptr, ix), &r, &g, &b, &a);
}
SI void store_16161616_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                         const size_t dx, const size_t dy,
                         F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint16_t* ptr = ptr_at_xy<uint16_t>(ctx, 4*dx,4*dy);

    U16 R = pack(to_unorm(r, 65535));
    U16 G = pack(to_unorm(g, 65535));
    U16 B = pack(to_unorm(b, 65535));
    U16 A = pack(to_unorm(a, 65535));

    store4(ptr, R,G,B,A);
}

SI void load_10x6_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint64_t* ptr = ptr_at_xy<const uint64_t>(ctx, dx, dy);
    from_10x6(load<U64>(ptr), &r,&g, &b, &a);
}
SI void load_10x6_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                        const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint64_t* ptr = ptr_at_xy<const uint64_t>(ctx, dx, dy);
    from_10x6(load<U64>(ptr), &dr, &dg, &db, &da);
}
SI void gather_10x6_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint64_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r, g);
    from_10x6(gather_unaligned(ptr, ix), &r, &g, &b, &a);
}
SI void store_10x6_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                     const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint16_t* ptr = ptr_at_xy<uint16_t>(ctx, 4*dx,4*dy);

    U16 R = pack(to_unorm(r, 1023)) << 6;
    U16 G = pack(to_unorm(g, 1023)) << 6;
    U16 B = pack(to_unorm(b, 1023)) << 6;
    U16 A = pack(to_unorm(a, 1023)) << 6;

    store4(ptr, R,G,B,A);
}

SI void load_1010102_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                       const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr = ptr_at_xy<const uint32_t>(ctx, dx,dy);
    from_1010102(load<U32>(ptr), &r,&g,&b,&a);
}
SI void load_1010102_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                           const size_t dx, const size_t dy,
                           F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr = ptr_at_xy<const uint32_t>(ctx, dx,dy);
    from_1010102(load<U32>(ptr), &dr,&dg,&db,&da);
}
SI void load_1010102_xr_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                          const size_t dx, const size_t dy,
                          F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr = ptr_at_xy<const uint32_t>(ctx, dx,dy);
    from_1010102_xr(load<U32>(ptr), &r,&g,&b,&a);
}
SI void load_1010102_xr_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                              const size_t dx, const size_t dy,
                              F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr = ptr_at_xy<const uint32_t>(ctx, dx,dy);
    from_1010102_xr(load<U32>(ptr), &dr,&dg,&db,&da);
}
SI void gather_1010102_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                         const size_t dx, const size_t dy,
                         F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r,g);
    from_1010102(gather_unaligned(ptr, ix), &r,&g,&b,&a);
}
SI void gather_1010102_xr_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                            const size_t dx, const size_t dy,
                            F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r, g);
    from_1010102_xr(gather_unaligned(ptr, ix), &r,&g,&b,&a);
}
SI void gather_10101010_xr_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                             const size_t dx, const size_t dy,
                             F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint64_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r, g);
    from_10101010_xr(gather_unaligned(ptr, ix), &r, &g, &b, &a);
}
SI void load_10101010_xr_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                           const size_t dx, const size_t dy,
                           F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint64_t* ptr = ptr_at_xy<const uint64_t>(ctx, dx, dy);
    from_10101010_xr(load<U64>(ptr), &r,&g, &b, &a);
}
SI void load_10101010_xr_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                               const size_t dx, const size_t dy,
                               F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint64_t* ptr = ptr_at_xy<const uint64_t>(ctx, dx, dy);
    from_10101010_xr(load<U64>(ptr), &dr, &dg, &db, &da);
}
SI void store_10101010_xr_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                            const size_t dx, const size_t dy,
                            F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint16_t* ptr = ptr_at_xy<uint16_t>(ctx, 4*dx,4*dy);

    // This is the inverse of from_10101010_xr, e.g. (v * 510 + 384)
    U16 R = pack(to_unorm(r, /*scale=*/510, /*bias=*/384, /*maxI=*/1023)) << 6;
    U16 G = pack(to_unorm(g, /*scale=*/510, /*bias=*/384, /*maxI=*/1023)) << 6;
    U16 B = pack(to_unorm(b, /*scale=*/510, /*bias=*/384, /*maxI=*/1023)) << 6;
    U16 A = pack(to_unorm(a, /*scale=*/510, /*bias=*/384, /*maxI=*/1023)) << 6;

    store4(ptr, R,G,B,A);
}
SI void store_1010102_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                        const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 px = to_unorm(r, 1023)
           | to_unorm(g, 1023) << 10
           | to_unorm(b, 1023) << 20
           | to_unorm(a,    3) << 30;
    store(ptr, px);
}
SI void store_1010102_xr_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                           const size_t dx, const size_t dy,
                           F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    // This is the inverse of from_1010102_xr, e.g. (v * 510 + 384)
    U32 px = to_unorm(r, /*scale=*/510, /*bias=*/384, /*maxI=*/1023)
           | to_unorm(g, /*scale=*/510, /*bias=*/384, /*maxI=*/1023) << 10
           | to_unorm(b, /*scale=*/510, /*bias=*/384, /*maxI=*/1023) << 20
           | to_unorm(a, /*scale=*/3) << 30;
    store(ptr, px);
}

SI void load_f16_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint64_t* ptr = ptr_at_xy<const uint64_t>(ctx, dx,dy);

    U16 R;
    U16 G;
    U16 B;
    U16 A;
    load4(reinterpret_cast<const uint16_t*>(ptr), &R,&G,&B,&A);
    r = from_half(R);
    g = from_half(G);
    b = from_half(B);
    a = from_half(A);
}
SI void load_f16_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                       const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint64_t* ptr = ptr_at_xy<const uint64_t>(ctx, dx,dy);

    U16 R;
    U16 G;
    U16 B;
    U16 A;
    load4(reinterpret_cast<const uint16_t*>(ptr), &R,&G,&B,&A);
    dr = from_half(R);
    dg = from_half(G);
    db = from_half(B);
    da = from_half(A);
}
SI void gather_f16_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                     const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint64_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r,g);
    U64 px = gather_unaligned(ptr, ix);

    U16 R;
    U16 G;
    U16 B;
    U16 A;
    load4(reinterpret_cast<const uint16_t*>(&px), &R,&G,&B,&A);
    r = from_half(R);
    g = from_half(G);
    b = from_half(B);
    a = from_half(A);
}
SI void store_f16_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint64_t* ptr = ptr_at_xy<uint64_t>(ctx, dx,dy);
    store4(reinterpret_cast<uint16_t*>(ptr), to_half(r)
                         , to_half(g)
                         , to_half(b)
                         , to_half(a));
}

SI void load_af16_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx,dy);

    U16 A = load<U16>(static_cast<const uint16_t*>(ptr));
    r = F0;
    g = F0;
    b = F0;
    a = from_half(A);
}
SI void load_af16_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                        const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx, dy);

    U16 A = load<U16>(static_cast<const uint16_t*>(ptr));
    dr = dg = db = F0;
    da = from_half(A);
}
SI void gather_af16_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r, g);
    r = g = b = F0;
    a = from_half(gather_unaligned(ptr, ix));
}
SI void store_af16_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                     const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint16_t* ptr = ptr_at_xy<uint16_t>(ctx, dx,dy);
    store(ptr, to_half(a));
}

SI void load_rf16_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx,dy);

    U16 R = load<U16>(static_cast<const uint16_t*>(ptr));
    r = from_half(R);
    g = b = F0;
    a = F1;
}
SI void load_rf16_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                        const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr = ptr_at_xy<const uint16_t>(ctx, dx, dy);

    U16 R = load<U16>(static_cast<const uint16_t*>(ptr));
    dr = from_half(R);
    dg = db = F0;
    da = F1;
}
SI void gather_rf16_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint16_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r, g);
    r = from_half(gather_unaligned(ptr, ix));
    g = b = F0;
    a = F1;
}
SI void store_rf16_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                     const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint16_t* ptr = ptr_at_xy<uint16_t>(ctx, dx,dy);
    store(ptr, to_half(r));
}

SI void load_rgf16_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                     const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr = ptr_at_xy<const uint32_t>(ctx, dx, dy);

    U16 R;
    U16 G;
    load2(reinterpret_cast<const uint16_t*>(ptr), &R, &G);
    r = from_half(R);
    g = from_half(G);
    b = F0;
    a = F1;
}
SI void load_rgf16_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                         const size_t dx, const size_t dy,
                         F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr = ptr_at_xy<const uint32_t>(ctx, dx, dy);

    U16 R;
    U16 G;
    load2(reinterpret_cast<const uint16_t*>(ptr), &R, &G);
    dr = from_half(R);
    dg = from_half(G);
    db = F0;
    da = F1;
}
SI void gather_rgf16_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                       const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const uint32_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r, g);
    U32 px = gather_unaligned(ptr, ix);

    U16 R;
    U16 G;
    load2(reinterpret_cast<const uint16_t*>(&px), &R, &G);
    r = from_half(R);
    g = from_half(G);
    b = F0;
    a = F1;
}
SI void store_rgf16_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx, dy);
    store2(reinterpret_cast<uint16_t*>(ptr), to_half(r)
                         , to_half(g));
}

SI void load_f32_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const float* ptr = ptr_at_xy<const float>(ctx, 4*dx,4*dy);
    load4(ptr, &r,&g,&b,&a);
}
SI void load_f32_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                       const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const float* ptr = ptr_at_xy<const float>(ctx, 4*dx,4*dy);
    load4(ptr, &dr,&dg,&db,&da);
}
SI void gather_f32_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                     const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const float* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, r,g);
    r = gather_unaligned(ptr, 4*ix + 0);
    g = gather_unaligned(ptr, 4*ix + 1);
    b = gather_unaligned(ptr, 4*ix + 2);
    a = gather_unaligned(ptr, 4*ix + 3);
}
SI void store_f32_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    float* ptr = ptr_at_xy<float>(ctx, 4*dx,4*dy);
    store4(ptr, r,g,b,a);
}

SI F exclusive_repeat(F v, const SkRasterPipelineContexts::TileCtx* ctx) {
    return v - floor_(v*(*ctx).invScale)*(*ctx).scale;
}
SI F exclusive_mirror(F v, const SkRasterPipelineContexts::TileCtx* ctx) {
    float limit = (*ctx).scale;
    float invLimit = (*ctx).invScale;

    // This is "repeat" over the range 0..2*limit
    F u = v - floor_(v*invLimit*0.5f)*2*limit;
    // s will be 0 when moving forward (e.g. [0, limit)) and 1 when moving backward (e.g.
    // [limit, 2*limit)).
    F s = floor_(u*invLimit);
    // This is the mirror result.
    F m = u - 2*s*(u - limit);
    // Apply a bias to m if moving backwards so that we snap consistently at exact integer coords in
    // the logical infinite image. This is tested by mirror_tile GM. Note that all values
    // that have a non-zero bias applied are > 0.
    U32 biasInUlps = trunc_(s);
    return sk_bit_cast<F>(sk_bit_cast<U32>(m) + (*ctx).mirrorBiasDir*biasInUlps);
}
// Tile x or y to [0,limit) == [0,limit - 1 ulp] (think, sampling from images).
// The gather stages will hard clamp the output of these stages to [0,limit)...
// we just need to do the basic repeat or mirroring.
SI void repeat_x_k(const SkRasterPipelineContexts::TileCtx* ctx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = exclusive_repeat(r, ctx);
}
SI void repeat_y_k(const SkRasterPipelineContexts::TileCtx* ctx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    g = exclusive_repeat(g, ctx);
}
SI void mirror_x_k(const SkRasterPipelineContexts::TileCtx* ctx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = exclusive_mirror(r, ctx);
}
SI void mirror_y_k(const SkRasterPipelineContexts::TileCtx* ctx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    g = exclusive_mirror(g, ctx);
}

SI void clamp_x_1_k(NoCtx, const size_t dx, const size_t dy,
                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { r = clamp_01_(r); }
SI void repeat_x_1_k(NoCtx, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { r = clamp_01_(r - floor_(r)); }
SI void mirror_x_1_k(NoCtx, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { r = clamp_01_(abs_( (r-1.0f) - two(floor_((r-1.0f)*0.5f)) - 1.0f )); }

SI void clamp_x_and_y_k(const SkRasterPipelineContexts::CoordClampCtx* ctx,
                        const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = min((*ctx).max_x, max((*ctx).min_x, r));
    g = min((*ctx).max_y, max((*ctx).min_y, g));
}

// Decal stores a 32bit mask after checking the coordinate (x and/or y) against its domain:
//      mask == 0x00000000 if the coordinate(s) are out of bounds
//      mask == 0xFFFFFFFF if the coordinate(s) are in bounds
// After the gather stage, the r,g,b,a values are AND'd with this mask, setting them to 0
// if either of the coordinates were out of bounds.

SI void decal_x_k(SkRasterPipelineContexts::DecalTileCtx* ctx, const size_t dx, const size_t dy,
                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    float w = (*ctx).limit_x;
    float e = (*ctx).inclusiveEdge_x;
    I32 cond = ((0 < r) & (r < w)) | (r == e);
    sk_unaligned_store((*ctx).mask, cond_to_mask(cond));
}
SI void decal_y_k(SkRasterPipelineContexts::DecalTileCtx* ctx, const size_t dx, const size_t dy,
                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    float h = (*ctx).limit_y;
    float e = (*ctx).inclusiveEdge_y;
    I32 cond = ((0 < g) & (g < h)) | (g == e);
    sk_unaligned_store((*ctx).mask, cond_to_mask(cond));
}
SI void decal_x_and_y_k(SkRasterPipelineContexts::DecalTileCtx* ctx,
                        const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    float w = (*ctx).limit_x;
    float h = (*ctx).limit_y;
    float ex = (*ctx).inclusiveEdge_x;
    float ey = (*ctx).inclusiveEdge_y;
    I32 cond = (((0 < r) & (r < w)) | (r == ex))
              & (((0 < g) & (g < h)) | (g == ey));
    sk_unaligned_store((*ctx).mask, cond_to_mask(cond));
}
SI void check_decal_mask_k(SkRasterPipelineContexts::DecalTileCtx* ctx,
                           const size_t dx, const size_t dy,
                           F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    U32 mask = sk_unaligned_load<U32>((*ctx).mask);
    r = sk_bit_cast<F>(sk_bit_cast<U32>(r) & mask);
    g = sk_bit_cast<F>(sk_bit_cast<U32>(g) & mask);
    b = sk_bit_cast<F>(sk_bit_cast<U32>(b) & mask);
    a = sk_bit_cast<F>(sk_bit_cast<U32>(a) & mask);
}

SI void alpha_to_gray_k(NoCtx, const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = g = b = a;
    a = F1;
}
SI void alpha_to_gray_dst_k(NoCtx, const size_t dx, const size_t dy,
                            F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    dr = dg = db = da;
    da = F1;
}
SI void alpha_to_red_k(NoCtx, const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = a;
    a = F1;
}
SI void alpha_to_red_dst_k(NoCtx, const size_t dx, const size_t dy,
                           F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    dr = da;
    da = F1;
}

SI void bt709_luminance_or_luma_to_alpha_k(NoCtx, const size_t dx, const size_t dy,
                                           F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    a = r*0.2126f + g*0.7152f + b*0.0722f;
    r = g = b = F0;
}
SI void bt709_luminance_or_luma_to_rgb_k(NoCtx, const size_t dx, const size_t dy,
                                         F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = g = b = r*0.2126f + g*0.7152f + b*0.0722f;
}

SI void matrix_translate_k(const float* m, const size_t dx, const size_t dy,
                           F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r += m[0];
    g += m[1];
}
SI void matrix_scale_translate_k(const float* m, const size_t dx, const size_t dy,
                                 F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = mad(r,m[0], m[2]);
    g = mad(g,m[1], m[3]);
}
SI void matrix_2x3_k(const float* m, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F R = mad(r,m[0], mad(g,m[1], m[2]));
    F G = mad(r,m[3], mad(g,m[4], m[5]));
    r = R;
    g = G;
}
SI void matrix_3x3_k(const float* m, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F R = mad(r,m[0], mad(g,m[3], b*m[6]));
    F G = mad(r,m[1], mad(g,m[4], b*m[7]));
    F B = mad(r,m[2], mad(g,m[5], b*m[8]));
    r = R;
    g = G;
    b = B;
}
SI void matrix_3x4_k(const float* m, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F R = mad(r,m[0], mad(g,m[3], mad(b,m[6], m[ 9])));
    F G = mad(r,m[1], mad(g,m[4], mad(b,m[7], m[10])));
    F B = mad(r,m[2], mad(g,m[5], mad(b,m[8], m[11])));
    r = R;
    g = G;
    b = B;
}
SI void matrix_4x5_k(const float* m, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F R = mad(r,m[ 0], mad(g,m[ 1], mad(b,m[ 2], mad(a,m[ 3], m[ 4]))));
    F G = mad(r,m[ 5], mad(g,m[ 6], mad(b,m[ 7], mad(a,m[ 8], m[ 9]))));
    F B = mad(r,m[10], mad(g,m[11], mad(b,m[12], mad(a,m[13], m[14]))));
    F A = mad(r,m[15], mad(g,m[16], mad(b,m[17], mad(a,m[18], m[19]))));
    r = R;
    g = G;
    b = B;
    a = A;
}
SI void matrix_4x3_k(const float* m, const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F X = r;
    F Y = g;

    r = mad(X, m[0], mad(Y, m[4], m[ 8]));
    g = mad(X, m[1], mad(Y, m[5], m[ 9]));
    b = mad(X, m[2], mad(Y, m[6], m[10]));
    a = mad(X, m[3], mad(Y, m[7], m[11]));
}
SI void matrix_perspective_k(const float* m, const size_t dx, const size_t dy,
                             F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    // N.B. Unlike the other matrix_ stages, this matrix is row-major.
    F R = mad(r,m[0], mad(g,m[1], m[2]));
    F G = mad(r,m[3], mad(g,m[4], m[5]));
    F Z = mad(r,m[6], mad(g,m[7], m[8]));
    r = R * rcp_precise(Z);
    g = G * rcp_precise(Z);
}

SI void gradient_lookup(const SkRasterPipelineContexts::GradientCtx* c, U32 idx, F t,
                        F* r, F* g, F* b, F* a) {
    F fr;
    F br;
    F fg;
    F bg;
    F fb;
    F bb;
    F fa;
    F ba;
#if defined(SKRP_CPU_AVX2)
    if ((*c).stopCount <=8) {
        fr = _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).factors[0]), bit_cast_lanes<__m256i>(idx));
        br = _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).biases[0]), bit_cast_lanes<__m256i>(idx));
        fg = _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).factors[1]), bit_cast_lanes<__m256i>(idx));
        bg = _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).biases[1]), bit_cast_lanes<__m256i>(idx));
        fb = _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).factors[2]), bit_cast_lanes<__m256i>(idx));
        bb = _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).biases[2]), bit_cast_lanes<__m256i>(idx));
        fa = _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).factors[3]), bit_cast_lanes<__m256i>(idx));
        ba = _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).biases[3]), bit_cast_lanes<__m256i>(idx));
    } else
#elif defined(SKRP_CPU_LASX)
    if ((*c).stopCount <= 8) {
        fr = (__m256)__lasx_xvperm_w(__lasx_xvld((*c).factors[0], 0), idx);
        br = (__m256)__lasx_xvperm_w(__lasx_xvld((*c).biases[0], 0), idx);
        fg = (__m256)__lasx_xvperm_w(__lasx_xvld((*c).factors[1], 0), idx);
        bg = (__m256)__lasx_xvperm_w(__lasx_xvld((*c).biases[1], 0), idx);
        fb = (__m256)__lasx_xvperm_w(__lasx_xvld((*c).factors[2], 0), idx);
        bb = (__m256)__lasx_xvperm_w(__lasx_xvld((*c).biases[2], 0), idx);
        fa = (__m256)__lasx_xvperm_w(__lasx_xvld((*c).factors[3], 0), idx);
        ba = (__m256)__lasx_xvperm_w(__lasx_xvld((*c).biases[3], 0), idx);
    } else
#elif defined(SKRP_CPU_LSX)
    if ((*c).stopCount <= 4) {
        __m128i zero = __lsx_vldi(0);
        fr = (__m128)__lsx_vshuf_w(idx, zero, __lsx_vld((*c).factors[0], 0));
        br = (__m128)__lsx_vshuf_w(idx, zero, __lsx_vld((*c).biases[0], 0));
        fg = (__m128)__lsx_vshuf_w(idx, zero, __lsx_vld((*c).factors[1], 0));
        bg = (__m128)__lsx_vshuf_w(idx, zero, __lsx_vld((*c).biases[1], 0));
        fb = (__m128)__lsx_vshuf_w(idx, zero, __lsx_vld((*c).factors[2], 0));
        bb = (__m128)__lsx_vshuf_w(idx, zero, __lsx_vld((*c).biases[2], 0));
        fa = (__m128)__lsx_vshuf_w(idx, zero, __lsx_vld((*c).factors[3], 0));
        ba = (__m128)__lsx_vshuf_w(idx, zero, __lsx_vld((*c).biases[3], 0));
    } else
#endif
    {
#if defined(SKRP_CPU_LSX)
        // This can reduce some vpickve2gr instructions.
        int i0 = __lsx_vpickve2gr_w(idx, 0);
        int i1 = __lsx_vpickve2gr_w(idx, 1);
        int i2 = __lsx_vpickve2gr_w(idx, 2);
        int i3 = __lsx_vpickve2gr_w(idx, 3);
        fr = gather((int *)(*c).factors[0], i0, i1, i2, i3);
        br = gather((int *)(*c).biases[0], i0, i1, i2, i3);
        fg = gather((int *)(*c).factors[1], i0, i1, i2, i3);
        bg = gather((int *)(*c).biases[1], i0, i1, i2, i3);
        fb = gather((int *)(*c).factors[2], i0, i1, i2, i3);
        bb = gather((int *)(*c).biases[2], i0, i1, i2, i3);
        fa = gather((int *)(*c).factors[3], i0, i1, i2, i3);
        ba = gather((int *)(*c).biases[3], i0, i1, i2, i3);
#else
        fr = gather((*c).factors[0], idx);
        br = gather((*c).biases[0], idx);
        fg = gather((*c).factors[1], idx);
        bg = gather((*c).biases[1], idx);
        fb = gather((*c).factors[2], idx);
        bb = gather((*c).biases[2], idx);
        fa = gather((*c).factors[3], idx);
        ba = gather((*c).biases[3], idx);
#endif
    }

    *r = mad(t, fr, br);
    *g = mad(t, fg, bg);
    *b = mad(t, fb, bb);
    *a = mad(t, fa, ba);
}

SI void evenly_spaced_gradient_k(const SkRasterPipelineContexts::GradientCtx* c,
                                 const size_t dx, const size_t dy,
                                 F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F t = r;
    U32 idx = trunc_(t * static_cast<float>((*c).stopCount-1));
    gradient_lookup(c, idx, t, &r, &g, &b, &a);
}

SI void gradient_k(const SkRasterPipelineContexts::GradientCtx* c, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F t = r;
    U32 idx = U32_(0);

    // N.B. The loop starts at 1 because idx 0 is the color to use before the first stop.
    for (size_t i = 1; i < (*c).stopCount; i++) {
        idx += bit_cast_lanes<U32>(if_then_else(t >= (*c).ts[i], I32_(1), I32_(0)));
    }

    gradient_lookup(c, idx, t, &r, &g, &b, &a);
}

SI void evenly_spaced_2_stop_gradient_k(const SkRasterPipelineContexts::EvenlySpaced2StopGradientCtx* c,
                                        const size_t dx, const size_t dy,
                                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F t = r;
    r = mad(t, (*c).factor[0], (*c).bias[0]);
    g = mad(t, (*c).factor[1], (*c).bias[1]);
    b = mad(t, (*c).factor[2], (*c).bias[2]);
    a = mad(t, (*c).factor[3], (*c).bias[3]);
}

SI void xy_to_unit_angle_k(NoCtx, const size_t dx, const size_t dy,
                           F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F X = r;
    F Y = g;
    F xabs = abs_(X);
    F yabs = abs_(Y);

    F slope = min(xabs, yabs)/max(xabs, yabs);
    F s = slope * slope;

    // Use a 7th degree polynomial to approximate atan.
    // This was generated using sollya.gforge.inria.fr.
    // A float optimized polynomial was generated using the following command.
    // P1 = fpminimax((1/(2*Pi))*atan(x),[|1,3,5,7|],[|24...|],[2^(-40),1],relative);
    F phi = slope
             * (0.15912117063999176025390625f     + s
             * (-5.185396969318389892578125e-2f   + s
             * (2.476101927459239959716796875e-2f + s
             * (-7.0547382347285747528076171875e-3f))));

    phi = if_then_else(xabs < yabs, 1.0f/4.0f - phi, phi);
    phi = if_then_else(X < 0.0f   , 1.0f/2.0f - phi, phi);
    phi = if_then_else(Y < 0.0f   , 1.0f - phi     , phi);
    phi = if_then_else(phi != phi , 0.0f           , phi);  // Check for NaN.
    r = phi;
}

SI void xy_to_radius_k(NoCtx, const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F X2 = r * r;
    F Y2 = g * g;
    r = sqrt_(X2 + Y2);
}

// Please see https://skia.org/dev/design/conical for how our 2pt conical shader works.

SI void negate_x_k(NoCtx, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { r = -r; }

SI void xy_to_2pt_conical_strip_k(const SkRasterPipelineContexts::Conical2PtCtx* ctx,
                                  const size_t dx, const size_t dy,
                                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F x = r;
    F y = g;
    F& t = r;
    t = x + sqrt_((*ctx).fP0 - y*y); // (*ctx).fP0 = r0 * r0
}

SI void xy_to_2pt_conical_focal_on_circle_k(NoCtx, const size_t dx, const size_t dy,
                                            F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F x = r;
    F y = g;
    F& t = r;
    t = x + y*y / x; // (x^2 + y^2) / x
}

SI void xy_to_2pt_conical_well_behaved_k(const SkRasterPipelineContexts::Conical2PtCtx* ctx,
                                         const size_t dx, const size_t dy,
                                         F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F x = r;
    F y = g;
    F& t = r;
    t = sqrt_(x*x + y*y) - x * (*ctx).fP0; // (*ctx).fP0 = 1/r1
}

SI void xy_to_2pt_conical_greater_k(const SkRasterPipelineContexts::Conical2PtCtx* ctx,
                                    const size_t dx, const size_t dy,
                                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F x = r;
    F y = g;
    F& t = r;
    t = sqrt_(x*x - y*y) - x * (*ctx).fP0; // (*ctx).fP0 = 1/r1
}

SI void xy_to_2pt_conical_smaller_k(const SkRasterPipelineContexts::Conical2PtCtx* ctx,
                                    const size_t dx, const size_t dy,
                                    F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F x = r;
    F y = g;
    F& t = r;
    t = -sqrt_(x*x - y*y) - x * (*ctx).fP0; // (*ctx).fP0 = 1/r1
}

SI void alter_2pt_conical_compensate_focal_k(const SkRasterPipelineContexts::Conical2PtCtx* ctx,
                                             const size_t dx, const size_t dy,
                                             F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F& t = r;
    t = t + (*ctx).fP1; // (*ctx).fP1 = f
}

SI void alter_2pt_conical_unswap_k(NoCtx, const size_t dx, const size_t dy,
                                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F& t = r;
    t = 1 - t;
}

SI void mask_2pt_conical_nan_k(SkRasterPipelineContexts::Conical2PtCtx* c,
                               const size_t dx, const size_t dy,
                               F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F& t = r;
    I32 is_degenerate = (t != t); // NaN
    t = if_then_else(is_degenerate, F0, t);
    sk_unaligned_store(&(*c).fMask, cond_to_mask(!is_degenerate));
}

SI void mask_2pt_conical_degenerates_k(SkRasterPipelineContexts::Conical2PtCtx* c,
                                       const size_t dx, const size_t dy,
                                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F& t = r;
    I32 is_degenerate = (t <= 0) | (t != t);
    t = if_then_else(is_degenerate, F0, t);
    sk_unaligned_store(&(*c).fMask, cond_to_mask(!is_degenerate));
}

SI void apply_vector_mask_k(const uint32_t* ctx, const size_t dx, const size_t dy,
                            F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    const U32 mask = sk_unaligned_load<U32>(ctx);
    r = sk_bit_cast<F>(sk_bit_cast<U32>(r) & mask);
    g = sk_bit_cast<F>(sk_bit_cast<U32>(g) & mask);
    b = sk_bit_cast<F>(sk_bit_cast<U32>(b) & mask);
    a = sk_bit_cast<F>(sk_bit_cast<U32>(a) & mask);
}

SI void save_xy(F* r, F* g, SkRasterPipelineContexts::SamplerCtx* c) {
    // Whether bilinear or bicubic, all sample points are at the same fractional offset (fx,fy).
    // They're either the 4 corners of a logical 1x1 pixel or the 16 corners of a 3x3 grid
    // surrounding (x,y) at (0.5,0.5) off-center.
    F fx = fract(*r + 0.5f);
    F fy = fract(*g + 0.5f);

    // Samplers will need to load x and fx, or y and fy.
    sk_unaligned_store((*c).x,  *r);
    sk_unaligned_store((*c).y,  *g);
    sk_unaligned_store((*c).fx, fx);
    sk_unaligned_store((*c).fy, fy);
}

SI void accumulate_k(const SkRasterPipelineContexts::SamplerCtx* c,
                     const size_t dx, const size_t dy,
                     F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    // Bilinear and bicubic filters are both separable, so we produce independent contributions
    // from x and y, multiplying them together here to get each pixel's total scale factor.
    F scale = sk_unaligned_load<F>((*c).scalex)
               * sk_unaligned_load<F>((*c).scaley);
    dr = mad(scale, r, dr);
    dg = mad(scale, g, dg);
    db = mad(scale, b, db);
    da = mad(scale, a, da);
}

// In bilinear interpolation, the 4 pixels at +/- 0.5 offsets from the sample pixel center
// are combined in direct proportion to their area overlapping that logical query pixel.
// At positive offsets, the x-axis contribution to that rectangle is fx, or (1-fx) at negative x.
// The y-axis is symmetric.

template <int kScale>
SI void bilinear_x(SkRasterPipelineContexts::SamplerCtx* ctx, F* x) {
    *x = sk_unaligned_load<F>((*ctx).x) + (kScale * 0.5f);
    F fx = sk_unaligned_load<F>((*ctx).fx);

    F scalex;
    if (kScale == -1) { scalex = 1.0f - fx; }
    if (kScale == +1) { scalex =        fx; }
    sk_unaligned_store((*ctx).scalex, scalex);
}
template <int kScale>
SI void bilinear_y(SkRasterPipelineContexts::SamplerCtx* ctx, F* y) {
    *y = sk_unaligned_load<F>((*ctx).y) + (kScale * 0.5f);
    F fy = sk_unaligned_load<F>((*ctx).fy);

    F scaley;
    if (kScale == -1) { scaley = 1.0f - fy; }
    if (kScale == +1) { scaley =        fy; }
    sk_unaligned_store((*ctx).scaley, scaley);
}

SI void bilinear_setup_k(SkRasterPipelineContexts::SamplerCtx* ctx,
                         const size_t dx, const size_t dy,
                         F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    save_xy(&r, &g, ctx);
    // Init for accumulate
    dr = dg = db = da = F0;
}

SI void bilinear_nx_k(SkRasterPipelineContexts::SamplerCtx* ctx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { bilinear_x<-1>(ctx, &r); }
SI void bilinear_px_k(SkRasterPipelineContexts::SamplerCtx* ctx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { bilinear_x<+1>(ctx, &r); }
SI void bilinear_ny_k(SkRasterPipelineContexts::SamplerCtx* ctx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { bilinear_y<-1>(ctx, &g); }
SI void bilinear_py_k(SkRasterPipelineContexts::SamplerCtx* ctx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { bilinear_y<+1>(ctx, &g); }

// In bicubic interpolation, the 16 pixels and +/- 0.5 and +/- 1.5 offsets from the sample
// pixel center are combined with a non-uniform cubic filter, with higher values near the center.
//
// This helper computes the total weight along one axis (our bicubic filter is separable), given one
// column of the sampling matrix, and a fractional pixel offset. See SkCubicResampler for details.

SI F bicubic_wts(F t, float A, float B, float C, float D) {
    return mad(t, mad(t, mad(t, D, C), B), A);
}

template <int kScale>
SI void bicubic_x(SkRasterPipelineContexts::SamplerCtx* ctx, F* x) {
    *x = sk_unaligned_load<F>((*ctx).x) + (kScale * 0.5f);

    F scalex;
    if (kScale == -3) { scalex = sk_unaligned_load<F>((*ctx).wx[0]); }
    if (kScale == -1) { scalex = sk_unaligned_load<F>((*ctx).wx[1]); }
    if (kScale == +1) { scalex = sk_unaligned_load<F>((*ctx).wx[2]); }
    if (kScale == +3) { scalex = sk_unaligned_load<F>((*ctx).wx[3]); }
    sk_unaligned_store((*ctx).scalex, scalex);
}
template <int kScale>
SI void bicubic_y(SkRasterPipelineContexts::SamplerCtx* ctx, F* y) {
    *y = sk_unaligned_load<F>((*ctx).y) + (kScale * 0.5f);

    F scaley;
    if (kScale == -3) { scaley = sk_unaligned_load<F>((*ctx).wy[0]); }
    if (kScale == -1) { scaley = sk_unaligned_load<F>((*ctx).wy[1]); }
    if (kScale == +1) { scaley = sk_unaligned_load<F>((*ctx).wy[2]); }
    if (kScale == +3) { scaley = sk_unaligned_load<F>((*ctx).wy[3]); }
    sk_unaligned_store((*ctx).scaley, scaley);
}

SI void bicubic_setup_k(SkRasterPipelineContexts::SamplerCtx* ctx, const size_t dx, const size_t dy,
                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    save_xy(&r, &g, ctx);

    const float* w = (*ctx).weights;

    F fx = sk_unaligned_load<F>((*ctx).fx);
    sk_unaligned_store((*ctx).wx[0], bicubic_wts(fx, w[0], w[4], w[ 8], w[12]));
    sk_unaligned_store((*ctx).wx[1], bicubic_wts(fx, w[1], w[5], w[ 9], w[13]));
    sk_unaligned_store((*ctx).wx[2], bicubic_wts(fx, w[2], w[6], w[10], w[14]));
    sk_unaligned_store((*ctx).wx[3], bicubic_wts(fx, w[3], w[7], w[11], w[15]));

    F fy = sk_unaligned_load<F>((*ctx).fy);
    sk_unaligned_store((*ctx).wy[0], bicubic_wts(fy, w[0], w[4], w[ 8], w[12]));
    sk_unaligned_store((*ctx).wy[1], bicubic_wts(fy, w[1], w[5], w[ 9], w[13]));
    sk_unaligned_store((*ctx).wy[2], bicubic_wts(fy, w[2], w[6], w[10], w[14]));
    sk_unaligned_store((*ctx).wy[3], bicubic_wts(fy, w[3], w[7], w[11], w[15]));

    // Init for accumulate
    dr = dg = db = da = F0;
}

SI void bicubic_n3x_k(SkRasterPipelineContexts::SamplerCtx* ctx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { bicubic_x<-3>(ctx, &r); }
SI void bicubic_n1x_k(SkRasterPipelineContexts::SamplerCtx* ctx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { bicubic_x<-1>(ctx, &r); }
SI void bicubic_p1x_k(SkRasterPipelineContexts::SamplerCtx* ctx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { bicubic_x<+1>(ctx, &r); }
SI void bicubic_p3x_k(SkRasterPipelineContexts::SamplerCtx* ctx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { bicubic_x<+3>(ctx, &r); }

SI void bicubic_n3y_k(SkRasterPipelineContexts::SamplerCtx* ctx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { bicubic_y<-3>(ctx, &g); }
SI void bicubic_n1y_k(SkRasterPipelineContexts::SamplerCtx* ctx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { bicubic_y<-1>(ctx, &g); }
SI void bicubic_p1y_k(SkRasterPipelineContexts::SamplerCtx* ctx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { bicubic_y<+1>(ctx, &g); }
SI void bicubic_p3y_k(SkRasterPipelineContexts::SamplerCtx* ctx, const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) { bicubic_y<+3>(ctx, &g); }

SI F compute_perlin_vector(U32 sample, F x, F y) {
    // We're relying on the packing of uint16s within a uint32, which will vary based on endianness.
#ifdef SK_CPU_BENDIAN
    U32 sampleLo = sample >> 16;
    U32 sampleHi = sample & 0xFFFF;
#else
    U32 sampleLo = sample & 0xFFFF;
    U32 sampleHi = sample >> 16;
#endif

    // Convert 32-bit sample value into two floats in the [-1..1] range.
    F vecX = mad(cast(sampleLo), 2.0f / 65535.0f, -1.0f);
    F vecY = mad(cast(sampleHi), 2.0f / 65535.0f, -1.0f);

    // Return the dot of the sample and the passed-in vector.
    return mad(vecX,  x,
               vecY * y);
}

SI void perlin_noise_k(const SkRasterPipelineContexts::PerlinNoiseCtx* ctx,
                       const size_t dx, const size_t dy,
                       F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F noiseVecX = (r + 0.5) * (*ctx).baseFrequencyX;
    F noiseVecY = (g + 0.5) * (*ctx).baseFrequencyY;
    r = g = b = a = F0;
    F stitchDataX = F_((*ctx).stitchDataInX);
    F stitchDataY = F_((*ctx).stitchDataInY);
    F ratio = F1;

    for (int octave = 0; octave < (*ctx).numOctaves; ++octave) {
        // Calculate noise coordinates. (Roughly $noise_helper in Graphite)
        F floorValX = floor_(noiseVecX);
        F floorValY = floor_(noiseVecY);
        F  ceilValX = floorValX + 1.0f;
        F  ceilValY = floorValY + 1.0f;
        F fractValX = noiseVecX - floorValX;
        F fractValY = noiseVecY - floorValY;

        if ((*ctx).stitching) {
            // If we are stitching, wrap the coordinates to the stitch position.
            floorValX -= sk_bit_cast<F>(cond_to_mask(floorValX >= stitchDataX) &
                                        sk_bit_cast<I32>(stitchDataX));
            floorValY -= sk_bit_cast<F>(cond_to_mask(floorValY >= stitchDataY) &
                                        sk_bit_cast<I32>(stitchDataY));
            ceilValX -= sk_bit_cast<F>(cond_to_mask(ceilValX >= stitchDataX) &
                                       sk_bit_cast<I32>(stitchDataX));
            ceilValY -= sk_bit_cast<F>(cond_to_mask(ceilValY >= stitchDataY) &
                                       sk_bit_cast<I32>(stitchDataY));
        }

        U32 latticeLookup = bit_cast_lanes<U32>((iround(floorValX))) & 0xFF;
        F latticeIdxX = cast(expand(gather((*ctx).latticeSelector, latticeLookup)));
        latticeLookup = bit_cast_lanes<U32>((iround(ceilValX))) & 0xFF;
        F latticeIdxY = cast(expand(gather((*ctx).latticeSelector, latticeLookup)));

        U32 b00 = bit_cast_lanes<U32>((iround(latticeIdxX + floorValY))) & 0xFF;
        U32 b10 = bit_cast_lanes<U32>((iround(latticeIdxY + floorValY))) & 0xFF;
        U32 b01 = bit_cast_lanes<U32>((iround(latticeIdxX + ceilValY))) & 0xFF;
        U32 b11 = bit_cast_lanes<U32>((iround(latticeIdxY + ceilValY))) & 0xFF;

        // Calculate noise colors. (Roughly $noise_function in Graphite)
        // Apply Hermite interpolation to the fractional value.
        F smoothX = fractValX * fractValX * (3.0f - 2.0f * fractValX);
        F smoothY = fractValY * fractValY * (3.0f - 2.0f * fractValY);

        F color[4];
        const uint32_t* channelNoiseData = reinterpret_cast<const uint32_t*>((*ctx).noiseData);
        for (int channel = 0; channel < 4; ++channel) {
            U32 sample00 = gather(channelNoiseData, b00);
            U32 sample10 = gather(channelNoiseData, b10);
            U32 sample01 = gather(channelNoiseData, b01);
            U32 sample11 = gather(channelNoiseData, b11);
            channelNoiseData += 256;

            F u = compute_perlin_vector(sample00, fractValX,        fractValY);
            F v = compute_perlin_vector(sample10, fractValX - 1.0f, fractValY);
            F A = lerp(u, v, smoothX);

              u = compute_perlin_vector(sample01, fractValX,        fractValY - 1.0f);
              v = compute_perlin_vector(sample11, fractValX - 1.0f, fractValY - 1.0f);
            F B = lerp(u, v, smoothX);

            color[channel] = lerp(A, B, smoothY);
        }

        if ((*ctx).noiseType != SkPerlinNoiseShaderType::kFractalNoise) {
            // For kTurbulence the result is: abs(noise[-1,1])
            color[0] = abs_(color[0]);
            color[1] = abs_(color[1]);
            color[2] = abs_(color[2]);
            color[3] = abs_(color[3]);
        }

        r = mad(color[0], ratio, r);
        g = mad(color[1], ratio, g);
        b = mad(color[2], ratio, b);
        a = mad(color[3], ratio, a);

        // Scale inputs for the next round.
        noiseVecX *= 2.0f;
        noiseVecY *= 2.0f;
        stitchDataX *= 2.0f;
        stitchDataY *= 2.0f;
        ratio *= 0.5f;
    }

    if ((*ctx).noiseType == SkPerlinNoiseShaderType::kFractalNoise) {
        // For kFractalNoise the result is: noise[-1,1] * 0.5 + 0.5
        r = mad(r, 0.5f, 0.5f);
        g = mad(g, 0.5f, 0.5f);
        b = mad(b, 0.5f, 0.5f);
        a = mad(a, 0.5f, 0.5f);
    }

    r = clamp_01_(r) * a;
    g = clamp_01_(g) * a;
    b = clamp_01_(b) * a;
    a = clamp_01_(a);
}

SI void mipmap_linear_init_k(SkRasterPipelineContexts::MipmapCtx* ctx,
                             const size_t dx, const size_t dy,
                             F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    sk_unaligned_store((*ctx).x, r);
    sk_unaligned_store((*ctx).y, g);
}

SI void mipmap_linear_update_k(SkRasterPipelineContexts::MipmapCtx* ctx,
                               const size_t dx, const size_t dy,
                               F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    sk_unaligned_store((*ctx).r, r);
    sk_unaligned_store((*ctx).g, g);
    sk_unaligned_store((*ctx).b, b);
    sk_unaligned_store((*ctx).a, a);

    r = sk_unaligned_load<F>((*ctx).x) * (*ctx).scaleX;
    g = sk_unaligned_load<F>((*ctx).y) * (*ctx).scaleY;
}

SI void mipmap_linear_finish_k(const SkRasterPipelineContexts::MipmapCtx* ctx,
                               const size_t dx, const size_t dy,
                               F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    r = lerp(sk_unaligned_load<F>((*ctx).r), r, F_((*ctx).lowerWeight));
    g = lerp(sk_unaligned_load<F>((*ctx).g), g, F_((*ctx).lowerWeight));
    b = lerp(sk_unaligned_load<F>((*ctx).b), b, F_((*ctx).lowerWeight));
    a = lerp(sk_unaligned_load<F>((*ctx).a), a, F_((*ctx).lowerWeight));
}

SI void callback_k(SkRasterPipelineContexts::CallbackCtx* c, const size_t dx, const size_t dy,
                   F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    store4((*c).rgba, r,g,b,a);
    (*c).fn(c, N);
    load4((*c).read_from, &r,&g,&b,&a);
}

SI void gauss_a_to_rgba_k(NoCtx, const size_t dx, const size_t dy,
                          F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    // x = 1 - x;
    // exp(-x * x * 4) - 0.018f;
    // ... now approximate with quartic
    //
    const float c4 = -2.26661229133605957031f;
    const float c3 = 2.89795351028442382812f;
    const float c2 = 0.21345567703247070312f;
    const float c1 = 0.15489584207534790039f;
    const float c0 = 0.00030726194381713867f;
    a = mad(a, mad(a, mad(a, mad(a, c4, c3), c2), c1), c0);
    r = a;
    g = a;
    b = a;
}

SI void bilerp_clamp_large(const SkRasterPipelineContexts::GatherCtx* ctx,
                           F* r, F* g, F* b, F* a) {
    // (cx,cy) are the center of our sample.
    F cx = *r;
    F cy = *g;

    // All sample points are at the same fractional offset (fx,fy).
    // They're the 4 corners of a logical 1x1 pixel surrounding (x,y) at (0.5,0.5) offsets.
    F fx = fract(cx + 0.5f);
    F fy = fract(cy + 0.5f);

    // We'll accumulate the color of all four samples into {r,g,b,a} directly.
    *r = *g = *b = *a = F0;

    for (float py = -0.5f; py <= +0.5f; py += 1.0f)
    for (float px = -0.5f; px <= +0.5f; px += 1.0f) {
        // (x,y) are the coordinates of this sample point.
        F x = cx + px;
        F y = cy + py;

        // ix_and_ptr() will clamp to the image's bounds for us.
        const uint32_t* ptr;
        U32 ix = ix_and_ptr(&ptr, ctx, x,y);

        F sr;
        F sg;
        F sb;
        F sa;
        from_8888(gather_unaligned(ptr, ix), &sr,&sg,&sb,&sa);

        // In bilinear interpolation, the 4 pixels at +/- 0.5 offsets from the sample pixel center
        // are combined in direct proportion to their area overlapping that logical query pixel.
        // At positive offsets, the x-axis contribution to that rectangle is fx,
        // or (1-fx) at negative x.  Same deal for y.
        F sx = (px > 0) ? fx : 1.0f - fx;
        F sy = (py > 0) ? fy : 1.0f - fy;
        F area = sx * sy;

        *r += sr * area;
        *g += sg * area;
        *b += sb * area;
        *a += sa * area;
    }
}

// A specialized fused image shader for clamp-x, clamp-y, non-sRGB sampling.
SI void bilerp_clamp_8888_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                            const size_t dx, const size_t dy,
                            F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    bilerp_clamp_large(ctx, &r, &g, &b, &a);
}

// A specialized fused image shader for clamp-x, clamp-y, non-sRGB sampling.
// This version exists to allow a shader to force high precision.
SI void bilerp_clamp_8888_force_highp_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                                        const size_t dx, const size_t dy,
                                        F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    bilerp_clamp_large(ctx, &r, &g, &b, &a);
}

// A specialized fused image shader for clamp-x, clamp-y, non-sRGB sampling.
SI void bicubic_clamp_8888_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                             const size_t dx, const size_t dy,
                             F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    // (cx,cy) are the center of our sample.
    F cx = r;
    F cy = g;

    // All sample points are at the same fractional offset (fx,fy).
    // They're the 4 corners of a logical 1x1 pixel surrounding (x,y) at (0.5,0.5) offsets.
    F fx = fract(cx + 0.5f);
    F fy = fract(cy + 0.5f);

    // We'll accumulate the color of all four samples into {r,g,b,a} directly.
    r = g = b = a = F0;

    const float* w = (*ctx).weights;
    const F scaley[4] = {bicubic_wts(fy, w[0], w[4], w[ 8], w[12]),
                         bicubic_wts(fy, w[1], w[5], w[ 9], w[13]),
                         bicubic_wts(fy, w[2], w[6], w[10], w[14]),
                         bicubic_wts(fy, w[3], w[7], w[11], w[15])};
    const F scalex[4] = {bicubic_wts(fx, w[0], w[4], w[ 8], w[12]),
                         bicubic_wts(fx, w[1], w[5], w[ 9], w[13]),
                         bicubic_wts(fx, w[2], w[6], w[10], w[14]),
                         bicubic_wts(fx, w[3], w[7], w[11], w[15])};

    F sample_y = cy - 1.5f;
    for (int yy = 0; yy <= 3; ++yy) {
        F sample_x = cx - 1.5f;
        for (int xx = 0; xx <= 3; ++xx) {
            F scale = scalex[xx] * scaley[yy];

            // ix_and_ptr() will clamp to the image's bounds for us.
            const uint32_t* ptr;
            U32 ix = ix_and_ptr(&ptr, ctx, sample_x, sample_y);

            F sr;
            F sg;
            F sb;
            F sa;
            from_8888(gather_unaligned(ptr, ix), &sr,&sg,&sb,&sa);

            r = mad(scale, sr, r);
            g = mad(scale, sg, g);
            b = mad(scale, sb, b);
            a = mad(scale, sa, a);

            sample_x += 1;
        }
        sample_y += 1;
    }
}

// ~~~~~~ skgpu::Swizzle stage ~~~~~~ //

SI void swizzle_k(void* ctx, const size_t dx, const size_t dy,
                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    F ir = r;
    F ig = g;
    F ib = b;
    F ia = a;
    F* o[] = {&r, &g, &b, &a};
    char swiz[4];
    memcpy(swiz, &ctx, sizeof(swiz));

    for (int i = 0; i < 4; ++i) {
        switch (swiz[i]) {
            case 'r': *o[i] = ir; break;
            case 'g': *o[i] = ig; break;
            case 'b': *o[i] = ib; break;
            case 'a': *o[i] = ia; break;
            case '0': *o[i] = F0; break;
            case '1': *o[i] = F1; break;
            default:              break;
        }
    }
}

namespace lowp {
#if defined(SKRP_CPU_SCALAR) || defined(SK_ENABLE_OPTIMIZE_SIZE) || \
        defined(SK_DISABLE_LOWP_RASTER_PIPELINE)
    // We don't bother generating the lowp stages if we are:
    //   - ... in scalar mode (MSVC, old clang, etc...)
    //   - ... trying to save code size
    //   - ... explicitly disabling it. This is currently used by Flutter and Google3.
    //
    // Having nullptr for every stage will cause SkRasterPipeline to always use the highp stages.
    struct LowpStageEntry {
        SkRasterPipelineOp op;
        void (*stage)();
    };
    consteval std::array<LowpStageEntry, kNumRasterPipelineLowpOps> no_lowp_stages() {
        std::array<LowpStageEntry, kNumRasterPipelineLowpOps> entries = {};
        for (size_t i = 0; i < entries.size(); ++i) {
            entries[i] = LowpStageEntry{static_cast<SkRasterPipelineOp>(i), nullptr};
        }
        return entries;
    }
    static constexpr std::array<LowpStageEntry, kNumRasterPipelineLowpOps> kLowpStages =
            no_lowp_stages();
    static void (*just_return)(void) = nullptr;

    static void start_pipeline(size_t,size_t,size_t,size_t, SkRasterPipelineStage*,
                               SkSpan<SkRasterPipelineContexts::MemoryCtxPatch>) {}

#else  // We are compiling vector code with Clang... let's make some lowp stages!

#if defined(SKRP_CPU_ML4) || defined(SKRP_CPU_AVX2) || defined(SKRP_CPU_LASX)
    template <typename T> using V = Vec<16, T>;
#else
    template <typename T> using V = Vec<8, T>;
#endif

using U8  = V<uint8_t >;
using U16 = V<uint16_t>;
using I16 = V< int16_t>;
using I32 = V< int32_t>;
using U32 = V<uint32_t>;
using I64 = V< int64_t>;
using U64 = V<uint64_t>;
using F   = V<float   >;

static constexpr size_t N = sizeof(U16) / sizeof(uint16_t);

// Promotion helpers (for GCC)
#if defined(__clang__)
SI constexpr U16 U16_(uint16_t x) { return x; }
SI constexpr I32 I32_( int32_t x) { return x; }
SI constexpr U32 U32_(uint32_t x) { return x; }
SI constexpr F   F_  (float    x) { return x; }
#else
SI constexpr U16 U16_(uint16_t x) { return x + U16(); }
SI constexpr I32 I32_( int32_t x) { return x + I32(); }
SI constexpr U32 U32_(uint32_t x) { return x + U32(); }
SI constexpr F   F_  (float    x) { return x - F  (); }
#endif

static constexpr U16 U16_0   = U16_(0),
                     U16_255 = U16_(255);

// Once again, some platforms benefit from a restricted Stage calling convention,
// but others can pass tons and tons of registers and we're happy to exploit that.
// It's exactly the same decision and implementation strategy as the F stages above.
#if SKRP_NARROW_STAGES
    struct Params {
        size_t dx, dy;
        U16 dr,dg,db,da;
    };
    using Stage = void (ABI*)(Params*, SkRasterPipelineStage* program, U16 r, U16 g, U16 b, U16 a);
#else
    using Stage = void (ABI*)(SkRasterPipelineStage* program,
                              const size_t dx, const size_t dy,
                              U16  r, U16  g, U16  b, U16  a,
                              U16 dr, U16 dg, U16 db, U16 da);
#endif

static void start_pipeline(size_t x0,     size_t y0,
                           size_t xlimit, size_t ylimit,
                           SkRasterPipelineStage* program,
                           SkSpan<SkRasterPipelineContexts::MemoryCtxPatch> memoryCtxPatches) {
    Stage start = reinterpret_cast<Stage>((*program).fn);
    for (size_t dy = y0; dy < ylimit; dy++) {
    #if SKRP_NARROW_STAGES
        Params params = { x0,dy, U16_0,U16_0,U16_0,U16_0 };
        for (; params.dx + N <= xlimit; params.dx += N) {
            start(&params, program, U16_0,U16_0,U16_0,U16_0);
        }
        if (size_t tail = xlimit - params.dx) {
            patch_memory_contexts(memoryCtxPatches, params.dx, dy, tail);
            start(&params, program, U16_0,U16_0,U16_0,U16_0);
            restore_memory_contexts(memoryCtxPatches, params.dx, dy, tail);
        }
    #else
        size_t dx = x0;
        for (; dx + N <= xlimit; dx += N) {
            start(program, dx,dy, U16_0,U16_0,U16_0,U16_0, U16_0,U16_0,U16_0,U16_0);
        }
        if (size_t tail = xlimit - dx) {
            patch_memory_contexts(memoryCtxPatches, dx, dy, tail);
            start(program, dx,dy, U16_0,U16_0,U16_0,U16_0, U16_0,U16_0,U16_0,U16_0);
            restore_memory_contexts(memoryCtxPatches, dx, dy, tail);
        }
    #endif
    }
}

#if SKRP_NARROW_STAGES
    static void ABI just_return(Params*, SkRasterPipelineStage*, U16,U16,U16,U16) {}
#else
    static void ABI just_return(SkRasterPipelineStage*, size_t,size_t,
                                U16,U16,U16,U16, U16,U16,U16,U16) {}
#endif

// All stages use the same function call ABI to chain into each other, but there are three types:
//   GG: geometry in, geometry out  -- think, a matrix
//   GP: geometry in, pixels out.   -- think, a memory gather
//   PP: pixels in, pixels out.     -- think, a blend mode
//
// (Some stages ignore their inputs or produce no logical output.  That's perfectly fine.)
//
// Each type has its own kernel form and stage wrapper below, with (x,y) geometry and/or
// (r,g,b,a, dr,dg,db,da) pixel arguments as appropriate.
//
// Why does the LOWP version have 3 versions of a stage while HIGHP only has 1?
// We don't want to lose precision on the x and y coordinates, so we fuse the rg and ba
// registers before passing them in (and need to know if we have to split that super
// register or not).

// Each type has a kernel form and a stage wrapper, defined with the lowp stage table below.

/**
 * Helpers to to properly rounded division (by 255). The ideal answer we want to compute is slow,
 * thanks to a division by a non-power of two:
 *   [1]  (v + 127) / 255
 *
 * There is a two-step process that computes the correct answer for all inputs:
 *   [2]  (v + 128 + ((v + 128) >> 8)) >> 8
 *
 * There is also a single iteration approximation, but it's wrong (+-1) ~25% of the time:
 *   [3]  (v + 255) >> 8;
 *
 * We offer two different implementations here, depending on the requirements of the calling stage.
 */

/**
 * div255 favors speed over accuracy. It uses formula [2] on NEON (where we can compute it as fast
 * as [3]), and uses [3] elsewhere.
 */
SI U16 div255(U16 v) {
#if defined(SKRP_CPU_NEON)
    // With NEON we can compute [2] just as fast as [3], so let's be correct.
    // First we compute v + ((v+128)>>8), then one more round of (...+128)>>8 to finish up:
    return vrshrq_n_u16(vrsraq_n_u16(v, v, 8), 8);
#else
    // Otherwise, use [3], which is never wrong by more than 1:
    return (v+255)/256;
#endif
}

/**
 * div255_accurate guarantees the right answer on all platforms, at the expense of performance.
 */
SI U16 div255_accurate(U16 v) {
#if defined(SKRP_CPU_NEON)
    // Our NEON implementation of div255 is already correct for all inputs:
    return div255(v);
#else
    // This is [2] (the same formulation as NEON), but written without the benefit of intrinsics:
    v += 128;
    return (v+(v/256))/256;
#endif
}

SI U16 inv(U16 v) { return 255-v; }

SI U16 from_float(float f) { return U16_(f * 255.0f + 0.5f); }

SI U16 lerp(U16 from, U16 to, U16 t) { return div255( from*inv(t) + to*t ); }

template <typename D, typename S>
SI D cast(S src) {
    return __builtin_convertvector(src, D);
}

template <typename D, typename S>
SI void split(S v, D* lo, D* hi) {
    static_assert(2*sizeof(D) == sizeof(S), "");
    memcpy(lo, reinterpret_cast<const char*>(&v) + 0*sizeof(D), sizeof(D));
    memcpy(hi, reinterpret_cast<const char*>(&v) + 1*sizeof(D), sizeof(D));
}
template <typename D, typename S>
SI D join(S lo, S hi) {
    static_assert(sizeof(D) == 2*sizeof(S), "");
    D v;
    memcpy(reinterpret_cast<char*>(&v) + 0*sizeof(S), &lo, sizeof(S));
    memcpy(reinterpret_cast<char*>(&v) + 1*sizeof(S), &hi, sizeof(S));
    return v;
}

SI F if_then_else(I32 c, F t, F e) {
    return sk_bit_cast<F>( (sk_bit_cast<I32>(t) & c) | (sk_bit_cast<I32>(e) & ~c) );
}
SI F if_then_else(I32 c, F     t, float e) { return if_then_else(c,    t , F_(e)); }
SI F if_then_else(I32 c, float t, F     e) { return if_then_else(c, F_(t),    e ); }
SI I32 if_then_else(I32 c, I32 t, I32 e) {
    return (t & c) | (e & ~c);
}
SI U16 if_then_else(I16 c, U16 t, U16 e) {
    return (t & sk_bit_cast<U16>(c)) | (e & sk_bit_cast<U16>(~c));
}
SI U32 if_then_else(I32 c, U32 t, U32 e) {
    return (t & sk_bit_cast<U32>(c)) | (e & sk_bit_cast<U32>(~c));
}

SI F max(F x, F y) { return if_then_else(x < y, y, x); }
SI F min(F x, F y) { return if_then_else(x < y, x, y); }

SI I32 max(I32 x, I32 y) { return if_then_else(x < y, y, x); }
SI I32 min(I32 x, I32 y) { return if_then_else(x < y, x, y); }

SI U16 max(U16 x, U16 y) { return if_then_else(x < y, y, x); }
SI U16 min(U16 x, U16 y) { return if_then_else(x < y, x, y); }

#if defined(SKRP_CPU_ML4)
// This helps split the broadcast (copying the float to all lanes of the register)
// from the min/max operation which helps free up ports.
SI F max(F     a, float b) { return max(a, bit_cast_lanes<F>(_mm512_set1_ps(b))); }
SI F max(float a, F b) { return max(bit_cast_lanes<F>(_mm512_set1_ps(a)), b); }
SI F min(F     a, float b) { return min(a, bit_cast_lanes<F>(_mm512_set1_ps(b))); }
SI F min(float a, F b) { return min(bit_cast_lanes<F>(_mm512_set1_ps(a)), b); }

SI I32 max(I32     a, int32_t b) { return max(a, bit_cast_lanes<I32>(_mm512_set1_epi32(b))); }
SI I32 max(int32_t a, I32     b) { return max(bit_cast_lanes<I32>(_mm512_set1_epi32(a)), b); }
SI I32 min(I32     a, int32_t b) { return min(a, bit_cast_lanes<I32>(_mm512_set1_epi32(b))); }
SI I32 min(int32_t a, I32     b) { return min(bit_cast_lanes<I32>(_mm512_set1_epi32(a)), b); }
#else
SI F max(F a, float b) { return max(a, F_(b)); }
SI F max(float a, F b) { return max(F_(a), b); }
SI F min(F a, float b) { return min(a, F_(b)); }
SI F min(float a, F b) { return min(F_(a), b); }

SI I32 max(I32 a, int32_t b) { return max(a, I32_(b)); }
SI I32 max(int32_t a, I32 b) { return max(I32_(a), b); }
SI I32 min(I32 a, int32_t b) { return min(a, I32_(b)); }
SI I32 min(int32_t a, I32 b) { return min(I32_(a), b); }
#endif
SI U16 max(U16      a, uint16_t b) { return max(     a , U16_(b)); }
SI U16 max(uint16_t a, U16      b) { return max(U16_(a),      b ); }
SI U16 min(U16      a, uint16_t b) { return min(     a , U16_(b)); }
SI U16 min(uint16_t a, U16      b) { return min(U16_(a),      b ); }

// Using explicit hardware min/max instructions can provide a speedup when the compiler doesn't
// know to turn the bitmasking fromif_then_else into intrinsics.
// DO NOT globally replace the conditional (bitwise) min/max with these.
// In highly complex stages (like mirror_x_1 or gradient), intrinsics can cause regressions from
// port contention (e.g. creating a traffic jam on AVX-512 math execution ports).
#if defined(SKRP_CPU_ML4)
SI F max_intr(F x, F y) { return _mm512_max_ps(x, y); }
SI F min_intr(F x, F y) { return _mm512_min_ps(x, y); }
SI I32 max_intr(I32 x, I32 y) { return bit_cast_lanes<I32>(_mm512_max_epi32(bit_cast_lanes<__m512i>(x), bit_cast_lanes<__m512i>(y))); }
SI I32 min_intr(I32 x, I32 y) { return bit_cast_lanes<I32>(_mm512_min_epi32(bit_cast_lanes<__m512i>(x), bit_cast_lanes<__m512i>(y))); }
SI U16 max_intr(U16 x, U16 y) { return bit_cast_lanes<U16>(_mm256_max_epu16(bit_cast_lanes<__m256i>(x), bit_cast_lanes<__m256i>(y))); }
SI U16 min_intr(U16 x, U16 y) { return bit_cast_lanes<U16>(_mm256_min_epu16(bit_cast_lanes<__m256i>(x), bit_cast_lanes<__m256i>(y))); }
#elif defined(SKRP_CPU_AVX2)
SI F max_intr(F x, F y) {
    __m256 x_lo;
    __m256 x_hi;
    __m256 y_lo;
    __m256 y_hi;
    split(x, &x_lo, &x_hi);
    split(y, &y_lo, &y_hi);
    return join<F>(_mm256_max_ps(x_lo, y_lo), _mm256_max_ps(x_hi, y_hi));
}
SI F min_intr(F x, F y) {
    __m256 x_lo;
    __m256 x_hi;
    __m256 y_lo;
    __m256 y_hi;
    split(x, &x_lo, &x_hi);
    split(y, &y_lo, &y_hi);
    return join<F>(_mm256_min_ps(x_lo, y_lo), _mm256_min_ps(x_hi, y_hi));
}
SI I32 max_intr(I32 x, I32 y) {
    __m256i x_lo;
    __m256i x_hi;
    __m256i y_lo;
    __m256i y_hi;
    split(x, &x_lo, &x_hi);
    split(y, &y_lo, &y_hi);
    return join<I32>(_mm256_max_epi32(x_lo, y_lo), _mm256_max_epi32(x_hi, y_hi));
}
SI I32 min_intr(I32 x, I32 y) {
    __m256i x_lo;
    __m256i x_hi;
    __m256i y_lo;
    __m256i y_hi;
    split(x, &x_lo, &x_hi);
    split(y, &y_lo, &y_hi);
    return join<I32>(_mm256_min_epi32(x_lo, y_lo), _mm256_min_epi32(x_hi, y_hi));
}
SI U16 max_intr(U16 x, U16 y) { return bit_cast_lanes<U16>(_mm256_max_epu16(bit_cast_lanes<__m256i>(x), bit_cast_lanes<__m256i>(y))); }
SI U16 min_intr(U16 x, U16 y) { return bit_cast_lanes<U16>(_mm256_min_epu16(bit_cast_lanes<__m256i>(x), bit_cast_lanes<__m256i>(y))); }
#elif defined(SKRP_CPU_SSE2) || defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
SI F max_intr(F x, F y) {
    __m128 x_lo;
    __m128 x_hi;
    __m128 y_lo;
    __m128 y_hi;
    split(x, &x_lo, &x_hi);
    split(y, &y_lo, &y_hi);
    return join<F>(_mm_max_ps(x_lo, y_lo), _mm_max_ps(x_hi, y_hi));
}
SI F min_intr(F x, F y) {
    __m128 x_lo;
    __m128 x_hi;
    __m128 y_lo;
    __m128 y_hi;
    split(x, &x_lo, &x_hi);
    split(y, &y_lo, &y_hi);
    return join<F>(_mm_min_ps(x_lo, y_lo), _mm_min_ps(x_hi, y_hi));
}
#if defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
SI I32 max_intr(I32 x, I32 y) {
    __m128i x_lo;
    __m128i x_hi;
    __m128i y_lo;
    __m128i y_hi;
    split(x, &x_lo, &x_hi);
    split(y, &y_lo, &y_hi);
    return join<I32>(_mm_max_epi32(x_lo, y_lo), _mm_max_epi32(x_hi, y_hi));
}
SI I32 min_intr(I32 x, I32 y) {
    __m128i x_lo;
    __m128i x_hi;
    __m128i y_lo;
    __m128i y_hi;
    split(x, &x_lo, &x_hi);
    split(y, &y_lo, &y_hi);
    return join<I32>(_mm_min_epi32(x_lo, y_lo), _mm_min_epi32(x_hi, y_hi));
}
SI U16 max_intr(U16 x, U16 y) { return bit_cast_lanes<U16>(_mm_max_epu16(bit_cast_lanes<__m128i>(x), bit_cast_lanes<__m128i>(y))); }
SI U16 min_intr(U16 x, U16 y) { return bit_cast_lanes<U16>(_mm_min_epu16(bit_cast_lanes<__m128i>(x), bit_cast_lanes<__m128i>(y))); }
#else
SI I32 max_intr(I32 x, I32 y) { return max(x, y); }
SI I32 min_intr(I32 x, I32 y) { return min(x, y); }
SI U16 max_intr(U16 x, U16 y) { return max(x, y); }
SI U16 min_intr(U16 x, U16 y) { return min(x, y); }
#endif // defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
#else
// TODO(kjlubick) make sure NEON and other architectures are handling this well
SI F   max_intr(F x, F y) { return max(x, y); }
SI F   min_intr(F x, F y) { return min(x, y); }
SI I32 max_intr(I32 x, I32 y) { return max(x, y); }
SI I32 min_intr(I32 x, I32 y) { return min(x, y); }
SI U16 max_intr(U16 x, U16 y) { return max(x, y); }
SI U16 min_intr(U16 x, U16 y) { return min(x, y); }
#endif

#if defined(SKRP_CPU_ML4)
// This helps split the broadcast (copying the float to all lanes of the register)
// from the min/max operation which helps free up ports.
SI F max_intr(F     a, float b) { return max_intr(a, bit_cast_lanes<F>(_mm512_set1_ps(b))); }
SI F max_intr(float a, F b) { return max_intr(bit_cast_lanes<F>(_mm512_set1_ps(a)), b); }
SI F min_intr(F     a, float b) { return min_intr(a, bit_cast_lanes<F>(_mm512_set1_ps(b))); }
SI F min_intr(float a, F b) { return min_intr(bit_cast_lanes<F>(_mm512_set1_ps(a)), b); }

SI I32 max_intr(I32     a, int32_t b) { return max_intr(a, bit_cast_lanes<I32>(_mm512_set1_epi32(b))); }
SI I32 max_intr(int32_t a, I32     b) { return max_intr(bit_cast_lanes<I32>(_mm512_set1_epi32(a)), b); }
SI I32 min_intr(I32     a, int32_t b) { return min_intr(a, bit_cast_lanes<I32>(_mm512_set1_epi32(b))); }
SI I32 min_intr(int32_t a, I32     b) { return min_intr(bit_cast_lanes<I32>(_mm512_set1_epi32(a)), b); }
#else
SI F max_intr(F a, float b) { return max_intr(a, F_(b)); }
SI F max_intr(float a, F b) { return max_intr(F_(a), b); }
SI F min_intr(F a, float b) { return min_intr(a, F_(b)); }
SI F min_intr(float a, F b) { return min_intr(F_(a), b); }

SI I32 max_intr(I32 a, int32_t b) { return max_intr(a, I32_(b)); }
SI I32 max_intr(int32_t a, I32 b) { return max_intr(I32_(a), b); }
SI I32 min_intr(I32 a, int32_t b) { return min_intr(a, I32_(b)); }
SI I32 min_intr(int32_t a, I32 b) { return min_intr(I32_(a), b); }
#endif

SI U16 max_intr(U16      a, uint16_t b) { return max_intr(     a , U16_(b)); }
SI U16 max_intr(uint16_t a, U16      b) { return max_intr(U16_(a),      b ); }
SI U16 min_intr(U16      a, uint16_t b) { return min_intr(     a , U16_(b)); }
SI U16 min_intr(uint16_t a, U16      b) { return min_intr(U16_(a),      b ); }

SI F mad(F     f, F     m, F     a) { return a+f*m; }
SI F mad(F     f, F     m, float a) { return mad(   f ,    m , F_(a)); }
SI F mad(F     f, float m, F     a) { return mad(   f , F_(m),    a ); }
SI F mad(F     f, float m, float a) { return mad(   f , F_(m), F_(a)); }
SI F mad(float f, F     m, F     a) { return mad(F_(f),    m ,    a ); }
SI F mad(float f, F     m, float a) { return mad(F_(f),    m , F_(a)); }
SI F mad(float f, float m, F     a) { return mad(F_(f), F_(m),    a ); }

SI F nmad(F     f, F     m, F     a) { return a-f*m; }
SI F nmad(F     f, F     m, float a) { return nmad(   f ,    m , F_(a)); }
SI F nmad(F     f, float m, F     a) { return nmad(   f , F_(m),    a ); }
SI F nmad(F     f, float m, float a) { return nmad(   f , F_(m), F_(a)); }
SI F nmad(float f, F     m, F     a) { return nmad(F_(f),    m ,    a ); }
SI F nmad(float f, F     m, float a) { return nmad(F_(f),    m , F_(a)); }
SI F nmad(float f, float m, F     a) { return nmad(F_(f), F_(m),    a ); }

SI U32 trunc_(F x) { return bit_cast_lanes<U32>(cast<I32>(x)); }

// Use approximate instructions and one Newton-Raphson step to calculate 1/x.
SI F rcp_precise(F x) {
#if defined(SKRP_CPU_ML4)
    F e = _mm512_rcp14_ps(x);
    return _mm512_fnmadd_ps(x, e, _mm512_set1_ps(2.0f)) * e;
#elif defined(SKRP_CPU_AVX2)
    __m256 lo;
    __m256 hi;
    split(x, &lo,&hi);
    return join<F>(SK_OPTS_NS::rcp_precise(lo), SK_OPTS_NS::rcp_precise(hi));
#elif defined(SKRP_CPU_SSE2) || defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
    __m128 lo;
    __m128 hi;
    split(x, &lo,&hi);
    return join<F>(SK_OPTS_NS::rcp_precise(lo), SK_OPTS_NS::rcp_precise(hi));
#elif defined(SKRP_CPU_NEON)
    float32x4_t lo;
    float32x4_t hi;
    split(x, &lo,&hi);
    return join<F>(SK_OPTS_NS::rcp_precise(lo), SK_OPTS_NS::rcp_precise(hi));
#elif defined(SKRP_CPU_LASX)
    __m256 lo,hi;
    split(x, &lo,&hi);
    return join<F>(__lasx_xvfrecip_s(lo), __lasx_xvfrecip_s(hi));
#elif defined(SKRP_CPU_LSX)
    __m128 lo,hi;
    split(x, &lo,&hi);
    return join<F>(__lsx_vfrecip_s(lo), __lsx_vfrecip_s(hi));
#else
    return 1.0f / x;
#endif
}
SI F sqrt_(F x) {
#if defined(SKRP_CPU_ML4)
    return _mm512_sqrt_ps(x);
#elif defined(SKRP_CPU_AVX2)
    __m256 lo;
    __m256 hi;
    split(x, &lo,&hi);
    return join<F>(_mm256_sqrt_ps(lo), _mm256_sqrt_ps(hi));
#elif defined(SKRP_CPU_SSE2) || defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
    __m128 lo;
    __m128 hi;
    split(x, &lo,&hi);
    return join<F>(_mm_sqrt_ps(lo), _mm_sqrt_ps(hi));
#elif defined(SK_CPU_ARM64)
    float32x4_t lo;
    float32x4_t hi;
    split(x, &lo,&hi);
    return join<F>(vsqrtq_f32(lo), vsqrtq_f32(hi));
#elif defined(SKRP_CPU_NEON)
    auto sqrt = [](float32x4_t v) {
        float32x4_t est = vrsqrteq_f32(v);  // Estimate and two refinement steps for est = rsqrt(v).
        est *= vrsqrtsq_f32(v,est*est);
        est *= vrsqrtsq_f32(v,est*est);
        return v*est;                // sqrt(v) == v*rsqrt(v).
    };
    float32x4_t lo,hi;
    split(x, &lo,&hi);
    return join<F>(sqrt(lo), sqrt(hi));
#elif defined(SKRP_CPU_LASX)
    __m256 lo,hi;
    split(x, &lo,&hi);
    return join<F>(__lasx_xvfsqrt_s(lo), __lasx_xvfsqrt_s(hi));
#elif defined(SKRP_CPU_LSX)
    __m128 lo,hi;
    split(x, &lo,&hi);
    return join<F>(__lsx_vfsqrt_s(lo), __lsx_vfsqrt_s(hi));
#else
    return F{
        sqrtf(x[0]), sqrtf(x[1]), sqrtf(x[2]), sqrtf(x[3]),
        sqrtf(x[4]), sqrtf(x[5]), sqrtf(x[6]), sqrtf(x[7]),
    };
#endif
}

SI F floor_(F x) {
#if defined(SK_CPU_ARM64)
    float32x4_t lo;
    float32x4_t hi;
    split(x, &lo,&hi);
    return join<F>(vrndmq_f32(lo), vrndmq_f32(hi));
#elif defined(SKRP_CPU_ML4)
    return _mm512_floor_ps(x);
#elif defined(SKRP_CPU_AVX2)
    __m256 lo;
    __m256 hi;
    split(x, &lo,&hi);
    return join<F>(_mm256_floor_ps(lo), _mm256_floor_ps(hi));
#elif defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
    __m128 lo;
    __m128 hi;
    split(x, &lo,&hi);
    return join<F>(_mm_floor_ps(lo), _mm_floor_ps(hi));
#elif defined(SKRP_CPU_LASX)
    __m256 lo,hi;
    split(x, &lo,&hi);
    return join<F>(__lasx_xvfrintrm_s(lo), __lasx_xvfrintrm_s(hi));
#elif defined(SKRP_CPU_LSX)
    __m128 lo,hi;
    split(x, &lo,&hi);
    return join<F>(__lsx_vfrintrm_s(lo), __lsx_vfrintrm_s(hi));
#else
    F roundtrip = cast<F>(cast<I32>(x));
    return roundtrip - if_then_else(roundtrip > x, F_(1), F_(0));
#endif
}

// scaled_mult interprets a and b as number on [-1, 1) which are numbers in Q15 format. Functionally
// this multiply is:
//     (2 * a * b + (1 << 15)) >> 16
// The result is a number on [-1, 1).
// Note: on neon this is a saturating multiply while the others are not.
SI I16 scaled_mult(I16 a, I16 b) {
#if defined(SKRP_CPU_ML4)
    return bit_cast_lanes<I16>(_mm256_mulhrs_epi16(bit_cast_lanes<__m256i>(a), bit_cast_lanes<__m256i>(b)));
#elif defined(SKRP_CPU_AVX2)
    return bit_cast_lanes<I16>(_mm256_mulhrs_epi16(bit_cast_lanes<__m256i>(a), bit_cast_lanes<__m256i>(b)));
#elif defined(SKRP_CPU_SSE41) || defined(SKRP_CPU_AVX)
    return bit_cast_lanes<I16>(_mm_mulhrs_epi16(bit_cast_lanes<__m128i>(a), bit_cast_lanes<__m128i>(b)));
#elif defined(SK_CPU_ARM64)
    return vqrdmulhq_s16(a, b);
#elif defined(SKRP_CPU_NEON)
    return vqrdmulhq_s16(a, b);
#elif defined(SKRP_CPU_LASX)
    I16 res = __lasx_xvmuh_h(a, b);
    return __lasx_xvslli_h(res, 1);
#elif defined(SKRP_CPU_LSX)
    I16 res = __lsx_vmuh_h(a, b);
    return __lsx_vslli_h(res, 1);
#else
    const I32 roundingTerm = I32_(1 << 14);
    return cast<I16>((cast<I32>(a) * cast<I32>(b) + roundingTerm) >> 15);
#endif
}

// This sum is to support lerp where the result will always be a positive number. In general,
// a sum like this would require an additional bit, but because we know the range of the result
// we know that the extra bit will always be zero.
SI U16 constrained_add(I16 a, U16 b) {
    #if defined(SK_DEBUG)
        for (size_t i = 0; i < N; i++) {
            // Ensure that a + b is on the interval [0, UINT16_MAX]
            int ia = a[i],
                ib = b[i];
            // Use 65535 here because fuchsia's compiler evaluates UINT16_MAX - ib, which is
            // 65536U - ib, as an uint32_t instead of an int32_t. This was forcing ia to be
            // interpreted as an uint32_t.
            SkASSERT(-ib <= ia && ia <= 65535 - ib);
        }
    #endif
    return b + sk_bit_cast<U16>(a);
}

SI F fract(F x) { return x - floor_(x); }
SI F abs_(F x) { return sk_bit_cast<F>( sk_bit_cast<I32>(x) & 0x7fffffff ); }

// ~~~~~~ Basic / misc. stages ~~~~~~ //

SI void seed_shader_k(NoCtx, const size_t dx, const size_t dy, F& x, F& y) {
#if defined(SKRP_CPU_LSX)
    __m128 val1 = {0.5f, 1.5f, 2.5f, 3.5f};
    __m128 val2 = {4.5f, 5.5f, 6.5f, 7.5f};
    __m128 val3 = {0.5f, 0.5f, 0.5f, 0.5f};

    __m128i v_d = __lsx_vreplgr2vr_w(dx);

    __m128 f_d = __lsx_vffint_s_w(v_d);
    val1 = __lsx_vfadd_s(val1, f_d);
    val2 = __lsx_vfadd_s(val2, f_d);
    x = join<F>(val1, val2);

    v_d = __lsx_vreplgr2vr_w(dy);
    f_d = __lsx_vffint_s_w(v_d);
    val3 = __lsx_vfadd_s(val3, f_d);
    y = join<F>(val3, val3);
#else
    static constexpr float iota[] = {
        0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f,
        8.5f, 9.5f,10.5f,11.5f,12.5f,13.5f,14.5f,15.5f,
    };
    static_assert(std::size(iota) >= SkRasterPipelineContexts::kMaxStride);

    x = cast<F>(I32_(dx)) + sk_unaligned_load<F>(iota);
    y = cast<F>(I32_(dy)) + 0.5f;
#endif
}

SI void matrix_translate_k(const float* m, const size_t dx, const size_t dy, F& x, F& y) {
    x += m[0];
    y += m[1];
}
SI void matrix_scale_translate_k(const float* m, const size_t dx, const size_t dy, F& x, F& y) {
    x = mad(x,m[0], m[2]);
    y = mad(y,m[1], m[3]);
}
SI void matrix_2x3_k(const float* m, const size_t dx, const size_t dy, F& x, F& y) {
    F X = mad(x,m[0], mad(y,m[1], m[2]));
    F Y = mad(x,m[3], mad(y,m[4], m[5]));
    x = X;
    y = Y;
}
SI void matrix_perspective_k(const float* m, const size_t dx, const size_t dy, F& x, F& y) {
    // N.B. Unlike the other matrix_ stages, this matrix is row-major.
    F X = mad(x,m[0], mad(y,m[1], m[2]));
    F Y = mad(x,m[3], mad(y,m[4], m[5]));
    F Z = mad(x,m[6], mad(y,m[7], m[8]));
    x = X * rcp_precise(Z);
    y = Y * rcp_precise(Z);
}

SI void uniform_color_k(const SkRasterPipelineContexts::UniformColorCtx* c,
                        const size_t dx, const size_t dy,
                        U16& r, U16& g, U16& b, U16& a,
                        U16& dr, U16& dg, U16& db, U16& da) {
    r = U16_((*c).rgba[0]);
    g = U16_((*c).rgba[1]);
    b = U16_((*c).rgba[2]);
    a = U16_((*c).rgba[3]);
}
SI void uniform_color_dst_k(const SkRasterPipelineContexts::UniformColorCtx* c,
                            const size_t dx, const size_t dy,
                            U16& r, U16& g, U16& b, U16& a,
                            U16& dr, U16& dg, U16& db, U16& da) {
    dr = U16_((*c).rgba[0]);
    dg = U16_((*c).rgba[1]);
    db = U16_((*c).rgba[2]);
    da = U16_((*c).rgba[3]);
}
SI void black_color_k(NoCtx, const size_t dx, const size_t dy,
                      U16& r, U16& g, U16& b, U16& a,
                      U16& dr, U16& dg, U16& db, U16& da) { r = g = b =   U16_0; a = U16_255; }
SI void white_color_k(NoCtx, const size_t dx, const size_t dy,
                      U16& r, U16& g, U16& b, U16& a,
                      U16& dr, U16& dg, U16& db, U16& da) { r = g = b = U16_255; a = U16_255; }

SI void set_rgb_k(const float rgb[3], const size_t dx, const size_t dy,
                  U16& r, U16& g, U16& b, U16& a,
                  U16& dr, U16& dg, U16& db, U16& da) {
    r = from_float(rgb[0]);
    g = from_float(rgb[1]);
    b = from_float(rgb[2]);
}

// No need to clamp against 0 here (values are unsigned)
SI void clamp_01_k(NoCtx, const size_t dx, const size_t dy,
                   U16& r, U16& g, U16& b, U16& a,
                   U16& dr, U16& dg, U16& db, U16& da) {
    r = min(r, 255);
    g = min(g, 255);
    b = min(b, 255);
    a = min(a, 255);
}

SI void clamp_a_01_k(NoCtx, const size_t dx, const size_t dy,
                     U16& r, U16& g, U16& b, U16& a,
                     U16& dr, U16& dg, U16& db, U16& da) {
    a = min(a, 255);
}

SI void clamp_gamut_k(NoCtx, const size_t dx, const size_t dy,
                      U16& r, U16& g, U16& b, U16& a,
                      U16& dr, U16& dg, U16& db, U16& da) {
    a = min(a, 255);
    r = min(r, a);
    g = min(g, a);
    b = min(b, a);
}

SI void premul_k(NoCtx, const size_t dx, const size_t dy,
                 U16& r, U16& g, U16& b, U16& a,
                 U16& dr, U16& dg, U16& db, U16& da) {
    r = div255_accurate(r * a);
    g = div255_accurate(g * a);
    b = div255_accurate(b * a);
}
SI void premul_dst_k(NoCtx, const size_t dx, const size_t dy,
                     U16& r, U16& g, U16& b, U16& a,
                     U16& dr, U16& dg, U16& db, U16& da) {
    dr = div255_accurate(dr * da);
    dg = div255_accurate(dg * da);
    db = div255_accurate(db * da);
}

SI void force_opaque_k(NoCtx, const size_t dx, const size_t dy,
                       U16& r, U16& g, U16& b, U16& a,
                       U16& dr, U16& dg, U16& db, U16& da) {  a = U16_255; }
SI void force_opaque_dst_k(NoCtx, const size_t dx, const size_t dy,
                           U16& r, U16& g, U16& b, U16& a,
                           U16& dr, U16& dg, U16& db, U16& da) { da = U16_255; }

SI void swap_rb_k(NoCtx, const size_t dx, const size_t dy,
                  U16& r, U16& g, U16& b, U16& a,
                  U16& dr, U16& dg, U16& db, U16& da) {
    U16 tmp = r;
    r = b;
    b = tmp;
}
SI void swap_rb_dst_k(NoCtx, const size_t dx, const size_t dy,
                      U16& r, U16& g, U16& b, U16& a,
                      U16& dr, U16& dg, U16& db, U16& da) {
    U16 tmp = dr;
    dr = db;
    db = tmp;
}

SI void move_src_dst_k(NoCtx, const size_t dx, const size_t dy,
                       U16& r, U16& g, U16& b, U16& a,
                       U16& dr, U16& dg, U16& db, U16& da) {
    dr = r;
    dg = g;
    db = b;
    da = a;
}

SI void move_dst_src_k(NoCtx, const size_t dx, const size_t dy,
                       U16& r, U16& g, U16& b, U16& a,
                       U16& dr, U16& dg, U16& db, U16& da) {
    r = dr;
    g = dg;
    b = db;
    a = da;
}

SI void swap_src_dst_k(NoCtx, const size_t dx, const size_t dy,
                       U16& r, U16& g, U16& b, U16& a,
                       U16& dr, U16& dg, U16& db, U16& da) {
    std::swap(r, dr);
    std::swap(g, dg);
    std::swap(b, db);
    std::swap(a, da);
}

// ~~~~~~ Blend modes ~~~~~~ //

// The same logic applied to all 4 channels.
// Each such mode's stage is blend_each_channel_k<mode_channel>.
template <U16 (&channel)(U16, U16, U16, U16)>
SI void blend_each_channel_k(NoCtx, const size_t dx, const size_t dy,
                             U16& r, U16& g, U16& b, U16& a,
                             U16& dr, U16& dg, U16& db, U16& da) {
    r = channel(r,dr,a,da);
    g = channel(g,dg,a,da);
    b = channel(b,db,a,da);
    a = channel(a,da,a,da);
}

#if defined(SK_USE_INACCURATE_DIV255_IN_BLEND)
    SI U16 clear_channel(U16 s, U16 d, U16 sa, U16 da) { return U16_0; }
    SI U16 srcatop_channel(U16 s, U16 d, U16 sa, U16 da) { return div255( s*da + d*inv(sa) ); }
    SI U16 dstatop_channel(U16 s, U16 d, U16 sa, U16 da) { return div255( d*sa + s*inv(da) ); }
    SI U16 srcin_channel(U16 s, U16 d, U16 sa, U16 da) { return div255( s*da ); }
    SI U16 dstin_channel(U16 s, U16 d, U16 sa, U16 da) { return div255( d*sa ); }
    SI U16 srcout_channel(U16 s, U16 d, U16 sa, U16 da) { return div255( s*inv(da) ); }
    SI U16 dstout_channel(U16 s, U16 d, U16 sa, U16 da) { return div255( d*inv(sa) ); }
    SI U16 srcover_channel(U16 s, U16 d, U16 sa, U16 da) { return s + div255( d*inv(sa) ); }
    SI U16 dstover_channel(U16 s, U16 d, U16 sa, U16 da) { return d + div255( s*inv(da) ); }
    SI U16 modulate_channel(U16 s, U16 d, U16 sa, U16 da) { return div255( s*d ); }
    SI U16 multiply_channel(U16 s, U16 d, U16 sa, U16 da) { return div255( s*inv(da) + d*inv(sa) + s*d ); }
    SI U16 plus__channel(U16 s, U16 d, U16 sa, U16 da) { return min(s+d, 255); }
    SI U16 screen_channel(U16 s, U16 d, U16 sa, U16 da) { return s + d - div255( s*d ); }
    SI U16 xor__channel(U16 s, U16 d, U16 sa, U16 da) { return div255( s*inv(da) + d*inv(sa) ); }
#else
    SI U16 clear_channel(U16 s, U16 d, U16 sa, U16 da) { return U16_0; }
    SI U16 srcatop_channel(U16 s, U16 d, U16 sa, U16 da) { return div255( s*da + d*inv(sa) ); }
    SI U16 dstatop_channel(U16 s, U16 d, U16 sa, U16 da) { return div255( d*sa + s*inv(da) ); }
    SI U16 srcin_channel(U16 s, U16 d, U16 sa, U16 da) { return div255_accurate( s*da ); }
    SI U16 dstin_channel(U16 s, U16 d, U16 sa, U16 da) { return div255_accurate( d*sa ); }
    SI U16 srcout_channel(U16 s, U16 d, U16 sa, U16 da) { return div255_accurate( s*inv(da) ); }
    SI U16 dstout_channel(U16 s, U16 d, U16 sa, U16 da) { return div255_accurate( d*inv(sa) ); }
    SI U16 srcover_channel(U16 s, U16 d, U16 sa, U16 da) { return s + div255_accurate( d*inv(sa) ); }
    SI U16 dstover_channel(U16 s, U16 d, U16 sa, U16 da) { return d + div255_accurate( s*inv(da) ); }
    SI U16 modulate_channel(U16 s, U16 d, U16 sa, U16 da) { return div255_accurate( s*d ); }
    SI U16 multiply_channel(U16 s, U16 d, U16 sa, U16 da) { return div255( s*inv(da) + d*inv(sa) + s*d ); }
    SI U16 plus__channel(U16 s, U16 d, U16 sa, U16 da) { return min(s+d, 255); }
    SI U16 screen_channel(U16 s, U16 d, U16 sa, U16 da) { return s + d - div255_accurate( s*d ); }
    SI U16 xor__channel(U16 s, U16 d, U16 sa, U16 da) { return div255( s*inv(da) + d*inv(sa) ); }
#endif

// The same logic applied to color, and srcover for alpha.
// Each such mode's stage is blend_colors_k<mode_channel>.
template <U16 (&channel)(U16, U16, U16, U16)>
SI void blend_colors_k(NoCtx, const size_t dx, const size_t dy,
                       U16& r, U16& g, U16& b, U16& a,
                       U16& dr, U16& dg, U16& db, U16& da) {
    r = channel(r,dr,a,da);
    g = channel(g,dg,a,da);
    b = channel(b,db,a,da);
    a = a + div255( da*inv(a) );
}

    SI U16 darken_channel(U16 s, U16 d, U16 sa, U16 da) { return s + d -   div255( max_intr(s*da, d*sa) ); }
    SI U16 lighten_channel(U16 s, U16 d, U16 sa, U16 da) { return s + d -   div255( min_intr(s*da, d*sa) ); }
    SI U16 difference_channel(U16 s, U16 d, U16 sa, U16 da) { return s + d - 2*div255( min_intr(s*da, d*sa) ); }
    SI U16 exclusion_channel(U16 s, U16 d, U16 sa, U16 da) { return s + d - 2*div255( s*d ); }

    SI U16 hardlight_channel(U16 s, U16 d, U16 sa, U16 da) {
        return div255( s*inv(da) + d*inv(sa) +
                       if_then_else(2*s <= sa, 2*s*d, sa*da - 2*(sa-s)*(da-d)) );
    }
    SI U16 overlay_channel(U16 s, U16 d, U16 sa, U16 da) {
        return div255( s*inv(da) + d*inv(sa) +
                       if_then_else(2*d <= da, 2*s*d, sa*da - 2*(sa-s)*(da-d)) );
    }

// ~~~~~~ Helpers for interacting with memory ~~~~~~ //

template <typename T>
SI T* ptr_at_xy(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy) {
    return static_cast<T*>((*ctx).pixels) + dy*(*ctx).stride + dx;
}

template <typename T>
SI U32 ix_and_ptr(T** ptr, const SkRasterPipelineContexts::GatherCtx* ctx, F x, F y) {
    // Exclusive -> inclusive.
    const F w = F_(sk_bit_cast<float>( sk_bit_cast<uint32_t>((*ctx).width ) - 1));
    const F h = F_(sk_bit_cast<float>( sk_bit_cast<uint32_t>((*ctx).height) - 1));

    const F z = F_(std::numeric_limits<float>::min());

    x = min_intr(max_intr(z, x), w);
    y = min_intr(max_intr(z, y), h);

    x = sk_bit_cast<F>(sk_bit_cast<U32>(x) - static_cast<uint32_t>((*ctx).roundDownAtInteger));
    y = sk_bit_cast<F>(sk_bit_cast<U32>(y) - static_cast<uint32_t>((*ctx).roundDownAtInteger));

    *ptr = static_cast<const T*>((*ctx).pixels);
    return trunc_(y)*(*ctx).stride + trunc_(x);
}

template <typename T>
SI U32 ix_and_ptr(T** ptr, const SkRasterPipelineContexts::GatherCtx* ctx, I32 x, I32 y) {
    // This flag doesn't make sense when the coords are integers.
    SkASSERT((*ctx).roundDownAtInteger == 0);
    // Exclusive -> inclusive.
    const I32 w = I32_( (*ctx).width - 1);
    const I32 h = I32_((*ctx).height - 1);

    U32 ax = cast<U32>(min_intr(max_intr(0, x), w));
    U32 ay = cast<U32>(min_intr(max_intr(0, y), h));

    *ptr = static_cast<const T*>((*ctx).pixels);
    return ay * (*ctx).stride + ax;
}

template <typename V, typename T>
SI V load(const T* ptr) {
    V v;
    memcpy(&v, ptr, sizeof(v));
    return v;
}
template <typename V, typename T>
SI void store(T* ptr, V v) {
    memcpy(ptr, &v, sizeof(v));
}

#if defined(SKRP_CPU_ML4)
    template <typename V, typename T>
    SI V gather(const T* ptr, U32 ix) {
        return V{ ptr[ix[ 0]], ptr[ix[ 1]], ptr[ix[ 2]], ptr[ix[ 3]],
                  ptr[ix[ 4]], ptr[ix[ 5]], ptr[ix[ 6]], ptr[ix[ 7]],
                  ptr[ix[ 8]], ptr[ix[ 9]], ptr[ix[10]], ptr[ix[11]],
                  ptr[ix[12]], ptr[ix[13]], ptr[ix[14]], ptr[ix[15]], };
    }

    template<>
    F gather(const float* ptr, U32 ix) {
        return _mm512_i32gather_ps(bit_cast_lanes<__m512i>(ix), ptr, 4);
    }

    template<>
    U32 gather(const uint32_t* ptr, U32 ix) {
        return bit_cast_lanes<U32>(_mm512_i32gather_epi32(bit_cast_lanes<__m512i>(ix), ptr, 4));
    }

    template <typename V, typename T>
    SI V gather_unaligned(const T* ptr, U32 ix) {
        return gather<V, T>(ptr, ix);
    }
#elif defined(SKRP_CPU_AVX2)
    template <typename V, typename T>
    SI V gather(const T* ptr, U32 ix) {
        return V{ ptr[ix[ 0]], ptr[ix[ 1]], ptr[ix[ 2]], ptr[ix[ 3]],
                  ptr[ix[ 4]], ptr[ix[ 5]], ptr[ix[ 6]], ptr[ix[ 7]],
                  ptr[ix[ 8]], ptr[ix[ 9]], ptr[ix[10]], ptr[ix[11]],
                  ptr[ix[12]], ptr[ix[13]], ptr[ix[14]], ptr[ix[15]], };
    }

    template<>
    F gather(const float* ptr, U32 ix) {
        __m256i lo;
        __m256i hi;
        split(ix, &lo, &hi);

        return join<F>(_mm256_i32gather_ps(ptr, lo, 4),
                       _mm256_i32gather_ps(ptr, hi, 4));
    }

    template<>
    U32 gather(const uint32_t* ptr, U32 ix) {
        __m256i lo;
        __m256i hi;
        split(ix, &lo, &hi);

        return join<U32>(_mm256_i32gather_epi32(reinterpret_cast<const int*>(ptr), lo, 4),
                         _mm256_i32gather_epi32(reinterpret_cast<const int*>(ptr), hi, 4));
    }

    template <typename V, typename T>
    SI V gather_unaligned(const T* ptr, U32 ix) {
        return gather<V, T>(ptr, ix);
    }
#elif defined(SKRP_CPU_LASX)
    template <typename V, typename T>
    SI V gather(const T* ptr, U32 ix) {
        return V{ ptr[ix[ 0]], ptr[ix[ 1]], ptr[ix[ 2]], ptr[ix[ 3]],
                  ptr[ix[ 4]], ptr[ix[ 5]], ptr[ix[ 6]], ptr[ix[ 7]],
                  ptr[ix[ 8]], ptr[ix[ 9]], ptr[ix[10]], ptr[ix[11]],
                  ptr[ix[12]], ptr[ix[13]], ptr[ix[14]], ptr[ix[15]], };
    }

    template <typename V, typename T>
    SI V gather_unaligned(const T* ptr, U32 ix) {
        return gather<V, T>(ptr, ix);
    }
#elif defined(SKRP_CPU_NEON)
    template <typename V, typename T>
    SI V gather(const T* ptr, U32 ix) {
        // The compiler assumes ptr is aligned, which caused crashes on some
        // arm32 chips because a register was marked as "aligned to 32 bits"
        // incorrectly. skbug.com/409859319
        SkASSERTF(reinterpret_cast<uintptr_t>(ptr) % alignof(T) == 0,
                 "Should use gather_unaligned");
        return V{ ptr[ix[ 0]], ptr[ix[ 1]], ptr[ix[ 2]], ptr[ix[ 3]],
                  ptr[ix[ 4]], ptr[ix[ 5]], ptr[ix[ 6]], ptr[ix[ 7]], };
    }

    template <typename V, typename T>
    SI V gather_unaligned(const T* ptr, U32 ix) {
        // This tells the compiler ptr might not be aligned appropriately, so
        // it generates better assembly.
        typedef T __attribute__ ((aligned (1))) unaligned_ptr;
        const unaligned_ptr* uptr = static_cast<const unaligned_ptr*>(ptr);
        return V{ uptr[ix[ 0]], uptr[ix[ 1]], uptr[ix[ 2]], uptr[ix[ 3]],
                  uptr[ix[ 4]], uptr[ix[ 5]], uptr[ix[ 6]], uptr[ix[ 7]], };
    }
#else
    template <typename V, typename T>
    SI V gather(const T* ptr, U32 ix) {
        return V{ ptr[ix[ 0]], ptr[ix[ 1]], ptr[ix[ 2]], ptr[ix[ 3]],
                  ptr[ix[ 4]], ptr[ix[ 5]], ptr[ix[ 6]], ptr[ix[ 7]], };
    }

    template <typename V, typename T>
    SI V gather_unaligned(const T* ptr, U32 ix) {
        return gather<V, T>(ptr, ix);
    }
#endif

// ~~~~~~ 32-bit memory loads and stores ~~~~~~ //

// Narrows 16-bit values held in 32-bit lanes for from_8888(). The AVX2 and LASX forms pack
// pairs of 128-bit lanes, so from_8888() first swaps its middle 128-bit lanes for them.
#if defined(SKRP_CPU_AVX2)
    SI U16 from_8888_cast_U16(U32 v) {
        __m256i _02;
        __m256i _13;
        split(v, &_02,&_13);
        return bit_cast_lanes<U16>(_mm256_packus_epi32(_02,_13));
    }
#elif defined(SKRP_CPU_LASX)
    SI U16 from_8888_cast_U16(U32 v) {
        __m256i _02, _13;
        split(v, &_02, &_13);
        __m256i tmp0 = __lasx_xvsat_wu(_02, 15);
        __m256i tmp1 = __lasx_xvsat_wu(_13, 15);
        return __lasx_xvpickev_h(tmp1, tmp0);
    }
#else
    SI U16 from_8888_cast_U16(U32 v) {
        return cast<U16>(v);
    }
#endif

SI void from_8888(U32 rgba, U16* r, U16* g, U16* b, U16* a) {
#if defined(SKRP_CPU_ML4)
    // Turns [0xAABBGGRR, 0xAABBGGRR, 0xAABBGGRR, ... 13 more times] to
    //       [0xGGRR, 0xGGRR, 0xGGRR, ...]
    U16 rg = bit_cast_lanes<U16>(_mm512_cvtepi32_epi16(bit_cast_lanes<__m512i>(rgba)));
    // and   [0xAABB, 0xAABB, 0xAABB, ...]
    U16 ba = bit_cast_lanes<U16>(_mm512_cvtepi32_epi16(_mm512_srli_epi32(bit_cast_lanes<__m512i>(rgba), 16)));
    *r = rg & 255;
    *g = rg >> 8;
    *b = ba & 255;
    *a = ba >> 8;
#elif defined(SKRP_CPU_LSX)
    __m128i _01, _23, rg, ba;
    split(rgba, &_01, &_23);
    rg = __lsx_vpickev_h(_23, _01);
    ba = __lsx_vpickod_h(_23, _01);

    __m128i mask_00ff = __lsx_vreplgr2vr_h(0xff);

    *r = __lsx_vand_v(rg, mask_00ff);
    *g = __lsx_vsrli_h(rg, 8);
    *b = __lsx_vand_v(ba, mask_00ff);
    *a = __lsx_vsrli_h(ba, 8);
#else
    #if defined(SKRP_CPU_AVX2)
        // Swap the middle 128-bit lanes to make _mm256_packus_epi32() in cast_U16() work out nicely.
        __m256i _01;
        __m256i _23;
        split(rgba, &_01, &_23);
        __m256i _02 = _mm256_permute2x128_si256(_01,_23, 0x20),
                _13 = _mm256_permute2x128_si256(_01,_23, 0x31);
        rgba = join<U32>(_02, _13);
    #elif defined(SKRP_CPU_LASX)
        __m256i _01, _23;
        split(rgba, &_01, &_23);
        __m256i _02 = __lasx_xvpermi_q(_01, _23, 0x02),
                _13 = __lasx_xvpermi_q(_01, _23, 0x13);
        rgba = join<U32>(_02, _13);
    #endif

    *r = from_8888_cast_U16(rgba & 65535) & 255;
    *g = from_8888_cast_U16(rgba & 65535) >>  8;
    *b = from_8888_cast_U16(rgba >>   16) & 255;
    *a = from_8888_cast_U16(rgba >>   16) >>  8;
#endif
}

SI void load_8888_(const uint32_t* ptr, U16* r, U16* g, U16* b, U16* a) {
#if defined(SKRP_CPU_NEON)
    uint8x8x4_t rgba = vld4_u8(reinterpret_cast<const uint8_t*>((ptr)));
    *r = cast<U16>(rgba.val[0]);
    *g = cast<U16>(rgba.val[1]);
    *b = cast<U16>(rgba.val[2]);
    *a = cast<U16>(rgba.val[3]);
#else
    from_8888(load<U32>(ptr), r,g,b,a);
#endif
}
SI void store_8888_(uint32_t* ptr, U16 r, U16 g, U16 b, U16 a) {
#if defined(SKRP_CPU_LSX)
    __m128i mask = __lsx_vreplgr2vr_h(255);
    r = __lsx_vmin_hu(r, mask);
    g = __lsx_vmin_hu(g, mask);
    b = __lsx_vmin_hu(b, mask);
    a = __lsx_vmin_hu(a, mask);

    g = __lsx_vslli_h(g, 8);
    r = r | g;
    a = __lsx_vslli_h(a, 8);
    a = a | b;

    __m128i r_lo = __lsx_vsllwil_wu_hu(r, 0);
    __m128i r_hi = __lsx_vexth_wu_hu(r);
    __m128i a_lo = __lsx_vsllwil_wu_hu(a, 0);
    __m128i a_hi = __lsx_vexth_wu_hu(a);

    a_lo = __lsx_vslli_w(a_lo, 16);
    a_hi = __lsx_vslli_w(a_hi, 16);

    r = r_lo | a_lo;
    a = r_hi | a_hi;
    store(ptr, join<U32>(r, a));
#else
    r = min(r, 255);
    g = min(g, 255);
    b = min(b, 255);
    a = min(a, 255);

#if defined(SKRP_CPU_NEON)
    uint8x8x4_t rgba = {{
        cast<U8>(r),
        cast<U8>(g),
        cast<U8>(b),
        cast<U8>(a),
    }};
    vst4_u8(reinterpret_cast<uint8_t*>((ptr)), rgba);
#else
    store(ptr, cast<U32>(r | (g<<8)) <<  0
             | cast<U32>(b | (a<<8)) << 16);
#endif
#endif
}

SI void load_8888_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    U16& r, U16& g, U16& b, U16& a,
                    U16& dr, U16& dg, U16& db, U16& da) {
    load_8888_(ptr_at_xy<const uint32_t>(ctx, dx,dy), &r,&g,&b,&a);
}
SI void load_8888_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                        const size_t dx, const size_t dy,
                        U16& r, U16& g, U16& b, U16& a,
                        U16& dr, U16& dg, U16& db, U16& da) {
    load_8888_(ptr_at_xy<const uint32_t>(ctx, dx,dy), &dr,&dg,&db,&da);
}
SI void store_8888_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                     const size_t dx, const size_t dy,
                     U16& r, U16& g, U16& b, U16& a,
                     U16& dr, U16& dg, U16& db, U16& da) {
    store_8888_(ptr_at_xy<uint32_t>(ctx, dx,dy), r,g,b,a);
}
SI void gather_8888_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                      const size_t dx, const size_t dy,
                      const F x, const F y,
                      U16& r, U16& g, U16& b, U16& a) {
    const uint32_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, x,y);
    from_8888(gather_unaligned<U32>(ptr, ix), &r, &g, &b, &a);
}

// ~~~~~~ 16-bit memory loads and stores ~~~~~~ //

SI void from_565(U16 rgb, U16* r, U16* g, U16* b) {
    // Format for 565 buffers: 15|rrrrr gggggg bbbbb|0
    U16 R = (rgb >> 11) & 31;
    U16 G = (rgb >>  5) & 63;
    U16 B = (rgb >>  0) & 31;

    // These bit replications are the same as multiplying by 255/31 or 255/63 to scale to 8-bit.
    *r = (R << 3) | (R >> 2);
    *g = (G << 2) | (G >> 4);
    *b = (B << 3) | (B >> 2);
}
SI void load_565_(const uint16_t* ptr, U16* r, U16* g, U16* b) {
    from_565(load<U16>(ptr), r,g,b);
}
SI void store_565_(uint16_t* ptr, U16 r, U16 g, U16 b) {
    r = min(r, 255);
    g = min(g, 255);
    b = min(b, 255);

    // Round from [0,255] to [0,31] or [0,63], as if x * (31/255.0f) + 0.5f.
    // (Don't feel like you need to find some fundamental truth in these...
    // they were brute-force searched.)
    U16 R = (r *  9 + 36) / 74;  //  9/74 ≈ 31/255, plus 36/74, about half.
    U16 G = (g * 21 + 42) / 85;  // 21/85 = 63/255 exactly.
    U16 B = (b *  9 + 36) / 74;
    // Pack them back into 15|rrrrr gggggg bbbbb|0.
    store(ptr, R << 11
             | G <<  5
             | B <<  0);
}

SI void load_565_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   U16& r, U16& g, U16& b, U16& a,
                   U16& dr, U16& dg, U16& db, U16& da) {
    load_565_(ptr_at_xy<const uint16_t>(ctx, dx,dy), &r,&g,&b);
    a = U16_255;
}
SI void load_565_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                       const size_t dx, const size_t dy,
                       U16& r, U16& g, U16& b, U16& a,
                       U16& dr, U16& dg, U16& db, U16& da) {
    load_565_(ptr_at_xy<const uint16_t>(ctx, dx,dy), &dr,&dg,&db);
    da = U16_255;
}
SI void store_565_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    U16& r, U16& g, U16& b, U16& a,
                    U16& dr, U16& dg, U16& db, U16& da) {
    store_565_(ptr_at_xy<uint16_t>(ctx, dx,dy), r,g,b);
}
SI void gather_565_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                     const size_t dx, const size_t dy,
                     const F x, const F y,
                     U16& r, U16& g, U16& b, U16& a) {
    const uint16_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, x,y);
    from_565(gather_unaligned<U16>(ptr, ix), &r, &g, &b);
    a = U16_255;
}

SI void from_4444(U16 rgba, U16* r, U16* g, U16* b, U16* a) {
    // Format for 4444 buffers: 15|rrrr gggg bbbb aaaa|0.
    U16 R = (rgba >> 12) & 15;
    U16 G = (rgba >>  8) & 15;
    U16 B = (rgba >>  4) & 15;
    U16 A = (rgba >>  0) & 15;

    // Scale [0,15] to [0,255].
    *r = (R << 4) | R;
    *g = (G << 4) | G;
    *b = (B << 4) | B;
    *a = (A << 4) | A;
}
SI void load_4444_(const uint16_t* ptr, U16* r, U16* g, U16* b, U16* a) {
    from_4444(load<U16>(ptr), r,g,b,a);
}
SI void store_4444_(uint16_t* ptr, U16 r, U16 g, U16 b, U16 a) {
    r = min(r, 255);
    g = min(g, 255);
    b = min(b, 255);
    a = min(a, 255);

    // Round from [0,255] to [0,15], producing the same value as (x*(15/255.0f) + 0.5f).
    U16 R = (r + 8) / 17;
    U16 G = (g + 8) / 17;
    U16 B = (b + 8) / 17;
    U16 A = (a + 8) / 17;
    // Pack them back into 15|rrrr gggg bbbb aaaa|0.
    store(ptr, R << 12
             | G <<  8
             | B <<  4
             | A <<  0);
}

SI void load_4444_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    U16& r, U16& g, U16& b, U16& a,
                    U16& dr, U16& dg, U16& db, U16& da) {
    load_4444_(ptr_at_xy<const uint16_t>(ctx, dx,dy), &r,&g,&b,&a);
}
SI void load_4444_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                        const size_t dx, const size_t dy,
                        U16& r, U16& g, U16& b, U16& a,
                        U16& dr, U16& dg, U16& db, U16& da) {
    load_4444_(ptr_at_xy<const uint16_t>(ctx, dx,dy), &dr,&dg,&db,&da);
}
SI void store_4444_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                     const size_t dx, const size_t dy,
                     U16& r, U16& g, U16& b, U16& a,
                     U16& dr, U16& dg, U16& db, U16& da) {
    store_4444_(ptr_at_xy<uint16_t>(ctx, dx,dy), r,g,b,a);
}
SI void gather_4444_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                      const size_t dx, const size_t dy,
                      const F x, const F y,
                      U16& r, U16& g, U16& b, U16& a) {
    const uint16_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, x,y);
    from_4444(gather_unaligned<U16>(ptr, ix), &r,&g,&b,&a);
}

SI void from_88(U16 rg, U16* r, U16* g) {
    *r = (rg & 0xFF);
    *g = (rg >> 8);
}

SI void load_88_(const uint16_t* ptr, U16* r, U16* g) {
#if defined(SKRP_CPU_NEON)
    uint8x8x2_t rg = vld2_u8(reinterpret_cast<const uint8_t*>((ptr)));
    *r = cast<U16>(rg.val[0]);
    *g = cast<U16>(rg.val[1]);
#else
    from_88(load<U16>(ptr), r,g);
#endif
}

SI void store_88_(uint16_t* ptr, U16 r, U16 g) {
    r = min(r, 255);
    g = min(g, 255);

#if defined(SKRP_CPU_NEON)
    uint8x8x2_t rg = {{
        cast<U8>(r),
        cast<U8>(g),
    }};
    vst2_u8(reinterpret_cast<uint8_t*>((ptr)), rg);
#else
    store(ptr, cast<U16>(r | (g<<8)) <<  0);
#endif
}

SI void load_rg88_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    U16& r, U16& g, U16& b, U16& a,
                    U16& dr, U16& dg, U16& db, U16& da) {
    load_88_(ptr_at_xy<const uint16_t>(ctx, dx, dy), &r, &g);
    b = U16_0;
    a = U16_255;
}
SI void load_rg88_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                        const size_t dx, const size_t dy,
                        U16& r, U16& g, U16& b, U16& a,
                        U16& dr, U16& dg, U16& db, U16& da) {
    load_88_(ptr_at_xy<const uint16_t>(ctx, dx, dy), &dr, &dg);
    db = U16_0;
    da = U16_255;
}
SI void store_rg88_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                     const size_t dx, const size_t dy,
                     U16& r, U16& g, U16& b, U16& a,
                     U16& dr, U16& dg, U16& db, U16& da) {
    store_88_(ptr_at_xy<uint16_t>(ctx, dx, dy), r, g);
}
SI void gather_rg88_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                      const size_t dx, const size_t dy,
                      const F x, const F y,
                      U16& r, U16& g, U16& b, U16& a) {
    const uint16_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, x, y);
    from_88(gather_unaligned<U16>(ptr, ix), &r, &g);
    b = U16_0;
    a = U16_255;
}

// ~~~~~~ 8-bit memory loads and stores ~~~~~~ //

SI U16 load_8(const uint8_t* ptr) {
    return cast<U16>(load<U8>(ptr));
}
SI void store_8(uint8_t* ptr, U16 v) {
    v = min(v, 255);
    store(ptr, cast<U8>(v));
}

SI void load_a8_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  U16& r, U16& g, U16& b, U16& a,
                  U16& dr, U16& dg, U16& db, U16& da) {
    r = g = b = U16_0;
    a = load_8(ptr_at_xy<const uint8_t>(ctx, dx,dy));
}
SI void load_a8_dst_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                      const size_t dx, const size_t dy,
                      U16& r, U16& g, U16& b, U16& a,
                      U16& dr, U16& dg, U16& db, U16& da) {
    dr = dg = db = U16_0;
    da = load_8(ptr_at_xy<const uint8_t>(ctx, dx,dy));
}
SI void store_a8_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   U16& r, U16& g, U16& b, U16& a,
                   U16& dr, U16& dg, U16& db, U16& da) {
    store_8(ptr_at_xy<uint8_t>(ctx, dx,dy), a);
}
SI void gather_a8_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                    const size_t dx, const size_t dy,
                    const F x, const F y,
                    U16& r, U16& g, U16& b, U16& a) {
    const uint8_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, x,y);
    r = g = b = U16_0;
    a = cast<U16>(gather<U8>(ptr, ix));
}
SI void store_r8_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   U16& r, U16& g, U16& b, U16& a,
                   U16& dr, U16& dg, U16& db, U16& da) {
    store_8(ptr_at_xy<uint8_t>(ctx, dx,dy), r);
}

SI void alpha_to_gray_k(NoCtx, const size_t dx, const size_t dy,
                        U16& r, U16& g, U16& b, U16& a,
                        U16& dr, U16& dg, U16& db, U16& da) {
    r = g = b = a;
    a = U16_255;
}
SI void alpha_to_gray_dst_k(NoCtx, const size_t dx, const size_t dy,
                            U16& r, U16& g, U16& b, U16& a,
                            U16& dr, U16& dg, U16& db, U16& da) {
    dr = dg = db = da;
    da = U16_255;
}
SI void alpha_to_red_k(NoCtx, const size_t dx, const size_t dy,
                       U16& r, U16& g, U16& b, U16& a,
                       U16& dr, U16& dg, U16& db, U16& da) {
    r = a;
    a = U16_255;
}
SI void alpha_to_red_dst_k(NoCtx, const size_t dx, const size_t dy,
                           U16& r, U16& g, U16& b, U16& a,
                           U16& dr, U16& dg, U16& db, U16& da) {
    dr = da;
    da = U16_255;
}

SI void bt709_luminance_or_luma_to_alpha_k(NoCtx, const size_t dx, const size_t dy,
                                           U16& r, U16& g, U16& b, U16& a,
                                           U16& dr, U16& dg, U16& db, U16& da) {
    a = (r*54 + g*183 + b*19)/256;  // 0.2126, 0.7152, 0.0722 with 256 denominator.
    r = g = b = U16_0;
}
SI void bt709_luminance_or_luma_to_rgb_k(NoCtx, const size_t dx, const size_t dy,
                                         U16& r, U16& g, U16& b, U16& a,
                                         U16& dr, U16& dg, U16& db, U16& da) {
    r = g = b =(r*54 + g*183 + b*19)/256;  // 0.2126, 0.7152, 0.0722 with 256 denominator.
}

// ~~~~~~ Coverage scales / lerps ~~~~~~ //

SI void load_src_k(const uint16_t* ptr, const size_t dx, const size_t dy,
                   U16& r, U16& g, U16& b, U16& a,
                   U16& dr, U16& dg, U16& db, U16& da) {
    r = sk_unaligned_load<U16>(ptr + 0*N);
    g = sk_unaligned_load<U16>(ptr + 1*N);
    b = sk_unaligned_load<U16>(ptr + 2*N);
    a = sk_unaligned_load<U16>(ptr + 3*N);
}
SI void store_src_k(uint16_t* ptr, const size_t dx, const size_t dy,
                    U16& r, U16& g, U16& b, U16& a,
                    U16& dr, U16& dg, U16& db, U16& da) {
    sk_unaligned_store(ptr + 0*N, r);
    sk_unaligned_store(ptr + 1*N, g);
    sk_unaligned_store(ptr + 2*N, b);
    sk_unaligned_store(ptr + 3*N, a);
}
SI void store_src_a_k(uint16_t* ptr, const size_t dx, const size_t dy,
                      U16& r, U16& g, U16& b, U16& a,
                      U16& dr, U16& dg, U16& db, U16& da) {
    sk_unaligned_store(ptr, a);
}
SI void load_dst_k(const uint16_t* ptr, const size_t dx, const size_t dy,
                   U16& r, U16& g, U16& b, U16& a,
                   U16& dr, U16& dg, U16& db, U16& da) {
    dr = sk_unaligned_load<U16>(ptr + 0*N);
    dg = sk_unaligned_load<U16>(ptr + 1*N);
    db = sk_unaligned_load<U16>(ptr + 2*N);
    da = sk_unaligned_load<U16>(ptr + 3*N);
}
SI void store_dst_k(uint16_t* ptr, const size_t dx, const size_t dy,
                    U16& r, U16& g, U16& b, U16& a,
                    U16& dr, U16& dg, U16& db, U16& da) {
    sk_unaligned_store(ptr + 0*N, dr);
    sk_unaligned_store(ptr + 1*N, dg);
    sk_unaligned_store(ptr + 2*N, db);
    sk_unaligned_store(ptr + 3*N, da);
}

// ~~~~~~ Coverage scales / lerps ~~~~~~ //

SI void scale_1_float_k(const float* f, const size_t dx, const size_t dy,
                        U16& r, U16& g, U16& b, U16& a,
                        U16& dr, U16& dg, U16& db, U16& da) {
    U16 c = from_float(*f);
    r = div255( r * c );
    g = div255( g * c );
    b = div255( b * c );
    a = div255( a * c );
}
SI void lerp_1_float_k(const float* f, const size_t dx, const size_t dy,
                       U16& r, U16& g, U16& b, U16& a,
                       U16& dr, U16& dg, U16& db, U16& da) {
    U16 c = from_float(*f);
    r = lerp(dr, r, c);
    g = lerp(dg, g, c);
    b = lerp(db, b, c);
    a = lerp(da, a, c);
}
SI void scale_native_k(const uint16_t scales[], const size_t dx, const size_t dy,
                       U16& r, U16& g, U16& b, U16& a,
                       U16& dr, U16& dg, U16& db, U16& da) {
    U16 c = sk_unaligned_load<U16>(scales);
    r = div255( r * c );
    g = div255( g * c );
    b = div255( b * c );
    a = div255( a * c );
}

SI void lerp_native_k(const uint16_t scales[], const size_t dx, const size_t dy,
                      U16& r, U16& g, U16& b, U16& a,
                      U16& dr, U16& dg, U16& db, U16& da) {
    U16 c = sk_unaligned_load<U16>(scales);
    r = lerp(dr, r, c);
    g = lerp(dg, g, c);
    b = lerp(db, b, c);
    a = lerp(da, a, c);
}

SI void scale_u8_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   U16& r, U16& g, U16& b, U16& a,
                   U16& dr, U16& dg, U16& db, U16& da) {
    U16 c = load_8(ptr_at_xy<const uint8_t>(ctx, dx,dy));
    r = div255( r * c );
    g = div255( g * c );
    b = div255( b * c );
    a = div255( a * c );
}
SI void lerp_u8_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  U16& r, U16& g, U16& b, U16& a,
                  U16& dr, U16& dg, U16& db, U16& da) {
    U16 c = load_8(ptr_at_xy<const uint8_t>(ctx, dx,dy));
    r = lerp(dr, r, c);
    g = lerp(dg, g, c);
    b = lerp(db, b, c);
    a = lerp(da, a, c);
}

// Derive alpha's coverage from rgb coverage and the values of src and dst alpha.
SI U16 alpha_coverage_from_rgb_coverage(U16 a, U16 da, U16 cr, U16 cg, U16 cb) {
    return if_then_else(a < da, min_intr(cr, min_intr(cg,cb))
                              , max_intr(cr, max_intr(cg,cb)));
}
SI void scale_565_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                    const size_t dx, const size_t dy,
                    U16& r, U16& g, U16& b, U16& a,
                    U16& dr, U16& dg, U16& db, U16& da) {
    U16 cr;
    U16 cg;
    U16 cb;
    load_565_(ptr_at_xy<const uint16_t>(ctx, dx,dy), &cr,&cg,&cb);
    U16 ca = alpha_coverage_from_rgb_coverage(a,da, cr,cg,cb);

    r = div255( r * cr );
    g = div255( g * cg );
    b = div255( b * cb );
    a = div255( a * ca );
}
SI void lerp_565_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                   U16& r, U16& g, U16& b, U16& a,
                   U16& dr, U16& dg, U16& db, U16& da) {
    U16 cr;
    U16 cg;
    U16 cb;
    load_565_(ptr_at_xy<const uint16_t>(ctx, dx,dy), &cr,&cg,&cb);
    U16 ca = alpha_coverage_from_rgb_coverage(a,da, cr,cg,cb);

    r = lerp(dr, r, cr);
    g = lerp(dg, g, cg);
    b = lerp(db, b, cb);
    a = lerp(da, a, ca);
}

SI void emboss_k(const SkRasterPipelineContexts::EmbossCtx* ctx, const size_t dx, const size_t dy,
                 U16& r, U16& g, U16& b, U16& a,
                 U16& dr, U16& dg, U16& db, U16& da) {
    U16 mul = load_8(ptr_at_xy<const uint8_t>(&(*ctx).mul, dx,dy));
    U16 add = load_8(ptr_at_xy<const uint8_t>(&(*ctx).add, dx,dy));

    r = min(div255(r*mul) + add, a);
    g = min(div255(g*mul) + add, a);
    b = min(div255(b*mul) + add, a);
}

// ~~~~~~ Gradient stages ~~~~~~ //

// Clamp x to [0,1], both sides inclusive (think, gradients).
// Even repeat and mirror funnel through a clamp to handle bad inputs like +Inf, NaN.
SI F clamp_01_(F v) { return min(max(0, v), 1); }
SI F fast_clamp_01_(F v) { return min_intr(max_intr(0, v), 1); }

SI void clamp_x_1_k(NoCtx, const size_t dx, const size_t dy, F& x, F& y) { x = fast_clamp_01_(x); }
SI void repeat_x_1_k(NoCtx,
                     const size_t dx, const size_t dy,
                     F& x, F& y) { x = fast_clamp_01_(x - floor_(x)); }
SI F twice(F x) { return x+x; }

SI void mirror_x_1_k(NoCtx, const size_t dx, const size_t dy, F& x, F& y) {
    x = clamp_01_(abs_( (x-1.0f) - twice(floor_((x-1.0f)*0.5f)) - 1.0f ));
}

SI I16 cond_to_mask_16(I32 cond) { return cast<I16>(cond); }

SI void decal_x_k(SkRasterPipelineContexts::DecalTileCtx* ctx,
                  const size_t dx, const size_t dy,
                  F& x, F& y) {
    float w = (*ctx).limit_x;
    sk_unaligned_store((*ctx).mask, cond_to_mask_16((0 <= x) & (x < w)));
}
SI void decal_y_k(SkRasterPipelineContexts::DecalTileCtx* ctx,
                  const size_t dx, const size_t dy,
                  F& x, F& y) {
    float h = (*ctx).limit_y;
    sk_unaligned_store((*ctx).mask, cond_to_mask_16((0 <= y) & (y < h)));
}
SI void decal_x_and_y_k(SkRasterPipelineContexts::DecalTileCtx* ctx,
                        const size_t dx, const size_t dy,
                        F& x, F& y) {
    float w = (*ctx).limit_x;
    float h = (*ctx).limit_y;
    sk_unaligned_store((*ctx).mask, cond_to_mask_16((0 <= x) & (x < w) & (0 <= y) & (y < h)));
}
SI void clamp_x_and_y_k(const SkRasterPipelineContexts::CoordClampCtx* ctx,
                        const size_t dx, const size_t dy,
                        F& x, F& y) {
    x = min_intr((*ctx).max_x, max_intr((*ctx).min_x, x));
    y = min_intr((*ctx).max_y, max_intr((*ctx).min_y, y));
}
SI void check_decal_mask_k(SkRasterPipelineContexts::DecalTileCtx* ctx,
                           const size_t dx, const size_t dy,
                           U16& r, U16& g, U16& b, U16& a,
                           U16& dr, U16& dg, U16& db, U16& da) {
    U16 mask = sk_unaligned_load<U16>((*ctx).mask);
    r = r & mask;
    g = g & mask;
    b = b & mask;
    a = a & mask;
}

SI U16 round_color_to_U16(F x) { return cast<U16>(x * 255.0f + 0.5f); }

SI void round_F_to_U16(F R, F G, F B, F A, U16* r, U16* g, U16* b, U16* a) {
    *r = round_color_to_U16(min(max(0, R), 1));
    *g = round_color_to_U16(min(max(0, G), 1));
    *b = round_color_to_U16(min(max(0, B), 1));
    *a = round_color_to_U16(A);  // we assume alpha is already in [0,1].
}

SI void gradient_lookup(const SkRasterPipelineContexts::GradientCtx* c,
                        U32 idx,
                        F t,
                        U16* r,
                        U16* g,
                        U16* b,
                        U16* a) {
    F fr;
    F fg;
    F fb;
    F fa;
    F br;
    F bg;
    F bb;
    F ba;
#if defined(SKRP_CPU_AVX2)
    if ((*c).stopCount <=8) {
        __m256i lo;
        __m256i hi;
        split(idx, &lo, &hi);

        fr = join<F>(_mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).factors[0]), lo),
                     _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).factors[0]), hi));
        br = join<F>(_mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).biases[0]), lo),
                     _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).biases[0]), hi));
        fg = join<F>(_mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).factors[1]), lo),
                     _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).factors[1]), hi));
        bg = join<F>(_mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).biases[1]), lo),
                     _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).biases[1]), hi));
        fb = join<F>(_mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).factors[2]), lo),
                     _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).factors[2]), hi));
        bb = join<F>(_mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).biases[2]), lo),
                     _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).biases[2]), hi));
        fa = join<F>(_mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).factors[3]), lo),
                     _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).factors[3]), hi));
        ba = join<F>(_mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).biases[3]), lo),
                     _mm256_permutevar8x32_ps(_mm256_loadu_ps((*c).biases[3]), hi));
    } else
#elif defined(SKRP_CPU_LASX)
    if ((*c).stopCount <= 8) {
        __m256i lo, hi;
        split(idx, &lo, &hi);

        fr = join<F>((__m256)__lasx_xvperm_w(__lasx_xvld((*c).factors[0], 0), lo),
                     (__m256)__lasx_xvperm_w(__lasx_xvld((*c).factors[0], 0), hi));
        br = join<F>((__m256)__lasx_xvperm_w(__lasx_xvld((*c).biases[0], 0), lo),
                     (__m256)__lasx_xvperm_w(__lasx_xvld((*c).biases[0], 0), hi));
        fg = join<F>((__m256)__lasx_xvperm_w(__lasx_xvld((*c).factors[1], 0), lo),
                     (__m256)__lasx_xvperm_w(__lasx_xvld((*c).factors[1], 0), hi));
        bg = join<F>((__m256)__lasx_xvperm_w(__lasx_xvld((*c).biases[1], 0), lo),
                     (__m256)__lasx_xvperm_w(__lasx_xvld((*c).biases[1], 0), hi));
        fb = join<F>((__m256)__lasx_xvperm_w(__lasx_xvld((*c).factors[2], 0), lo),
                     (__m256)__lasx_xvperm_w(__lasx_xvld((*c).factors[2], 0), hi));
        bb = join<F>((__m256)__lasx_xvperm_w(__lasx_xvld((*c).biases[2], 0), lo),
                     (__m256)__lasx_xvperm_w(__lasx_xvld((*c).biases[2], 0), hi));
        fa = join<F>((__m256)__lasx_xvperm_w(__lasx_xvld((*c).factors[3], 0), lo),
                     (__m256)__lasx_xvperm_w(__lasx_xvld((*c).factors[3], 0), hi));
        ba = join<F>((__m256)__lasx_xvperm_w(__lasx_xvld((*c).biases[3], 0), lo),
                     (__m256)__lasx_xvperm_w(__lasx_xvld((*c).biases[3], 0), hi));
    } else
#elif defined(SKRP_CPU_LSX)
    if ((*c).stopCount <= 4) {
        __m128i lo, hi;
        split(idx, &lo, &hi);
        __m128i zero = __lsx_vldi(0);
        fr = join<F>((__m128)__lsx_vshuf_w(lo, zero, __lsx_vld((*c).factors[0], 0)),
                     (__m128)__lsx_vshuf_w(hi, zero, __lsx_vld((*c).factors[0], 0)));
        br = join<F>((__m128)__lsx_vshuf_w(lo, zero, __lsx_vld((*c).biases[0], 0)),
                     (__m128)__lsx_vshuf_w(hi, zero, __lsx_vld((*c).biases[0], 0)));
        fg = join<F>((__m128)__lsx_vshuf_w(lo, zero, __lsx_vld((*c).factors[1], 0)),
                     (__m128)__lsx_vshuf_w(hi, zero, __lsx_vld((*c).factors[1], 0)));
        bg = join<F>((__m128)__lsx_vshuf_w(lo, zero, __lsx_vld((*c).biases[1], 0)),
                     (__m128)__lsx_vshuf_w(hi, zero, __lsx_vld((*c).biases[1], 0)));
        fb = join<F>((__m128)__lsx_vshuf_w(lo, zero, __lsx_vld((*c).factors[2], 0)),
                     (__m128)__lsx_vshuf_w(hi, zero, __lsx_vld((*c).factors[2], 0)));
        bb = join<F>((__m128)__lsx_vshuf_w(lo, zero, __lsx_vld((*c).biases[2], 0)),
                     (__m128)__lsx_vshuf_w(hi, zero, __lsx_vld((*c).biases[2], 0)));
        fa = join<F>((__m128)__lsx_vshuf_w(lo, zero, __lsx_vld((*c).factors[3], 0)),
                     (__m128)__lsx_vshuf_w(hi, zero, __lsx_vld((*c).factors[3], 0)));
        ba = join<F>((__m128)__lsx_vshuf_w(lo, zero, __lsx_vld((*c).biases[3], 0)),
                     (__m128)__lsx_vshuf_w(hi, zero, __lsx_vld((*c).biases[3], 0)));
    } else
#endif
    {
        fr = gather<F>((*c).factors[0], idx);
        fg = gather<F>((*c).factors[1], idx);
        fb = gather<F>((*c).factors[2], idx);
        fa = gather<F>((*c).factors[3], idx);
        br = gather<F>((*c).biases[0], idx);
        bg = gather<F>((*c).biases[1], idx);
        bb = gather<F>((*c).biases[2], idx);
        ba = gather<F>((*c).biases[3], idx);
    }
    round_F_to_U16(mad(t, fr, br),
                   mad(t, fg, bg),
                   mad(t, fb, bb),
                   mad(t, fa, ba),
                   r,g,b,a);
}

SI void gradient_k(const SkRasterPipelineContexts::GradientCtx* c, const size_t dx, const size_t dy,
                   const F x, const F y,
                   U16& r, U16& g, U16& b, U16& a) {
    F t = x;
    U32 idx = U32_(0);

    // N.B. The loop starts at 1 because idx 0 is the color to use before the first stop.
    for (size_t i = 1; i < (*c).stopCount; i++) {
        idx += if_then_else(t >= (*c).ts[i], U32_(1), U32_(0));
    }

    gradient_lookup(c, idx, t, &r, &g, &b, &a);
}

SI void evenly_spaced_gradient_k(const SkRasterPipelineContexts::GradientCtx* c,
                                 const size_t dx, const size_t dy,
                                 const F x, const F y,
                                 U16& r, U16& g, U16& b, U16& a) {
    F t = x;
    U32 idx = trunc_(t * static_cast<float>((*c).stopCount-1));
    gradient_lookup(c, idx, t, &r, &g, &b, &a);
}

SI void evenly_spaced_2_stop_gradient_k(const SkRasterPipelineContexts::EvenlySpaced2StopGradientCtx* c,
                                        const size_t dx, const size_t dy,
                                        const F x, const F y,
                                        U16& r, U16& g, U16& b, U16& a) {
    F t = x;
    round_F_to_U16(mad(t, (*c).factor[0], (*c).bias[0]),
                   mad(t, (*c).factor[1], (*c).bias[1]),
                   mad(t, (*c).factor[2], (*c).bias[2]),
                   mad(t, (*c).factor[3], (*c).bias[3]),
                   &r,&g,&b,&a);
}

// Horizontal and vertical steps of bilerp_clamp_8888, with tx and ty its Q15 lerp parameters.
// These keep ordinary inlining; see gamma_channel.
// Substituting the {qx} by the equation for tx from above into the lerp equation where v is
// the lerped value:
//         v = {qx}*(R - L) + L,
//         v = 1/2*(tx + 1)*(R - L) + L
//     2 * v = (tx + 1)*(R - L) + 2*L
//           = tx*R - tx*L + R - L + 2*L
//           = tx*(R - L) + (R + L).
// Since R and L are on [0, 255] we need them on the interval [0, 1/2] to get them into form
// for Q15_mult. If L and R where in 16.16 format, this would be done by dividing by 2^9. In
// code, we can multiply by 2^7 to get the value directly.
//            2 * v = tx*(R - L) + (R + L)
//     2^-9 * 2 * v = tx*(R - L)*2^-9 + (R + L)*2^-9
//         2^-8 * v = 2^-9 * (tx*(R - L) + (R + L))
//                v = 1/2 * (tx*(R - L) + (R + L))
static inline U16 bilerp_lerp_x(U16 left, U16 right, I16 tx) {
    I16 width  = bit_cast_lanes<I16>((right - left)) << 7;
    U16 middle = (right + left) << 7;
    // The constrained_add is the most subtle part of lerp. The first term is on the interval
    // [-1, 1), and the second term is on the interval is on the interval [0, 1) because
    // both terms are too high by a factor of 2 which will be handled below. (Both R and L are
    // on [0, 1/2), but the sum R + L is on the interval [0, 1).) Generally, the sum below
    // should overflow, but because we know that sum produces an output on the
    // interval [0, 1) we know that the extra bit that would be needed will always be 0. So
    // we need to be careful to treat this sum as an unsigned positive number in the divide
    // by 2 below. Add +1 for rounding.
    U16 v2  = constrained_add(scaled_mult(tx, width), middle) + 1;
    // Divide by 2 to calculate v and at the same time bring the intermediate value onto the
    // interval [0, 1/2] to set up for bilerp_lerp_y.
    return v2 >> 1;
}

// bilerp_lerp_y plays the same mathematical tricks as bilerp_lerp_x, but the final divide is by
// 256 resulting in a value on [0, 255].
static inline U16 bilerp_lerp_y(U16 top, U16 bottom, I16 ty) {
    I16 width  = bit_cast_lanes<I16>(bottom) - bit_cast_lanes<I16>(top);
    U16 middle = bottom + top;
    // Add + 0x80 for rounding.
    U16 blend  = constrained_add(scaled_mult(ty, width), middle) + 0x80;

    return blend >> 8;
}

SI void bilerp_clamp_8888_k(const SkRasterPipelineContexts::GatherCtx* ctx,
                            const size_t dx, const size_t dy,
                            const F x, const F y,
                            U16& r, U16& g, U16& b, U16& a) {
    // Quantize sample point and transform into lerp coordinates converting them to 16.16 fixed
    // point number.
#if defined(SKRP_CPU_LSX)
    __m128 _01, _23, _45, _67;
    v4f32 v_tmp1 = {0.5f, 0.5f, 0.5f, 0.5f};
    v4f32 v_tmp2 = {65536.0f, 65536.0f, 65536.0f, 65536.0f};
    split(x, &_01,&_23);
    split(y, &_45,&_67);
    __m128 val1 = __lsx_vfmadd_s((__m128)v_tmp2, _01, (__m128)v_tmp1);
    __m128 val2 = __lsx_vfmadd_s((__m128)v_tmp2, _23, (__m128)v_tmp1);
    __m128 val3 = __lsx_vfmadd_s((__m128)v_tmp2, _45, (__m128)v_tmp1);
    __m128 val4 = __lsx_vfmadd_s((__m128)v_tmp2, _67, (__m128)v_tmp1);
    I32 qx = cast<I32>((join<F>(__lsx_vfrintrm_s(val1), __lsx_vfrintrm_s(val2)))) - 32768,
    qy = cast<I32>((join<F>(__lsx_vfrintrm_s(val3), __lsx_vfrintrm_s(val4)))) - 32768;
#else
    I32 qx = cast<I32>(floor_(65536.0f * x + 0.5f)) - 32768;
    I32 qy = cast<I32>(floor_(65536.0f * y + 0.5f)) - 32768;
#endif

    // Calculate screen coordinates sx & sy by flooring qx and qy.
    I32 sx = qx >> 16;
    I32 sy = qy >> 16;

    // We are going to perform a change of parameters for qx on [0, 1) to tx on [-1, 1).
    // This will put tx in Q15 format for use with q_mult.
    // Calculate tx and ty on the interval of [-1, 1). Give {qx} and {qy} are on the interval
    // [0, 1), where {v} is fract(v), we can transform to tx in the following manner ty follows
    // the same math:
    //     tx = 2 * {qx} - 1, so
    //     {qx} = (tx + 1) / 2.
    // Calculate {qx} - 1 and {qy} - 1 where the {} operation is handled by the cast, and the - 1
    // is handled by the ^ 0x8000, dividing by 2 is deferred and handled in bilerp_lerp_x and _y in
    // order to use the full 16-bit resolution.
#if defined(SKRP_CPU_LSX)
    __m128i qx_lo, qx_hi, qy_lo, qy_hi;
    split(qx, &qx_lo, &qx_hi);
    split(qy, &qy_lo, &qy_hi);
    __m128i temp = __lsx_vreplgr2vr_w(0x8000);
    qx_lo = __lsx_vxor_v(qx_lo, temp);
    qx_hi = __lsx_vxor_v(qx_hi, temp);
    qy_lo = __lsx_vxor_v(qy_lo, temp);
    qy_hi = __lsx_vxor_v(qy_hi, temp);

    I16 tx = __lsx_vpickev_h(qx_hi, qx_lo);
    I16 ty = __lsx_vpickev_h(qy_hi, qy_lo);
#else
    I16 tx = cast<I16>(qx ^ 0x8000);
    I16 ty = cast<I16>(qy ^ 0x8000);
#endif


    const uint32_t* ptr;
    U32 ix = ix_and_ptr(&ptr, ctx, sx, sy);
    U16 leftR;
    U16 leftG;
    U16 leftB;
    U16 leftA;
    from_8888(gather_unaligned<U32>(ptr, ix), &leftR,&leftG,&leftB,&leftA);

    ix = ix_and_ptr(&ptr, ctx, sx+1, sy);
    U16 rightR;
    U16 rightG;
    U16 rightB;
    U16 rightA;
    from_8888(gather_unaligned<U32>(ptr, ix), &rightR,&rightG,&rightB,&rightA);

    U16 topR = bilerp_lerp_x(leftR, rightR, tx);
    U16 topG = bilerp_lerp_x(leftG, rightG, tx);
    U16 topB = bilerp_lerp_x(leftB, rightB, tx);
    U16 topA = bilerp_lerp_x(leftA, rightA, tx);

    ix = ix_and_ptr(&ptr, ctx, sx, sy+1);
    from_8888(gather_unaligned<U32>(ptr, ix), &leftR,&leftG,&leftB,&leftA);

    ix = ix_and_ptr(&ptr, ctx, sx+1, sy+1);
    from_8888(gather_unaligned<U32>(ptr, ix), &rightR,&rightG,&rightB,&rightA);

    U16 bottomR = bilerp_lerp_x(leftR, rightR, tx);
    U16 bottomG = bilerp_lerp_x(leftG, rightG, tx);
    U16 bottomB = bilerp_lerp_x(leftB, rightB, tx);
    U16 bottomA = bilerp_lerp_x(leftA, rightA, tx);


    r = bilerp_lerp_y(topR, bottomR, ty);
    g = bilerp_lerp_y(topG, bottomG, ty);
    b = bilerp_lerp_y(topB, bottomB, ty);
    a = bilerp_lerp_y(topA, bottomA, ty);
}

SI void xy_to_unit_angle_k(NoCtx, const size_t dx, const size_t dy, F& x, F& y) {
    F xabs = abs_(x);
    F yabs = abs_(y);

    F slope = min(xabs, yabs)/max(xabs, yabs);
    F s = slope * slope;

    // Use a 7th degree polynomial to approximate atan.
    // This was generated using sollya.gforge.inria.fr.
    // A float optimized polynomial was generated using the following command.
    // P1 = fpminimax((1/(2*Pi))*atan(x),[|1,3,5,7|],[|24...|],[2^(-40),1],relative);
    F phi = slope
             * (0.15912117063999176025390625f     + s
             * (-5.185396969318389892578125e-2f   + s
             * (2.476101927459239959716796875e-2f + s
             * (-7.0547382347285747528076171875e-3f))));

    phi = if_then_else(xabs < yabs, 1.0f/4.0f - phi, phi);
    phi = if_then_else(x < 0.0f   , 1.0f/2.0f - phi, phi);
    phi = if_then_else(y < 0.0f   , 1.0f - phi     , phi);
    phi = if_then_else(phi != phi , 0              , phi);  // Check for NaN.
    x = phi;
}
SI void xy_to_radius_k(NoCtx, const size_t dx, const size_t dy, F& x, F& y) {
    x = sqrt_(x*x + y*y);
}

// ~~~~~~ Compound stages ~~~~~~ //

SI void srcover_rgba_8888_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                            const size_t dx, const size_t dy,
                            U16& r, U16& g, U16& b, U16& a,
                            U16& dr, U16& dg, U16& db, U16& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    load_8888_(ptr, &dr,&dg,&db,&da);
    r = r + div255( dr*inv(a) );
    g = g + div255( dg*inv(a) );
    b = b + div255( db*inv(a) );
    a = a + div255( da*inv(a) );
    store_8888_(ptr, r,g,b,a);
}

// ~~~~~~ skgpu::Swizzle stage ~~~~~~ //

SI void swizzle_k(void* ctx, const size_t dx, const size_t dy,
                  U16& r, U16& g, U16& b, U16& a,
                  U16& dr, U16& dg, U16& db, U16& da) {
    U16 ir = r;
    U16 ig = g;
    U16 ib = b;
    U16 ia = a;
    U16* o[] = {&r, &g, &b, &a};
    char swiz[4];
    memcpy(swiz, &ctx, sizeof(swiz));

    for (int i = 0; i < 4; ++i) {
        switch (swiz[i]) {
            case 'r': *o[i] = ir;       break;
            case 'g': *o[i] = ig;       break;
            case 'b': *o[i] = ib;       break;
            case 'a': *o[i] = ia;       break;
            case '0': *o[i] = U16_0;    break;
            case '1': *o[i] = U16_255;  break;
            default:                    break;
        }
    }
}

// These debug stages are meant to be used with SkRasterPipelineVisualizer functions
// to look at certain lanes in a pipeline at various times.
// There are 3 flavors of functions for lowp, 2 for highp
//   debug_L_255: clamps the value of the lane to [0, 255] and puts it into
//                a channel of an appropriate color to visualize that component.
//   debug_L: Converts the value of the lane to 12.8 fixed point such that it is
//            easier to understand when using Viewer's debugger. A number like
//            532.5 will be displayed as 0x02 0x14 0x80 0xFF. 532 -> hex is 214
//            0.5 is 0x80 / 0x100 and 0xFF indicates positive (0x00 is negative).
//            Note on lowp there will be no fractional components nor negative numbers.
//   debug_x/y: (lowp only) visualizes the lane, which is a 32 bit float (combined from
//               rg or ba) as 12.8 fixed point (outlined above).

SI void debug_r_255_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                      const size_t dx, const size_t dy,
                      U16& r, U16& g, U16& b, U16& a,
                      U16& dr, U16& dg, U16& db, U16& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 px = cast<U32>(min(r, 255));
    px |= 0xFF000000; // make opaque
    store(ptr, px);
}
SI void debug_g_255_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                      const size_t dx, const size_t dy,
                      U16& r, U16& g, U16& b, U16& a,
                      U16& dr, U16& dg, U16& db, U16& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 px = cast<U32>(min(g, 255)) << 8;
    px |= 0xFF000000; // make opaque
    store(ptr, px);
}
SI void debug_b_255_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                      const size_t dx, const size_t dy,
                      U16& r, U16& g, U16& b, U16& a,
                      U16& dr, U16& dg, U16& db, U16& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 px = cast<U32>(min(b, 255)) << 16;
    px |= 0xFF000000; // make opaque
    store(ptr, px);
}
SI void debug_a_255_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                      const size_t dx, const size_t dy,
                      U16& r, U16& g, U16& b, U16& a,
                      U16& dr, U16& dg, U16& db, U16& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 px = cast<U32>(min(a, 255));
    // Render alpha as greyscale
    store(ptr, px | px << 8 | px << 16 | px << 24);
}

SI void lowp_fixed_point(const SkRasterPipelineContexts::MemoryCtx* ctx,
                         const size_t dx, const size_t dy,
                         const F lane) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 r2 = cast<U32>(abs_(lane) / 256) & 0xFF;
    U32 g2 = cast<U32>(abs_(lane)) & 0xFF;
    U32 b2 = cast<U32>(abs_(lane) * 256) & 0xFF;
    U32 a2 = cast<U32>(min(max(lane * -256 * 256, 0), 0xFF));
    store(ptr, r2 | (g2 << 8) | (b2 << 16) | (a2 << 24));
}

SI void debug_x_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                  const size_t dx, const size_t dy,
                  F& x, F& y) {
    lowp_fixed_point(ctx, dx, dy, x);
}
SI void debug_y_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                  const size_t dx, const size_t dy,
                  F& x, F& y) {
    lowp_fixed_point(ctx, dx, dy, y);
}

// Note that there won't be negative numbers nor fractional points.
SI void lowp_fixed_point(const SkRasterPipelineContexts::MemoryCtx* ctx,
                         const size_t dx, const size_t dy,
                         const U16 lane) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);
    // Flip the byte ordering so it aligns with the fixed point used above
    U32 px = cast<U32>((lane & 0xFF00) >> 8 |
                        (lane & 0x00FF) << 8);
    store(ptr, px);
}

SI void debug_r_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  U16& r, U16& g, U16& b, U16& a,
                  U16& dr, U16& dg, U16& db, U16& da) {
    lowp_fixed_point(ctx, dx, dy, r);
}
SI void debug_g_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  U16& r, U16& g, U16& b, U16& a,
                  U16& dr, U16& dg, U16& db, U16& da) {
    lowp_fixed_point(ctx, dx, dy, g);
}
SI void debug_b_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  U16& r, U16& g, U16& b, U16& a,
                  U16& dr, U16& dg, U16& db, U16& da) {
    lowp_fixed_point(ctx, dx, dy, b);
}
SI void debug_a_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  U16& r, U16& g, U16& b, U16& a,
                  U16& dr, U16& dg, U16& db, U16& da) {
    lowp_fixed_point(ctx, dx, dy, a);
}


// Lowp kernels take their op's context, the coordinates of their first pixel, and, by form:
//   geometry (GG): x and y, as floats, which they may modify;
//   gather (GP): x and y, producing the registers r, g, b, a;
//   pixel (PP): the source and destination registers, which they may modify.
template <SkRasterPipelineOp op>
using LowpGeometryKernel = void (*)(StageContextPointer<op, false>, const size_t, const size_t,
                                    F&, F&);
template <SkRasterPipelineOp op>
using LowpGatherKernel = void (*)(StageContextPointer<op, false>, const size_t, const size_t,
                                  F, F, U16&, U16&, U16&, U16&);
template <SkRasterPipelineOp op>
using LowpPixelKernel = void (*)(StageContextPointer<op, false>, const size_t, const size_t,
                                 U16&, U16&, U16&, U16&, U16&, U16&, U16&, U16&);

// Each lowp stage runs its kernel, advances the program by one stage and calls the next one.
// Geometry travels fused in the register pairs (r,g) and (b,a), so x and y keep float precision.
#if SKRP_NARROW_STAGES
    template <SkRasterPipelineOp op, LowpGeometryKernel<op> kernel>
    static void ABI lowp_geometry_stage(Params* params, SkRasterPipelineStage* program,
                                        U16 r, U16 g, U16 b, U16 a) {
        F x = join<F>(r,g);
        F y = join<F>(b,a);
        kernel(StageContext<op, false>{program}, (*params).dx,(*params).dy, x,y);
        split(x, &r,&g);
        split(y, &b,&a);
        ++program;
        Stage fn = reinterpret_cast<Stage>((*program).fn);
        fn(params, program, r,g,b,a);
    }

    template <SkRasterPipelineOp op, LowpGatherKernel<op> kernel>
    static void ABI lowp_gather_stage(Params* params, SkRasterPipelineStage* program,
                                      U16 r, U16 g, U16 b, U16 a) {
        F x = join<F>(r,g);
        F y = join<F>(b,a);
        kernel(StageContext<op, false>{program}, (*params).dx,(*params).dy, x,y, r,g,b,a);
        ++program;
        Stage fn = reinterpret_cast<Stage>((*program).fn);
        fn(params, program, r,g,b,a);
    }

    template <SkRasterPipelineOp op, LowpPixelKernel<op> kernel>
    static void ABI lowp_pixel_stage(Params* params, SkRasterPipelineStage* program,
                                     U16 r, U16 g, U16 b, U16 a) {
        kernel(StageContext<op, false>{program}, (*params).dx,(*params).dy, r,g,b,a,
               (*params).dr,(*params).dg,(*params).db,(*params).da);
        ++program;
        Stage fn = reinterpret_cast<Stage>((*program).fn);
        fn(params, program, r,g,b,a);
    }
#else
    template <SkRasterPipelineOp op, LowpGeometryKernel<op> kernel>
    static void ABI lowp_geometry_stage(SkRasterPipelineStage* program,
                                        const size_t dx, const size_t dy,
                                        U16  r, U16  g, U16  b, U16  a,
                                        U16 dr, U16 dg, U16 db, U16 da) {
        F x = join<F>(r,g);
        F y = join<F>(b,a);
        kernel(StageContext<op, false>{program}, dx,dy, x,y);
        split(x, &r,&g);
        split(y, &b,&a);
        ++program;
        Stage fn = reinterpret_cast<Stage>((*program).fn);
        fn(program, dx,dy, r,g,b,a, dr,dg,db,da);
    }

    template <SkRasterPipelineOp op, LowpGatherKernel<op> kernel>
    static void ABI lowp_gather_stage(SkRasterPipelineStage* program,
                                      const size_t dx, const size_t dy,
                                      U16  r, U16  g, U16  b, U16  a,
                                      U16 dr, U16 dg, U16 db, U16 da) {
        F x = join<F>(r,g);
        F y = join<F>(b,a);
        kernel(StageContext<op, false>{program}, dx,dy, x,y, r,g,b,a);
        ++program;
        Stage fn = reinterpret_cast<Stage>((*program).fn);
        fn(program, dx,dy, r,g,b,a, dr,dg,db,da);
    }

    template <SkRasterPipelineOp op, LowpPixelKernel<op> kernel>
    static void ABI lowp_pixel_stage(SkRasterPipelineStage* program,
                                     const size_t dx, const size_t dy,
                                     U16  r, U16  g, U16  b, U16  a,
                                     U16 dr, U16 dg, U16 db, U16 da) {
        kernel(StageContext<op, false>{program}, dx,dy, r,g,b,a, dr,dg,db,da);
        ++program;
        Stage fn = reinterpret_cast<Stage>((*program).fn);
        fn(program, dx,dy, r,g,b,a, dr,dg,db,da);
    }
#endif

// The lowp stage for each lowp op, in SkRasterPipelineOp order.
struct LowpStageEntry {
    SkRasterPipelineOp op;
    Stage stage;
};
using Op = SkRasterPipelineOp;
static constexpr std::array<LowpStageEntry, kNumRasterPipelineLowpOps> kLowpStages = {{
        {Op::move_src_dst, lowp_pixel_stage<Op::move_src_dst, move_src_dst_k>},
        {Op::move_dst_src, lowp_pixel_stage<Op::move_dst_src, move_dst_src_k>},
        {Op::swap_src_dst, lowp_pixel_stage<Op::swap_src_dst, swap_src_dst_k>},
        {Op::clamp_01, lowp_pixel_stage<Op::clamp_01, clamp_01_k>},
        {Op::clamp_a_01, lowp_pixel_stage<Op::clamp_a_01, clamp_a_01_k>},
        {Op::clamp_gamut, lowp_pixel_stage<Op::clamp_gamut, clamp_gamut_k>},
        {Op::premul, lowp_pixel_stage<Op::premul, premul_k>},
        {Op::premul_dst, lowp_pixel_stage<Op::premul_dst, premul_dst_k>},
        {Op::force_opaque, lowp_pixel_stage<Op::force_opaque, force_opaque_k>},
        {Op::force_opaque_dst, lowp_pixel_stage<Op::force_opaque_dst, force_opaque_dst_k>},
        {Op::set_rgb, lowp_pixel_stage<Op::set_rgb, set_rgb_k>},
        {Op::swap_rb, lowp_pixel_stage<Op::swap_rb, swap_rb_k>},
        {Op::swap_rb_dst, lowp_pixel_stage<Op::swap_rb_dst, swap_rb_dst_k>},
        {Op::black_color, lowp_pixel_stage<Op::black_color, black_color_k>},
        {Op::white_color, lowp_pixel_stage<Op::white_color, white_color_k>},
        {Op::uniform_color, lowp_pixel_stage<Op::uniform_color, uniform_color_k>},
        {Op::uniform_color_dst, lowp_pixel_stage<Op::uniform_color_dst, uniform_color_dst_k>},
        {Op::seed_shader, lowp_geometry_stage<Op::seed_shader, seed_shader_k>},
        {Op::load_a8, lowp_pixel_stage<Op::load_a8, load_a8_k>},
        {Op::load_a8_dst, lowp_pixel_stage<Op::load_a8_dst, load_a8_dst_k>},
        {Op::store_a8, lowp_pixel_stage<Op::store_a8, store_a8_k>},
        {Op::gather_a8, lowp_gather_stage<Op::gather_a8, gather_a8_k>},
        {Op::load_565, lowp_pixel_stage<Op::load_565, load_565_k>},
        {Op::load_565_dst, lowp_pixel_stage<Op::load_565_dst, load_565_dst_k>},
        {Op::store_565, lowp_pixel_stage<Op::store_565, store_565_k>},
        {Op::gather_565, lowp_gather_stage<Op::gather_565, gather_565_k>},
        {Op::load_4444, lowp_pixel_stage<Op::load_4444, load_4444_k>},
        {Op::load_4444_dst, lowp_pixel_stage<Op::load_4444_dst, load_4444_dst_k>},
        {Op::store_4444, lowp_pixel_stage<Op::store_4444, store_4444_k>},
        {Op::gather_4444, lowp_gather_stage<Op::gather_4444, gather_4444_k>},
        {Op::load_8888, lowp_pixel_stage<Op::load_8888, load_8888_k>},
        {Op::load_8888_dst, lowp_pixel_stage<Op::load_8888_dst, load_8888_dst_k>},
        {Op::store_8888, lowp_pixel_stage<Op::store_8888, store_8888_k>},
        {Op::gather_8888, lowp_gather_stage<Op::gather_8888, gather_8888_k>},
        {Op::load_rg88, lowp_pixel_stage<Op::load_rg88, load_rg88_k>},
        {Op::load_rg88_dst, lowp_pixel_stage<Op::load_rg88_dst, load_rg88_dst_k>},
        {Op::store_rg88, lowp_pixel_stage<Op::store_rg88, store_rg88_k>},
        {Op::gather_rg88, lowp_gather_stage<Op::gather_rg88, gather_rg88_k>},
        {Op::store_r8, lowp_pixel_stage<Op::store_r8, store_r8_k>},
        {Op::alpha_to_gray, lowp_pixel_stage<Op::alpha_to_gray, alpha_to_gray_k>},
        {Op::alpha_to_gray_dst, lowp_pixel_stage<Op::alpha_to_gray_dst, alpha_to_gray_dst_k>},
        {Op::alpha_to_red, lowp_pixel_stage<Op::alpha_to_red, alpha_to_red_k>},
        {Op::alpha_to_red_dst, lowp_pixel_stage<Op::alpha_to_red_dst, alpha_to_red_dst_k>},
        {Op::bt709_luminance_or_luma_to_alpha,
         lowp_pixel_stage<Op::bt709_luminance_or_luma_to_alpha, bt709_luminance_or_luma_to_alpha_k>},
        {Op::bt709_luminance_or_luma_to_rgb,
         lowp_pixel_stage<Op::bt709_luminance_or_luma_to_rgb, bt709_luminance_or_luma_to_rgb_k>},
        {Op::bilerp_clamp_8888, lowp_gather_stage<Op::bilerp_clamp_8888, bilerp_clamp_8888_k>},
        {Op::load_src, lowp_pixel_stage<Op::load_src, load_src_k>},
        {Op::store_src, lowp_pixel_stage<Op::store_src, store_src_k>},
        {Op::store_src_a, lowp_pixel_stage<Op::store_src_a, store_src_a_k>},
        {Op::load_dst, lowp_pixel_stage<Op::load_dst, load_dst_k>},
        {Op::store_dst, lowp_pixel_stage<Op::store_dst, store_dst_k>},
        {Op::scale_u8, lowp_pixel_stage<Op::scale_u8, scale_u8_k>},
        {Op::scale_565, lowp_pixel_stage<Op::scale_565, scale_565_k>},
        {Op::scale_1_float, lowp_pixel_stage<Op::scale_1_float, scale_1_float_k>},
        {Op::scale_native, lowp_pixel_stage<Op::scale_native, scale_native_k>},
        {Op::lerp_u8, lowp_pixel_stage<Op::lerp_u8, lerp_u8_k>},
        {Op::lerp_565, lowp_pixel_stage<Op::lerp_565, lerp_565_k>},
        {Op::lerp_1_float, lowp_pixel_stage<Op::lerp_1_float, lerp_1_float_k>},
        {Op::lerp_native, lowp_pixel_stage<Op::lerp_native, lerp_native_k>},
        {Op::dstatop, lowp_pixel_stage<Op::dstatop, blend_each_channel_k<dstatop_channel>>},
        {Op::dstin, lowp_pixel_stage<Op::dstin, blend_each_channel_k<dstin_channel>>},
        {Op::dstout, lowp_pixel_stage<Op::dstout, blend_each_channel_k<dstout_channel>>},
        {Op::dstover, lowp_pixel_stage<Op::dstover, blend_each_channel_k<dstover_channel>>},
        {Op::srcatop, lowp_pixel_stage<Op::srcatop, blend_each_channel_k<srcatop_channel>>},
        {Op::srcin, lowp_pixel_stage<Op::srcin, blend_each_channel_k<srcin_channel>>},
        {Op::srcout, lowp_pixel_stage<Op::srcout, blend_each_channel_k<srcout_channel>>},
        {Op::srcover, lowp_pixel_stage<Op::srcover, blend_each_channel_k<srcover_channel>>},
        {Op::clear, lowp_pixel_stage<Op::clear, blend_each_channel_k<clear_channel>>},
        {Op::modulate, lowp_pixel_stage<Op::modulate, blend_each_channel_k<modulate_channel>>},
        {Op::multiply, lowp_pixel_stage<Op::multiply, blend_each_channel_k<multiply_channel>>},
        {Op::plus_, lowp_pixel_stage<Op::plus_, blend_each_channel_k<plus__channel>>},
        {Op::screen, lowp_pixel_stage<Op::screen, blend_each_channel_k<screen_channel>>},
        {Op::xor_, lowp_pixel_stage<Op::xor_, blend_each_channel_k<xor__channel>>},
        {Op::darken, lowp_pixel_stage<Op::darken, blend_colors_k<darken_channel>>},
        {Op::difference, lowp_pixel_stage<Op::difference, blend_colors_k<difference_channel>>},
        {Op::exclusion, lowp_pixel_stage<Op::exclusion, blend_colors_k<exclusion_channel>>},
        {Op::hardlight, lowp_pixel_stage<Op::hardlight, blend_colors_k<hardlight_channel>>},
        {Op::lighten, lowp_pixel_stage<Op::lighten, blend_colors_k<lighten_channel>>},
        {Op::overlay, lowp_pixel_stage<Op::overlay, blend_colors_k<overlay_channel>>},
        {Op::srcover_rgba_8888, lowp_pixel_stage<Op::srcover_rgba_8888, srcover_rgba_8888_k>},
        {Op::matrix_translate, lowp_geometry_stage<Op::matrix_translate, matrix_translate_k>},
        {Op::matrix_scale_translate,
         lowp_geometry_stage<Op::matrix_scale_translate, matrix_scale_translate_k>},
        {Op::matrix_2x3, lowp_geometry_stage<Op::matrix_2x3, matrix_2x3_k>},
        {Op::matrix_perspective, lowp_geometry_stage<Op::matrix_perspective, matrix_perspective_k>},
        {Op::decal_x, lowp_geometry_stage<Op::decal_x, decal_x_k>},
        {Op::decal_y, lowp_geometry_stage<Op::decal_y, decal_y_k>},
        {Op::decal_x_and_y, lowp_geometry_stage<Op::decal_x_and_y, decal_x_and_y_k>},
        {Op::check_decal_mask, lowp_pixel_stage<Op::check_decal_mask, check_decal_mask_k>},
        {Op::clamp_x_1, lowp_geometry_stage<Op::clamp_x_1, clamp_x_1_k>},
        {Op::mirror_x_1, lowp_geometry_stage<Op::mirror_x_1, mirror_x_1_k>},
        {Op::repeat_x_1, lowp_geometry_stage<Op::repeat_x_1, repeat_x_1_k>},
        {Op::clamp_x_and_y, lowp_geometry_stage<Op::clamp_x_and_y, clamp_x_and_y_k>},
        {Op::evenly_spaced_gradient,
         lowp_gather_stage<Op::evenly_spaced_gradient, evenly_spaced_gradient_k>},
        {Op::gradient, lowp_gather_stage<Op::gradient, gradient_k>},
        {Op::evenly_spaced_2_stop_gradient,
         lowp_gather_stage<Op::evenly_spaced_2_stop_gradient, evenly_spaced_2_stop_gradient_k>},
        {Op::xy_to_unit_angle, lowp_geometry_stage<Op::xy_to_unit_angle, xy_to_unit_angle_k>},
        {Op::xy_to_radius, lowp_geometry_stage<Op::xy_to_radius, xy_to_radius_k>},
        {Op::emboss, lowp_pixel_stage<Op::emboss, emboss_k>},
        {Op::swizzle, lowp_pixel_stage<Op::swizzle, swizzle_k>},
        {Op::debug_x, lowp_geometry_stage<Op::debug_x, debug_x_k>},
        {Op::debug_y, lowp_geometry_stage<Op::debug_y, debug_y_k>},
        {Op::debug_r, lowp_pixel_stage<Op::debug_r, debug_r_k>},
        {Op::debug_g, lowp_pixel_stage<Op::debug_g, debug_g_k>},
        {Op::debug_b, lowp_pixel_stage<Op::debug_b, debug_b_k>},
        {Op::debug_a, lowp_pixel_stage<Op::debug_a, debug_a_k>},
        {Op::debug_r_255, lowp_pixel_stage<Op::debug_r_255, debug_r_255_k>},
        {Op::debug_g_255, lowp_pixel_stage<Op::debug_g_255, debug_g_255_k>},
        {Op::debug_b_255, lowp_pixel_stage<Op::debug_b_255, debug_b_255_k>},
        {Op::debug_a_255, lowp_pixel_stage<Op::debug_a_255, debug_a_255_k>},
}};
static_assert(stages_in_op_order(kLowpStages));

#endif//defined(SKRP_CPU_SCALAR) controlling whether we build lowp stages
}  // namespace lowp

SI void debug_r_255_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 px = to_unorm(r, 255);
    px |= 0xFF000000; // make opaque
    store(ptr, px);
}
SI void debug_g_255_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 px = to_unorm(g, 255) << 8;
    px |= 0xFF000000; // make opaque
    store(ptr, px);
}
SI void debug_b_255_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 px = to_unorm(b, 255) << 16;
    px |= 0xFF000000; // make opaque
    store(ptr, px);
}
SI void debug_a_255_k(const SkRasterPipelineContexts::MemoryCtx* ctx,
                      const size_t dx, const size_t dy,
                      F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 px = to_unorm(a, 255);
    // Render alpha as greyscale
    store(ptr, px | px << 8 | px << 16 | px << 24);
}

SI void highp_fixed_point(const SkRasterPipelineContexts::MemoryCtx* ctx,
                          const size_t dx, const size_t dy,
                          const F lane) {
    uint32_t* ptr = ptr_at_xy<uint32_t>(ctx, dx,dy);

    U32 r2 = trunc_(abs_(lane) / 256) & 0xFF;
    U32 g2 = trunc_(abs_(lane)) & 0xFF;
    U32 b2 = trunc_(abs_(lane) * 256) & 0xFF;
    U32 a2 = to_unorm(lane * -256 * 256, 255);
    store(ptr, r2 | (g2 << 8) | (b2 << 16) | (a2 << 24));
}

SI void debug_r_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    highp_fixed_point(ctx, dx, dy, r);
}
SI void debug_g_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    highp_fixed_point(ctx, dx, dy, g);
}
SI void debug_b_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    highp_fixed_point(ctx, dx, dy, b);
}
SI void debug_a_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    highp_fixed_point(ctx, dx, dy, a);
}
// The special handling of x and y only make sense in lowp mode. If they are
// called in highp mode (e.g. someone made a lowp pipelin and then added something
// which required highp), we'll just treat x and y as r and g respectively (we have
// to define *some* behavior or this won't link).
SI void debug_x_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    highp_fixed_point(ctx, dx, dy, r);
}
SI void debug_y_k(const SkRasterPipelineContexts::MemoryCtx* ctx, const size_t dx, const size_t dy,
                  F& r, F& g, F& b, F& a, F& dr, F& dg, F& db, F& da) {
    highp_fixed_point(ctx, dx, dy, g);
}

// The highp stage for each op, in SkRasterPipelineOp order.
struct HighpStageEntry {
    SkRasterPipelineOp op;
    Stage stage;
};
using Op = SkRasterPipelineOp;
static constexpr std::array<HighpStageEntry, kNumRasterPipelineHighpOps> kHighpStages = {{
        {Op::move_src_dst, highp_stage<Op::move_src_dst, move_src_dst_k>},
        {Op::move_dst_src, highp_stage<Op::move_dst_src, move_dst_src_k>},
        {Op::swap_src_dst, highp_stage<Op::swap_src_dst, swap_src_dst_k>},
        {Op::clamp_01, highp_stage<Op::clamp_01, clamp_01_k>},
        {Op::clamp_a_01, highp_stage<Op::clamp_a_01, clamp_a_01_k>},
        {Op::clamp_gamut, highp_stage<Op::clamp_gamut, clamp_gamut_k>},
        {Op::premul, highp_stage<Op::premul, premul_k>},
        {Op::premul_dst, highp_stage<Op::premul_dst, premul_dst_k>},
        {Op::force_opaque, highp_stage<Op::force_opaque, force_opaque_k>},
        {Op::force_opaque_dst, highp_stage<Op::force_opaque_dst, force_opaque_dst_k>},
        {Op::set_rgb, highp_stage<Op::set_rgb, set_rgb_k>},
        {Op::swap_rb, highp_stage<Op::swap_rb, swap_rb_k>},
        {Op::swap_rb_dst, highp_stage<Op::swap_rb_dst, swap_rb_dst_k>},
        {Op::black_color, highp_stage<Op::black_color, black_color_k>},
        {Op::white_color, highp_stage<Op::white_color, white_color_k>},
        {Op::uniform_color, highp_stage<Op::uniform_color, uniform_color_k>},
        {Op::uniform_color_dst, highp_stage<Op::uniform_color_dst, uniform_color_dst_k>},
        {Op::seed_shader, highp_stage<Op::seed_shader, seed_shader_k>},
        {Op::load_a8, highp_stage<Op::load_a8, load_a8_k>},
        {Op::load_a8_dst, highp_stage<Op::load_a8_dst, load_a8_dst_k>},
        {Op::store_a8, highp_stage<Op::store_a8, store_a8_k>},
        {Op::gather_a8, highp_stage<Op::gather_a8, gather_a8_k>},
        {Op::load_565, highp_stage<Op::load_565, load_565_k>},
        {Op::load_565_dst, highp_stage<Op::load_565_dst, load_565_dst_k>},
        {Op::store_565, highp_stage<Op::store_565, store_565_k>},
        {Op::gather_565, highp_stage<Op::gather_565, gather_565_k>},
        {Op::load_4444, highp_stage<Op::load_4444, load_4444_k>},
        {Op::load_4444_dst, highp_stage<Op::load_4444_dst, load_4444_dst_k>},
        {Op::store_4444, highp_stage<Op::store_4444, store_4444_k>},
        {Op::gather_4444, highp_stage<Op::gather_4444, gather_4444_k>},
        {Op::load_8888, highp_stage<Op::load_8888, load_8888_k>},
        {Op::load_8888_dst, highp_stage<Op::load_8888_dst, load_8888_dst_k>},
        {Op::store_8888, highp_stage<Op::store_8888, store_8888_k>},
        {Op::gather_8888, highp_stage<Op::gather_8888, gather_8888_k>},
        {Op::load_rg88, highp_stage<Op::load_rg88, load_rg88_k>},
        {Op::load_rg88_dst, highp_stage<Op::load_rg88_dst, load_rg88_dst_k>},
        {Op::store_rg88, highp_stage<Op::store_rg88, store_rg88_k>},
        {Op::gather_rg88, highp_stage<Op::gather_rg88, gather_rg88_k>},
        {Op::store_r8, highp_stage<Op::store_r8, store_r8_k>},
        {Op::alpha_to_gray, highp_stage<Op::alpha_to_gray, alpha_to_gray_k>},
        {Op::alpha_to_gray_dst, highp_stage<Op::alpha_to_gray_dst, alpha_to_gray_dst_k>},
        {Op::alpha_to_red, highp_stage<Op::alpha_to_red, alpha_to_red_k>},
        {Op::alpha_to_red_dst, highp_stage<Op::alpha_to_red_dst, alpha_to_red_dst_k>},
        {Op::bt709_luminance_or_luma_to_alpha,
         highp_stage<Op::bt709_luminance_or_luma_to_alpha, bt709_luminance_or_luma_to_alpha_k>},
        {Op::bt709_luminance_or_luma_to_rgb,
         highp_stage<Op::bt709_luminance_or_luma_to_rgb, bt709_luminance_or_luma_to_rgb_k>},
        {Op::bilerp_clamp_8888, highp_stage<Op::bilerp_clamp_8888, bilerp_clamp_8888_k>},
        {Op::load_src, highp_stage<Op::load_src, load_src_k>},
        {Op::store_src, highp_stage<Op::store_src, store_src_k>},
        {Op::store_src_a, highp_stage<Op::store_src_a, store_src_a_k>},
        {Op::load_dst, highp_stage<Op::load_dst, load_dst_k>},
        {Op::store_dst, highp_stage<Op::store_dst, store_dst_k>},
        {Op::scale_u8, highp_stage<Op::scale_u8, scale_u8_k>},
        {Op::scale_565, highp_stage<Op::scale_565, scale_565_k>},
        {Op::scale_1_float, highp_stage<Op::scale_1_float, scale_1_float_k>},
        {Op::scale_native, highp_stage<Op::scale_native, scale_native_k>},
        {Op::lerp_u8, highp_stage<Op::lerp_u8, lerp_u8_k>},
        {Op::lerp_565, highp_stage<Op::lerp_565, lerp_565_k>},
        {Op::lerp_1_float, highp_stage<Op::lerp_1_float, lerp_1_float_k>},
        {Op::lerp_native, highp_stage<Op::lerp_native, lerp_native_k>},
        {Op::dstatop, highp_stage<Op::dstatop, blend_each_channel_k<dstatop_channel>>},
        {Op::dstin, highp_stage<Op::dstin, blend_each_channel_k<dstin_channel>>},
        {Op::dstout, highp_stage<Op::dstout, blend_each_channel_k<dstout_channel>>},
        {Op::dstover, highp_stage<Op::dstover, blend_each_channel_k<dstover_channel>>},
        {Op::srcatop, highp_stage<Op::srcatop, blend_each_channel_k<srcatop_channel>>},
        {Op::srcin, highp_stage<Op::srcin, blend_each_channel_k<srcin_channel>>},
        {Op::srcout, highp_stage<Op::srcout, blend_each_channel_k<srcout_channel>>},
        {Op::srcover, highp_stage<Op::srcover, blend_each_channel_k<srcover_channel>>},
        {Op::clear, highp_stage<Op::clear, blend_each_channel_k<clear_channel>>},
        {Op::modulate, highp_stage<Op::modulate, blend_each_channel_k<modulate_channel>>},
        {Op::multiply, highp_stage<Op::multiply, blend_each_channel_k<multiply_channel>>},
        {Op::plus_, highp_stage<Op::plus_, blend_each_channel_k<plus__channel>>},
        {Op::screen, highp_stage<Op::screen, blend_each_channel_k<screen_channel>>},
        {Op::xor_, highp_stage<Op::xor_, blend_each_channel_k<xor__channel>>},
        {Op::darken, highp_stage<Op::darken, blend_colors_k<darken_channel>>},
        {Op::difference, highp_stage<Op::difference, blend_colors_k<difference_channel>>},
        {Op::exclusion, highp_stage<Op::exclusion, blend_colors_k<exclusion_channel>>},
        {Op::hardlight, highp_stage<Op::hardlight, blend_colors_k<hardlight_channel>>},
        {Op::lighten, highp_stage<Op::lighten, blend_colors_k<lighten_channel>>},
        {Op::overlay, highp_stage<Op::overlay, blend_colors_k<overlay_channel>>},
        {Op::srcover_rgba_8888, highp_stage<Op::srcover_rgba_8888, srcover_rgba_8888_k>},
        {Op::matrix_translate, highp_stage<Op::matrix_translate, matrix_translate_k>},
        {Op::matrix_scale_translate,
         highp_stage<Op::matrix_scale_translate, matrix_scale_translate_k>},
        {Op::matrix_2x3, highp_stage<Op::matrix_2x3, matrix_2x3_k>},
        {Op::matrix_perspective, highp_stage<Op::matrix_perspective, matrix_perspective_k>},
        {Op::decal_x, highp_stage<Op::decal_x, decal_x_k>},
        {Op::decal_y, highp_stage<Op::decal_y, decal_y_k>},
        {Op::decal_x_and_y, highp_stage<Op::decal_x_and_y, decal_x_and_y_k>},
        {Op::check_decal_mask, highp_stage<Op::check_decal_mask, check_decal_mask_k>},
        {Op::clamp_x_1, highp_stage<Op::clamp_x_1, clamp_x_1_k>},
        {Op::mirror_x_1, highp_stage<Op::mirror_x_1, mirror_x_1_k>},
        {Op::repeat_x_1, highp_stage<Op::repeat_x_1, repeat_x_1_k>},
        {Op::clamp_x_and_y, highp_stage<Op::clamp_x_and_y, clamp_x_and_y_k>},
        {Op::evenly_spaced_gradient,
         highp_stage<Op::evenly_spaced_gradient, evenly_spaced_gradient_k>},
        {Op::gradient, highp_stage<Op::gradient, gradient_k>},
        {Op::evenly_spaced_2_stop_gradient,
         highp_stage<Op::evenly_spaced_2_stop_gradient, evenly_spaced_2_stop_gradient_k>},
        {Op::xy_to_unit_angle, highp_stage<Op::xy_to_unit_angle, xy_to_unit_angle_k>},
        {Op::xy_to_radius, highp_stage<Op::xy_to_radius, xy_to_radius_k>},
        {Op::emboss, highp_stage<Op::emboss, emboss_k>},
        {Op::swizzle, highp_stage<Op::swizzle, swizzle_k>},
        {Op::debug_x, highp_stage<Op::debug_x, debug_x_k>},
        {Op::debug_y, highp_stage<Op::debug_y, debug_y_k>},
        {Op::debug_r, highp_stage<Op::debug_r, debug_r_k>},
        {Op::debug_g, highp_stage<Op::debug_g, debug_g_k>},
        {Op::debug_b, highp_stage<Op::debug_b, debug_b_k>},
        {Op::debug_a, highp_stage<Op::debug_a, debug_a_k>},
        {Op::debug_r_255, highp_stage<Op::debug_r_255, debug_r_255_k>},
        {Op::debug_g_255, highp_stage<Op::debug_g_255, debug_g_255_k>},
        {Op::debug_b_255, highp_stage<Op::debug_b_255, debug_b_255_k>},
        {Op::debug_a_255, highp_stage<Op::debug_a_255, debug_a_255_k>},
        {Op::callback, highp_stage<Op::callback, callback_k>},
        {Op::unbounded_set_rgb, highp_stage<Op::unbounded_set_rgb, unbounded_set_rgb_k>},
        {Op::unbounded_uniform_color,
         highp_stage<Op::unbounded_uniform_color, unbounded_uniform_color_k>},
        {Op::unpremul, highp_stage<Op::unpremul, unpremul_k>},
        {Op::unpremul_polar, highp_stage<Op::unpremul_polar, unpremul_polar_k>},
        {Op::dither, highp_stage<Op::dither, dither_k>},
        {Op::load_16161616, highp_stage<Op::load_16161616, load_16161616_k>},
        {Op::load_16161616_dst, highp_stage<Op::load_16161616_dst, load_16161616_dst_k>},
        {Op::store_16161616, highp_stage<Op::store_16161616, store_16161616_k>},
        {Op::gather_16161616, highp_stage<Op::gather_16161616, gather_16161616_k>},
        {Op::load_a16, highp_stage<Op::load_a16, load_a16_k>},
        {Op::load_a16_dst, highp_stage<Op::load_a16_dst, load_a16_dst_k>},
        {Op::store_a16, highp_stage<Op::store_a16, store_a16_k>},
        {Op::gather_a16, highp_stage<Op::gather_a16, gather_a16_k>},
        {Op::load_r16, highp_stage<Op::load_r16, load_r16_k>},
        {Op::load_r16_dst, highp_stage<Op::load_r16_dst, load_r16_dst_k>},
        {Op::store_r16, highp_stage<Op::store_r16, store_r16_k>},
        {Op::gather_r16, highp_stage<Op::gather_r16, gather_r16_k>},
        {Op::load_rg1616, highp_stage<Op::load_rg1616, load_rg1616_k>},
        {Op::load_rg1616_dst, highp_stage<Op::load_rg1616_dst, load_rg1616_dst_k>},
        {Op::store_rg1616, highp_stage<Op::store_rg1616, store_rg1616_k>},
        {Op::gather_rg1616, highp_stage<Op::gather_rg1616, gather_rg1616_k>},
        {Op::load_f16, highp_stage<Op::load_f16, load_f16_k>},
        {Op::load_f16_dst, highp_stage<Op::load_f16_dst, load_f16_dst_k>},
        {Op::store_f16, highp_stage<Op::store_f16, store_f16_k>},
        {Op::gather_f16, highp_stage<Op::gather_f16, gather_f16_k>},
        {Op::load_af16, highp_stage<Op::load_af16, load_af16_k>},
        {Op::load_af16_dst, highp_stage<Op::load_af16_dst, load_af16_dst_k>},
        {Op::store_af16, highp_stage<Op::store_af16, store_af16_k>},
        {Op::gather_af16, highp_stage<Op::gather_af16, gather_af16_k>},
        {Op::load_rf16, highp_stage<Op::load_rf16, load_rf16_k>},
        {Op::load_rf16_dst, highp_stage<Op::load_rf16_dst, load_rf16_dst_k>},
        {Op::store_rf16, highp_stage<Op::store_rf16, store_rf16_k>},
        {Op::gather_rf16, highp_stage<Op::gather_rf16, gather_rf16_k>},
        {Op::load_rgf16, highp_stage<Op::load_rgf16, load_rgf16_k>},
        {Op::load_rgf16_dst, highp_stage<Op::load_rgf16_dst, load_rgf16_dst_k>},
        {Op::store_rgf16, highp_stage<Op::store_rgf16, store_rgf16_k>},
        {Op::gather_rgf16, highp_stage<Op::gather_rgf16, gather_rgf16_k>},
        {Op::load_f32, highp_stage<Op::load_f32, load_f32_k>},
        {Op::load_f32_dst, highp_stage<Op::load_f32_dst, load_f32_dst_k>},
        {Op::store_f32, highp_stage<Op::store_f32, store_f32_k>},
        {Op::gather_f32, highp_stage<Op::gather_f32, gather_f32_k>},
        {Op::load_1010102, highp_stage<Op::load_1010102, load_1010102_k>},
        {Op::load_1010102_dst, highp_stage<Op::load_1010102_dst, load_1010102_dst_k>},
        {Op::store_1010102, highp_stage<Op::store_1010102, store_1010102_k>},
        {Op::gather_1010102, highp_stage<Op::gather_1010102, gather_1010102_k>},
        {Op::load_1010102_xr, highp_stage<Op::load_1010102_xr, load_1010102_xr_k>},
        {Op::load_1010102_xr_dst, highp_stage<Op::load_1010102_xr_dst, load_1010102_xr_dst_k>},
        {Op::store_1010102_xr, highp_stage<Op::store_1010102_xr, store_1010102_xr_k>},
        {Op::gather_1010102_xr, highp_stage<Op::gather_1010102_xr, gather_1010102_xr_k>},
        {Op::load_10x6, highp_stage<Op::load_10x6, load_10x6_k>},
        {Op::load_10x6_dst, highp_stage<Op::load_10x6_dst, load_10x6_dst_k>},
        {Op::store_10x6, highp_stage<Op::store_10x6, store_10x6_k>},
        {Op::gather_10x6, highp_stage<Op::gather_10x6, gather_10x6_k>},
        {Op::gather_10101010_xr, highp_stage<Op::gather_10101010_xr, gather_10101010_xr_k>},
        {Op::load_10101010_xr, highp_stage<Op::load_10101010_xr, load_10101010_xr_k>},
        {Op::load_10101010_xr_dst, highp_stage<Op::load_10101010_xr_dst, load_10101010_xr_dst_k>},
        {Op::store_10101010_xr, highp_stage<Op::store_10101010_xr, store_10101010_xr_k>},
        {Op::store_src_rg, highp_stage<Op::store_src_rg, store_src_rg_k>},
        {Op::load_src_rg, highp_stage<Op::load_src_rg, load_src_rg_k>},
        {Op::byte_tables, highp_stage<Op::byte_tables, byte_tables_k>},
        {Op::colorburn, highp_stage<Op::colorburn, blend_colors_k<colorburn_channel>>},
        {Op::colordodge, highp_stage<Op::colordodge, blend_colors_k<colordodge_channel>>},
        {Op::softlight, highp_stage<Op::softlight, blend_colors_k<softlight_channel>>},
        {Op::hue, highp_stage<Op::hue, hue_k>},
        {Op::saturation, highp_stage<Op::saturation, saturation_k>},
        {Op::color, highp_stage<Op::color, color_k>},
        {Op::luminosity, highp_stage<Op::luminosity, luminosity_k>},
        {Op::matrix_3x3, highp_stage<Op::matrix_3x3, matrix_3x3_k>},
        {Op::matrix_3x4, highp_stage<Op::matrix_3x4, matrix_3x4_k>},
        {Op::matrix_4x5, highp_stage<Op::matrix_4x5, matrix_4x5_k>},
        {Op::matrix_4x3, highp_stage<Op::matrix_4x3, matrix_4x3_k>},
        {Op::parametric, highp_stage<Op::parametric, parametric_k>},
        {Op::gamma_, highp_stage<Op::gamma_, gamma__k>},
        {Op::PQish, highp_stage<Op::PQish, PQish_k>},
        {Op::HLGish, highp_stage<Op::HLGish, HLGish_k>},
        {Op::HLGinvish, highp_stage<Op::HLGinvish, HLGinvish_k>},
        {Op::ootf, highp_stage<Op::ootf, ootf_k>},
        {Op::rgb_to_hsl, highp_stage<Op::rgb_to_hsl, rgb_to_hsl_k>},
        {Op::hsl_to_rgb, highp_stage<Op::hsl_to_rgb, hsl_to_rgb_k>},
        {Op::css_lab_to_xyz, highp_stage<Op::css_lab_to_xyz, css_lab_to_xyz_k>},
        {Op::css_oklab_to_linear_srgb,
         highp_stage<Op::css_oklab_to_linear_srgb, css_oklab_to_linear_srgb_k>},
        {Op::css_oklab_gamut_map_to_linear_srgb,
         highp_stage<Op::css_oklab_gamut_map_to_linear_srgb, css_oklab_gamut_map_to_linear_srgb_k>},
        {Op::css_hcl_to_lab, highp_stage<Op::css_hcl_to_lab, css_hcl_to_lab_k>},
        {Op::css_hsl_to_srgb, highp_stage<Op::css_hsl_to_srgb, css_hsl_to_srgb_k>},
        {Op::css_hwb_to_srgb, highp_stage<Op::css_hwb_to_srgb, css_hwb_to_srgb_k>},
        {Op::gauss_a_to_rgba, highp_stage<Op::gauss_a_to_rgba, gauss_a_to_rgba_k>},
        {Op::mirror_x, highp_stage<Op::mirror_x, mirror_x_k>},
        {Op::repeat_x, highp_stage<Op::repeat_x, repeat_x_k>},
        {Op::mirror_y, highp_stage<Op::mirror_y, mirror_y_k>},
        {Op::repeat_y, highp_stage<Op::repeat_y, repeat_y_k>},
        {Op::negate_x, highp_stage<Op::negate_x, negate_x_k>},
        {Op::bilerp_clamp_8888_force_highp,
         highp_stage<Op::bilerp_clamp_8888_force_highp, bilerp_clamp_8888_force_highp_k>},
        {Op::bicubic_clamp_8888, highp_stage<Op::bicubic_clamp_8888, bicubic_clamp_8888_k>},
        {Op::bilinear_setup, highp_stage<Op::bilinear_setup, bilinear_setup_k>},
        {Op::bilinear_nx, highp_stage<Op::bilinear_nx, bilinear_nx_k>},
        {Op::bilinear_px, highp_stage<Op::bilinear_px, bilinear_px_k>},
        {Op::bilinear_ny, highp_stage<Op::bilinear_ny, bilinear_ny_k>},
        {Op::bilinear_py, highp_stage<Op::bilinear_py, bilinear_py_k>},
        {Op::bicubic_setup, highp_stage<Op::bicubic_setup, bicubic_setup_k>},
        {Op::bicubic_n3x, highp_stage<Op::bicubic_n3x, bicubic_n3x_k>},
        {Op::bicubic_n1x, highp_stage<Op::bicubic_n1x, bicubic_n1x_k>},
        {Op::bicubic_p1x, highp_stage<Op::bicubic_p1x, bicubic_p1x_k>},
        {Op::bicubic_p3x, highp_stage<Op::bicubic_p3x, bicubic_p3x_k>},
        {Op::bicubic_n3y, highp_stage<Op::bicubic_n3y, bicubic_n3y_k>},
        {Op::bicubic_n1y, highp_stage<Op::bicubic_n1y, bicubic_n1y_k>},
        {Op::bicubic_p1y, highp_stage<Op::bicubic_p1y, bicubic_p1y_k>},
        {Op::bicubic_p3y, highp_stage<Op::bicubic_p3y, bicubic_p3y_k>},
        {Op::accumulate, highp_stage<Op::accumulate, accumulate_k>},
        {Op::perlin_noise, highp_stage<Op::perlin_noise, perlin_noise_k>},
        {Op::mipmap_linear_init, highp_stage<Op::mipmap_linear_init, mipmap_linear_init_k>},
        {Op::mipmap_linear_update, highp_stage<Op::mipmap_linear_update, mipmap_linear_update_k>},
        {Op::mipmap_linear_finish, highp_stage<Op::mipmap_linear_finish, mipmap_linear_finish_k>},
        {Op::xy_to_2pt_conical_strip,
         highp_stage<Op::xy_to_2pt_conical_strip, xy_to_2pt_conical_strip_k>},
        {Op::xy_to_2pt_conical_focal_on_circle,
         highp_stage<Op::xy_to_2pt_conical_focal_on_circle, xy_to_2pt_conical_focal_on_circle_k>},
        {Op::xy_to_2pt_conical_well_behaved,
         highp_stage<Op::xy_to_2pt_conical_well_behaved, xy_to_2pt_conical_well_behaved_k>},
        {Op::xy_to_2pt_conical_smaller,
         highp_stage<Op::xy_to_2pt_conical_smaller, xy_to_2pt_conical_smaller_k>},
        {Op::xy_to_2pt_conical_greater,
         highp_stage<Op::xy_to_2pt_conical_greater, xy_to_2pt_conical_greater_k>},
        {Op::alter_2pt_conical_compensate_focal,
         highp_stage<Op::alter_2pt_conical_compensate_focal, alter_2pt_conical_compensate_focal_k>},
        {Op::alter_2pt_conical_unswap,
         highp_stage<Op::alter_2pt_conical_unswap, alter_2pt_conical_unswap_k>},
        {Op::mask_2pt_conical_nan, highp_stage<Op::mask_2pt_conical_nan, mask_2pt_conical_nan_k>},
        {Op::mask_2pt_conical_degenerates,
         highp_stage<Op::mask_2pt_conical_degenerates, mask_2pt_conical_degenerates_k>},
        {Op::apply_vector_mask, highp_stage<Op::apply_vector_mask, apply_vector_mask_k>},
}};
static_assert(stages_in_op_order(kHighpStages));

/* This gives us SK_OPTS::lowp::N if lowp::N has been set, or SK_OPTS::N if it hasn't. */
namespace lowp { static constexpr size_t lowp_N = N; }

/** Allow outside code to access the Raster Pipeline pixel stride. */
constexpr size_t raster_pipeline_lowp_stride() { return lowp::lowp_N; }
constexpr size_t raster_pipeline_highp_stride() { return N; }

}  // namespace SK_OPTS_NS

#undef SI

#endif//SkRasterPipeline_opts_DEFINED
