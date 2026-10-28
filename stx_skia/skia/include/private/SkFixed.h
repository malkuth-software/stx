/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkFixed_DEFINED
#define SkFixed_DEFINED

#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h"
#include "include/private/SkMath.h" // IWYU pragma: keep
#include "include/private/SkTPin.h" // IWYU pragma: keep
#include "include/private/SkTo.h"  // SkFixedDiv

#include <cstdint>

/** \file SkFixed.h

    Types and macros for 16.16 fixed point
*/

/** 32 bit signed integer used to represent fractions values with 16 bits to the right of the decimal point
*/
using SkFixed = int32_t;
#define SK_Fixed1           (1 << 16)
#define SK_FixedHalf        (1 << 15)
#define SK_FixedQuarter     (1 << 14)
#define SK_FixedMax         (0x7FFFFFFF)
#define SK_FixedMin         (-SK_FixedMax)
#define SK_FixedPI          (0x3243F)
#define SK_FixedSqrt2       (92682)
#define SK_FixedTanPIOver8  (0x6A0A)
#define SK_FixedRoot2Over2  (0xB505)

// NOTE: SkFixedToFloat is exact. SkFloatToFixed seems to lack a rounding step. For all fixed-point
// values, this version is as accurate as possible for (fixed -> float -> fixed). Rounding reduces
// accuracy if the intermediate floats are in the range that only holds integers (adding 0.5f to an
// odd integer then snaps to nearest even). Using double for the rounding math gives maximum
// accuracy for (float -> fixed -> float), but that's usually overkill.
#define SkFixedToFloat(x)   ((x) * 1.52587890625e-5f)
#define SkFloatToFixed(x)   sk_float_saturate2int((x) * SK_Fixed1)

#ifdef SK_DEBUG
    inline SkFixed SkFloatToFixed_Check(float x) {
        const int64_t n64 = static_cast<int64_t>(x * SK_Fixed1);
        const SkFixed n32 = static_cast<SkFixed>(n64);
        SkASSERT(n64 == n32);
        return n32;
    }
#else
    #define SkFloatToFixed_Check(x) SkFloatToFixed(x)
#endif

#define SkFixedToDouble(x)  ((x) * 1.52587890625e-5)
// Exact over the whole int32 range (INT32_MIN included, as in the intake cast), and saturating
// beyond it, where the intake cast was undefined. NaN gives SK_MaxS32, as the saturating float
// conversions do.
inline SkFixed SkDoubleToFixedChecked(double x) {
    const double scaled = x * SK_Fixed1;
    if (scaled != scaled) {
        return SK_MaxS32;
    }
    if (scaled >= 2147483647.0) {
        return INT32_MAX;
    }
    if (scaled <= -2147483648.0) {
        return INT32_MIN;
    }
    return static_cast<SkFixed>(scaled);
}
#define SkDoubleToFixed(x)  SkDoubleToFixedChecked(x)

/** Converts an integer to a SkFixed, asserting that the result does not overflow
    a 32 bit signed integer
*/
#ifdef SK_DEBUG
    inline SkFixed SkIntToFixed(int n)
    {
        SkASSERT(n >= -32768 && n <= 32767);
        // Left shifting a negative value has undefined behavior in C, so we cast to unsigned before
        // shifting.
        return static_cast<SkFixed>(static_cast<unsigned>(n) << 16);
    }
#else
    // Left shifting a negative value has undefined behavior in C, so we cast to unsigned before
    // shifting. Then we force the cast to SkFixed to ensure that the answer is signed (like the
    // debug version).
    #define SkIntToFixed(n)     (SkFixed)((unsigned)(n) << 16)
#endif

// Rounded in 64 bits: in 32, a value near SK_FixedMax overflowed before the shift.
#define SkFixedRoundToInt(x)    (static_cast<int>((static_cast<int64_t>(x) + SK_FixedHalf) >> 16))
#define SkFixedCeilToInt(x)     (static_cast<int>((static_cast<int64_t>(x) + SK_Fixed1 - 1) >> 16))
#define SkFixedFloorToInt(x)    ((x) >> 16)

// The largest whole number a SkFixed holds; rounding up past it saturates there.
inline constexpr SkFixed SK_FixedMaxWhole = 0x7FFF0000;

// Computed in 64 bits: in 32, the addition overflowed for values near SK_FixedMax.
inline SkFixed SkFixedRoundToFixed(SkFixed x) {
    const int64_t rounded = (static_cast<int64_t>(x) + SK_FixedHalf) & ~int64_t{0xFFFF};
    return static_cast<SkFixed>(rounded > SK_FixedMaxWhole ? SK_FixedMaxWhole : rounded);
}
inline SkFixed SkFixedCeilToFixed(SkFixed x) {
    const int64_t rounded = (static_cast<int64_t>(x) + SK_Fixed1 - 1) & ~int64_t{0xFFFF};
    return static_cast<SkFixed>(rounded > SK_FixedMaxWhole ? SK_FixedMaxWhole : rounded);
}
inline SkFixed SkFixedFloorToFixed(SkFixed x) {
    return static_cast<SkFixed>(static_cast<uint32_t>(x) & 0xFFFF0000);
}

// Averaged in 64 bits, so that the sum of two large values does not overflow.
#define SkFixedAve(a, b)    (static_cast<int>((static_cast<int64_t>(a) + (b)) >> 1))

// The divide may exceed 32 bits. Clamp to a signed 32 bit result. A zero denominator saturates
// to the sign of the numerator (0/0 gives 0); the intake macro divided by it.
constexpr SkFixed SkFixedDiv(int64_t numer, int64_t denom) {
    const int64_t shifted = SkLeftShift(numer, 16);
    if (denom == 0) {
        return shifted > 0 ? SK_MaxS32 : (shifted < 0 ? SK_MinS32 : 0);
    }
    return SkToS32(SkTPin<int64_t>(shifted / denom, SK_MinS32, SK_MaxS32));
}

inline SkFixed SkFixedMul(SkFixed a, SkFixed b) {
    return static_cast<SkFixed>(static_cast<int64_t>(a) * b >> 16);
}

///////////////////////////////////////////////////////////////////////////////
// Platform-specific alternatives to our portable versions.

// The VCVT float-to-fixed instruction is part of the VFPv3 instruction set.
#if defined(__ARM_VFPV3__)
    #include <cstring>

    /* This does not handle NaN or other obscurities, but is faster than
       than (int)(x*65536).  When built on Android with -Os, needs forcing
       to inline or we lose the speed benefit.
    */
    SK_ALWAYS_INLINE SkFixed SkFloatToFixed_arm(float x)
    {
        int32_t y;
        asm("vcvt.s32.f32 %0, %0, #16": "+w"(x));
        std::memcpy(&y, &x, sizeof(y));
        return y;
    }
    #undef SkFloatToFixed
    #define SkFloatToFixed(x)  SkFloatToFixed_arm(x)
#endif

///////////////////////////////////////////////////////////////////////////////

#define SkFixedToScalar(x)          SkFixedToFloat(x)
#define SkScalarToFixed(x)          SkFloatToFixed(x)

///////////////////////////////////////////////////////////////////////////////

using SkFixed3232 = int64_t;   // 32.32

#define SkFixed3232Max            SK_MaxS64
#define SkFixed3232Min            (-SkFixed3232Max)

#define SkIntToFixed3232(x)       (SkLeftShift((SkFixed3232)(x), 32))
#define SkFixed3232ToInt(x)       ((int)((x) >> 32))
#define SkFixedToFixed3232(x)     (SkLeftShift((SkFixed3232)(x), 16))
#define SkFixed3232ToFixed(x)     ((SkFixed)((x) >> 16))
#define SkFloatToFixed3232(x)     sk_float_saturate2int64((x) * (65536.0f * 65536.0f))
#define SkFixed3232ToFloat(x)     (x * (1 / (65536.0f * 65536.0f)))

#define SkScalarToFixed3232(x)    SkFloatToFixed3232(x)

#endif
