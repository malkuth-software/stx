/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkBezierCurves.h"

#include "include/core/SkPoint.h"
#include "include/core/SkSpan.h"
#include "include/private/SkAssert.h"
#include "include/private/SkFloatingPoint.h"
#include "src/core/SkCubics.h"
#include "src/core/SkQuads.h"

#include <array>
#include <cstddef>

static inline double interpolate(double A, double B, double t) {
    const double value = A + (B - A) * t;
    return value;
}

std::array<double, 2> SkBezierCubic::EvalAt(const double curve[8], double t) {
    // curve holds x0 y0 x1 y1 x2 y2 x3 y3.
    const double x0 = curve[0];
    const double y0 = curve[1];
    const double x1 = curve[2];
    const double y1 = curve[3];
    const double x2 = curve[4];
    const double y2 = curve[5];
    const double x3 = curve[6];
    const double y3 = curve[7];

    // Two semi-common fast paths
    if (t == 0) {
        const std::array<double, 2> start{x0, y0};
        return start;
    }
    if (t == 1) {
        const std::array<double, 2> end{x3, y3};
        return end;
    }
    // X(t) = X_0*(1-t)^3 + 3*X_1*t(1-t)^2 + 3*X_2*t^2(1-t) + X_3*t^3
    // Y(t) = Y_0*(1-t)^3 + 3*Y_1*t(1-t)^2 + 3*Y_2*t^2(1-t) + Y_3*t^3
    // Some compilers are smart enough and have sufficient registers/intrinsics to write optimal
    // code from
    //    double one_minus_t = 1 - t;
    //    double a = one_minus_t * one_minus_t * one_minus_t;
    //    double b = 3 * one_minus_t * one_minus_t * t;
    //    double c = 3 * one_minus_t * t * t;
    //    double d = t * t * t;
    // However, some (e.g. when compiling for ARM) fail to do so, so we use this form
    // to help more compilers generate smaller/faster ASM. https://godbolt.org/z/M6jG9x45c
    const double one_minus_t = 1 - t;
    const double one_minus_t_squared = one_minus_t * one_minus_t;
    const double a = (one_minus_t_squared * one_minus_t);
    const double b = 3 * one_minus_t_squared * t;
    const double t_squared = t * t;
    const double c = 3 * one_minus_t * t_squared;
    const double d = t_squared * t;

    const std::array<double, 2> point{a * x0 + b * x1 + c * x2 + d * x3,
                                      a * y0 + b * y1 + c * y2 + d * y3};
    return point;
}

// Perform subdivision using De Casteljau's algorithm, that is, repeated linear
// interpolation between adjacent points.
// twoCurves receives alpha (points 0..3, doubles 0..7) and beta (points 3..6, doubles 6..13);
// the two curves share point 3. curve and twoCurves must not overlap.
void SkBezierCubic::Subdivide(const double curve[8], double t,
                              double twoCurves[14]) {
    SkASSERT(0.0 <= t && t <= 1.0);
    const double x0 = curve[0];
    const double y0 = curve[1];
    const double x1 = curve[2];
    const double y1 = curve[3];
    const double x2 = curve[4];
    const double y2 = curve[5];
    const double x3 = curve[6];
    const double y3 = curve[7];

    const double x01 = interpolate(x0, x1, t);
    const double y01 = interpolate(y0, y1, t);
    const double x12 = interpolate(x1, x2, t);
    const double y12 = interpolate(y1, y2, t);
    const double x23 = interpolate(x2, x3, t);
    const double y23 = interpolate(y2, y3, t);

    const double alphaX2 = interpolate(x01, x12, t);
    const double alphaY2 = interpolate(y01, y12, t);
    const double betaX1 = interpolate(x12, x23, t);
    const double betaY1 = interpolate(y12, y23, t);

    twoCurves[0] = x0;
    twoCurves[1] = y0;
    twoCurves[2] = x01;
    twoCurves[3] = y01;
    twoCurves[4] = alphaX2;
    twoCurves[5] = alphaY2;
    twoCurves[6] = interpolate(alphaX2, betaX1, t);
    twoCurves[7] = interpolate(alphaY2, betaY1, t);
    twoCurves[8] = betaX1;
    twoCurves[9] = betaY1;
    twoCurves[10] = x23;
    twoCurves[11] = y23;
    twoCurves[12] = x3;
    twoCurves[13] = y3;
}

std::array<double, 4> SkBezierCubic::ConvertToPolynomial(const double curve[8], bool yValues) {
    const int offset = yValues ? 1 : 0;
    const double P0 = curve[offset];
    const double P1 = curve[offset + 2];
    const double P2 = curve[offset + 4];
    const double P3 = curve[offset + 6];
    // A cubic Bézier curve is interpolated as follows:
    //  c(t) = (1 - t)^3 P_0 + 3t(1 - t)^2 P_1 + 3t^2 (1 - t) P_2 + t^3 P_3
    //       = (-P_0 + 3P_1 + -3P_2 + P_3) t^3 + (3P_0 - 6P_1 + 3P_2) t^2 +
    //         (-3P_0 + 3P_1) t + P_0
    // Where P_N is the Nth point. The second step expands the polynomial and groups
    // by powers of t. The desired output is a cubic formula, so we just need to
    // combine the appropriate points to make the coefficients.
    std::array<double, 4> results{};
    results[0] = -P0 + 3*P1 - 3*P2 + P3;
    results[1] = 3*P0 - 6*P1 + 3*P2;
    results[2] = -3*P0 + 3*P1;
    results[3] = P0;
    return results;
}

namespace {
struct DPoint {
    double x = 0;
    double y = 0;
};

DPoint to_dpoint(const SkPoint& p) {
    const DPoint point{p.fX, p.fY};
    return point;
}

DPoint operator- (const DPoint& a) {
    const DPoint negated{-a.x, -a.y};
    return negated;
}

DPoint operator+ (const DPoint& a, const DPoint& b) {
    const DPoint sum{a.x + b.x, a.y + b.y};
    return sum;
}

DPoint operator- (const DPoint& a, const DPoint& b) {
    const DPoint difference{a.x - b.x, a.y - b.y};
    return difference;
}

DPoint operator* (double s, const DPoint& a) {
    const DPoint scaled{s * a.x, s * a.y};
    return scaled;
}

// Pin to 0 or 1 if within half a float ulp of 0 or 1.
double pinTRange(double t) {
    // The ULPs around 0 are tiny compared to the ULPs around 1. Shift to 1 to use the same
    // size ULPs.
    double pinned = t;
    if (sk_double_to_float(t + 1.0) == 1.0f) {
        pinned = 0.0;
    } else if (sk_double_to_float(t) == 1.0f) {
        pinned = 1.0;
    }
    return pinned;
}
}  // namespace

SkSpan<const float>
SkBezierCubic::IntersectWithHorizontalLine(
        SkSpan<const SkPoint> controlPoints, float yIntercept, float* intersectionStorage) {
    SkASSERT(controlPoints.size() >= 4);
    const DPoint P0 = to_dpoint(controlPoints[0]);
    const DPoint P1 = to_dpoint(controlPoints[1]);
    const DPoint P2 = to_dpoint(controlPoints[2]);
    const DPoint P3 = to_dpoint(controlPoints[3]);

    const DPoint A =   -P0 + 3*P1 - 3*P2 + P3;
    const DPoint B =  3*P0 - 6*P1 + 3*P2;
    const DPoint C = -3*P0 + 3*P1;
    const DPoint D =    P0;

    const SkSpan<const float> intersections =
            Intersect(A.x, B.x, C.x, D.x, A.y, B.y, C.y, D.y, yIntercept, intersectionStorage);
    return intersections;
}

SkSpan<const float>
SkBezierCubic::Intersect(double AX, double BX, double CX, double DX,
                         double AY, double BY, double CY, double DY,
                         float toIntersect, float intersectionsStorage[3]) {
    double roots[3] = {0, 0, 0};
    const int rootCount = SkCubics::RootsReal(AY, BY, CY, DY - toIntersect, roots);

    size_t intersectionCount = 0;
    for (int i = 0; i < rootCount; ++i) {
        const double pinnedT = pinTRange(roots[i]);
        if (0 <= pinnedT && pinnedT <= 1) {
            intersectionsStorage[intersectionCount] =
                    static_cast<float>(SkCubics::EvalAt(AX, BX, CX, DX, pinnedT));
            ++intersectionCount;
        }
    }

    const SkSpan<const float> intersections(intersectionsStorage, intersectionCount);
    return intersections;
}

SkSpan<const float>
SkBezierQuad::IntersectWithHorizontalLine(SkSpan<const SkPoint> controlPoints, float yIntercept,
                                          float intersectionStorage[2]) {
    SkASSERT(controlPoints.size() >= 3);
    const DPoint p0 = to_dpoint(controlPoints[0]);
    const DPoint p1 = to_dpoint(controlPoints[1]);
    const DPoint p2 = to_dpoint(controlPoints[2]);

    // Calculate A, B, C using doubles to reduce round-off error.
    const DPoint A = p0 - 2 * p1 + p2;
    // Remember we are generating the polynomial in the form A*t^2 -2*B*t + C, so the factor
    // of 2 is not needed and the term is negated. This term for a Bézier curve is usually
    // 2(p1-p0).
    const DPoint B = p0 - p1;
    const DPoint C = p0;

    const SkSpan<const float> intersections =
            Intersect(A.x, B.x, C.x, A.y, B.y, C.y, yIntercept, intersectionStorage);
    return intersections;
}

SkSpan<const float> SkBezierQuad::Intersect(
        double AX, double BX, double CX, double AY, double BY, double CY,
        double yIntercept, float intersectionStorage[2]) {
    const SkQuads::RootResult result = SkQuads::Roots(AY, BY, CY - yIntercept);

    size_t intersectionCount = 0;
    // Round the roots to the nearest float to generate the values t. Valid t's are on the
    // domain [0, 1].
    const double t0 = pinTRange(result.root0);
    if (0 <= t0 && t0 <= 1) {
        intersectionStorage[intersectionCount] =
                static_cast<float>(SkQuads::EvalAt(AX, -2 * BX, CX, t0));
        ++intersectionCount;
    }

    const double t1 = pinTRange(result.root1);
    if (0 <= t1 && t1 <= 1 && t1 != t0) {
        intersectionStorage[intersectionCount] =
                static_cast<float>(SkQuads::EvalAt(AX, -2 * BX, CX, t1));
        ++intersectionCount;
    }

    const SkSpan<const float> intersections(intersectionStorage, intersectionCount);
    return intersections;
}
