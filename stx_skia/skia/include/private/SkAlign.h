/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkAlign_DEFINED
#define SkAlign_DEFINED

#include "include/private/SkAssert.h"
#include <cstddef>

// Integer size/address alignment. Signed inputs are nonnegative and their
// rounding addition must be representable. Unsigned wrap is intentionally
// preserved for existing format checks; validate allocation extents separately.
template <typename T> static constexpr T SkAlign2(T value) {
    const T aligned = (value + 1) >> 1 << 1;
    return aligned;
}
template <typename T> static constexpr T SkAlign4(T value) {
    const T aligned = (value + 3) >> 2 << 2;
    return aligned;
}
template <typename T> static constexpr T SkAlign8(T value) {
    const T aligned = (value + 7) >> 3 << 3;
    return aligned;
}
template <typename T> static constexpr T SkAlign16(T value) {
    const T aligned = (value + 15) >> 4 << 4;
    return aligned;
}
template <typename T> static constexpr bool SkIsAlign2(T value) {
    const bool aligned = (value & 1) == 0;
    return aligned;
}
template <typename T> static constexpr bool SkIsAlign4(T value) {
    const bool aligned = (value & 3) == 0;
    return aligned;
}
template <typename T> static constexpr bool SkIsAlign8(T value) {
    const bool aligned = (value & 7) == 0;
    return aligned;
}
template <typename T> static constexpr bool SkIsAlign16(T value) {
    const bool aligned = (value & 15) == 0;
    return aligned;
}
template <typename T> static constexpr T SkAlignPtr(T value) {
    static_assert(sizeof(void*) == 4 || sizeof(void*) == 8);
    if constexpr (sizeof(void*) == 8) {
        const T aligned = SkAlign8(value);
        return aligned;
    } else {
        const T aligned = SkAlign4(value);
        return aligned;
    }
}
template <typename T> static constexpr bool SkIsAlignPtr(T value) {
    static_assert(sizeof(void*) == 4 || sizeof(void*) == 8);
    if constexpr (sizeof(void*) == 8) {
        const bool aligned = SkIsAlign8(value);
        return aligned;
    } else {
        const bool aligned = SkIsAlign4(value);
        return aligned;
    }
}

// alignment is a positive power of two, representable in T.
template <typename T> static constexpr T SkAlignTo(T value, T alignment) {
    SkASSERT(alignment > 0 && (alignment & (alignment - 1)) == 0);
    const T aligned = (value + alignment - 1) & ~(alignment - 1);
    return aligned;
}

// alignment is positive. The rounding addition must be representable.
template <typename T> static constexpr T SkAlignNonPow2(T value, T alignment) {
    const T aligned = ((value + alignment - 1) / alignment) * alignment;
    return aligned;
}

#endif
