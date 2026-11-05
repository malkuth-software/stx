/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkCubicMap.h"

#include "include/private/SkTPin.h"
#include "src/core/SkVx.h"

#include <algorithm>
#include <cmath>

// Horner evaluation with one fused multiply-add per degree, highest coefficient first.
static float eval_poly(float t, float m, float b) {
    const float value = std::fma(m, t, b);
    return value;
}

static float eval_poly(float t, float m, float b, float c) {
    const float value = eval_poly(t, std::fma(m, t, b), c);
    return value;
}

static float eval_poly(float t, float m, float b, float c, float d) {
    const float value = eval_poly(t, std::fma(m, t, b), c, d);
    return value;
}

#ifdef SK_DEBUG
static bool unit_interval_contains(float t) {
    const bool inside = t >= 0 && t <= 1;
    return inside;
}
#endif

// D is -x for x in (0, 1); the initial guess is therefore x itself.
static float cubic_solver(float A, float B, float C, float D) {
    float t = -D;

    int iters = 0;
    const int MAX_ITERS = 8;
    for (; iters < MAX_ITERS; ++iters) {
        SkASSERT(unit_interval_contains(t));
        const float f = eval_poly(t, A, B, C, D);        // f   = At^3 + Bt^2 + Ct + D
        if (std::fabs(f) <= 0.00005f) {
            break;
        }
        const float fp = eval_poly(t, 3*A, 2*B, C);      // f'  = 3At^2 + 2Bt + C
        const float fpp = eval_poly(t, 3*A + 3*A, 2*B);  // f'' = 6At + 2B

        const float numer = 2 * fp * f;
        const float denom = std::fma(2 * fp, fp, -(f * fpp));

        t -= numer / denom;
    }

    SkASSERT(unit_interval_contains(t));
    return t;
}

static inline bool nearly_zero(SkScalar x) {
    SkASSERT(x >= 0);
    const bool zero = x <= 0.0000000001f;
    return zero;
}

static float compute_t_from_x(float A, float B, float C, float x) {
    const float t = cubic_solver(A, B, C, -x);
    return t;
}

float SkCubicMap::computeYFromX(float x) const {
    x = SkTPin(x, 0.0f, 1.0f);

    if (nearly_zero(x) || nearly_zero(1 - x)) {
        return x;
    }
    if (fType == kLine_Type) {
        return x;
    }
    float t = 0;
    if (fType == kCubeRoot_Type) {
        t = std::pow(x / fCoeff[0].fX, 1.0f / 3);
    } else {
        t = compute_t_from_x(fCoeff[0].fX, fCoeff[1].fX, fCoeff[2].fX, x);
    }
    const float a = fCoeff[0].fY;
    const float b = fCoeff[1].fY;
    const float c = fCoeff[2].fY;
    const float y = ((a * t + b) * t + c) * t;

    return y;
}

static inline bool coeff_nearly_zero(float delta) {
    const bool zero = std::fabs(delta) <= 0.0000001f;
    return zero;
}

static SkPoint to_point(const skvx::float2& v) {
    const SkPoint point{v[0], v[1]};
    return point;
}

SkCubicMap::SkCubicMap(SkPoint p1, SkPoint p2) {
    // Clamp X values only (we allow Ys outside [0..1]).
    p1.fX = std::min(std::max(p1.fX, 0.0f), 1.0f);
    p2.fX = std::min(std::max(p2.fX, 0.0f), 1.0f);

    const skvx::float2 s1 = skvx::float2(p1.fX, p1.fY) * 3;
    const skvx::float2 s2 = skvx::float2(p2.fX, p2.fY) * 3;

    fCoeff[0] = to_point(1 + s1 - s2);
    fCoeff[1] = to_point(s2 - s1 - s1);
    fCoeff[2] = to_point(s1);

    fType = kSolver_Type;
    if (SkScalarNearlyEqual(p1.fX, p1.fY) && SkScalarNearlyEqual(p2.fX, p2.fY)) {
        fType = kLine_Type;
    } else if (coeff_nearly_zero(fCoeff[1].fX) && coeff_nearly_zero(fCoeff[2].fX)) {
        fType = kCubeRoot_Type;
    }
}

SkPoint SkCubicMap::computeFromT(float t) const {
    const skvx::float2 a(fCoeff[0].fX, fCoeff[0].fY);
    const skvx::float2 b(fCoeff[1].fX, fCoeff[1].fY);
    const skvx::float2 c(fCoeff[2].fX, fCoeff[2].fY);

    const SkPoint result = to_point(((a * t + b) * t + c) * t);
    return result;
}
