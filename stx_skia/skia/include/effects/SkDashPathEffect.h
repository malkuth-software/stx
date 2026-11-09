/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkDashPathEffect_DEFINED
#define SkDashPathEffect_DEFINED

#include "include/core/SkPathEffect.h"  // IWYU pragma: keep    (for unspanned apis)
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSpan.h"
#include "include/core/SkTypes.h"

/** Why SkDashPathEffect::Make made no effect. The checks run in this order. */
enum class SkDashRejection : uint8_t {
    kNone,                // an effect was made
    kIntervalCount,       // fewer than 2 intervals, or an odd number
    kNegativeInterval,    // an interval is less than zero
    kNonFinite,           // the phase, an interval or the intervals' sum is not finite
    kZeroLength,          // every interval is zero
};

/** The result of SkDashPathEffect::Make: effect is null exactly when rejection is not kNone. */
struct SkDashPathEffectResult {
    sk_sp<SkPathEffect> effect;
    SkDashRejection rejection = SkDashRejection::kNone;
};

class SK_API SkDashPathEffect {
public:
    /** intervals: array containing an even number of entries (>=2), with
         the even indices specifying the length of "on" intervals, and the odd
         indices specifying the length of "off" intervals. This array will be
         copied in Make, and can be disposed of freely after.
        count: number of elements in the intervals array
        phase: offset into the intervals array (mod the sum of all of the
         intervals).

        For example: if intervals[] = {10, 20}, count = 2, and phase = 25,
         this will set up a dashed path like so:
         5 pixels off
         10 pixels on
         20 pixels off
         10 pixels on
         20 pixels off
         ...
        A phase of -5, 25, 55, 85, etc. would all result in the same path,
         because the sum of all the intervals is 30.

        Note: only affects stroked paths.

        Returns the effect, or the reason none was made.
    */
    static SkDashPathEffectResult Make(SkSpan<const SkScalar> intervals, SkScalar phase);
};

#endif
