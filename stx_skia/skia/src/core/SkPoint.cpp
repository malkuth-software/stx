/*
 * Copyright 2008 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPoint.h"
#include "include/core/SkTypes.h"
#include "include/private/SkFloatingPoint.h"
#include "src/core/SkPointPriv.h"

#include <cmath>

///////////////////////////////////////////////////////////////////////////////

void SkPoint::scale(float scale, SkPoint* dst) const {
    SkASSERT(dst);
    const SkPoint scaled{fX * scale, fY * scale};
    *dst = scaled;
}

bool SkPoint::normalize() {
    const bool normalized = setLength(fX, fY, 1);
    return normalized;
}

bool SkPoint::setNormalize(float x, float y) {
    const bool normalized = setLength(x, y, 1);
    return normalized;
}

bool SkPoint::setLength(float length) {
    const bool resized = setLength(fX, fY, length);
    return resized;
}

namespace {

// The new vector and the original magnitude computed by set_point_length.
struct PointLength {
    SkPoint vector = {0, 0};
    float originalLength = 0;
    bool valid = false;
};

}  // namespace

/*
 *  The magnitude is always computed in double precision, so x * x + y * y
 *  cannot overflow or underflow for any finite float input. The scale factor
 *  is applied to each float coordinate in double precision and rounded once.
 *  A non-finite or zero result is invalid and produces the zero vector.
 */
static PointLength set_point_length(float x, float y, float length) {
    const double xx = x;
    const double yy = y;
    const double dmag = sqrt(xx * xx + yy * yy);
    const double dscale = sk_ieee_double_divide(length, dmag);
    const float scaledX = static_cast<float>(x * dscale);
    const float scaledY = static_cast<float>(y * dscale);
    PointLength result{};
    // check if we're not finite, or we're zero-length
    if (!SkIsFinite(scaledX, scaledY) || (scaledX == 0 && scaledY == 0)) {
        return result;
    }
    result.vector = {scaledX, scaledY};
    result.originalLength = sk_double_to_float(dmag);
    result.valid = true;
    return result;
}

float SkPoint::Normalize(SkPoint* pt) {
    const PointLength result = set_point_length((*pt).fX, (*pt).fY, 1.0f);
    *pt = result.vector;
    if (!result.valid) {
        return 0;
    }
    return result.originalLength;
}

float SkPoint::Length(float dx, float dy) {
    const float mag2 = dx * dx + dy * dy;
    if (SkIsFinite(mag2)) {
        const float magnitude = std::sqrt(mag2);
        return magnitude;
    }
    // The single-precision square overflowed; repeat the sum in double precision.
    const double xx = dx;
    const double yy = dy;
    const float magnitude = sk_double_to_float(sqrt(xx * xx + yy * yy));
    return magnitude;
}

bool SkPoint::setLength(float x, float y, float length) {
    const PointLength result = set_point_length(x, y, length);
    *this = result.vector;
    return result.valid;
}

bool SkPointPriv::SetLengthFast(SkPoint* pt, float length) {
    const bool resized = (*pt).setLength((*pt).fX, (*pt).fY, length);
    return resized;
}


///////////////////////////////////////////////////////////////////////////////

float SkPointPriv::DistanceToLineSegmentBetweenSqd(const SkPoint& pt, const SkPoint& a,
                                                      const SkPoint& b) {
    // If the projection of pt onto u is between a and b then this returns the
    // squared distance to the line ab. Otherwise, it returns the squared
    // distance to the closest of a and b. Let the projection of v onto u be v'.
    // There are three cases:
    //    1. v' points opposite to u. pt is not between a and b and is closer
    //       to a than b.
    //    2. v' points along u and has magnitude less than u. pt is between
    //       a and b and the distance to the segment is the same as distance
    //       to the line ab.
    //    3. v' points along u and has greater magnitude than u. pt is not
    //       between a and b and is closer to b than a.
    // v' = (u dot v) * u / |u|. So if (u dot v)/|u| is less than zero we're
    // in case 1. If (u dot v)/|u| is > |u| we are in case 3. Otherwise,
    // we're in case 2. We actually compare (u dot v) to 0 and |u|^2 to
    // avoid a sqrt to compute |u|.

    const SkVector u = b - a;
    const SkVector v = pt - a;

    const float uLengthSqd = LengthSqd(u);
    const float uDotV = SkPoint::DotProduct(u, v);

    // closest point is point A
    if (uDotV <= 0) {
        const float distanceSquared = LengthSqd(v);
        return distanceSquared;
    }
    // closest point is point B
    if (uDotV > uLengthSqd) {
        const float distanceSquared = DistanceToSqd(b, pt);
        return distanceSquared;
    }
    // closest point is inside segment
    const float det = u.cross(v);
    const float quotient = sk_ieee_float_divide(det, uLengthSqd);
    const float distanceSquared = quotient * det;
    // It's possible we have a degenerate segment, or we're so far away it looks degenerate
    // In this case, return squared distance to point A.
    if (!SkIsFinite(distanceSquared)) {
        const float fallback = LengthSqd(v);
        return fallback;
    }
    return distanceSquared;
}
