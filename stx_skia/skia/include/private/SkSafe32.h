/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkSafe32_DEFINED
#define SkSafe32_DEFINED

#include "include/private/SkAssert.h"
#include "include/private/SkMath.h"

#include <cstdint>

constexpr int32_t Sk64_pin_to_s32(int64_t x) {
    return x < SK_MinS32 ? SK_MinS32 : (x > SK_MaxS32 ? SK_MaxS32 : static_cast<int32_t>(x));
}

constexpr int32_t Sk32_sat_add(int32_t a, int32_t b) {
    return Sk64_pin_to_s32(static_cast<int64_t>(a) + static_cast<int64_t>(b));
}

constexpr int32_t Sk32_sat_sub(int32_t a, int32_t b) {
    return Sk64_pin_to_s32(static_cast<int64_t>(a) - static_cast<int64_t>(b));
}

// To avoid UBSAN complaints about 2's compliment overflows
//
constexpr int32_t Sk32_can_overflow_add(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}
constexpr int32_t Sk32_can_overflow_sub(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

/**
 * This is a 'safe' abs for 32-bit integers. SkTAbs (in SkTemplates.h) is a general purpose
 * absolute-value function.
 */
// The most negative int32_t has no positive counterpart; its magnitude saturates to SK_MaxS32.
// (The intake code negated it, which is undefined and in practice left it negative, so a
// huge magnitude compared as tiny.) The scan converters call this on saturated differences,
// which can be exactly that value.
inline int32_t SkAbs32(int32_t value) {
    if (value == SK_NaN32) {
        return SK_MaxS32;
    }
    if (value < 0) {
        value = -value;
    }
    return value;
}

#endif
