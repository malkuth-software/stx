/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkPointPriv_DEFINED
#define SkPointPriv_DEFINED

#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"

// Point helpers retained by stroking, dashing, curve evaluation and distance fields.
// Pointer arguments name one writable point; source and destination may be the same point.
class SkPointPriv {
public:
    // Returns true when every coordinate of the count points is finite.
    // A count of zero or less is vacuously finite and reads no points.
    static bool AreFinite(const SkPoint array[], int count) {
        for (int i = 0; i < count; ++i) {
            if (!array[i].isFinite()) {
                return false;
            }
        }
        return true;
    }

    static bool CanNormalize(SkScalar dx, SkScalar dy) {
        const bool normalizable = SkIsFinite(dx, dy) && (dx || dy);
        return normalizable;
    }

    static SkScalar DistanceToLineSegmentBetweenSqd(const SkPoint& pt, const SkPoint& a,
                                                   const SkPoint& b);

    static SkScalar DistanceToLineSegmentBetween(const SkPoint& pt, const SkPoint& a,
                                                 const SkPoint& b) {
        const SkScalar distanceSquared = DistanceToLineSegmentBetweenSqd(pt, a, b);
        const SkScalar distance = SkScalarSqrt(distanceSquared);
        return distance;
    }

    static SkScalar DistanceToSqd(const SkPoint& pt, const SkPoint& a) {
        const SkScalar dx = pt.fX - a.fX;
        const SkScalar dy = pt.fY - a.fY;
        const SkScalar distanceSquared = dx * dx + dy * dy;
        return distanceSquared;
    }

    static bool EqualsWithinTolerance(const SkPoint& p1, const SkPoint& p2) {
        const bool equal = !CanNormalize(p1.fX - p2.fX, p1.fY - p2.fY);
        return equal;
    }

    static bool EqualsWithinTolerance(const SkPoint& pt, const SkPoint& p, SkScalar tol) {
        const bool equal = SkScalarNearlyZero(pt.fX - p.fX, tol)
                           && SkScalarNearlyZero(pt.fY - p.fY, tol);
        return equal;
    }

    static SkScalar LengthSqd(const SkPoint& pt) {
        const SkScalar lengthSquared = SkPoint::DotProduct(pt, pt);
        return lengthSquared;
    }

    // Writes src rotated a quarter turn counter-clockwise. dst may be &src.
    static void RotateCCW(const SkPoint& src, SkPoint* dst) {
        const SkPoint rotated{src.fY, -src.fX};
        *dst = rotated;
    }

    static void RotateCCW(SkPoint* pt) {
        RotateCCW(*pt, pt);
    }

    // Writes src rotated a quarter turn clockwise. dst may be &src.
    static void RotateCW(const SkPoint& src, SkPoint* dst) {
        const SkPoint rotated{-src.fY, src.fX};
        *dst = rotated;
    }

    static void RotateCW(SkPoint* pt) {
        RotateCW(*pt, pt);
    }

    // Same result as SkPoint::setLength.
    static bool SetLengthFast(SkPoint* pt, float length);
};

#endif
