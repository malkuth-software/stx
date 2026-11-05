/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/core/SkStrokerPriv.h"

#include "include/core/SkMatrix.h"
#include "include/core/SkPathBuilder.h"
#include "include/private/SkAssert.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkPointPriv.h"

#include <cmath>
#include <utility>

static void ButtCapper(SkPathBuilder* sink, const SkPoint& /*pivot*/, const SkVector& /*normal*/,
                       const SkPoint& stop, bool) {
    (*sink).lineTo(stop.fX, stop.fY);
}

static void RoundCapper(SkPathBuilder* sink, const SkPoint& pivot, const SkVector& normal,
                        const SkPoint& stop, bool) {
    SkVector parallel{0, 0};
    SkPointPriv::RotateCW(normal, &parallel);

    const SkPoint projectedCenter = pivot + parallel;

    (*sink).conicTo(projectedCenter + normal, projectedCenter, SK_ScalarRoot2Over2);
    (*sink).conicTo(projectedCenter - normal, stop, SK_ScalarRoot2Over2);
}

static void SquareCapper(SkPathBuilder* sink, const SkPoint& pivot, const SkVector& normal,
                         const SkPoint& stop, bool extendLastPt) {
    SkVector parallel{0, 0};
    SkPointPriv::RotateCW(normal, &parallel);

    if (extendLastPt) {
        (*sink).setLastPoint(pivot + normal + parallel);
        (*sink).lineTo(pivot - normal + parallel);
    } else {
        (*sink).lineTo(pivot + normal + parallel);
        (*sink).lineTo(pivot - normal + parallel);
        (*sink).lineTo(stop);
    }
}

/////////////////////////////////////////////////////////////////////////////

static bool is_clockwise(const SkVector& before, const SkVector& after) {
    const bool clockwise = before.fX * after.fY > before.fY * after.fX;
    return clockwise;
}

enum AngleType {
    kNearly180_AngleType,
    kSharp_AngleType,
    kShallow_AngleType,
    kNearlyLine_AngleType
};

static AngleType Dot2AngleType(SkScalar dot) {
// need more precise fixed normalization
//  SkASSERT(SkScalarAbs(dot) <= SK_Scalar1 + SK_ScalarNearlyZero);

    AngleType angleType = kSharp_AngleType;
    if (dot >= 0) { // shallow or line
        angleType = SkScalarNearlyZero(SK_Scalar1 - dot) ? kNearlyLine_AngleType
                                                         : kShallow_AngleType;
    } else {           // sharp or 180
        angleType = SkScalarNearlyZero(SK_Scalar1 + dot) ? kNearly180_AngleType
                                                         : kSharp_AngleType;
    }
    return angleType;
}

static void HandleInnerJoin(SkPathBuilder* inner, const SkPoint& pivot, const SkVector& after) {
    /*  In the degenerate case that the stroke radius is larger than our segments
        just connecting the two inner segments may "show through" as a funny
        diagonal. To pseudo-fix this, we go through the pivot point. This adds
        an extra point/edge, but I can't see a cheap way to know when this is
        not needed :(
    */
    (*inner).lineTo(pivot.fX, pivot.fY);

    (*inner).lineTo(pivot.fX - after.fX, pivot.fY - after.fY);
}

static void BluntJoiner(SkPathBuilder* outer, SkPathBuilder* inner,
                        const SkVector& beforeUnitNormal,
                        const SkPoint& pivot, const SkVector& afterUnitNormal,
                        SkScalar radius, SkScalar /*invMiterLimit*/, bool, bool) {
    SkVector    after{0, 0};
    afterUnitNormal.scale(radius, &after);

    if (!is_clockwise(beforeUnitNormal, afterUnitNormal)) {
        std::swap(outer, inner);
        after.negate();
    }

    (*outer).lineTo(pivot.fX + after.fX, pivot.fY + after.fY);
    HandleInnerJoin(inner, pivot, after);
}

static void RoundJoiner(SkPathBuilder* outer, SkPathBuilder* inner,
                        const SkVector& beforeUnitNormal,
                        const SkPoint& pivot, const SkVector& afterUnitNormal,
                        SkScalar radius, SkScalar /*invMiterLimit*/, bool, bool) {
    const SkScalar  dotProd = SkPoint::DotProduct(beforeUnitNormal, afterUnitNormal);
    const AngleType angleType = Dot2AngleType(dotProd);

    if (angleType == kNearlyLine_AngleType) {
        return;
    }

    SkVector before     = beforeUnitNormal;
    SkVector after      = afterUnitNormal;
    SkPathDirection dir = SkPathDirection::kCW;

    if (!is_clockwise(before, after)) {
        std::swap(outer, inner);
        before.negate();
        after.negate();
        dir = SkPathDirection::kCCW;
    }

    SkMatrix    matrix{};
    matrix.setScale(radius, radius);
    matrix.postTranslate(pivot.fX, pivot.fY);
    SkConic conics[SkConic::kMaxConicsForArc];
    const int count = SkConic::BuildUnitArc(before, after, dir, &matrix, conics);
    if (count > 0) {
        for (int i = 0; i < count; ++i) {
            (*outer).conicTo(conics[i].fPts[1], conics[i].fPts[2], conics[i].fW);
        }
        after.scale(radius);
        HandleInnerJoin(inner, pivot, after);
    }
}

static constexpr float kOneOverSqrt2 = 0.707106781f;

// The blunt ending shared by every miter outcome: after is the (possibly negated) unit
// normal; it is scaled to the radius here.
static void finish_miter_join(SkPathBuilder* outer, SkPathBuilder* inner, const SkPoint& pivot,
                              SkVector after, SkScalar radius, bool currIsLine) {
    after.scale(radius);
    if (!currIsLine) {
        (*outer).lineTo(pivot.fX + after.fX, pivot.fY + after.fY);
    }
    HandleInnerJoin(inner, pivot, after);
}

static void MiterJoiner(SkPathBuilder* outer, SkPathBuilder* inner,
                        const SkVector& beforeUnitNormal,
                        const SkPoint& pivot, const SkVector& afterUnitNormal,
                        SkScalar radius, SkScalar invMiterLimit,
                        bool prevIsLine, bool currIsLine) {
    // negate the dot since we're using normals instead of tangents
    const SkScalar  dotProd = SkPoint::DotProduct(beforeUnitNormal, afterUnitNormal);
    const AngleType angleType = Dot2AngleType(dotProd);
    SkVector    before = beforeUnitNormal;
    SkVector    after = afterUnitNormal;

    if (angleType == kNearlyLine_AngleType) {
        return;
    }
    if (angleType == kNearly180_AngleType) {
        finish_miter_join(outer, inner, pivot, after, radius, false);
        return;
    }

    const bool ccw = !is_clockwise(before, after);
    if (ccw) {
        std::swap(outer, inner);
        before.negate();
        after.negate();
    }

    SkVector mid{0, 0};
    /*  Before we enter the world of square-roots and divides,
        check if we're trying to join an upright right angle
        (common case for stroking rectangles). If so, special case
        that (for speed an accuracy).
        Note: we only need to check one normal if dot==0
    */
    if (0 == dotProd && invMiterLimit <= kOneOverSqrt2) {
        mid = (before + after) * radius;
    } else {
        /*  midLength = radius / sinHalfAngle
            if (midLength > miterLimit * radius) abort
            if (radius / sinHalf > miterLimit * radius) abort
            if (1 / sinHalf > miterLimit) abort
            if (1 / miterLimit > sinHalf) abort
            My dotProd is opposite sign, since it is built from normals and not tangents
            hence 1 + dot instead of 1 - dot in the formula
        */
        const float sinHalfAngle = std::sqrt((SK_Scalar1 + dotProd) / 2.f);
        if (sinHalfAngle < invMiterLimit) {
            finish_miter_join(outer, inner, pivot, after, radius, false);
            return;
        }

        // choose the most accurate way to form the initial mid-vector
        if (angleType == kSharp_AngleType) {
            mid.set(after.fY - before.fY, before.fX - after.fX);
            if (ccw) {
                mid.negate();
            }
        } else {
            mid = before + after;
        }

        static_cast<void>(mid.setLength(radius / sinHalfAngle));
    }

    if (prevIsLine) {
        (*outer).setLastPoint(pivot + mid);
    } else {
        (*outer).lineTo(pivot + mid);
    }

    finish_miter_join(outer, inner, pivot, after, radius, currIsLine);
}

/////////////////////////////////////////////////////////////////////////////

SkStrokerPriv::CapProc SkStrokerPriv::CapFactory(SkPaint::Cap cap) {
    SkASSERT(static_cast<unsigned>(cap) < SkPaint::kCapCount);
    CapProc capper = ButtCapper;
    switch (cap) {
        case SkPaint::kRound_Cap:
            capper = RoundCapper;
            break;
        case SkPaint::kSquare_Cap:
            capper = SquareCapper;
            break;
        case SkPaint::kButt_Cap:
        default:
            capper = ButtCapper;
            break;
    }
    return capper;
}

SkStrokerPriv::JoinProc SkStrokerPriv::JoinFactory(SkPaint::Join join) {
    SkASSERT(static_cast<unsigned>(join) < SkPaint::kJoinCount);
    JoinProc joiner = MiterJoiner;
    switch (join) {
        case SkPaint::kRound_Join:
            joiner = RoundJoiner;
            break;
        case SkPaint::kBevel_Join:
            joiner = BluntJoiner;
            break;
        case SkPaint::kMiter_Join:
        default:
            joiner = MiterJoiner;
            break;
    }
    return joiner;
}
