/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkAnalyticEdge_DEFINED
#define SkAnalyticEdge_DEFINED

#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h"
#include "include/private/SkFixed.h"
#include "include/private/SkSafe32.h"

#include <cstdint>

struct SkPoint;

struct SkAnalyticEdge {
    // Similar to SkEdge, the conic edges will be converted to quadratic edges
    enum class Type : int8_t {
        kLine,
        kQuad,
        kCubic,
    };
    enum class Winding : int8_t {
        kCW = 1,    // clockwise
        kCCW = -1,  // counter clockwise
    };

    // Links borrow edges owned by the edge builder's arena (or a scan converter's sentinels).
    SkAnalyticEdge* fNext = nullptr;
    SkAnalyticEdge* fPrev = nullptr;

    SkFixed fX = 0;
    SkFixed fDX = 0;
    SkFixed fUpperX = 0;    // The x value when y = fUpperY
    SkFixed fY = 0;         // The current y
    SkFixed fUpperY = 0;    // The upper bound of y (our edge is from y = fUpperY to y = fLowerY)
    SkFixed fLowerY = 0;    // The lower bound of y (our edge is from y = fUpperY to y = fLowerY)
    SkFixed fDY = 0;        // abs(1/fDX); may be SK_MaxS32 when fDX is close to 0.
                            // fDY is only used for blitting trapezoids.

    Type fEdgeType = Type::kLine;  // Remembers the *initial* edge type

    // only used by kQuad(+) and kCubic(-); the sign selects the subclass in update().
    int8_t  fCurveCount = 0;
    // The parametric subdivision shift (stepExponent) used to scale derivatives during
    // forward-differencing. In quadratics, it scales the first derivative during position
    // updates. In cubics, it scales the second derivative, while the first derivative
    // uses fToFixedShift instead.
    uint8_t fCurveShift = 0;
    Winding fWinding = Winding::kCW;

    static constexpr int kDefaultAccuracy = 2;  // default accuracy for snapping

    static inline SkFixed SnapY(SkFixed y) {
        constexpr int accuracy = kDefaultAccuracy;
        // This approach is safer than left shift, round, then right shift
        const SkFixed snapped = static_cast<SkFixed>(
                (static_cast<unsigned>(y) + (SK_Fixed1 >> (accuracy + 1))) >> (16 - accuracy)
                << (16 - accuracy));
        return snapped;
    }

    // Update fX, fY of this edge so fY = y
    inline void goY(SkFixed y) {
        if (y == fY + SK_Fixed1) {
            fX = fX + fDX;
            fY = y;
        } else if (y != fY) {
            // Drop lower digits as our alpha only has 8 bits
            // (fDX and y - fUpperY may be greater than SK_Fixed1)
            fX = fUpperX + SkFixedMul(fDX, y - fUpperY);
            fY = y;
        }
    }

    inline void goY(SkFixed y, int yShift) {
        SkASSERT(yShift >= 0 && yShift <= kDefaultAccuracy);
        SkASSERT(fDX == 0 || y - fY == SK_Fixed1 >> yShift);
        fY = y;
        fX += fDX >> yShift;
    }

    bool setLine(const SkPoint& p0, const SkPoint& p1);
    bool updateLine(SkFixed ax, SkFixed ay, SkFixed bx, SkFixed by, SkFixed slope);

    // return true if we're NOT done with this edge
    bool update(SkFixed last_y);

#ifdef SK_DEBUG
    void dump() const {
        SkDebugf("edge: upperY:%d lowerY:%d y:%g x:%g dx:%g w:%d\n",
                 fUpperY,
                 fLowerY,
                 SkFixedToFloat(fY),
                 SkFixedToFloat(fX),
                 SkFixedToFloat(fDX),
                 static_cast<int8_t>(fWinding));
    }

    void validate() const {
         SkASSERT(fPrev && fNext);
         SkASSERT((*fPrev).fNext == this);
         SkASSERT((*fNext).fPrev == this);

         SkASSERT(fUpperY < fLowerY);
         SkASSERT(fWinding == Winding::kCW || fWinding == Winding::kCCW);
    }
#endif
};

struct SkAnalyticQuadraticEdge : public SkAnalyticEdge {
    SkFixed fQx = 0;
    SkFixed fQy = 0;
    SkFixed fQDx = 0;
    SkFixed fQDy = 0;
    SkFixed fQDDx = 0;
    SkFixed fQDDy = 0;
    SkFixed fQLastX = 0;
    SkFixed fQLastY = 0;

    // snap y to integer points in the middle of the curve to accelerate AAA path filling
    SkFixed fSnappedX = 0;
    SkFixed fSnappedY = 0;

    bool setQuadraticWithoutUpdate(const SkPoint pts[3], int shiftUp);
    bool setQuadratic(const SkPoint pts[3]);
    bool updateQuadratic();
    inline void keepContinuous() {
        // We use fX as the starting x to ensure the continuouty.
        // Without it, we may break the sorted edge list.
        SkASSERT(SkAbs32(fX - SkFixedMul(fY - fSnappedY, fDX) - fSnappedX) < SK_Fixed1);
        SkASSERT(SkAbs32(fY - fSnappedY) < SK_Fixed1); // This may differ due to smooth jump
        fSnappedX = fX;
        fSnappedY = fY;
    }
};

struct SkAnalyticCubicEdge : public SkAnalyticEdge {
    SkFixed fCx = 0;
    SkFixed fCy = 0;
    SkFixed fCDx = 0;
    SkFixed fCDy = 0;
    SkFixed fCDDx = 0;
    SkFixed fCDDy = 0;
    SkFixed fCDDDx = 0;
    SkFixed fCDDDy = 0;
    SkFixed fCLastX = 0;
    SkFixed fCLastY = 0;

    SkFixed fSnappedY = 0; // to make sure that y is increasing with smooth jump and snapping

    uint8_t fToFixedShift = 0;    // applied to fCDx and fCDy

    bool setCubicWithoutUpdate(const SkPoint pts[4], int shiftUp);
    bool setCubic(const SkPoint pts[4]);
    bool updateCubic();
    inline void keepContinuous() {
        SkASSERT(SkAbs32(fX - SkFixedMul(fDX, fY - SnapY(fCy)) - fCx) < SK_Fixed1);
        fCx = fX;
        fSnappedY = fY;
    }
};

#endif
