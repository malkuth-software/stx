/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkGeometry_DEFINED
#define SkGeometry_DEFINED

#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSpan.h"
#include "include/core/SkTypes.h"
#include "include/private/SkFloatingPoint.h"
#include "src/core/SkVx.h"


class SkMatrix;
struct SkRect;

static inline skvx::float2 from_point(const SkPoint& point) {
    const skvx::float2 xy(point.fX, point.fY);
    return xy;
}

static inline SkPoint to_point(const skvx::float2& x) {
    const SkPoint point{x[0], x[1]};
    return point;
}

static skvx::float2 times_2(const skvx::float2& value) {
    const skvx::float2 doubled = value + value;
    return doubled;
}

/** Given a quadratic equation Ax^2 + Bx + C = 0, return 0, 1, 2 roots for the
    equation.
*/
int SkFindUnitQuadRoots(SkScalar A, SkScalar B, SkScalar C, SkScalar roots[2]);

///////////////////////////////////////////////////////////////////////////////

SkPoint SkEvalQuadAt(const SkPoint src[3], SkScalar t);
SkPoint SkEvalQuadTangentAt(const SkPoint src[3], SkScalar t);

/** Set pt to the point on the src quadratic specified by t. t must be
    0 <= t <= 1.0
*/
void SkEvalQuadAt(const SkPoint src[3], SkScalar t, SkPoint* pt, SkVector* tangent = nullptr);

/** Given a src quadratic bezier, chop it at the specified t value,
    where 0 < t < 1, and return the two new quadratics in dst:
    dst[0..2] and dst[2..4]
*/
void SkChopQuadAt(const SkPoint src[3], SkPoint dst[5], SkScalar t);

/** Given a src quadratic bezier, chop it at the specified t == 1/2,
    The new quads are returned in dst[0..2] and dst[2..4]
*/
void SkChopQuadAtHalf(const SkPoint src[3], SkPoint dst[5]);

/** Given the 3 coefficients for a quadratic bezier (either X or Y values), look
    for extrema, and return the number of t-values that are found that represent
    these extrema. If the quadratic has no extrema betwee (0..1) exclusive, the
    function returns 0.
    Returned count      tValues[]
    0                   ignored
    1                   0 < tValues[0] < 1
*/
int SkFindQuadExtrema(SkScalar a, SkScalar b, SkScalar c, SkScalar tValues[1]);

/** Given 3 points on a quadratic bezier, chop it into 1, 2 beziers such that
    the resulting beziers are monotonic in Y. This is called by the scan converter.
    Depending on what is returned, dst[] is treated as follows
    0   dst[0..2] is the original quad
    1   dst[0..2] and dst[2..4] are the two new quads
*/
int SkChopQuadAtYExtrema(const SkPoint src[3], SkPoint dst[5]);
int SkChopQuadAtXExtrema(const SkPoint src[3], SkPoint dst[5]);

/** Given 3 points on a quadratic bezier, if the point of maximum
    curvature exists on the segment, returns the t value for this
    point along the curve. Otherwise it will return a value of 0.
*/
SkScalar SkFindQuadMaxCurvature(const SkPoint src[3]);

///////////////////////////////////////////////////////////////////////////////

/** Set pt to the point on the src cubic specified by t. t must be
    0 <= t <= 1.0
*/
void SkEvalCubicAt(const SkPoint src[4], SkScalar t, SkPoint* locOrNull,
                   SkVector* tangentOrNull, SkVector* curvatureOrNull);

/** Given a src cubic bezier, chop it at the specified t value,
    where 0 <= t <= 1, and return the two new cubics in dst:
    dst[0..3] and dst[3..6]
*/
void SkChopCubicAt(const SkPoint src[4], SkPoint dst[7], SkScalar t);

/** Given a src cubic bezier, chop it at the specified t0 and t1 values,
    where 0 <= t0 <= t1 <= 1, and return the three new cubics in dst:
    dst[0..3], dst[3..6], and dst[6..9]
*/
void SkChopCubicAt(const SkPoint src[4], SkPoint dst[10], float t0, float t1);

/** Given a src cubic bezier, chop it at the specified t values,
    where 0 <= t0 <= t1 <= ... <= 1, and return the new cubics in dst:
    dst[0..3],dst[3..6],...,dst[3*t_count..3*(t_count+1)]
*/
void SkChopCubicAt(const SkPoint src[4], SkPoint dst[], const SkScalar t[],
                   int t_count);

/** Given a src cubic bezier, chop it at the specified t == 1/2,
    The new cubics are returned in dst[0..3] and dst[3..6]
*/
void SkChopCubicAtHalf(const SkPoint src[4], SkPoint dst[7]);

/** Given the 4 coefficients for a cubic bezier (either X or Y values), look
    for extrema, and return the number of t-values that are found that represent
    these extrema. If the cubic has no extrema betwee (0..1) exclusive, the
    function returns 0.
    Returned count      tValues[]
    0                   ignored
    1                   0 < tValues[0] < 1
    2                   0 < tValues[0] < tValues[1] < 1
*/
int SkFindCubicExtrema(SkScalar a, SkScalar b, SkScalar c, SkScalar d,
                       SkScalar tValues[2]);

/** Given 4 points on a cubic bezier, chop it into 1, 2, 3 beziers such that
    the resulting beziers are monotonic in Y. This is called by the scan converter.
    Depending on what is returned, dst[] is treated as follows
    0   dst[0..3] is the original cubic
    1   dst[0..3] and dst[3..6] are the two new cubics
    2   dst[0..3], dst[3..6], dst[6..9] are the three new cubics
    If dst == null, it is ignored and only the count is returned.
*/
int SkChopCubicAtYExtrema(const SkPoint src[4], SkPoint dst[10]);
int SkChopCubicAtXExtrema(const SkPoint src[4], SkPoint dst[10]);

/** Given a cubic bezier, return 0, 1, or 2 t-values that represent the
    inflection points.
*/
int SkFindCubicInflections(const SkPoint src[4], SkScalar tValues[2]);

int SkFindCubicMaxCurvature(const SkPoint src[4], SkScalar tValues[3]);
int SkChopCubicAtMaxCurvature(const SkPoint src[4], SkPoint dst[13],
                              SkScalar tValues[3] = nullptr);
/** Returns t value of cusp if cubic has one; returns -1 otherwise.
 */
SkScalar SkFindCubicCusp(const SkPoint src[4]);

/** Given a monotonically increasing or decreasing cubic bezier src, chop it
 *  where the X value is the specified value. The returned cubics will be in
 *  dst, sharing the middle point. That is, the first cubic is dst[0..3] and
 *  the second dst[3..6].
 *
 *  If the cubic provided is *not* monotone, it will be chopped at the first
 *  time the curve has the specified X value.
 *
 *  If the cubic never reaches the specified value, the function returns false.
*/
bool SkChopMonoCubicAtX(const SkPoint src[4], SkScalar x, SkPoint dst[7]);

/** Given a monotonically increasing or decreasing cubic bezier src, chop it
 *  where the Y value is the specified value. The returned cubics will be in
 *  dst, sharing the middle point. That is, the first cubic is dst[0..3] and
 *  the second dst[3..6].
 *
 *  If the cubic provided is *not* monotone, it will be chopped at the first
 *  time the curve has the specified Y value.
 *
 *  If the cubic never reaches the specified value, the function returns false.
*/
bool SkChopMonoCubicAtY(const SkPoint src[4], SkScalar y, SkPoint dst[7]);

///////////////////////////////////////////////////////////////////////////////

// A rational quadratic with end weights 1 and middle weight fW. A default conic is the
// degenerate unit-weight curve at the origin.
struct SkConic {
    SkConic() = default;
    SkConic(const SkPoint& p0, const SkPoint& p1, const SkPoint& p2, SkScalar w) {
        set(p0, p1, p2, w);
    }

    SkConic(const SkPoint pts[3], SkScalar w) {
        set(pts, w);
    }

    SkPoint  fPts[3] = {{0, 0}, {0, 0}, {0, 0}};
    SkScalar fW = 1;

    // pts may be fPts.
    void set(const SkPoint pts[3], SkScalar w) {
        const SkPoint p0 = pts[0];
        const SkPoint p1 = pts[1];
        const SkPoint p2 = pts[2];
        set(p0, p1, p2, w);
    }

    void set(const SkPoint& p0, const SkPoint& p1, const SkPoint& p2, SkScalar w) {
        fPts[0] = p0;
        fPts[1] = p1;
        fPts[2] = p2;
        setW(w);
    }

    void setW(SkScalar w) {
        if (SkIsFinite(w)) {
            SkASSERT(w > 0);
        }

        // Guard against bad weights by forcing them to 1.
        fW = w > 0 && SkIsFinite(w) ? w : 1;
    }

    /**
     *  Given a t-value [0...1] return its position and/or tangent.
     *  If pos is not null, return its position at the t-value.
     *  If tangent is not null, return its tangent at the t-value. NOTE the
     *  tangent value's length is arbitrary, and only its direction should
     *  be used.
     */
    void evalAt(SkScalar t, SkPoint* pos, SkVector* tangent = nullptr) const;
    [[nodiscard]] bool chopAt(SkScalar t, SkConic dst[2]) const;
    void chopAt(SkScalar t1, SkScalar t2, SkConic* dst) const;
    void chop(SkConic dst[2]) const;

    SkPoint evalAt(SkScalar t) const;
    SkVector evalTangentAt(SkScalar t) const;

    /**
     *  return the power-of-2 number of quads needed to approximate this conic
     *  with a sequence of quads. Will be >= 0.
     */
    int SK_SPI computeQuadPOW2(SkScalar tol) const;

    /**
     *  Chop this conic into N quads, stored continguously in pts[], where
     *  N = 1 << pow2. The amount of storage needed is (1 + 2 * N)
     */
    [[nodiscard]] int SK_SPI chopIntoQuadsPOW2(SkPoint pts[], int pow2) const;

    bool findXExtrema(SkScalar* t) const;
    bool findYExtrema(SkScalar* t) const;
    bool chopAtYExtrema(SkConic dst[2]) const;

    void computeTightBounds(SkRect* bounds) const;
    void computeFastBounds(SkRect* bounds) const;

    static SkScalar TransformW(const SkPoint[3], SkScalar w, const SkMatrix&);

    enum {
        kMaxConicsForArc = 5
    };
    static int BuildUnitArc(const SkVector& start, const SkVector& stop, SkPathDirection,
                            const SkMatrix*, SkConic conics[kMaxConicsForArc]);
};

// inline helpers are contained in a namespace to avoid external leakage to fragile SkVx members
namespace {  // NOLINT(google-build-namespaces)

/**
 *  use for : eval(t) == A * t^2 + B * t + C
 */
struct SkQuadCoeff {
    SkQuadCoeff() = default;

    SkQuadCoeff(const skvx::float2& A, const skvx::float2& B, const skvx::float2& C)
        : fA(A)
        , fB(B)
        , fC(C)
    {
    }

    SkQuadCoeff(const SkPoint src[3]) {
        fC = from_point(src[0]);
        const skvx::float2 P1 = from_point(src[1]);
        const skvx::float2 P2 = from_point(src[2]);
        fB = times_2(P1 - fC);
        fA = P2 - times_2(P1) + fC;
    }

    skvx::float2 eval(const skvx::float2& tt) const {
        const skvx::float2 value = (fA * tt + fB) * tt + fC;
        return value;
    }

    skvx::float2 fA = skvx::float2(0.0f);
    skvx::float2 fB = skvx::float2(0.0f);
    skvx::float2 fC = skvx::float2(0.0f);
};

struct SkConicCoeff {
    SkConicCoeff(const SkConic& conic) {
        const skvx::float2 p0 = from_point(conic.fPts[0]);
        const skvx::float2 p1 = from_point(conic.fPts[1]);
        const skvx::float2 p2 = from_point(conic.fPts[2]);
        const skvx::float2 ww(conic.fW);

        const skvx::float2 p1w = p1 * ww;
        fNumer.fC = p0;
        fNumer.fA = p2 - times_2(p1w) + p0;
        fNumer.fB = times_2(p1w - p0);

        fDenom.fC = 1;
        fDenom.fB = times_2(ww - fDenom.fC);
        fDenom.fA = 0 - fDenom.fB;
    }

    skvx::float2 eval(SkScalar t) const {
        const skvx::float2 tt(t);
        const skvx::float2 numer = fNumer.eval(tt);
        const skvx::float2 denom = fDenom.eval(tt);
        const skvx::float2 point = numer / denom;
        return point;
    }

    SkQuadCoeff fNumer;
    SkQuadCoeff fDenom;
};

struct SkCubicCoeff {
    SkCubicCoeff(const SkPoint src[4]) {
        const skvx::float2 P0 = from_point(src[0]);
        const skvx::float2 P1 = from_point(src[1]);
        const skvx::float2 P2 = from_point(src[2]);
        const skvx::float2 P3 = from_point(src[3]);
        const skvx::float2 three(3);
        fA = P3 + three * (P1 - P2) - P0;
        fB = three * (P2 - times_2(P1) + P0);
        fC = three * (P1 - P0);
        fD = P0;
    }

    skvx::float2 eval(const skvx::float2& t) const {
        const skvx::float2 value = ((fA * t + fB) * t + fC) * t + fD;
        return value;
    }

    skvx::float2 fA = skvx::float2(0.0f);
    skvx::float2 fB = skvx::float2(0.0f);
    skvx::float2 fC = skvx::float2(0.0f);
    skvx::float2 fD = skvx::float2(0.0f);
};

}  // namespace

#include "include/private/SkTemplates.h"

/**
 *  Help class to allocate storage for approximating a conic with N quads.
 */
// The returned points borrow this object's storage until the next computeQuads call.
class [[nodiscard]] SkAutoConicToQuads {
public:
    SkAutoConicToQuads() = default;

    /**
     *  Given a conic and a tolerance, return the array of points for the
     *  approximating quad(s). Call countQuads() to know the number of quads
     *  represented in these points.
     *
     *  The quads are allocated to share end-points. e.g. if there are 4 quads,
     *  there will be 9 points allocated as follows
     *      quad[0] == pts[0..2]
     *      quad[1] == pts[2..4]
     *      quad[2] == pts[4..6]
     *      quad[3] == pts[6..8]
     */
    const SkPoint* computeQuads(const SkConic& conic, SkScalar tol) {
        const int pow2 = conic.computeQuadPOW2(tol);
        fQuadCount = 1 << pow2;
        SkPoint* pts = fStorage.reset(1 + 2 * fQuadCount);
        fQuadCount = conic.chopIntoQuadsPOW2(pts, pow2);
        return pts;
    }

    const SkPoint* computeQuads(SkSpan<const SkPoint> pts, SkScalar weight, SkScalar tol) {
        if (weight <= 0) {
            // The underlying conic.chopIntoQuads... is based on an algorithm that
            // requires a positive, non-zero weight. So for w = 0 (or any invalid w < 0),
            // we just make this a quad that is a line between the two points.
            fQuadCount = 1;
            SkPoint* outPts = fStorage.reset(3);
            outPts[0] = pts[0];
            outPts[1] = SkPoint{(pts[0].fX + pts[2].fX) * 0.5f, (pts[0].fY + pts[2].fY) * 0.5f};
            outPts[2] = pts[2];
            return outPts;
        }
        const SkConic conic(pts.data(), weight);
        const SkPoint* quads = computeQuads(conic, tol);
        return quads;
    }

    const SkPoint* computeQuads(const SkPoint pts[3], SkScalar weight, SkScalar tol) {
        const SkPoint* quads = computeQuads(SkSpan<const SkPoint>(pts, 3), weight, tol);
        return quads;
    }

    int countQuads() const { return fQuadCount; }

private:
    enum {
        kQuadCount = 8, // should handle most conics
        kPointCount = 1 + 2 * kQuadCount,
    };
    skia_private::AutoSTMalloc<kPointCount, SkPoint> fStorage;
    int fQuadCount = 0; // #quads for current usage
};

#endif
