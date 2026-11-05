/*
 * Copyright 2017 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkSafeMath_DEFINED
#define SkSafeMath_DEFINED

#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h" // IWYU pragma: keep
#include "include/private/SkTFitsIn.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>

// SkSafeMath checks that a series of operations do not overflow.
// This must be correct for all platforms, because this is a check for safety at runtime.

class SkSafeMath {
public:
    SkSafeMath() = default;

    bool ok() const { return fOK; }
    explicit operator bool() const { return fOK; }

    size_t mul(size_t x, size_t y) {
        size_t result = 0;
        if constexpr (sizeof(size_t) == sizeof(uint64_t)) {
            result = mul64(x, y);
        } else {
            result = mul32(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        }
        return result;
    }

    size_t add(size_t x, size_t y) {
        const size_t result = x + y;
        fOK &= result >= x;
        return result;
    }

    size_t sub(size_t x, size_t y) {
        fOK &= x >= y;
        const size_t result = x - y;
        return result;
    }

    int addInt(int a, int b) {
        const int64_t result = static_cast<int64_t>(a) + static_cast<int64_t>(b);
        if (!SkTFitsIn<int>(result)) {
            fOK = false;
        }
        const int narrowed = static_cast<int>(result);
        return narrowed;
    }

    int subInt(int a, int b) {
        const int64_t result = static_cast<int64_t>(a) - static_cast<int64_t>(b);
        if (!SkTFitsIn<int>(result)) {
            fOK = false;
        }
        const int narrowed = static_cast<int>(result);
        return narrowed;
    }

    int mulInt(int x, int y) {
        const int64_t result = static_cast<int64_t>(x) * static_cast<int64_t>(y);
        if (!SkTFitsIn<int>(result)) {
            fOK = false;
        }
        const int narrowed = static_cast<int>(result);
        return narrowed;
    }

    int divInt(int a, int b) {
        if (b == 0 || (a == std::numeric_limits<int>::min() && b == -1)) {
            fOK = false;
            return a;
        }
        const int quotient = a / b;
        return quotient;
    }

    int modInt(int a, int b) {
        if (b == 0 || (a == std::numeric_limits<int>::min() && b == -1)) {
            fOK = false;
            return a;
        }
        const int remainder = a % b;
        return remainder;
    }

    size_t alignUp(size_t x, size_t alignment) {
        SkASSERT(alignment && !(alignment & (alignment - 1)));
        const size_t mask = alignment - 1;
        const size_t enlarged = add(x, mask);
        const size_t aligned = enlarged & ~mask;
        return aligned;
    }

    size_t alignUpNonPow2(size_t x, size_t alignment) {
        if (alignment == 0) {
            fOK = false;
            return 0;
        }
        const size_t enlarged = add(x, alignment - 1);
        const size_t blocks = enlarged / alignment;
        const size_t aligned = blocks * alignment;
        return aligned;
    }

    size_t lcm(size_t a, size_t b) {
        if (a == 0 || b == 0) {
            fOK = false;
            return 0;
        }
        const size_t divisor = std::gcd(a, b);
        const size_t reduced = a / divisor;
        const size_t result = mul(reduced, b);
        return result;
    }

    template <typename TDst, typename TSrc> TDst castTo(TSrc value) {
        if (!SkTFitsIn<TDst, TSrc>(value)) {
            fOK = false;
        }
        const TDst converted = static_cast<TDst>(value);
        return converted;
    }

    // Add and Mul return SIZE_MAX on overflow. Align4 preserves the wrapped result;
    // its callers must ensure that rounding up to four bytes is representable.
    static size_t Add(size_t x, size_t y);
    static size_t Mul(size_t x, size_t y);
    static size_t Align4(size_t x) {
        SkSafeMath safe;
        const size_t aligned = safe.alignUp(x, 4);
        return aligned;
    }

private:
    uint32_t mul32(uint32_t x, uint32_t y) {
        const uint64_t bx = x;
        const uint64_t by = y;
        const uint64_t result = bx * by;
        fOK &= result >> 32 == 0;
        // Overflow information is captured in fOK. Return the result modulo 2^32.
        const uint32_t low = static_cast<uint32_t>(result);
        return low;
    }

    uint64_t mul64(uint64_t x, uint64_t y) {
        constexpr uint64_t low_mask = 0xFFFFFFFFULL;
        if (x <= low_mask && y <= low_mask) {
            const uint64_t product = x * y;
            return product;
        }
        const uint64_t x_low = x & low_mask;
        const uint64_t x_high = x >> 32;
        const uint64_t y_low = y & low_mask;
        const uint64_t y_high = y >> 32;
        const uint64_t low_low = x_low * y_low;
        const uint64_t high_low = x_high * y_low;
        const uint64_t low_high = x_low * y_high;
        const uint64_t high_high = x_high * y_high;
        uint64_t result = add(low_low, high_low << 32);
        result = add(result, low_high << 32);
        const uint64_t high_sum = high_high + (high_low >> 32) + (low_high >> 32);
        fOK &= high_sum == 0;

#if defined(SK_DEBUG) && defined(__clang__) && defined(__x86_64__)
        const unsigned __int128 check = static_cast<unsigned __int128>(x) * y;
        SkASSERT(result == (check & 0xFFFFFFFFFFFFFFFFULL));
        SkASSERT(!fOK || (check >> 64 == 0));
#endif
        return result;
    }

    bool fOK = true;
};

#endif  // SkSafeMath_DEFINED
