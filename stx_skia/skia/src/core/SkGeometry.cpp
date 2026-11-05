/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkGeometry.h"

#include "include/core/SkMatrix.h"
#include "include/core/SkPoint3.h"
#include "include/core/SkRect.h"
#include "include/core/SkScalar.h"
#include "include/private/SkDebug.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkTPin.h"
#include "include/private/SkTo.h"
#include "src/core/SkBezierCurves.h"
#include "src/core/SkCubics.h"
#include "src/core/SkPointPriv.h"
#include "src/core/SkVx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using float2 = skvx::float2;
using float4 = skvx::float4;

SkVector to_vector(const float2& x) {
    const SkVector vector{x[0], x[1]};
    return vector;
}

////////////////////////////////////////////////////////////////////////

// Returns nonzero when b is not between a and c, i.e. the three values are not monotonic.
int is_not_monotonic(SkScalar a, SkScalar b, SkScalar c) {
    const SkScalar ab = a - b;
    SkScalar bc = b - c;
    if (ab < 0) {
        bc = -bc;
    }
    const int notMonotonic = ab == 0 || bc < 0;
    return notMonotonic;
}

////////////////////////////////////////////////////////////////////////

// Writes numer / denom to *ratio and returns 1 when the quotient lies strictly in (0, 1);
// otherwise returns 0 and leaves *ratio unchanged.
int valid_unit_divide(SkScalar numer, SkScalar denom, SkScalar* ratio) {
    SkASSERT(ratio);

    if (numer < 0) {
        numer = -numer;
        denom = -denom;
    }

    if (denom == 0 || numer == 0 || numer >= denom) {
        return 0;
    }

    const SkScalar r = numer / denom;
    if (SkIsNaN(r)) {
        return 0;
    }
    SkASSERTF(r >= 0 && r < SK_Scalar1, "numer %f, denom %f, r %f", numer, denom, r);
    if (r == 0) { // catch underflow if numer <<<< denom
        return 0;
    }
    *ratio = r;
    return 1;
}

} // namespace

/** From Numerical Recipes in C.

    Q = -1/2 (B + sign(B) sqrt[B*B - 4*A*C])
    x1 = Q / A
    x2 = C / Q
*/
int SkFindUnitQuadRoots(SkScalar A, SkScalar B, SkScalar C, SkScalar roots[2]) {
    SkASSERT(roots);

    if (A == 0) {
        const int count = valid_unit_divide(-C, B, roots);
        return count;
    }

    // use doubles so we don't overflow temporarily trying to compute R
    double dr = static_cast<double>(B) * B - 4 * static_cast<double>(A) * C;
    if (dr < 0) {
        return 0;
    }
    dr = sqrt(dr);
    const SkScalar R = SkDoubleToScalar(dr);
    if (!SkIsFinite(R)) {
        return 0;
    }

    const SkScalar Q = (B < 0) ? -(B-R)/2 : -(B+R)/2;
    int count = 0;
    count += valid_unit_divide(Q, A, &roots[count]);
    count += valid_unit_divide(C, Q, &roots[count]);
    if (count == 2) {
        if (roots[0] > roots[1]) {
            std::swap(roots[0], roots[1]);
        } else if (roots[0] == roots[1]) { // nearly-equal?
            count -= 1; // skip the double root
        }
    }
    return count;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void SkEvalQuadAt(const SkPoint src[3], SkScalar t, SkPoint* pt, SkVector* tangent) {
    SkASSERT(src);
    SkASSERT(t >= 0 && t <= SK_Scalar1);

    if (pt) {
        *pt = SkEvalQuadAt(src, t);
    }
    if (tangent) {
        *tangent = SkEvalQuadTangentAt(src, t);
    }
}

SkPoint SkEvalQuadAt(const SkPoint src[3], SkScalar t) {
    const SkQuadCoeff coeff(src);
    const SkPoint point = to_point(coeff.eval(t));
    return point;
}

SkVector SkEvalQuadTangentAt(const SkPoint src[3], SkScalar t) {
    // The derivative equation is 2(b - a +(a - 2b +c)t). This returns a
    // zero tangent vector when t is 0 or 1, and the control point is equal
    // to the end point. In this case, use the quad end points to compute the tangent.
    if ((t == 0 && src[0] == src[1]) || (t == 1 && src[1] == src[2])) {
        const SkVector chord = src[2] - src[0];
        return chord;
    }
    SkASSERT(src);
    SkASSERT(t >= 0 && t <= SK_Scalar1);

    const float2 P0 = from_point(src[0]);
    const float2 P1 = from_point(src[1]);
    const float2 P2 = from_point(src[2]);

    const float2 B = P1 - P0;
    const float2 A = P2 - P1 - B;
    const float2 T = A * t + B;

    const SkVector tangent = to_vector(T + T);
    return tangent;
}

static inline float2 interp(const float2& v0,
                            const float2& v1,
                            const float2& t) {
    const float2 mixed = v0 + (v1 - v0) * t;
    return mixed;
}

void SkChopQuadAt(const SkPoint src[3], SkPoint dst[5], SkScalar t) {
    SkASSERT(t > 0 && t < SK_Scalar1);

    const float2 p0 = from_point(src[0]);
    const float2 p1 = from_point(src[1]);
    const float2 p2 = from_point(src[2]);
    const float2 tt(t);

    const float2 p01 = interp(p0, p1, tt);
    const float2 p12 = interp(p1, p2, tt);

    dst[0] = to_point(p0);
    dst[1] = to_point(p01);
    dst[2] = to_point(interp(p01, p12, tt));
    dst[3] = to_point(p12);
    dst[4] = to_point(p2);
}

void SkChopQuadAtHalf(const SkPoint src[3], SkPoint dst[5]) {
    SkChopQuadAt(src, dst, 0.5f);
}

/** Quad'(t) = At + B, where
    A = 2(a - 2b + c)
    B = 2(b - a)
    Solve for t, only if it fits between 0 < t < 1
*/
int SkFindQuadExtrema(SkScalar a, SkScalar b, SkScalar c, SkScalar tValue[1]) {
    /*  At + B == 0
        t = -B / A
    */
    const int count = valid_unit_divide(a - b, a - b - b + c, tValue);
    return count;
}

// After a chop at an extremum, the three middle values on that axis are made equal so both
// quads stay monotonic.
static inline void flatten_double_quad_extrema_y(SkPoint dst[5]) {
    const SkScalar extremum = dst[2].fY;
    dst[1].fY = extremum;
    dst[3].fY = extremum;
}

static inline void flatten_double_quad_extrema_x(SkPoint dst[5]) {
    const SkScalar extremum = dst[2].fX;
    dst[1].fX = extremum;
    dst[3].fX = extremum;
}

/*  Returns 0 for 1 quad, and 1 for two quads, either way the answer is
 stored in dst[]. Guarantees that the 1/2 quads will be monotonic.
 */
int SkChopQuadAtYExtrema(const SkPoint src[3], SkPoint dst[5]) {
    SkASSERT(src);
    SkASSERT(dst);

    const SkScalar a = src[0].fY;
    SkScalar b = src[1].fY;
    const SkScalar c = src[2].fY;

    if (is_not_monotonic(a, b, c)) {
        SkScalar tValue = 0;
        if (valid_unit_divide(a - b, a - b - b + c, &tValue)) {
            SkChopQuadAt(src, dst, tValue);
            flatten_double_quad_extrema_y(dst);
            return 1;
        }
        // if we get here, we need to force dst to be monotonic, even though
        // we couldn't compute a unit_divide value (probably underflow).
        b = SkScalarAbs(a - b) < SkScalarAbs(b - c) ? a : c;
    }
    dst[0].set(src[0].fX, a);
    dst[1].set(src[1].fX, b);
    dst[2].set(src[2].fX, c);
    return 0;
}

/*  Returns 0 for 1 quad, and 1 for two quads, either way the answer is
    stored in dst[]. Guarantees that the 1/2 quads will be monotonic.
 */
int SkChopQuadAtXExtrema(const SkPoint src[3], SkPoint dst[5]) {
    SkASSERT(src);
    SkASSERT(dst);

    const SkScalar a = src[0].fX;
    SkScalar b = src[1].fX;
    const SkScalar c = src[2].fX;

    if (is_not_monotonic(a, b, c)) {
        SkScalar tValue = 0;
        if (valid_unit_divide(a - b, a - b - b + c, &tValue)) {
            SkChopQuadAt(src, dst, tValue);
            flatten_double_quad_extrema_x(dst);
            return 1;
        }
        // if we get here, we need to force dst to be monotonic, even though
        // we couldn't compute a unit_divide value (probably underflow).
        b = SkScalarAbs(a - b) < SkScalarAbs(b - c) ? a : c;
    }
    dst[0].set(a, src[0].fY);
    dst[1].set(b, src[1].fY);
    dst[2].set(c, src[2].fY);
    return 0;
}

//  F(t)    = a (1 - t) ^ 2 + 2 b t (1 - t) + c t ^ 2
//  F'(t)   = 2 (b - a) + 2 (a - 2b + c) t
//  F''(t)  = 2 (a - 2b + c)
//
//  A = 2 (b - a)
//  B = 2 (a - 2b + c)
//
//  Maximum curvature for a quadratic means solving
//  Fx' Fx'' + Fy' Fy'' = 0
//
//  t = - (Ax Bx + Ay By) / (Bx ^ 2 + By ^ 2)
//
SkScalar SkFindQuadMaxCurvature(const SkPoint src[3]) {
    const SkScalar Ax = src[1].fX - src[0].fX;
    const SkScalar Ay = src[1].fY - src[0].fY;
    const SkScalar Bx = src[0].fX - src[1].fX - src[1].fX + src[2].fX;
    const SkScalar By = src[0].fY - src[1].fY - src[1].fY + src[2].fY;

    SkScalar numer = -(Ax * Bx + Ay * By);
    SkScalar denom = Bx * Bx + By * By;
    if (denom < 0) {
        numer = -numer;
        denom = -denom;
    }
    if (numer <= 0) {
        return 0;
    }
    if (numer >= denom) {  // Also catches denom=0.
        return 1;
    }
    const SkScalar t = numer / denom;
    SkASSERT((0 <= t && t < 1) || SkIsNaN(t));
    return t;
}

//////////////////////////////////////////////////////////////////////////////
///// CUBICS // CUBICS // CUBICS // CUBICS // CUBICS // CUBICS // CUBICS /////
//////////////////////////////////////////////////////////////////////////////

static SkVector eval_cubic_derivative(const SkPoint src[4], SkScalar t) {
    SkQuadCoeff coeff{};
    const float2 P0 = from_point(src[0]);
    const float2 P1 = from_point(src[1]);
    const float2 P2 = from_point(src[2]);
    const float2 P3 = from_point(src[3]);

    coeff.fA = P3 + 3 * (P1 - P2) - P0;
    coeff.fB = times_2(P2 - times_2(P1) + P0);
    coeff.fC = P1 - P0;
    const SkVector derivative = to_vector(coeff.eval(t));
    return derivative;
}

static SkVector eval_cubic_2ndDerivative(const SkPoint src[4], SkScalar t) {
    const float2 P0 = from_point(src[0]);
    const float2 P1 = from_point(src[1]);
    const float2 P2 = from_point(src[2]);
    const float2 P3 = from_point(src[3]);
    const float2 A = P3 + 3 * (P1 - P2) - P0;
    const float2 B = P2 - times_2(P1) + P0;

    const SkVector second = to_vector(A * t + B);
    return second;
}

void SkEvalCubicAt(const SkPoint src[4], SkScalar t, SkPoint* loc,
                   SkVector* tangent, SkVector* curvature) {
    SkASSERT(src);
    SkASSERT(t >= 0 && t <= SK_Scalar1);

    if (loc) {
        const SkCubicCoeff coeff(src);
        *loc = to_point(coeff.eval(t));
    }
    if (tangent) {
        // The derivative equation returns a zero tangent vector when t is 0 or 1, and the
        // adjacent control point is equal to the end point. In this case, use the
        // next control point or the end points to compute the tangent.
        if ((t == 0 && src[0] == src[1]) || (t == 1 && src[2] == src[3])) {
            if (t == 0) {
                *tangent = src[2] - src[0];
            } else {
                *tangent = src[3] - src[1];
            }
            if (!(*tangent).fX && !(*tangent).fY) {
                *tangent = src[3] - src[0];
            }
        } else {
            *tangent = eval_cubic_derivative(src, t);
        }
    }
    if (curvature) {
        *curvature = eval_cubic_2ndDerivative(src, t);
    }
}

/** Cubic'(t) = At^2 + Bt + C, where
    A = 3(-a + 3(b - c) + d)
    B = 6(a - 2b + c)
    C = 3(b - a)
    Solve for t, keeping only those that fit betwee 0 < t < 1
*/
int SkFindCubicExtrema(SkScalar a, SkScalar b, SkScalar c, SkScalar d,
                       SkScalar tValues[2]) {
    // we divide A,B,C by 3 to simplify
    const SkScalar A = d - a + 3*(b - c);
    const SkScalar B = 2*(a - b - b + c);
    const SkScalar C = b - a;

    const int count = SkFindUnitQuadRoots(A, B, C, tValues);
    return count;
}

// This does not return b when t==1, but it otherwise seems to get better precision than
// "a*(1 - t) + b*t" for things like chopping cubics on exact cusp points.
// The responsibility falls on the caller to check that t != 1 before calling.
template<int N, typename T>
inline static skvx::Vec<N,T> unchecked_mix(const skvx::Vec<N,T>& a, const skvx::Vec<N,T>& b,
                                           const skvx::Vec<N,T>& t) {
    const skvx::Vec<N,T> mixed = (b - a)*t + a;
    return mixed;
}

void SkChopCubicAt(const SkPoint src[4], SkPoint dst[7], SkScalar t) {
    SkASSERT(0 <= t && t <= 1);

    if (t == 1) {
        // src may be dst: copy forward, then repeat the end point.
        for (int i = 0; i < 4; ++i) {
            dst[i] = src[i];
        }
        dst[4] = dst[3];
        dst[5] = dst[3];
        dst[6] = dst[3];
        return;
    }

    const float2 p0 = from_point(src[0]);
    const float2 p1 = from_point(src[1]);
    const float2 p2 = from_point(src[2]);
    const float2 p3 = from_point(src[3]);
    const float2 T = t;

    const float2 ab = unchecked_mix(p0, p1, T);
    const float2 bc = unchecked_mix(p1, p2, T);
    const float2 cd = unchecked_mix(p2, p3, T);
    const float2 abc = unchecked_mix(ab, bc, T);
    const float2 bcd = unchecked_mix(bc, cd, T);
    const float2 abcd = unchecked_mix(abc, bcd, T);

    dst[0] = to_point(p0);
    dst[1] = to_point(ab);
    dst[2] = to_point(abc);
    dst[3] = to_point(abcd);
    dst[4] = to_point(bcd);
    dst[5] = to_point(cd);
    dst[6] = to_point(p3);
}

// Returns a 4-lane vector holding p in both halves.
static float4 duplicate_point(const SkPoint& p) {
    const float4 lanes(p.fX, p.fY, p.fX, p.fY);
    return lanes;
}

void SkChopCubicAt(const SkPoint src[4], SkPoint dst[10], float t0, float t1) {
    SkASSERT(0 <= t0 && t0 <= t1 && t1 <= 1);

    if (t1 == 1) {
        // src may be dst; the single chop below overwrites dst[3].
        const SkPoint end = src[3];
        SkChopCubicAt(src, dst, t0);
        dst[7] = end;
        dst[8] = end;
        dst[9] = end;
        return;
    }

    // Perform both chops in parallel using 4-lane SIMD: the low half chops at t0, the high
    // half at t1.
    const float4 p00 = duplicate_point(src[0]);
    const float4 p11 = duplicate_point(src[1]);
    const float4 p22 = duplicate_point(src[2]);
    const float4 p33 = duplicate_point(src[3]);
    const float4 T(t0, t0, t1, t1);

    const float4 ab = unchecked_mix(p00, p11, T);
    const float4 bc = unchecked_mix(p11, p22, T);
    const float4 cd = unchecked_mix(p22, p33, T);
    const float4 abc = unchecked_mix(ab, bc, T);
    const float4 bcd = unchecked_mix(bc, cd, T);
    const float4 abcd = unchecked_mix(abc, bcd, T);
    const float4 middle = unchecked_mix(abc, bcd, skvx::shuffle<2,3,0,1>(T));

    dst[0] = to_point(p00.lo);
    dst[1] = to_point(ab.lo);
    dst[2] = to_point(abc.lo);
    dst[3] = to_point(abcd.lo);
    dst[4] = to_point(middle.lo);
    dst[5] = to_point(middle.hi);
    dst[6] = to_point(abcd.hi);
    dst[7] = to_point(bcd.hi);
    dst[8] = to_point(cd.hi);
    dst[9] = to_point(p33.hi);
}

#ifdef SK_DEBUG
static bool unit_interval_contains(SkScalar t) {
    const bool inside = t >= 0 && t <= 1;
    return inside;
}
#endif

void SkChopCubicAt(const SkPoint src[4], SkPoint dst[],
                   const SkScalar tValues[], int tCount) {
    SkASSERT(std::all_of(tValues, tValues + tCount, unit_interval_contains));
    SkASSERT(std::is_sorted(tValues, tValues + tCount));

    if (!dst) {
        return;
    }
    if (tCount == 0) { // nothing to chop
        for (int i = 0; i < 4; ++i) {
            dst[i] = src[i];
        }
        return;
    }
    // Each chop writes its cubics into dst and continues from the last of them, so later chops
    // read points that earlier chops wrote.
    int i = 0;
    for (; i < tCount - 1; i += 2) {
        // Do two chops at once.
        float2 tt = float2::Load(tValues + i);
        if (i != 0) {
            const float lastT = tValues[i - 1];
            tt = skvx::pin((tt - lastT) / (1 - lastT), float2(0), float2(1));
        }
        SkChopCubicAt(src, dst, tt[0], tt[1]);
        dst = dst + 6;
        src = dst;
    }
    if (i < tCount) {
        // Chop the final cubic if there was an odd number of chops.
        SkASSERT(i + 1 == tCount);
        float t = tValues[i];
        if (i != 0) {
            const float lastT = tValues[i - 1];
            t = SkTPin(sk_ieee_float_divide(t - lastT, 1 - lastT), 0.f, 1.f);
        }
        SkChopCubicAt(src, dst, t);
    }
}

void SkChopCubicAtHalf(const SkPoint src[4], SkPoint dst[7]) {
    SkChopCubicAt(src, dst, 0.5f);
}

// After chopping at an extremum, the three points around the shared end point are made equal
// on that axis so both cubics stay monotonic.
static void flatten_double_cubic_extrema_y(SkPoint cubic[7]) {
    const SkScalar extremum = cubic[3].fY;
    cubic[2].fY = extremum;
    cubic[4].fY = extremum;
}

static void flatten_double_cubic_extrema_x(SkPoint cubic[7]) {
    const SkScalar extremum = cubic[3].fX;
    cubic[2].fX = extremum;
    cubic[4].fX = extremum;
}

/** Given 4 points on a cubic bezier, chop it into 1, 2, 3 beziers such that
    the resulting beziers are monotonic in Y. This is called by the scan
    converter.  Depending on what is returned, dst[] is treated as follows:
    0   dst[0..3] is the original cubic
    1   dst[0..3] and dst[3..6] are the two new cubics
    2   dst[0..3], dst[3..6], dst[6..9] are the three new cubics
    If dst == null, it is ignored and only the count is returned.
*/
int SkChopCubicAtYExtrema(const SkPoint src[4], SkPoint dst[10]) {
    SkScalar tValues[2] = {0, 0};
    const int roots = SkFindCubicExtrema(src[0].fY, src[1].fY, src[2].fY,
                                         src[3].fY, tValues);

    SkChopCubicAt(src, dst, tValues, roots);
    if (dst && roots > 0) {
        // we do some cleanup to ensure our Y extrema are flat
        flatten_double_cubic_extrema_y(&dst[0]);
        if (roots == 2) {
            flatten_double_cubic_extrema_y(&dst[3]);
        }
    }
    return roots;
}

int SkChopCubicAtXExtrema(const SkPoint src[4], SkPoint dst[10]) {
    SkScalar tValues[2] = {0, 0};
    const int roots = SkFindCubicExtrema(src[0].fX, src[1].fX, src[2].fX,
                                         src[3].fX, tValues);

    SkChopCubicAt(src, dst, tValues, roots);
    if (dst && roots > 0) {
        // we do some cleanup to ensure our Y extrema are flat
        flatten_double_cubic_extrema_x(&dst[0]);
        if (roots == 2) {
            flatten_double_cubic_extrema_x(&dst[3]);
        }
    }
    return roots;
}

/** http://www.faculty.idc.ac.il/arik/quality/appendixA.html

    Inflection means that curvature is zero.
    Curvature is [F' x F''] / [F'^3]
    So we solve F'x X F''y - F'y X F''y == 0
    After some canceling of the cubic term, we get
    A = b - a
    B = c - 2b + a
    C = d - 3c + 3b - a
    (BxCy - ByCx)t^2 + (AxCy - AyCx)t + AxBy - AyBx == 0
*/
int SkFindCubicInflections(const SkPoint src[4], SkScalar tValues[2]) {
    const SkScalar Ax = src[1].fX - src[0].fX;
    const SkScalar Ay = src[1].fY - src[0].fY;
    const SkScalar Bx = src[2].fX - 2 * src[1].fX + src[0].fX;
    const SkScalar By = src[2].fY - 2 * src[1].fY + src[0].fY;
    const SkScalar Cx = src[3].fX + 3 * (src[1].fX - src[2].fX) - src[0].fX;
    const SkScalar Cy = src[3].fY + 3 * (src[1].fY - src[2].fY) - src[0].fY;

    const int count = SkFindUnitQuadRoots(Bx*Cy - By*Cx,
                                          Ax*Cy - Ay*Cx,
                                          Ax*By - Ay*Bx,
                                          tValues);
    return count;
}

template <typename T> void bubble_sort(T array[], int count) {
    for (int i = count - 1; i > 0; --i) {
        for (int j = i; j > 0; --j) {
            if (array[j] < array[j-1]) {
                const T tmp(array[j]);
                array[j] = array[j-1];
                array[j-1] = tmp;
            }
        }
    }
}

/**
 *  Given an array and count, remove all pair-wise duplicates from the array,
 *  keeping the existing sorting, and return the new count
 */
static int collaps_duplicates(SkScalar array[], int count) {
    int start = 0;
    for (int n = count; n > 1; --n) {
        if (array[start] == array[start + 1]) {
            for (int i = 1; i < n; ++i) {
                array[start + i - 1] = array[start + i];
            }
            count -= 1;
        } else {
            start += 1;
        }
    }
    return count;
}

static SkScalar SkScalarCubeRoot(SkScalar x) {
    const SkScalar root = SkScalarPow(x, 0.3333333f);
    return root;
}

/*  Solve coeff(t) == 0, returning the number of roots that
    lie within 0 < t < 1.
    coeff[0]t^3 + coeff[1]t^2 + coeff[2]t + coeff[3]

    Eliminates repeated roots (so that all tValues are distinct, and are always
    in increasing order.
*/
static int solve_cubic_poly(const SkScalar coeff[4], SkScalar tValues[3]) {
    if (SkScalarNearlyZero(coeff[0])) {  // we're just a quadratic
        const int count = SkFindUnitQuadRoots(coeff[1], coeff[2], coeff[3], tValues);
        return count;
    }

    SkASSERT(coeff[0] != 0);

    const SkScalar inva = SkScalarInvert(coeff[0]);
    const SkScalar a = coeff[1] * inva;
    const SkScalar b = coeff[2] * inva;
    const SkScalar c = coeff[3] * inva;
    const SkScalar Q = (a*a - b*3) / 9;
    const SkScalar R = (2*a*a*a - 9*a*b + 27*c) / 54;

    const SkScalar Q3 = Q * Q * Q;
    const SkScalar R2MinusQ3 = R * R - Q3;
    const SkScalar adiv3 = a / 3;

    if (R2MinusQ3 < 0) { // we have 3 real roots
        // the divide/root can, due to finite precisions, be slightly outside of -1...1
        const SkScalar theta = SkScalarACos(SkTPin(R / SkScalarSqrt(Q3), -1.0f, 1.0f));
        const SkScalar neg2RootQ = -2 * SkScalarSqrt(Q);

        tValues[0] = SkTPin(neg2RootQ * SkScalarCos(theta/3) - adiv3, 0.0f, 1.0f);
        tValues[1] = SkTPin(neg2RootQ * SkScalarCos((theta + 2*SK_ScalarPI)/3) - adiv3, 0.0f, 1.0f);
        tValues[2] = SkTPin(neg2RootQ * SkScalarCos((theta - 2*SK_ScalarPI)/3) - adiv3, 0.0f, 1.0f);

        // now sort the roots
        bubble_sort(tValues, 3);
        const int count = collaps_duplicates(tValues, 3);
        return count;
    }
    // we have 1 real root
    SkScalar A = SkScalarAbs(R) + SkScalarSqrt(R2MinusQ3);
    A = SkScalarCubeRoot(A);
    if (R > 0) {
        A = -A;
    }
    if (A != 0) {
        A += Q / A;
    }
    tValues[0] = SkTPin(A - adiv3, 0.0f, 1.0f);
    return 1;
}

/*  Looking for F' dot F'' == 0

    A = b - a
    B = c - 2b + a
    C = d - 3c + 3b - a

    F' = 3Ct^2 + 6Bt + 3A
    F'' = 6Ct + 6B

    F' dot F'' -> CCt^3 + 3BCt^2 + (2BB + CA)t + AB

    p0..p3 are one coordinate of the four control points.
*/
static void formulate_F1DotF2(SkScalar p0, SkScalar p1, SkScalar p2, SkScalar p3,
                              SkScalar coeff[4]) {
    const SkScalar a = p1 - p0;
    const SkScalar b = p2 - 2 * p1 + p0;
    const SkScalar c = p3 + 3 * (p1 - p2) - p0;

    coeff[0] = c * c;
    coeff[1] = 3 * b * c;
    coeff[2] = 2 * b * b + c * a;
    coeff[3] = a * b;
}

/*  Looking for F' dot F'' == 0

    A = b - a
    B = c - 2b + a
    C = d - 3c + 3b - a

    F' = 3Ct^2 + 6Bt + 3A
    F'' = 6Ct + 6B

    F' dot F'' -> CCt^3 + 3BCt^2 + (2BB + CA)t + AB
*/
int SkFindCubicMaxCurvature(const SkPoint src[4], SkScalar tValues[3]) {
    SkScalar coeffX[4] = {0, 0, 0, 0};
    SkScalar coeffY[4] = {0, 0, 0, 0};

    formulate_F1DotF2(src[0].fX, src[1].fX, src[2].fX, src[3].fX, coeffX);
    formulate_F1DotF2(src[0].fY, src[1].fY, src[2].fY, src[3].fY, coeffY);

    for (int i = 0; i < 4; i++) {
        coeffX[i] += coeffY[i];
    }

    const int numRoots = solve_cubic_poly(coeffX, tValues);
    // now remove extrema where the curvature is zero (mins)
    // !!!! need a test for this !!!!
    return numRoots;
}

int SkChopCubicAtMaxCurvature(const SkPoint src[4], SkPoint dst[13],
                              SkScalar tValues[3]) {
    SkScalar t_storage[3] = {0, 0, 0};

    if (tValues == nullptr) {
        tValues = t_storage;
    }

    SkScalar roots[3] = {0, 0, 0};
    const int rootCount = SkFindCubicMaxCurvature(src, roots);

    // Throw out values not inside 0..1.
    int count = 0;
    for (int i = 0; i < rootCount; ++i) {
        if (0 < roots[i] && roots[i] < 1) {
            tValues[count] = roots[i];
            ++count;
        }
    }

    if (dst) {
        if (count == 0) {
            for (int i = 0; i < 4; ++i) {
                dst[i] = src[i];
            }
        } else {
            SkChopCubicAt(src, dst, tValues, count);
        }
    }
    return count + 1;
}

// Returns a constant proportional to the dimensions of the cubic.
// Constant found through experimentation -- maybe there's a better way....
static SkScalar calc_cubic_precision(const SkPoint src[4]) {
    const SkScalar precision =
            (SkPointPriv::DistanceToSqd(src[1], src[0]) + SkPointPriv::DistanceToSqd(src[2], src[1])
             + SkPointPriv::DistanceToSqd(src[3], src[2])) * 1e-8f;
    return precision;
}

// Returns true if both points src[testIndex], src[testIndex+1] are in the same half plane defined
// by the line segment src[lineIndex], src[lineIndex+1].
static bool on_same_side(const SkPoint src[4], int testIndex, int lineIndex) {
    const SkPoint origin = src[lineIndex];
    const SkVector line = src[lineIndex + 1] - origin;
    SkScalar crosses[2] = {0, 0};
    for (int index = 0; index < 2; ++index) {
        const SkVector testLine = src[testIndex + index] - origin;
        crosses[index] = line.cross(testLine);
    }
    const bool sameSide = crosses[0] * crosses[1] >= 0;
    return sameSide;
}

// Return location (in t) of cubic cusp, if there is one.
// Note that classify cubic code does not reliably return all cusp'd cubics, so
// it is not called here.
SkScalar SkFindCubicCusp(const SkPoint src[4]) {
    // When the adjacent control point matches the end point, it behaves as if
    // the cubic has a cusp: there's a point of max curvature where the derivative
    // goes to zero. Ideally, this would be where t is zero or one, but math
    // error makes not so. It is not uncommon to create cubics this way; skip them.
    if (src[0] == src[1]) {
        return -1;
    }
    if (src[2] == src[3]) {
        return -1;
    }
    // Cubics only have a cusp if the line segments formed by the control and end points cross.
    // Detect crossing if line ends are on opposite sides of plane formed by the other line.
    if (on_same_side(src, 0, 2) || on_same_side(src, 2, 0)) {
        return -1;
    }
    // Cubics may have multiple points of maximum curvature, although at most only
    // one is a cusp.
    SkScalar maxCurvature[3] = {0, 0, 0};
    const int roots = SkFindCubicMaxCurvature(src, maxCurvature);
    for (int index = 0; index < roots; ++index) {
        const SkScalar testT = maxCurvature[index];
        if (0 >= testT || testT >= 1) {  // no need to consider max curvature on the end
            continue;
        }
        // A cusp is at the max curvature, and also has a derivative close to zero.
        // Choose the 'close to zero' meaning by comparing the derivative length
        // with the overall cubic size.
        const SkVector dPt = eval_cubic_derivative(src, testT);
        const SkScalar dPtMagnitude = SkPointPriv::LengthSqd(dPt);
        const SkScalar precision = calc_cubic_precision(src);
        if (dPtMagnitude < precision) {
            // All three max curvature t values may be close to the cusp;
            // return the first one.
            return testT;
        }
    }
    return -1;
}

static bool close_enough_to_zero(double x) {
    const bool close = std::fabs(x) < 0.00001;
    return close;
}

// coefficients holds the four control points as x0 y0 x1 y1 x2 y2 x3 y3.
static bool first_axis_intersection(const double coefficients[8], bool yDirection,
                                    double axisIntercept, double* solution) {
    const std::array<double, 4> polynomial =
            SkBezierCubic::ConvertToPolynomial(coefficients, yDirection);
    const double A = polynomial[0];
    const double B = polynomial[1];
    const double C = polynomial[2];
    const double D = polynomial[3] - axisIntercept;
    double roots[3] = {0, 0, 0};
    int count = SkCubics::RootsValidT(A, B, C, D, roots);
    if (count == 0) {
        return false;
    }
    // Verify that at least one of the roots is accurate.
    for (int i = 0; i < count; i++) {
        if (close_enough_to_zero(SkCubics::EvalAt(A, B, C, D, roots[i]))) {
            *solution = roots[i];
            return true;
        }
    }
    // None of the roots returned by our normal cubic solver were correct enough
    // (e.g. https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=55732)
    // So we need to fallback to a more accurate solution.
    count = SkCubics::BinarySearchRootsValidT(A, B, C, D, roots);
    if (count == 0) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (close_enough_to_zero(SkCubics::EvalAt(A, B, C, D, roots[i]))) {
            *solution = roots[i];
            return true;
        }
    }
    return false;
}

// Chops src where the selected axis reaches intercept, in double precision.
static bool chop_mono_cubic_at(const SkPoint src[4], bool yDirection, SkScalar intercept,
                               SkPoint dst[7]) {
    const double coefficients[8] = {src[0].fX, src[0].fY, src[1].fX, src[1].fY,
                                    src[2].fX, src[2].fY, src[3].fX, src[3].fY};
    double solution = 0;
    if (!first_axis_intersection(coefficients, yDirection, intercept, &solution)) {
        return false;
    }
    double cubicPair[14] = {};
    SkBezierCubic::Subdivide(coefficients, solution, cubicPair);
    for (int i = 0; i < 7; i ++) {
        dst[i].fX = sk_double_to_float(cubicPair[i*2]);
        dst[i].fY = sk_double_to_float(cubicPair[i*2 + 1]);
    }
    return true;
}

bool SkChopMonoCubicAtY(const SkPoint src[4], SkScalar y, SkPoint dst[7]) {
    const bool chopped = chop_mono_cubic_at(src, true, y, dst);
    return chopped;
}

bool SkChopMonoCubicAtX(const SkPoint src[4], SkScalar x, SkPoint dst[7]) {
    const bool chopped = chop_mono_cubic_at(src, false, x, dst);
    return chopped;
}

///////////////////////////////////////////////////////////////////////////////
//
// NURB representation for conics.  Helpful explanations at:
//
// http://citeseerx.ist.psu.edu/viewdoc/
//   download?doi=10.1.1.44.5740&rep=rep1&type=ps
// and
// http://www.cs.mtu.edu/~shene/COURSES/cs3621/NOTES/spline/NURBS/RB-conics.html
//
// F = (A (1 - t)^2 + C t^2 + 2 B (1 - t) t w)
//     ------------------------------------------
//         ((1 - t)^2 + t^2 + 2 (1 - t) t w)
//
//   = {t^2 (P0 + P2 - 2 P1 w), t (-2 P0 + 2 P1 w), P0}
//     ------------------------------------------------
//             {t^2 (2 - 2 w), t (-2 + 2 w), 1}
//

// F' = 2 (C t (1 + t (-1 + w)) - A (-1 + t) (t (-1 + w) - w) + B (1 - 2 t) w)
//
//  t^2 : (2 P0 - 2 P2 - 2 P0 w + 2 P2 w)
//  t^1 : (-2 P0 + 2 P2 + 4 P0 w - 4 P1 w)
//  t^0 : -2 P0 w + 2 P1 w
//
//  We disregard magnitude, so we can freely ignore the denominator of F', and
//  divide the numerator by 2
//
//    coeff[0] for t^2
//    coeff[1] for t^1
//    coeff[2] for t^0
//
//  p0, p1 and p2 are one coordinate of the three control points.
//
static void conic_deriv_coeff(SkScalar p0, SkScalar p1, SkScalar p2,
                              SkScalar w,
                              SkScalar coeff[3]) {
    const SkScalar P20 = p2 - p0;
    const SkScalar P10 = p1 - p0;
    const SkScalar wP10 = w * P10;
    coeff[0] = w * P20 - P20;
    coeff[1] = P20 - 2 * wP10;
    coeff[2] = wP10;
}

static bool conic_find_extrema(SkScalar p0, SkScalar p1, SkScalar p2, SkScalar w, SkScalar* t) {
    SkScalar coeff[3] = {0, 0, 0};
    conic_deriv_coeff(p0, p1, p2, w, coeff);

    SkScalar tValues[2] = {0, 0};
    const int roots = SkFindUnitQuadRoots(coeff[0], coeff[1], coeff[2], tValues);
    SkASSERT(0 == roots || 1 == roots);

    if (1 == roots) {
        *t = tValues[0];
        return true;
    }
    return false;
}

// de Casteljau interpolation of the three homogeneous points at t, one component at a time.
static void p3d_interp(const SkPoint3 src[3], SkPoint3 dst[3], SkScalar t) {
    const SkScalar abX = SkScalarInterp(src[0].fX, src[1].fX, t);
    const SkScalar bcX = SkScalarInterp(src[1].fX, src[2].fX, t);
    dst[0].fX = abX;
    dst[1].fX = SkScalarInterp(abX, bcX, t);
    dst[2].fX = bcX;

    const SkScalar abY = SkScalarInterp(src[0].fY, src[1].fY, t);
    const SkScalar bcY = SkScalarInterp(src[1].fY, src[2].fY, t);
    dst[0].fY = abY;
    dst[1].fY = SkScalarInterp(abY, bcY, t);
    dst[2].fY = bcY;

    const SkScalar abZ = SkScalarInterp(src[0].fZ, src[1].fZ, t);
    const SkScalar bcZ = SkScalarInterp(src[1].fZ, src[2].fZ, t);
    dst[0].fZ = abZ;
    dst[1].fZ = SkScalarInterp(abZ, bcZ, t);
    dst[2].fZ = bcZ;
}

static void ratquad_mapTo3D(const SkPoint src[3], SkScalar w, SkPoint3 dst[3]) {
    dst[0].set(src[0].fX * 1, src[0].fY * 1, 1);
    dst[1].set(src[1].fX * w, src[1].fY * w, w);
    dst[2].set(src[2].fX * 1, src[2].fY * 1, 1);
}

static SkPoint project_down(const SkPoint3& src) {
    const SkPoint projected{src.fX / src.fZ, src.fY / src.fZ};
    return projected;
}

static bool conic_is_finite(const SkConic& conic) {
    const bool finite = SkPointPriv::AreFinite(conic.fPts, 3) && SkIsFinite(conic.fW);
    return finite;
}

// return false if infinity or NaN is generated; caller must check
bool SkConic::chopAt(SkScalar t, SkConic dst[2]) const {
    SkPoint3 tmp[3] = {};
    SkPoint3 tmp2[3] = {};

    ratquad_mapTo3D(fPts, fW, tmp);

    p3d_interp(tmp, tmp2, t);

    dst[0].fPts[0] = fPts[0];
    dst[0].fPts[1] = project_down(tmp2[0]);
    dst[0].fPts[2] = project_down(tmp2[1]);
    dst[1].fPts[0] = dst[0].fPts[2];
    dst[1].fPts[1] = project_down(tmp2[2]);
    dst[1].fPts[2] = fPts[2];

    // to put in "standard form", where w0 and w2 are both 1, we compute the
    // new w1 as sqrt(w1*w1/w0*w2)
    // or
    // w1 /= sqrt(w0*w2)
    //
    // However, in our case, we know that for dst[0]:
    //     w0 == 1, and for dst[1], w2 == 1
    //
    const SkScalar root = SkScalarSqrt(tmp2[1].fZ);
    dst[0].fW = tmp2[0].fZ / root;
    dst[1].fW = tmp2[2].fZ / root;
    const bool finite = conic_is_finite(dst[0]) && conic_is_finite(dst[1]);
    return finite;
}

void SkConic::chopAt(SkScalar t1, SkScalar t2, SkConic* dst) const {
    if (0 == t1 || 1 == t2) {
        if (0 == t1 && 1 == t2) {
            *dst = *this;
            return;
        }
        SkConic pair[2];
        if (chopAt(t1 ? t1 : t2, pair)) {
            *dst = pair[SkToBool(t1)];
            return;
        }
    }
    const SkConicCoeff coeff(*this);
    const float2 tt1(t1);
    const float2 aXY = coeff.fNumer.eval(tt1);
    const float2 aZZ = coeff.fDenom.eval(tt1);
    const float2 midTT((t1 + t2) / 2);
    const float2 dXY = coeff.fNumer.eval(midTT);
    const float2 dZZ = coeff.fDenom.eval(midTT);
    const float2 tt2(t2);
    const float2 cXY = coeff.fNumer.eval(tt2);
    const float2 cZZ = coeff.fDenom.eval(tt2);
    const float2 bXY = times_2(dXY) - (aXY + cXY) * 0.5f;
    const float2 bZZ = times_2(dZZ) - (aZZ + cZZ) * 0.5f;
    SkConic& out = *dst;
    out.fPts[0] = to_point(aXY / aZZ);
    out.fPts[1] = to_point(bXY / bZZ);
    out.fPts[2] = to_point(cXY / cZZ);
    const float2 ww = bZZ / sqrt(aZZ * cZZ);
    out.fW = ww[0];
}

SkPoint SkConic::evalAt(SkScalar t) const {
    const SkConicCoeff coeff(*this);
    const SkPoint point = to_point(coeff.eval(t));
    return point;
}

SkVector SkConic::evalTangentAt(SkScalar t) const {
    // The derivative equation returns a zero tangent vector when t is 0 or 1,
    // and the control point is equal to the end point.
    // In this case, use the conic endpoints to compute the tangent.
    if ((t == 0 && fPts[0] == fPts[1]) || (t == 1 && fPts[1] == fPts[2])) {
        const SkVector chord = fPts[2] - fPts[0];
        return chord;
    }
    const float2 p0 = from_point(fPts[0]);
    const float2 p1 = from_point(fPts[1]);
    const float2 p2 = from_point(fPts[2]);
    const float2 ww(fW);

    const float2 p20 = p2 - p0;
    const float2 p10 = p1 - p0;

    const float2 C = ww * p10;
    const float2 A = ww * p20 - p20;
    const float2 B = p20 - C - C;

    const SkQuadCoeff coeff(A, B, C);
    const SkVector tangent = to_vector(coeff.eval(t));
    return tangent;
}

void SkConic::evalAt(SkScalar t, SkPoint* pt, SkVector* tangent) const {
    SkASSERT(t >= 0 && t <= SK_Scalar1);

    if (pt) {
        *pt = evalAt(t);
    }
    if (tangent) {
        *tangent = evalTangentAt(t);
    }
}

static SkScalar subdivide_w_value(SkScalar w) {
    const SkScalar subdivided = SkScalarSqrt(SK_ScalarHalf + w * SK_ScalarHalf);
    return subdivided;
}

// dst must not overlap this conic.
void SkConic::chop(SkConic dst[2]) const {

    // Observe that scale will always be smaller than 1 because fW > 0.
    const float scale = SkScalarInvert(SK_Scalar1 + fW);

    // The subdivided control points below are the sums of the following three terms. Because the
    // terms are multiplied by something <1, and the resulting control points lie within the
    // control points of the original then the terms and the sums below will not overflow. Note
    // that fW * scale approaches 1 as fW becomes very large.
    const float2 t0 = from_point(fPts[0]) * scale;
    const float2 t1 = from_point(fPts[1]) * (fW * scale);
    const float2 t2 = from_point(fPts[2]) * scale;

    // Calculate the subdivided control points
    const SkPoint p1 = to_point(t0 + t1);
    const SkPoint p3 = to_point(t1 + t2);

    // p2 = (t0 + 2*t1 + t2) / 2. Divide the terms by 2 before the sum to keep the sum for p2
    // from overflowing.
    const SkPoint p2 = to_point(0.5f * t0 + t1 + 0.5f * t2);

    SkASSERT(p1.isFinite() && p2.isFinite() && p3.isFinite());

    dst[0].fPts[0] = fPts[0];
    dst[0].fPts[1] = p1;
    dst[0].fPts[2] = p2;
    dst[1].fPts[0] = p2;
    dst[1].fPts[1] = p3;
    dst[1].fPts[2] = fPts[2];

    // Update w.
    const SkScalar newW = subdivide_w_value(fW);
    dst[0].fW = newW;
    dst[1].fW = newW;
}

// Limit the number of suggested quads to approximate a conic
static constexpr int kMaxConicToQuadPOW2 = 5;

static inline bool bad_conic_w(float w) {
    const bool bad = w < 0 || !SkIsFinite(w);
    return bad;
}

/*
 *  "High order approximation of conic sections by quadratic splines"
 *      by Michael Floater, 1993
 */
int SkConic::computeQuadPOW2(SkScalar tol) const {
    if (tol < 0 || !SkIsFinite(tol) || !SkPointPriv::AreFinite(fPts, 3) || bad_conic_w(fW)) {
        return 0;
    }

    // The quad's approximation error vector.
    const SkScalar a = fW - 1;
    const SkScalar k = a / (4 * (2 + a));
    const SkScalar x = k * (fPts[0].fX - 2 * fPts[1].fX + fPts[2].fX);
    const SkScalar y = k * (fPts[0].fY - 2 * fPts[1].fY + fPts[2].fY);

    SkScalar error = SkScalarSqrt(x * x + y * y);
    int pow2 = 0;
    for (pow2 = 0; pow2 < kMaxConicToQuadPOW2; ++pow2) {
        if (error <= tol) {
            break;
        }
        error *= 0.25f;
    }
    return pow2;
}

// This was originally developed and tested for pathops: see SkOpTypes.h
// returns true if (a <= b <= c) || (a >= b >= c)
static bool between(SkScalar a, SkScalar b, SkScalar c) {
    const bool inside = (a - b) * (c - b) <= 0;
    return inside;
}

// Appends the control and end points of 2^level quads approximating src; returns the next
// unwritten point.
static SkPoint* subdivide(const SkConic& src, SkPoint pts[], int level) {
    SkASSERT(level >= 0);

    if (0 == level) {
        pts[0] = src.fPts[1];
        pts[1] = src.fPts[2];
        return pts + 2;
    }
    SkConic dst[2];
    src.chop(dst);
    const SkScalar startY = src.fPts[0].fY;
    const SkScalar endY = src.fPts[2].fY;
    if (between(startY, src.fPts[1].fY, endY)) {
        // If the input is monotonic and the output is not, the scan converter hangs.
        // Ensure that the chopped conics maintain their y-order.
        const SkScalar midY = dst[0].fPts[2].fY;
        if (!between(startY, midY, endY)) {
            // If the computed midpoint is outside the ends, move it to the closer one.
            const SkScalar closerY = SkTAbs(midY - startY) < SkTAbs(midY - endY) ? startY : endY;
            dst[0].fPts[2].fY = closerY;
            dst[1].fPts[0].fY = closerY;
        }
        if (!between(startY, dst[0].fPts[1].fY, dst[0].fPts[2].fY)) {
            // If the 1st control is not between the start and end, put it at the start.
            // This also reduces the quad to a line.
            dst[0].fPts[1].fY = startY;
        }
        if (!between(dst[1].fPts[0].fY, dst[1].fPts[1].fY, endY)) {
            // If the 2nd control is not between the start and end, put it at the end.
            // This also reduces the quad to a line.
            dst[1].fPts[1].fY = endY;
        }
        // Verify that all five points are in order.
        SkASSERT(between(startY, dst[0].fPts[1].fY, dst[0].fPts[2].fY));
        SkASSERT(between(dst[0].fPts[1].fY, dst[0].fPts[2].fY, dst[1].fPts[1].fY));
        SkASSERT(between(dst[0].fPts[2].fY, dst[1].fPts[1].fY, endY));
    }
    --level;
    pts = subdivide(dst[0], pts, level);
    SkPoint* next = subdivide(dst[1], pts, level);
    return next;
}

int SkConic::chopIntoQuadsPOW2(SkPoint pts[], int pow2) const {
    SkASSERT(pow2 >= 0 && pow2 <= kMaxConicToQuadPOW2);

    if (bad_conic_w(fW)) {
        pow2 = 0;
    }

    pts[0] = fPts[0];
    SkDEBUGCODE(SkPoint* endPts = nullptr;)
    bool madeLines = false;
    if (pow2 == kMaxConicToQuadPOW2) {  // If an extreme weight generates many quads ...
        SkConic dst[2];
        chop(dst);
        // check to see if the first chop generates a pair of lines
        if (SkPointPriv::EqualsWithinTolerance(dst[0].fPts[1], dst[0].fPts[2]) &&
                SkPointPriv::EqualsWithinTolerance(dst[1].fPts[0], dst[1].fPts[1])) {
            // set ctrl == end to make lines
            pts[1] = dst[0].fPts[1];
            pts[2] = dst[0].fPts[1];
            pts[3] = dst[0].fPts[1];
            pts[4] = dst[1].fPts[2];
            pow2 = 1;
            SkDEBUGCODE(endPts = &pts[5];)
            madeLines = true;
        }
    }
    if (!madeLines) {
        SkDEBUGCODE(endPts = ) subdivide(*this, pts + 1, pow2);
    }
    const int quadCount = 1 << pow2;
    const int ptCount = 2 * quadCount + 1;
    SkASSERT(endPts - pts == ptCount);
    if (!SkPointPriv::AreFinite(pts, ptCount)) {
        // if we generated a non-finite, pin ourselves to the middle of the hull,
        // as our first and last are already on the first/last pts of the hull.
        for (int i = 1; i < ptCount - 1; ++i) {
            pts[i] = fPts[1];
        }
    }
    return quadCount;
}

bool SkConic::findXExtrema(SkScalar* t) const {
    const bool found = conic_find_extrema(fPts[0].fX, fPts[1].fX, fPts[2].fX, fW, t);
    return found;
}

bool SkConic::findYExtrema(SkScalar* t) const {
    const bool found = conic_find_extrema(fPts[0].fY, fPts[1].fY, fPts[2].fY, fW, t);
    return found;
}

bool SkConic::chopAtYExtrema(SkConic dst[2]) const {
    SkScalar t = 0;
    if (!findYExtrema(&t)) {
        return false;
    }
    if (!chopAt(t, dst)) {
        // if chop can't return finite values, don't chop
        return false;
    }
    // now clean-up the middle, since we know t was meant to be at
    // an Y-extrema
    const SkScalar value = dst[0].fPts[2].fY;
    dst[0].fPts[1].fY = value;
    dst[1].fPts[0].fY = value;
    dst[1].fPts[1].fY = value;
    return true;
}

void SkConic::computeTightBounds(SkRect* bounds) const {
    SkPoint pts[4] = {fPts[0], fPts[2], {0, 0}, {0, 0}};
    size_t count = 2;

    SkScalar t = 0;
    if (findXExtrema(&t)) {
        evalAt(t, &pts[count]);
        ++count;
    }
    if (findYExtrema(&t)) {
        evalAt(t, &pts[count]);
        ++count;
    }
    *bounds = SkRect::BoundsOrEmpty(SkSpan<const SkPoint>(pts, count));
}

void SkConic::computeFastBounds(SkRect* bounds) const {
    *bounds = SkRect::BoundsOrEmpty(fPts);
}

SkScalar SkConic::TransformW(const SkPoint pts[3], SkScalar w, const SkMatrix& matrix) {
    if (!matrix.hasPerspective()) {
        return w;
    }

    SkPoint3 src[3] = {};
    SkPoint3 dst[3] = {};

    ratquad_mapTo3D(pts, w, src);

    matrix.mapHomogeneousPoints(dst, src);

    // w' = sqrt(w1*w1/w0*w2)
    // use doubles temporarily, to handle small numer/denom
    const double w0 = dst[0].fZ;
    const double w1 = dst[1].fZ;
    const double w2 = dst[2].fZ;
    const SkScalar weight = sk_double_to_float(sqrt(sk_ieee_double_divide(w1 * w1, w0 * w2)));
    return weight;
}

int SkConic::BuildUnitArc(const SkVector& uStart, const SkVector& uStop, SkPathDirection dir,
                          const SkMatrix* userMatrix, SkConic dst[kMaxConicsForArc]) {
    // rotate by x,y so that uStart is (1.0)
    const SkScalar x = SkPoint::DotProduct(uStart, uStop);
    SkScalar y = SkPoint::CrossProduct(uStart, uStop);

    const SkScalar absY = SkScalarAbs(y);

    // check for (effectively) coincident vectors
    // this can happen if our angle is nearly 0 or nearly 180 (y == 0)
    // ... we use the dot-prod to distinguish between 0 and 180 (x > 0)
    if (absY <= SK_ScalarNearlyZero && x > 0 && ((y >= 0 && SkPathDirection::kCW == dir) ||
                                                 (y <= 0 && SkPathDirection::kCCW == dir))) {
        return 0;
    }

    if (dir == SkPathDirection::kCCW) {
        y = -y;
    }

    // We decide to use 1-conic per quadrant of a circle. What quadrant does [xy] lie in?
    //      0 == [0  .. 90)
    //      1 == [90 ..180)
    //      2 == [180..270)
    //      3 == [270..360)
    //
    int quadrant = 0;
    if (0 == y) {
        quadrant = 2;        // 180
        SkASSERT(SkScalarAbs(x + SK_Scalar1) <= SK_ScalarNearlyZero);
    } else if (0 == x) {
        SkASSERT(absY - SK_Scalar1 <= SK_ScalarNearlyZero);
        quadrant = y > 0 ? 1 : 3; // 90 : 270
    } else {
        if (y < 0) {
            quadrant += 2;
        }
        if ((x < 0) != (y < 0)) {
            quadrant += 1;
        }
    }

    const SkPoint quadrantPts[] = {
        { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }
    };
    const SkScalar quadrantWeight = SK_ScalarRoot2Over2;

    int conicCount = quadrant;
    for (int i = 0; i < conicCount; ++i) {
        dst[i].set(&quadrantPts[i * 2], quadrantWeight);
    }

    // Now compute any remaing (sub-90-degree) arc for the last conic
    const SkPoint finalP = { x, y };
    const SkPoint& lastQ = quadrantPts[quadrant * 2];  // will already be a unit-vector
    const SkScalar dot = SkVector::DotProduct(lastQ, finalP);
    if (SkIsNaN(dot)) {
        return 0;
    }
    SkASSERT(0 <= dot && dot <= SK_Scalar1 + SK_ScalarNearlyZero);

    if (dot < 1) {
        SkVector offCurve = { lastQ.x() + x, lastQ.y() + y };
        // compute the bisector vector, and then rescale to be the off-curve point.
        // we compute its length from cos(theta/2) = length / 1, using half-angle identity we get
        // length = sqrt(2 / (1 + cos(theta)). We already have cos() when to computed the dot.
        // This is nice, since our computed weight is cos(theta/2) as well!
        //
        const SkScalar cosThetaOver2 = SkScalarSqrt((1 + dot) / 2);
        offCurve.setLength(SkScalarInvert(cosThetaOver2));
        if (!SkPointPriv::EqualsWithinTolerance(lastQ, offCurve)) {
            dst[conicCount].set(lastQ, offCurve, finalP, cosThetaOver2);
            conicCount += 1;
        }
    }

    // now handle counter-clockwise and the initial unitStart rotation
    SkMatrix matrix{};
    matrix.setSinCos(uStart.fY, uStart.fX);
    if (dir == SkPathDirection::kCCW) {
        matrix.preScale(SK_Scalar1, -SK_Scalar1);
    }
    if (userMatrix) {
        matrix.postConcat(*userMatrix);
    }
    for (int i = 0; i < conicCount; ++i) {
        matrix.mapPoints(dst[i].fPts);
    }
    return conicCount;
}
