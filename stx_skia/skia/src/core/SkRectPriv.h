/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRectPriv_DEFINED
#define SkRectPriv_DEFINED

#include "include/core/SkRect.h"
#include "src/core/SkMathPriv.h"

// Rectangle helpers retained by clipping, scan conversion, drawing and rounded rectangles.
class SkRectPriv {
public:
    // Returns an irect that is very large, and can be safely round-trip with SkRect and still
    // be considered non-empty (i.e. width/height > 0) even if we round-out the SkRect.
    static SkIRect MakeILarge() {
        // SK_MaxS32 >> 1 seemed better, but it did not survive round-trip with SkRect and rounding.
        // Also, 1 << 29 can be perfectly represented in float, while SK_MaxS32 >> 1 cannot.
        const int32_t large = 1 << 29;
        const SkIRect bounds{-large, -large, large, large};
        return bounds;
    }

    static SkRect MakeLargeS32() {
        SkRect r{};
        r.set(MakeILarge());
        return r;
    }

    // Conservative check if r can be expressed in fixed-point.
    // Will return false for very large values that might have fit
    static bool FitsInFixed(const SkRect& r) {
        const bool fits = SkFitsInFixed(r.fLeft) && SkFitsInFixed(r.fTop) &&
                          SkFitsInFixed(r.fRight) && SkFitsInFixed(r.fBottom);
        return fits;
    }

    // Returns r.width()/2 but divides first to avoid width() overflowing.
    static constexpr float HalfWidth(const SkRect& r) {
        const float half = sk_float_midpoint(-r.fLeft, r.fRight);
        return half;
    }
    // Returns r.height()/2 but divides first to avoid height() overflowing.
    static constexpr float HalfHeight(const SkRect& r) {
        const float half = sk_float_midpoint(-r.fTop, r.fBottom);
        return half;
    }

    // Evaluate A-B. If the difference shape cannot be represented as a rectangle then false is
    // returned and 'out' is set to the largest rectangle contained in said shape. If true is
    // returned then A-B is representable as a rectangle, which is stored in 'out'.
    // 'out' may alias either input; it is written once after the result is known.
    static bool Subtract(const SkIRect& a, const SkIRect& b, SkIRect* out);
};


#endif
