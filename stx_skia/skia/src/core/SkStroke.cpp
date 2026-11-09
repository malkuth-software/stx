/*
 * Copyright 2008 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkStroke.h"

#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSpan.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkTo.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkPaintDefaults.h"
#include "src/core/SkPathEnums.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPointPriv.h"
#include "src/core/SkStrokerPriv.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

enum {
    kTangent_RecursiveLimit,
    kCubic_RecursiveLimit,
    kConic_RecursiveLimit,
    kQuad_RecursiveLimit
};

// quads with extreme widths (e.g. (0,1) (1,6) (0,3) width=5e7) recurse to point of failure
// largest seen for normal cubics : 5, 26
// largest seen for normal quads : 11
// 3x limits seen in practice, except for cubics (3x limit would be ~75).
// For cubics, we never get close to 75 when running through dm. The limit of 24
// was chosen because it's close to the peak in a count of cubic recursion depths visited
// and no diffs were produced on gold when using it.
static constexpr int kRecursiveLimits[] = { 5*3, 24, 11*3, 11*3 };

static_assert(0 == kTangent_RecursiveLimit, "cubic_stroke_relies_on_tangent_equalling_zero");
static_assert(1 == kCubic_RecursiveLimit, "cubic_stroke_relies_on_cubic_equalling_one");
static_assert(std::size(kRecursiveLimits) == kQuad_RecursiveLimit + 1,
              "recursive_limits_mismatch");

static inline bool degenerate_vector(const SkVector& v) {
    const bool degenerate = !SkPointPriv::CanNormalize(v.fX, v.fY);
    return degenerate;
}

static bool set_normal_unitnormal(const SkPoint& before, const SkPoint& after, SkScalar scale,
                                  SkScalar radius,
                                  SkVector* normal, SkVector* unitNormal) {
    if (!(*unitNormal).setNormalize((after.fX - before.fX) * scale,
                                    (after.fY - before.fY) * scale)) {
        return false;
    }
    SkPointPriv::RotateCCW(unitNormal);
    (*unitNormal).scale(radius, normal);
    return true;
}

static bool set_normal_unitnormal(const SkVector& vec,
                                  SkScalar radius,
                                  SkVector* normal, SkVector* unitNormal) {
    if (!(*unitNormal).setNormalize(vec.fX, vec.fY)) {
        return false;
    }
    SkPointPriv::RotateCCW(unitNormal);
    (*unitNormal).scale(radius, normal);
    return true;
}

///////////////////////////////////////////////////////////////////////////////

struct SkQuadConstruct {    // The state of the quad stroke under construction.
    SkPoint fQuad[3] = {};          // the stroked quad parallel to the original curve
    SkVector fTangentStart{0, 0};   // tangent vector at fQuad[0]
    SkVector fTangentEnd{0, 0};     // tangent vector at fQuad[2]
    SkScalar fStartT = 0;           // a segment of the original curve
    SkScalar fMidT = 0;             //              "
    SkScalar fEndT = 0;             //              "
    bool fStartSet = false;         // state to share common points across structs
    bool fEndSet = false;           //                     "
    bool fOppositeTangents = false; // set if coincident tangents have opposite directions

    // return false if start and end are too close to have a unique middle
    bool init(SkScalar start, SkScalar end) {
        fStartT = start;
        fMidT = (start + end) * SK_ScalarHalf;
        fEndT = end;
        fStartSet = false;
        fEndSet = false;
        const bool distinct = fStartT < fMidT && fMidT < fEndT;
        return distinct;
    }

    bool initWithStart(const SkQuadConstruct* parent) {
        if (!init((*parent).fStartT, (*parent).fMidT)) {
            return false;
        }
        fQuad[0] = (*parent).fQuad[0];
        fTangentStart = (*parent).fTangentStart;
        fStartSet = true;
        return true;
    }

    bool initWithEnd(const SkQuadConstruct* parent) {
        if (!init((*parent).fMidT, (*parent).fEndT)) {
            return false;
        }
        fQuad[2] = (*parent).fQuad[2];
        fTangentEnd = (*parent).fTangentEnd;
        fEndSet = true;
        return true;
   }
};

static bool isZeroLengthSincePoint(SkSpan<const SkPoint> span, int startPtIndex) {
    const int count = SkToInt(span.size()) - startPtIndex;
    if (count < 2) {
        return true;
    }
    const SkPoint& first = span[static_cast<size_t>(startPtIndex)];
    for (int index = 1; index < count; ++index) {
        if (first != span[static_cast<size_t>(startPtIndex + index)]) {
            return false;
        }
    }
    return true;
}

class SkPathStroker {
public:
    SkPathStroker(const SkPath& src,
                  SkScalar radius, SkScalar miterLimit, SkPaint::Cap,
                  SkPaint::Join, SkScalar resScale,
                  bool canIgnoreCenter);

    bool hasOnlyMoveTo() const {
        const bool onlyMove = 0 == fSegmentCount;
        return onlyMove;
    }
    SkPoint moveToPt() const { return fFirstPt; }

    void moveTo(const SkPoint&);
    void lineTo(const SkPoint&, const SkPath::Iter* iter = nullptr);
    void quadTo(const SkPoint&, const SkPoint&);
    void conicTo(const SkPoint&, const SkPoint&, SkScalar weight);
    void cubicTo(const SkPoint&, const SkPoint&, const SkPoint&);
    void close(bool isLine) { finishContour(true, isLine); }

    void done(SkPathBuilder* dst, bool isLine) {
        finishContour(false, isLine);
        *dst = fOuter;
        fOuter.reset(); // is this needed? we used to "swap" it with dst
    }

    SkScalar getResScale() const { return fResScale; }

    bool isCurrentContourEmpty() const {
        const bool empty = isZeroLengthSincePoint(fInner.points(), 0) &&
                           isZeroLengthSincePoint(fOuter.points(), fFirstOuterPtIndexInContour);
        return empty;
    }

private:
    SkScalar    fRadius = 0;
    SkScalar    fInvMiterLimit = 0;
    SkScalar    fResScale = 1;
    SkScalar    fInvResScale = 0;
    SkScalar    fInvResScaleSquared = 0;

    SkVector    fFirstNormal{0, 0};
    SkVector    fPrevNormal{0, 0};
    SkVector    fFirstUnitNormal{0, 0};
    SkVector    fPrevUnitNormal{0, 0};
    SkPoint     fFirstPt{0, 0};  // on original path
    SkPoint     fPrevPt{0, 0};
    SkPoint     fFirstOuterPt{0, 0};
    int         fFirstOuterPtIndexInContour = 0;
    int         fSegmentCount = -1;
    bool        fPrevIsLine = false;
    bool        fCanIgnoreCenter = false;

    SkStrokerPriv::CapProc  fCapper = nullptr;
    SkStrokerPriv::JoinProc fJoiner = nullptr;

    SkPathBuilder  fInner;  // per-contour temp
    SkPathBuilder  fOuter;  // our working answer
    SkPathBuilder  fCusper;

    enum StrokeType {
        kOuter_StrokeType = 1,      // use sign-opposite values later to flip perpendicular axis
        kInner_StrokeType = -1
    };
    StrokeType fStrokeType = kOuter_StrokeType;

    enum ResultType {
        kSplit_ResultType,          // the caller should split the quad stroke in two
        kDegenerate_ResultType,     // the caller should add a line
        kQuad_ResultType,           // the caller should (continue to try to) add a quad stroke
    };

    enum ReductionType {
        kPoint_ReductionType,       // all curve points are practically identical
        kLine_ReductionType,        // the control point is on the line between the ends
        kQuad_ReductionType,        // the control point is outside the line between the ends
        kDegenerate_ReductionType,  // the control point is on the line but outside the ends
        kDegenerate2_ReductionType, // two control points are on the line but outside ends (cubic)
        kDegenerate3_ReductionType, // three areas of max curvature found (for cubic)
    };

    enum IntersectRayType {
        kCtrlPt_RayType,
        kResultType_RayType,
    };

    int fRecursionDepth = 0;        // track stack depth to abort if numerics run amok
    bool fFoundTangents = false;    // do less work until tangents meet (cubic)
    bool fJoinCompleted = false;    // previous join was not degenerate

    // The builder the current stroke side appends to.
    SkPathBuilder& strokeSink() {
        SkPathBuilder& sink = fStrokeType == kOuter_StrokeType ? fOuter : fInner;
        return sink;
    }

    void addDegenerateLine(const SkQuadConstruct* );
    static ReductionType CheckConicLinear(const SkConic& , SkPoint* reduction);
    static ReductionType CheckCubicLinear(const SkPoint cubic[4], SkPoint reduction[3],
                                   const SkPoint** tanPtPtr);
    static ReductionType CheckQuadLinear(const SkPoint quad[3], SkPoint* reduction);
    ResultType compareQuadConic(const SkConic& , SkQuadConstruct* ) const;
    ResultType compareQuadCubic(const SkPoint cubic[4], SkQuadConstruct* );
    ResultType compareQuadQuad(const SkPoint quad[3], SkQuadConstruct* );
    void conicPerpRay(const SkConic& , SkScalar t, SkPoint* tPt, SkPoint* onPt,
                      SkVector* tangent) const;
    void conicQuadEnds(const SkConic& , SkQuadConstruct* ) const;
    bool conicStroke(const SkConic& , SkQuadConstruct* );
    bool cubicMidOnLine(const SkPoint cubic[4], const SkQuadConstruct* ) const;
    void cubicPerpRay(const SkPoint cubic[4], SkScalar t, SkPoint* tPt, SkPoint* onPt,
                      SkVector* tangent) const;
    void cubicQuadEnds(const SkPoint cubic[4], SkQuadConstruct* );
    void cubicQuadMid(const SkPoint cubic[4], const SkQuadConstruct* , SkPoint* mid) const;
    bool cubicStroke(const SkPoint cubic[4], SkQuadConstruct* );
    void init(StrokeType strokeType, SkQuadConstruct* , SkScalar tStart, SkScalar tEnd);
    ResultType intersectRay(SkQuadConstruct* , IntersectRayType) const;
    bool ptInQuadBounds(const SkPoint quad[3], const SkPoint& pt) const;
    void quadPerpRay(const SkPoint quad[3], SkScalar t, SkPoint* tPt, SkPoint* onPt,
                     SkPoint* tangent) const;
    bool quadStroke(const SkPoint quad[3], SkQuadConstruct* );
    void setConicEndNormal(const SkConic& ,
                           const SkVector& normalAB, const SkVector& unitNormalAB,
                           SkVector* normalBC, SkVector* unitNormalBC);
    void setCubicEndNormal(const SkPoint cubic[4],
                           const SkVector& normalAB, const SkVector& unitNormalAB,
                           SkVector* normalCD, SkVector* unitNormalCD);
    void setQuadEndNormal(const SkPoint quad[3],
                          const SkVector& normalAB, const SkVector& unitNormalAB,
                          SkVector* normalBC, SkVector* unitNormalBC);
    void setRayPts(const SkPoint& tPt, SkVector* dxy, SkPoint* onPt, SkVector* tangent) const;
    ResultType strokeCloseEnough(const SkPoint stroke[3], const SkPoint ray[2],
                                 SkQuadConstruct* ) const;
    ResultType tangentsMeet(const SkPoint cubic[4], SkQuadConstruct* );

    void    finishContour(bool close, bool isLine);
    bool    preJoinTo(const SkPoint&, SkVector* normal, SkVector* unitNormal,
                      bool isLine);
    void    postJoinTo(const SkPoint&, const SkVector& normal,
                       const SkVector& unitNormal);

    void    line_to(const SkPoint& currPt, const SkVector& normal);
};

///////////////////////////////////////////////////////////////////////////////

bool SkPathStroker::preJoinTo(const SkPoint& currPt, SkVector* normal,
                              SkVector* unitNormal, bool currIsLine) {
    SkASSERT(fSegmentCount >= 0);

    if (!set_normal_unitnormal(fPrevPt, currPt, fResScale, fRadius, normal, unitNormal)) {
        if (SkStrokerPriv::CapFactory(SkPaint::kButt_Cap) == fCapper) {
            return false;
        }
        /* Square caps and round caps draw even if the segment length is zero.
           Since the zero length segment has no direction, set the orientation
           to upright as the default orientation */
        (*normal).set(fRadius, 0);
        (*unitNormal).set(1, 0);
    }

    if (fSegmentCount == 0) {
        fFirstNormal = *normal;
        fFirstUnitNormal = *unitNormal;
        fFirstOuterPt = fPrevPt + *normal;

        fOuter.moveTo(fFirstOuterPt);
        fInner.moveTo(fPrevPt - *normal);
    } else {    // we have a previous segment
        fJoiner(&fOuter, &fInner, fPrevUnitNormal, fPrevPt, *unitNormal,
                fRadius, fInvMiterLimit, fPrevIsLine, currIsLine);
    }
    fPrevIsLine = currIsLine;
    return true;
}

void SkPathStroker::postJoinTo(const SkPoint& currPt, const SkVector& normal,
                               const SkVector& unitNormal) {
    fJoinCompleted = true;
    fPrevPt = currPt;
    fPrevUnitNormal = unitNormal;
    fPrevNormal = normal;
    fSegmentCount += 1;
}

void SkPathStroker::finishContour(bool close, bool currIsLine) {
    if (fSegmentCount > 0) {
        if (close) {
            fJoiner(&fOuter, &fInner, fPrevUnitNormal, fPrevPt,
                    fFirstUnitNormal, fRadius, fInvMiterLimit,
                    fPrevIsLine, currIsLine);
            fOuter.close();

            if (fCanIgnoreCenter) {
                // If we can ignore the center just make sure the larger of the two paths
                // is preserved and don't add the smaller one.
                if (fInner.computeBounds().contains(fOuter.computeBounds())) {
                    fOuter = fInner;
                }
            } else {
                // now add fInner as its own contour
                const std::optional<SkPoint> innerLast = fInner.getLastPt();
                if (innerLast.has_value()) {
                    fOuter.moveTo(innerLast.value());
                    fOuter.privateReversePathTo(fInner.detach()); // todo: take builder or raw
                    fOuter.close();
                }
            }
        } else {    // add caps to start and end
            // cap the end
            const std::optional<SkPoint> innerLast = fInner.getLastPt();
            if (innerLast.has_value()) {
                fCapper(&fOuter, fPrevPt, fPrevNormal, innerLast.value(), currIsLine);
                fOuter.privateReversePathTo(fInner.detach());
                // cap the start
                fCapper(&fOuter, fFirstPt, -fFirstNormal, fFirstOuterPt, fPrevIsLine);
                fOuter.close();
            }
        }
        if (!fCusper.isEmpty()) {
            fOuter.addPath(fCusper.detach());
        }
    }
    fInner.reset();
    fSegmentCount = -1;
    fFirstOuterPtIndexInContour = fOuter.countPoints();
}

///////////////////////////////////////////////////////////////////////////////

SkPathStroker::SkPathStroker(const SkPath& src,
                             SkScalar radius, SkScalar miterLimit,
                             SkPaint::Cap cap, SkPaint::Join join, SkScalar resScale,
                             bool canIgnoreCenter)
        : fRadius(radius)
        , fResScale(resScale)
        , fCanIgnoreCenter(canIgnoreCenter) {

    /*  This is only used when join is miter_join, but we initialize it here
        so that it is always defined, to fix sanitizer warnings.
    */
    fInvMiterLimit = 0;

    if (join == SkPaint::kMiter_Join) {
        if (miterLimit <= SK_Scalar1) {
            join = SkPaint::kBevel_Join;
        } else {
            fInvMiterLimit = SkScalarInvert(miterLimit);
        }
    }
    fCapper = SkStrokerPriv::CapFactory(cap);
    fJoiner = SkStrokerPriv::JoinFactory(join);
    fSegmentCount = -1;
    fFirstOuterPtIndexInContour = 0;
    fPrevIsLine = false;

    // Need some estimate of how large our final result (fOuter)
    // and our per-contour temp (fInner) will be, so we don't spend
    // extra time repeatedly growing these arrays.
    //
    // 3x for result == inner + outer + join (swag)
    // 1x for inner == 'wag' (worst contour length would be better guess)
    fOuter.incReserve(src.countPoints() * 3);
    fOuter.setIsVolatile(true);
    fInner.incReserve(src.countPoints());
    fInner.setIsVolatile(true);
    // TODO : write a common error function used by stroking and filling
    // The '4' below matches the fill scan converter's error term
    fInvResScale = SkScalarInvert(resScale * 4);
    fInvResScaleSquared = fInvResScale * fInvResScale;
    fRecursionDepth = 0;
}

void SkPathStroker::moveTo(const SkPoint& pt) {
    if (fSegmentCount > 0) {
        finishContour(false, false);
    }
    fSegmentCount = 0;
    fFirstPt = pt;
    fPrevPt = pt;
    fJoinCompleted = false;
}

void SkPathStroker::line_to(const SkPoint& currPt, const SkVector& normal) {
    fOuter.lineTo(currPt + normal);
    fInner.lineTo(currPt - normal);
}

// Looks ahead (on a copy of iter) for a segment with length before the contour ends.
static bool has_valid_tangent(const SkPath::Iter* iter) {
    SkPath::Iter copy = *iter;
    for (std::optional<SkPath::IterRec> rec = copy.next(); rec.has_value(); rec = copy.next()) {
        const SkPath::IterRec& record = rec.value();
        const SkSpan<const SkPoint> pts = record.fPoints;
        switch (record.fVerb) {
            case SkPathVerb::kMove:
                return false;
            case SkPathVerb::kLine:
                if (pts[0] == pts[1]) {
                    continue;
                }
                return true;
            case SkPathVerb::kQuad:
            case SkPathVerb::kConic:
                if (pts[0] == pts[1] && pts[0] == pts[2]) {
                    continue;
                }
                return true;
            case SkPathVerb::kCubic:
                if (pts[0] == pts[1] && pts[0] == pts[2] && pts[0] == pts[3]) {
                    continue;
                }
                return true;
            case SkPathVerb::kClose:
                return false;
        }
    }
    return false;
}

void SkPathStroker::lineTo(const SkPoint& currPt, const SkPath::Iter* iter) {
    const bool teenyLine = SkPointPriv::EqualsWithinTolerance(fPrevPt, currPt,
                                                              SK_ScalarNearlyZero * fInvResScale);
    if (SkStrokerPriv::CapFactory(SkPaint::kButt_Cap) == fCapper && teenyLine) {
        return;
    }
    if (teenyLine && (fJoinCompleted || (iter && has_valid_tangent(iter)))) {
        return;
    }
    SkVector    normal{0, 0};
    SkVector    unitNormal{0, 0};

    if (!preJoinTo(currPt, &normal, &unitNormal, true)) {
        return;
    }
    line_to(currPt, normal);
    postJoinTo(currPt, normal, unitNormal);
}

void SkPathStroker::setQuadEndNormal(const SkPoint quad[3], const SkVector& normalAB,
        const SkVector& unitNormalAB, SkVector* normalBC, SkVector* unitNormalBC) {
    if (!set_normal_unitnormal(quad[1], quad[2], fResScale, fRadius, normalBC, unitNormalBC)) {
        *normalBC = normalAB;
        *unitNormalBC = unitNormalAB;
    }
}

void SkPathStroker::setConicEndNormal(const SkConic& conic, const SkVector& normalAB,
        const SkVector& unitNormalAB, SkVector* normalBC, SkVector* unitNormalBC) {
    setQuadEndNormal(conic.fPts, normalAB, unitNormalAB, normalBC, unitNormalBC);
}

void SkPathStroker::setCubicEndNormal(const SkPoint cubic[4], const SkVector& normalAB,
        const SkVector& unitNormalAB, SkVector* normalCD, SkVector* unitNormalCD) {
    SkVector    ab = cubic[1] - cubic[0];
    SkVector    cd = cubic[3] - cubic[2];

    bool    degenerateAB = degenerate_vector(ab);
    bool    degenerateCD = degenerate_vector(cd);

    // When both end tangents are degenerate, the end normal repeats the start normal.
    if (!(degenerateAB && degenerateCD)) {
        if (degenerateAB) {
            ab = cubic[2] - cubic[0];
            degenerateAB = degenerate_vector(ab);
        }
        if (degenerateCD) {
            cd = cubic[3] - cubic[1];
            degenerateCD = degenerate_vector(cd);
        }
    }
    if (degenerateAB || degenerateCD) {
        *normalCD = normalAB;
        *unitNormalCD = unitNormalAB;
        return;
    }
    const bool normalized = set_normal_unitnormal(cd, fRadius, normalCD, unitNormalCD);
    SkASSERT(normalized);
    static_cast<void>(normalized);
}

void SkPathStroker::init(StrokeType strokeType, SkQuadConstruct* quadPts, SkScalar tStart,
        SkScalar tEnd) {
    fStrokeType = strokeType;
    fFoundTangents = false;
    static_cast<void>((*quadPts).init(tStart, tEnd));
}

// returns the distance squared from the point to the line
static SkScalar pt_to_line(const SkPoint& pt, const SkPoint& lineStart, const SkPoint& lineEnd) {
    const SkVector dxy = lineEnd - lineStart;
    const SkVector ab0 = pt - lineStart;
    const SkScalar numer = dxy.dot(ab0);
    const SkScalar denom = dxy.dot(dxy);
    const SkScalar t = sk_ieee_float_divide(numer, denom);
    SkScalar distanceSqd = 0;
    if (t >= 0 && t <= 1) {
        const SkPoint hit = lineStart * (1 - t) + lineEnd * t;
        distanceSqd = SkPointPriv::DistanceToSqd(hit, pt);
    } else {
        distanceSqd = SkPointPriv::DistanceToSqd(pt, lineStart);
    }
    return distanceSqd;
}

// returns the distance squared from the point to the line
static SkScalar pt_to_tangent_line(const SkPoint& pt,
                                   const SkPoint& lineStart,
                                   const SkVector& tangent) {
    const SkVector dxy = tangent;
    const SkVector ab0 = pt - lineStart;
    const SkScalar numer = dxy.dot(ab0);
    const SkScalar denom = dxy.dot(dxy);
    const SkScalar t = sk_ieee_float_divide(numer, denom);
    SkScalar distanceSqd = 0;
    if (t >= 0 && t <= 1) {
        const SkPoint hit = lineStart + tangent * t;
        distanceSqd = SkPointPriv::DistanceToSqd(hit, pt);
    } else {
        distanceSqd = SkPointPriv::DistanceToSqd(pt, lineStart);
    }
    return distanceSqd;
}

/*  Given a cubic, determine if all four points are in a line.
    Return true if the inner points is close to a line connecting the outermost points.

    Find the outermost point by looking for the largest difference in X or Y.
    Given the indices of the outermost points, and that outer_1 is greater than outer_2,
    this table shows the index of the smaller of the remaining points:

                      outer_2
                  0    1    2    3
      outer_1     ----------------
         0     |  -    2    1    1
         1     |  -    -    0    0
         2     |  -    -    -    0
         3     |  -    -    -    -

    If outer_1 == 0 and outer_2 == 1, the smaller of the remaining indices (2 and 3) is 2.

    This table can be collapsed to: (1 + (2 >> outer_2)) >> outer_1

    Given three indices (outer_1 outer_2 mid_1) from 0..3, the remaining index is:

               mid_2 == (outer_1 ^ outer_2 ^ mid_1)
 */
static bool cubic_in_line(const SkPoint cubic[4]) {
    SkScalar ptMax = -1;
    int outer1 = 0;
    int outer2 = 0;
    for (int index = 0; index < 3; ++index) {
        for (int inner = index + 1; inner < 4; ++inner) {
            const SkVector testDiff = cubic[inner] - cubic[index];
            const SkScalar testMax = std::max(SkScalarAbs(testDiff.fX), SkScalarAbs(testDiff.fY));
            if (ptMax < testMax) {
                outer1 = index;
                outer2 = inner;
                ptMax = testMax;
            }
        }
    }
    // No pair was found only when every difference is NaN; such points are not in a line.
    // Otherwise 0 <= outer1 < outer2 <= 3, and mid1 and mid2 are the other two indices.
    if (outer1 >= outer2) {
        return false;
    }
    const int mid1 = (1 + (2 >> outer2)) >> outer1;
    const int mid2 = outer1 ^ outer2 ^ mid1;
    SkASSERT(((1 << outer1) | (1 << outer2) | (1 << mid1) | (1 << mid2)) == 0x0f);
    const SkScalar lineSlop = ptMax * ptMax * 0.00001f;  // this multiplier is pulled out of the air
    if (!(pt_to_line(cubic[mid1], cubic[outer1], cubic[outer2]) <= lineSlop)) {
        return false;
    }
    const bool inLine = pt_to_line(cubic[mid2], cubic[outer1], cubic[outer2]) <= lineSlop;
    return inLine;
}

/* Given quad, see if all there points are in a line.
   Return true if the inside point is close to a line connecting the outermost points.

   Find the outermost point by looking for the largest difference in X or Y.
   Since the XOR of the indices is 3  (0 ^ 1 ^ 2)
   the missing index equals: outer_1 ^ outer_2 ^ 3
 */
static bool quad_in_line(const SkPoint quad[3]) {
    SkScalar ptMax = -1;
    int outer1 = 0;
    int outer2 = 0;
    for (int index = 0; index < 2; ++index) {
        for (int inner = index + 1; inner < 3; ++inner) {
            const SkVector testDiff = quad[inner] - quad[index];
            const SkScalar testMax = std::max(SkScalarAbs(testDiff.fX), SkScalarAbs(testDiff.fY));
            if (ptMax < testMax) {
                outer1 = index;
                outer2 = inner;
                ptMax = testMax;
            }
        }
    }
    // No pair was found only when every difference is NaN; the original then read quad[3].
    // Otherwise 0 <= outer1 < outer2 <= 2, and mid is the remaining index.
    if (outer1 >= outer2) {
        return false;
    }
    const int mid = outer1 ^ outer2 ^ 3;
    const float kCurvatureSlop = 0.000005f;  // this multiplier is pulled out of the air
    const SkScalar lineSlop =  ptMax * ptMax * kCurvatureSlop;
    const bool inLine = pt_to_line(quad[mid], quad[outer1], quad[outer2]) <= lineSlop;
    return inLine;
}

static bool conic_in_line(const SkConic& conic) {
    const bool inLine = quad_in_line(conic.fPts);
    return inLine;
}

SkPathStroker::ReductionType SkPathStroker::CheckCubicLinear(const SkPoint cubic[4],
        SkPoint reduction[3], const SkPoint** tangentPtPtr) {
    const bool degenerateAB = degenerate_vector(cubic[1] - cubic[0]);
    const bool degenerateBC = degenerate_vector(cubic[2] - cubic[1]);
    const bool degenerateCD = degenerate_vector(cubic[3] - cubic[2]);
    if (degenerateAB && degenerateBC && degenerateCD) {
        return kPoint_ReductionType;
    }
    const int degenerateCount = static_cast<int>(degenerateAB) +
                                static_cast<int>(degenerateBC) +
                                static_cast<int>(degenerateCD);
    if (degenerateCount == 2) {
        return kLine_ReductionType;
    }
    if (!cubic_in_line(cubic)) {
        *tangentPtPtr = degenerateAB ? &cubic[2] : &cubic[1];
        return kQuad_ReductionType;
    }
    SkScalar tValues[3] = {0, 0, 0};
    const int count = SkFindCubicMaxCurvature(cubic, tValues);
    int rCount = 0;
    // Now loop over the t-values, and reject any that evaluate to either end-point
    for (int index = 0; index < count; ++index) {
        const SkScalar t = tValues[index];
        if (0 >= t || t >= 1) {
            continue;
        }
        SkEvalCubicAt(cubic, t, &reduction[rCount], nullptr, nullptr);
        if (reduction[rCount] != cubic[0] && reduction[rCount] != cubic[3]) {
            ++rCount;
        }
    }
    if (rCount == 0) {
        return kLine_ReductionType;
    }
    static_assert(kQuad_ReductionType + 1 == kDegenerate_ReductionType, "enum_out_of_whack");
    static_assert(kQuad_ReductionType + 2 == kDegenerate2_ReductionType, "enum_out_of_whack");
    static_assert(kQuad_ReductionType + 3 == kDegenerate3_ReductionType, "enum_out_of_whack");

    const ReductionType reductionType = static_cast<ReductionType>(kQuad_ReductionType + rCount);
    return reductionType;
}

SkPathStroker::ReductionType SkPathStroker::CheckConicLinear(const SkConic& conic,
        SkPoint* reduction) {
    const bool degenerateAB = degenerate_vector(conic.fPts[1] - conic.fPts[0]);
    const bool degenerateBC = degenerate_vector(conic.fPts[2] - conic.fPts[1]);
    if (degenerateAB && degenerateBC) {
        return kPoint_ReductionType;
    }
    if (degenerateAB || degenerateBC) {
        return kLine_ReductionType;
    }
    if (!conic_in_line(conic)) {
        return kQuad_ReductionType;
    }
    // SkFindConicMaxCurvature would be a better solution, once we know how to
    // implement it. Quad curvature is a reasonable substitute
    const SkScalar t = SkFindQuadMaxCurvature(conic.fPts);
    if (0 == t || SkIsNaN(t)) {
        return kLine_ReductionType;
    }
    conic.evalAt(t, reduction, nullptr);
    return kDegenerate_ReductionType;
}

SkPathStroker::ReductionType SkPathStroker::CheckQuadLinear(const SkPoint quad[3],
        SkPoint* reduction) {
    const bool degenerateAB = degenerate_vector(quad[1] - quad[0]);
    const bool degenerateBC = degenerate_vector(quad[2] - quad[1]);
    if (degenerateAB && degenerateBC) {
        return kPoint_ReductionType;
    }
    if (degenerateAB || degenerateBC) {
        return kLine_ReductionType;
    }
    if (!quad_in_line(quad)) {
        return kQuad_ReductionType;
    }
    const SkScalar t = SkFindQuadMaxCurvature(quad);
    if (0 == t || 1 == t) {
        return kLine_ReductionType;
    }
    *reduction = SkEvalQuadAt(quad, t);
    return kDegenerate_ReductionType;
}

void SkPathStroker::conicTo(const SkPoint& pt1, const SkPoint& pt2, SkScalar weight) {
    const SkConic conic(fPrevPt, pt1, pt2, weight);
    SkPoint reduction{0, 0};
    const ReductionType reductionType = CheckConicLinear(conic, &reduction);
    if (kPoint_ReductionType == reductionType) {
        /* If the stroke consists of a moveTo followed by a degenerate curve, treat it
            as if it were followed by a zero-length line. Lines without length
            can have square and round end caps. */
        lineTo(pt2);
        return;
    }
    if (kLine_ReductionType == reductionType) {
        lineTo(pt2);
        return;
    }
    if (kDegenerate_ReductionType == reductionType) {
        lineTo(reduction);
        const SkStrokerPriv::JoinProc saveJoiner = fJoiner;
        fJoiner = SkStrokerPriv::JoinFactory(SkPaint::kRound_Join);
        lineTo(pt2);
        fJoiner = saveJoiner;
        return;
    }
    SkASSERT(kQuad_ReductionType == reductionType);
    SkVector normalAB{0, 0};
    SkVector unitAB{0, 0};
    SkVector normalBC{0, 0};
    SkVector unitBC{0, 0};
    if (!preJoinTo(pt1, &normalAB, &unitAB, false)) {
        lineTo(pt2);
        return;
    }
    SkQuadConstruct quadPts;
    init(kOuter_StrokeType, &quadPts, 0, 1);
    static_cast<void>(conicStroke(conic, &quadPts));
    init(kInner_StrokeType, &quadPts, 0, 1);
    static_cast<void>(conicStroke(conic, &quadPts));
    setConicEndNormal(conic, normalAB, unitAB, &normalBC, &unitBC);
    postJoinTo(pt2, normalBC, unitBC);
}

void SkPathStroker::quadTo(const SkPoint& pt1, const SkPoint& pt2) {
    const SkPoint quad[3] = { fPrevPt, pt1, pt2 };
    SkPoint reduction{0, 0};
    const ReductionType reductionType = CheckQuadLinear(quad, &reduction);
    if (kPoint_ReductionType == reductionType) {
        /* If the stroke consists of a moveTo followed by a degenerate curve, treat it
            as if it were followed by a zero-length line. Lines without length
            can have square and round end caps. */
        lineTo(pt2);
        return;
    }
    if (kLine_ReductionType == reductionType) {
        lineTo(pt2);
        return;
    }
    if (kDegenerate_ReductionType == reductionType) {
        lineTo(reduction);
        const SkStrokerPriv::JoinProc saveJoiner = fJoiner;
        fJoiner = SkStrokerPriv::JoinFactory(SkPaint::kRound_Join);
        lineTo(pt2);
        fJoiner = saveJoiner;
        return;
    }
    SkASSERT(kQuad_ReductionType == reductionType);
    SkVector normalAB{0, 0};
    SkVector unitAB{0, 0};
    SkVector normalBC{0, 0};
    SkVector unitBC{0, 0};
    if (!preJoinTo(pt1, &normalAB, &unitAB, false)) {
        lineTo(pt2);
        return;
    }
    SkQuadConstruct quadPts;
    init(kOuter_StrokeType, &quadPts, 0, 1);
    static_cast<void>(quadStroke(quad, &quadPts));
    init(kInner_StrokeType, &quadPts, 0, 1);
    static_cast<void>(quadStroke(quad, &quadPts));
    setQuadEndNormal(quad, normalAB, unitAB, &normalBC, &unitBC);

    postJoinTo(pt2, normalBC, unitBC);
}

// Given a point on the curve and its derivative, scale the derivative by the radius, and
// compute the perpendicular point and its tangent.
void SkPathStroker::setRayPts(const SkPoint& tPt, SkVector* dxy, SkPoint* onPt,
        SkVector* tangent) const {
    if (!(*dxy).setLength(fRadius)) {
        (*dxy).set(fRadius, 0);
    }
    const SkScalar axisFlip = SkIntToScalar(fStrokeType);  // go opposite ways for outer, inner
    (*onPt).fX = tPt.fX + axisFlip * (*dxy).fY;
    (*onPt).fY = tPt.fY - axisFlip * (*dxy).fX;
    if (tangent) {
        *tangent = *dxy;
    }
}

// Given a conic and t, return the point on curve, its perpendicular, and the perpendicular tangent.
// Returns false if the perpendicular could not be computed (because the derivative collapsed to 0)
void SkPathStroker::conicPerpRay(const SkConic& conic, SkScalar t, SkPoint* tPt, SkPoint* onPt,
        SkVector* tangent) const {
    SkVector dxy{0, 0};
    conic.evalAt(t, tPt, &dxy);
    if (dxy.isZero()) {
        dxy = conic.fPts[2] - conic.fPts[0];
    }
    setRayPts(*tPt, &dxy, onPt, tangent);
}

// Given a conic and a t range, find the start and end if they haven't been found already.
void SkPathStroker::conicQuadEnds(const SkConic& conic, SkQuadConstruct* quadPts) const {
    if (!(*quadPts).fStartSet) {
        SkPoint conicStartPt{0, 0};
        conicPerpRay(conic, (*quadPts).fStartT, &conicStartPt, &(*quadPts).fQuad[0],
                     &(*quadPts).fTangentStart);
        (*quadPts).fStartSet = true;
    }
    if (!(*quadPts).fEndSet) {
        SkPoint conicEndPt{0, 0};
        conicPerpRay(conic, (*quadPts).fEndT, &conicEndPt, &(*quadPts).fQuad[2],
                     &(*quadPts).fTangentEnd);
        (*quadPts).fEndSet = true;
    }
}


// Given a cubic and t, return the point on curve, its perpendicular, and the perpendicular tangent.
void SkPathStroker::cubicPerpRay(const SkPoint cubic[4], SkScalar t, SkPoint* tPt, SkPoint* onPt,
        SkVector* tangent) const {
    SkVector dxy{0, 0};
    SkPoint chopped[7] = {};
    SkEvalCubicAt(cubic, t, tPt, &dxy, nullptr);
    if (dxy.isZero()) {
        const SkPoint* cPts = cubic;
        if (SkScalarNearlyZero(t)) {
            dxy = cubic[2] - cubic[0];
        } else if (SkScalarNearlyZero(1 - t)) {
            dxy = cubic[3] - cubic[1];
        } else {
            // If the cubic inflection falls on the cusp, subdivide the cubic
            // to find the tangent at that point.
            SkChopCubicAt(cubic, chopped, t);
            dxy = chopped[3] - chopped[2];
            if (dxy.isZero()) {
                dxy = chopped[3] - chopped[1];
                cPts = chopped;
            }
        }
        if (dxy.isZero()) {
            dxy = cPts[3] - cPts[0];
        }
    }
    setRayPts(*tPt, &dxy, onPt, tangent);
}

// Given a cubic and a t range, find the start and end if they haven't been found already.
void SkPathStroker::cubicQuadEnds(const SkPoint cubic[4], SkQuadConstruct* quadPts) {
    if (!(*quadPts).fStartSet) {
        SkPoint cubicStartPt{0, 0};
        cubicPerpRay(cubic, (*quadPts).fStartT, &cubicStartPt, &(*quadPts).fQuad[0],
                     &(*quadPts).fTangentStart);
        (*quadPts).fStartSet = true;
    }
    if (!(*quadPts).fEndSet) {
        SkPoint cubicEndPt{0, 0};
        cubicPerpRay(cubic, (*quadPts).fEndT, &cubicEndPt, &(*quadPts).fQuad[2],
                     &(*quadPts).fTangentEnd);
        (*quadPts).fEndSet = true;
    }
}

void SkPathStroker::cubicQuadMid(const SkPoint cubic[4], const SkQuadConstruct* quadPts,
        SkPoint* mid) const {
    SkPoint cubicMidPt{0, 0};
    cubicPerpRay(cubic, (*quadPts).fMidT, &cubicMidPt, mid, nullptr);
}

// Given a quad and t, return the point on curve, its perpendicular, and the perpendicular tangent.
void SkPathStroker::quadPerpRay(const SkPoint quad[3], SkScalar t, SkPoint* tPt, SkPoint* onPt,
        SkVector* tangent) const {
    SkVector dxy{0, 0};
    SkEvalQuadAt(quad, t, tPt, &dxy);
    if (dxy.isZero()) {
        dxy = quad[2] - quad[0];
    }
    setRayPts(*tPt, &dxy, onPt, tangent);
}

// Find the intersection of the stroke tangents to construct a stroke quad.
// Return whether the stroke is a degenerate (a line), a quad, or must be split.
// Optionally compute the quad's control point.
SkPathStroker::ResultType SkPathStroker::intersectRay(SkQuadConstruct* quadPts,
        IntersectRayType intersectRayType) const {
    const SkPoint& start = (*quadPts).fQuad[0];
    const SkPoint& end = (*quadPts).fQuad[2];
    const SkVector aLen = (*quadPts).fTangentStart;
    const SkVector bLen = (*quadPts).fTangentEnd;
    /* Slopes match when denom goes to zero:
                      axLen / ayLen ==                   bxLen / byLen
    (ayLen * byLen) * axLen / ayLen == (ayLen * byLen) * bxLen / byLen
             byLen  * axLen         ==  ayLen          * bxLen
             byLen  * axLen         -   ayLen          * bxLen         ( == denom )
     */
    const SkScalar denom = aLen.cross(bLen);
    if (denom == 0 || !SkIsFinite(denom)) {
        (*quadPts).fOppositeTangents = aLen.dot(bLen) < 0;
        return kDegenerate_ResultType;
    }
    (*quadPts).fOppositeTangents = false;
    const SkVector ab0 = start - end;
    SkScalar numerA = bLen.cross(ab0);
    const SkScalar numerB = aLen.cross(ab0);
    if ((numerA >= 0) == (numerB >= 0)) { // if the control point is outside the quad ends
        // if the perpendicular distances from the quad points to the opposite tangent line
        // are small, a straight line is good enough
        const SkScalar dist1 = pt_to_tangent_line(start, end, (*quadPts).fTangentEnd);
        const SkScalar dist2 = pt_to_tangent_line(end, start, (*quadPts).fTangentStart);
        if (std::max(dist1, dist2) <= fInvResScaleSquared) {
            return kDegenerate_ResultType;
        }
        return kSplit_ResultType;
    }
    // check to see if the denominator is teeny relative to the numerator
    // if the offset by one will be lost, the ratio is too large
    numerA /= denom;
    const bool validDivide = numerA > numerA - 1;
    if (validDivide) {
        if (kCtrlPt_RayType == intersectRayType) {
            // the intersection of the tangents need not be on the tangent segment
            // so 0 <= numerA <= 1 is not necessarily true
            (*quadPts).fQuad[1] = start + (*quadPts).fTangentStart * numerA;
        }
        return kQuad_ResultType;
    }
    (*quadPts).fOppositeTangents = aLen.dot(bLen) < 0;
    // if the lines are parallel, straight line is good enough
    return kDegenerate_ResultType;
}

// Given a cubic and a t-range, determine if the stroke can be described by a quadratic.
SkPathStroker::ResultType SkPathStroker::tangentsMeet(const SkPoint cubic[4],
        SkQuadConstruct* quadPts) {
    cubicQuadEnds(cubic, quadPts);
    const ResultType resultType = intersectRay(quadPts, kResultType_RayType);
    return resultType;
}

// Intersect the line with the quad and return the t values on the quad where the line crosses.
static int intersect_quad_ray(const SkPoint line[2], const SkPoint quad[3], SkScalar roots[2]) {
    const SkVector vec = line[1] - line[0];
    SkScalar r[3] = {0, 0, 0};
    for (int n = 0; n < 3; ++n) {
        r[n] = vec.cross(quad[n] - line[0]);
    }
    SkScalar A = r[2];
    SkScalar B = r[1];
    const SkScalar C = r[0];
    A += C - 2 * B;  // A = a - 2*b + c
    B -= C;  // B = -(b - c)
    const int count = SkFindUnitQuadRoots(A, 2 * B, C, roots);
    return count;
}

// Return true if the point is close to the bounds of the quad. This is used as a quick reject.
bool SkPathStroker::ptInQuadBounds(const SkPoint quad[3], const SkPoint& pt) const {
    const SkScalar xMin = std::min({quad[0].fX, quad[1].fX, quad[2].fX});
    if (pt.fX + fInvResScale < xMin) {
        return false;
    }
    const SkScalar xMax = std::max({quad[0].fX, quad[1].fX, quad[2].fX});
    if (pt.fX - fInvResScale > xMax) {
        return false;
    }
    const SkScalar yMin = std::min({quad[0].fY, quad[1].fY, quad[2].fY});
    if (pt.fY + fInvResScale < yMin) {
        return false;
    }
    const SkScalar yMax = std::max({quad[0].fY, quad[1].fY, quad[2].fY});
    if (pt.fY - fInvResScale > yMax) {
        return false;
    }
    return true;
}

static bool points_within_dist(const SkPoint& nearPt, const SkPoint& farPt, SkScalar limit) {
    const bool within = SkPointPriv::DistanceToSqd(nearPt, farPt) <= limit * limit;
    return within;
}

static bool sharp_angle(const SkPoint quad[3]) {
    SkVector smaller = quad[1] - quad[0];
    SkVector larger = quad[1] - quad[2];
    const SkScalar smallerLen = SkPointPriv::LengthSqd(smaller);
    SkScalar largerLen = SkPointPriv::LengthSqd(larger);
    if (smallerLen > largerLen) {
        std::swap(smaller, larger);
        largerLen = smallerLen;
    }
    if (!smaller.setLength(largerLen)) {
        return false;
    }
    const SkScalar dot = smaller.dot(larger);
    const bool sharp = dot > 0;
    return sharp;
}

SkPathStroker::ResultType SkPathStroker::strokeCloseEnough(const SkPoint stroke[3],
        const SkPoint ray[2], SkQuadConstruct* quadPts) const {
    const SkPoint strokeMid = SkEvalQuadAt(stroke, SK_ScalarHalf);
    // measure the distance from the curve to the quad-stroke midpoint, compare to radius
    if (points_within_dist(ray[0], strokeMid, fInvResScale)) {  // if the difference is small
        if (sharp_angle((*quadPts).fQuad)) {
            return kSplit_ResultType;
        }
        return kQuad_ResultType;
    }
    // measure the distance to quad's bounds (quick reject)
        // an alternative : look for point in triangle
    if (!ptInQuadBounds(stroke, ray[0])) {  // if far, subdivide
        return kSplit_ResultType;
    }
    // measure the curve ray distance to the quad-stroke
    SkScalar roots[2] = {0, 0};
    const int rootCount = intersect_quad_ray(ray, stroke, roots);
    if (rootCount != 1) {
        return kSplit_ResultType;
    }
    const SkPoint quadPt = SkEvalQuadAt(stroke, roots[0]);
    const SkScalar error = fInvResScale * (SK_Scalar1 - SkScalarAbs(roots[0] - 0.5f) * 2);
    if (points_within_dist(ray[0], quadPt, error)) {  // if the difference is small, we're done
        if (sharp_angle((*quadPts).fQuad)) {
            return kSplit_ResultType;
        }
        return kQuad_ResultType;
    }
    // otherwise, subdivide
    return kSplit_ResultType;
}

SkPathStroker::ResultType SkPathStroker::compareQuadCubic(const SkPoint cubic[4],
        SkQuadConstruct* quadPts) {
    // get the quadratic approximation of the stroke
    cubicQuadEnds(cubic, quadPts);
    const ResultType resultType = intersectRay(quadPts, kCtrlPt_RayType);
    if (resultType != kQuad_ResultType) {
        return resultType;
    }
    // project a ray from the curve to the stroke
    SkPoint ray[2] = {};  // points near midpoint on quad, midpoint on cubic
    cubicPerpRay(cubic, (*quadPts).fMidT, &ray[1], &ray[0], nullptr);
    const ResultType closeEnough = strokeCloseEnough((*quadPts).fQuad, ray, quadPts);
    return closeEnough;
}

SkPathStroker::ResultType SkPathStroker::compareQuadConic(const SkConic& conic,
        SkQuadConstruct* quadPts) const {
    // get the quadratic approximation of the stroke
    conicQuadEnds(conic, quadPts);
    const ResultType resultType = intersectRay(quadPts, kCtrlPt_RayType);
    if (resultType != kQuad_ResultType) {
        return resultType;
    }
    // project a ray from the curve to the stroke
    SkPoint ray[2] = {};  // points near midpoint on quad, midpoint on conic
    conicPerpRay(conic, (*quadPts).fMidT, &ray[1], &ray[0], nullptr);
    const ResultType closeEnough = strokeCloseEnough((*quadPts).fQuad, ray, quadPts);
    return closeEnough;
}

SkPathStroker::ResultType SkPathStroker::compareQuadQuad(const SkPoint quad[3],
        SkQuadConstruct* quadPts) {
    // get the quadratic approximation of the stroke
    if (!(*quadPts).fStartSet) {
        SkPoint quadStartPt{0, 0};
        quadPerpRay(quad, (*quadPts).fStartT, &quadStartPt, &(*quadPts).fQuad[0],
                    &(*quadPts).fTangentStart);
        (*quadPts).fStartSet = true;
    }
    if (!(*quadPts).fEndSet) {
        SkPoint quadEndPt{0, 0};
        quadPerpRay(quad, (*quadPts).fEndT, &quadEndPt, &(*quadPts).fQuad[2],
                    &(*quadPts).fTangentEnd);
        (*quadPts).fEndSet = true;
    }
    const ResultType resultType = intersectRay(quadPts, kCtrlPt_RayType);
    if (resultType != kQuad_ResultType) {
        return resultType;
    }
    // project a ray from the curve to the stroke
    SkPoint ray[2] = {};
    quadPerpRay(quad, (*quadPts).fMidT, &ray[1], &ray[0], nullptr);
    const ResultType closeEnough = strokeCloseEnough((*quadPts).fQuad, ray, quadPts);
    return closeEnough;
}

void SkPathStroker::addDegenerateLine(const SkQuadConstruct* quadPts) {
    strokeSink().lineTo((*quadPts).fQuad[2]);
}

bool SkPathStroker::cubicMidOnLine(const SkPoint cubic[4], const SkQuadConstruct* quadPts) const {
    SkPoint strokeMid{0, 0};
    cubicQuadMid(cubic, quadPts, &strokeMid);
    const SkScalar dist = pt_to_line(strokeMid, (*quadPts).fQuad[0], (*quadPts).fQuad[2]);
    const bool onLine = dist < fInvResScaleSquared;
    return onLine;
}

bool SkPathStroker::cubicStroke(const SkPoint cubic[4], SkQuadConstruct* quadPts) {
    if (!fFoundTangents) {
        const ResultType resultType = tangentsMeet(cubic, quadPts);
        if (kQuad_ResultType != resultType) {
            if ((kDegenerate_ResultType == resultType
                    || points_within_dist((*quadPts).fQuad[0], (*quadPts).fQuad[2],
                    fInvResScale)) && cubicMidOnLine(cubic, quadPts)) {
                addDegenerateLine(quadPts);
                return true;
            }
        } else {
            fFoundTangents = true;
        }
    }
    if (fFoundTangents) {
        const ResultType resultType = compareQuadCubic(cubic, quadPts);
        if (kQuad_ResultType == resultType) {
            const SkPoint* stroke = (*quadPts).fQuad;
            strokeSink().quadTo(stroke[1], stroke[2]);
            return true;
        }
        if (kDegenerate_ResultType == resultType) {
            if (!(*quadPts).fOppositeTangents) {
              addDegenerateLine(quadPts);
              return true;
            }
        }
    }
    if (!(*quadPts).fQuad[2].isFinite()) {
        return false;  // just abort if projected quad isn't representable
    }
    fRecursionDepth += 1;
    if (fRecursionDepth > kRecursiveLimits[static_cast<int>(fFoundTangents)]) {
        // If we stop making progress, just emit a line and move on
        addDegenerateLine(quadPts);
        return true;
    }
    SkQuadConstruct half;
    if (!half.initWithStart(quadPts)) {
        addDegenerateLine(quadPts);
        --fRecursionDepth;
        return true;
    }
    if (!cubicStroke(cubic, &half)) {
        return false;
    }
    if (!half.initWithEnd(quadPts)) {
        addDegenerateLine(quadPts);
        --fRecursionDepth;
        return true;
    }
    if (!cubicStroke(cubic, &half)) {
        return false;
    }
    --fRecursionDepth;
    return true;
}

bool SkPathStroker::conicStroke(const SkConic& conic, SkQuadConstruct* quadPts) {
    const ResultType resultType = compareQuadConic(conic, quadPts);
    if (kQuad_ResultType == resultType) {
        const SkPoint* stroke = (*quadPts).fQuad;
        strokeSink().quadTo(stroke[1], stroke[2]);
        return true;
    }
    if (kDegenerate_ResultType == resultType) {
        addDegenerateLine(quadPts);
        return true;
    }
    fRecursionDepth += 1;
    if (fRecursionDepth > kRecursiveLimits[kConic_RecursiveLimit]) {
        // If we stop making progress, just emit a line and move on
        addDegenerateLine(quadPts);
        return true;
    }
    SkQuadConstruct half;
    static_cast<void>(half.initWithStart(quadPts));
    if (!conicStroke(conic, &half)) {
        return false;
    }
    static_cast<void>(half.initWithEnd(quadPts));
    if (!conicStroke(conic, &half)) {
        return false;
    }
    --fRecursionDepth;
    return true;
}

bool SkPathStroker::quadStroke(const SkPoint quad[3], SkQuadConstruct* quadPts) {
    const ResultType resultType = compareQuadQuad(quad, quadPts);
    if (kQuad_ResultType == resultType) {
        const SkPoint* stroke = (*quadPts).fQuad;
        strokeSink().quadTo(stroke[1], stroke[2]);
        return true;
    }
    if (kDegenerate_ResultType == resultType) {
        addDegenerateLine(quadPts);
        return true;
    }
    fRecursionDepth += 1;
    if (fRecursionDepth > kRecursiveLimits[kQuad_RecursiveLimit]) {
        // If we stop making progress, just emit a line and move on
        addDegenerateLine(quadPts);
        return true;
    }
    SkQuadConstruct half;
    static_cast<void>(half.initWithStart(quadPts));
    if (!quadStroke(quad, &half)) {
        return false;
    }
    static_cast<void>(half.initWithEnd(quadPts));
    if (!quadStroke(quad, &half)) {
        return false;
    }
    --fRecursionDepth;
    return true;
}

void SkPathStroker::cubicTo(const SkPoint& pt1, const SkPoint& pt2,
                            const SkPoint& pt3) {
    const SkPoint cubic[4] = { fPrevPt, pt1, pt2, pt3 };
    SkPoint reduction[3] = {};
    const SkPoint* tangentPt = nullptr;
    const ReductionType reductionType = CheckCubicLinear(cubic, reduction, &tangentPt);
    if (kPoint_ReductionType == reductionType) {
        /* If the stroke consists of a moveTo followed by a degenerate curve, treat it
            as if it were followed by a zero-length line. Lines without length
            can have square and round end caps. */
        lineTo(pt3);
        return;
    }
    if (kLine_ReductionType == reductionType) {
        lineTo(pt3);
        return;
    }
    if (kDegenerate_ReductionType <= reductionType && kDegenerate3_ReductionType >= reductionType) {
        lineTo(reduction[0]);
        const SkStrokerPriv::JoinProc saveJoiner = fJoiner;
        fJoiner = SkStrokerPriv::JoinFactory(SkPaint::kRound_Join);
        if (kDegenerate2_ReductionType <= reductionType) {
            lineTo(reduction[1]);
        }
        if (kDegenerate3_ReductionType == reductionType) {
            lineTo(reduction[2]);
        }
        lineTo(pt3);
        fJoiner = saveJoiner;
        return;
    }
    SkASSERT(kQuad_ReductionType == reductionType);
    SkASSERT(tangentPt);
    SkVector normalAB{0, 0};
    SkVector unitAB{0, 0};
    SkVector normalCD{0, 0};
    SkVector unitCD{0, 0};
    if (!preJoinTo(*tangentPt, &normalAB, &unitAB, false)) {
        lineTo(pt3);
        return;
    }
    SkScalar tValues[2] = {0, 0};
    const int count = SkFindCubicInflections(cubic, tValues);
    SkScalar lastT = 0;
    for (int index = 0; index <= count; ++index) {
        const SkScalar nextT = index < count ? tValues[index] : 1;
        SkQuadConstruct quadPts;
        init(kOuter_StrokeType, &quadPts, lastT, nextT);
        static_cast<void>(cubicStroke(cubic, &quadPts));
        init(kInner_StrokeType, &quadPts, lastT, nextT);
        static_cast<void>(cubicStroke(cubic, &quadPts));
        lastT = nextT;
    }
    const SkScalar cusp = SkFindCubicCusp(cubic);
    if (cusp > 0) {
        SkPoint cuspLoc{0, 0};
        SkEvalCubicAt(cubic, cusp, &cuspLoc, nullptr, nullptr);
        fCusper.addCircle(cuspLoc.fX, cuspLoc.fY, fRadius);
    }
    // emit the join even if one stroke succeeded but the last one failed
    // this avoids reversing an inner stroke with a partial path followed by another moveto
    setCubicEndNormal(cubic, normalAB, unitAB, &normalCD, &unitCD);

    postJoinTo(pt3, normalCD, unitCD);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

SkStroke::SkStroke()
    : fWidth(SK_Scalar1)
    , fMiterLimit(SkPaintDefaults_MiterLimit)
    , fResScale(1)
    , fCap(static_cast<uint8_t>(SkPaint::kDefault_Cap))
    , fJoin(static_cast<uint8_t>(SkPaint::kDefault_Join))
    , fDoFill(false) {}

SkStroke::SkStroke(const SkPaint& p)
    : fWidth(p.getStrokeWidth())
    , fMiterLimit(p.getStrokeMiter())
    , fResScale(1)
    , fCap(static_cast<uint8_t>(p.getStrokeCap()))
    , fJoin(static_cast<uint8_t>(p.getStrokeJoin()))
    , fDoFill(p.getStyle() == SkPaint::kStrokeAndFill_Style) {}

SkStroke::SkStroke(const SkPaint& p, SkScalar width)
    : fWidth(width)
    , fMiterLimit(p.getStrokeMiter())
    , fResScale(1)
    , fCap(static_cast<uint8_t>(p.getStrokeCap()))
    , fJoin(static_cast<uint8_t>(p.getStrokeJoin()))
    , fDoFill(p.getStyle() == SkPaint::kStrokeAndFill_Style) {}

void SkStroke::setWidth(SkScalar width) {
    SkASSERT(width >= 0);
    fWidth = width;
}

void SkStroke::setMiterLimit(SkScalar miterLimit) {
    SkASSERT(miterLimit >= 0);
    fMiterLimit = miterLimit;
}

void SkStroke::setCap(SkPaint::Cap cap) {
    SkASSERT(static_cast<unsigned>(cap) < SkPaint::kCapCount);
    fCap = SkToU8(cap);
}

void SkStroke::setJoin(SkPaint::Join join) {
    SkASSERT(static_cast<unsigned>(join) < SkPaint::kJoinCount);
    fJoin = SkToU8(join);
}

///////////////////////////////////////////////////////////////////////////////

void SkStroke::strokePath(const SkPath& src, SkPathBuilder* dst) const {
    SkASSERT(dst);

    const float radius = fWidth / 2.f;

    if (radius <= 0) {
        return;
    }

    const std::optional<SkPathRaw> raw = SkPathPriv::Raw(src, SkResolveConvexity::kNo);
    if (!raw.has_value()) {
        return;
    }

    // If src is really a rect, call our specialty strokeRect() method
    {
        SkRect rect = SkRect::MakeEmpty();
        bool isClosed = false;
        SkPathDirection dir = SkPathDirection::kCW;
        if (src.isRect(&rect, &isClosed, &dir) && isClosed) {
            strokeRect(rect, dst, dir);
            // our answer should preserve the inverseness of the src
            if (src.isInverseFillType()) {
                SkASSERT(!(*dst).isInverseFillType());
                (*dst).toggleInverseFillType();
            }
            return;
        }
    }

    // We can always ignore centers for stroke and fill convex line-only paths
    // TODO: remove the line-only restriction
    const bool ignoreCenter = fDoFill &&
                              (src.getSegmentMasks() == SkPath::kLine_SegmentMask) &&
                              src.isLastContourClosed() && src.isConvex();

    SkPathStroker   stroker(src, radius, fMiterLimit, getCap(), getJoin(),
                            fResScale, ignoreCenter);

    SkPath::Iter iter(src, false);
    SkPathVerb   lastSegment = SkPathVerb::kMove;
    for (std::optional<SkPath::IterRec> rec = iter.next(); rec.has_value(); rec = iter.next()) {
        const SkPath::IterRec& record = rec.value();
        const SkSpan<const SkPoint> pts = record.fPoints;
        switch (record.fVerb) {
            case SkPathVerb::kMove:
                stroker.moveTo(pts[0]);
                break;
            case SkPathVerb::kLine:
                stroker.lineTo(pts[1], &iter);
                lastSegment = SkPathVerb::kLine;
                break;
            case SkPathVerb::kQuad:
                stroker.quadTo(pts[1], pts[2]);
                lastSegment = SkPathVerb::kQuad;
                break;
            case SkPathVerb::kConic:
                stroker.conicTo(pts[1], pts[2], record.conicWeight());
                lastSegment = SkPathVerb::kConic;
                break;
            case SkPathVerb::kCubic:
                stroker.cubicTo(pts[1], pts[2], pts[3]);
                lastSegment = SkPathVerb::kCubic;
                break;
            case SkPathVerb::kClose: {
                bool zeroLength = false;
                if (SkPaint::kButt_Cap != getCap()) {
                    if (stroker.hasOnlyMoveTo()) {
                        /* If the stroke consists of a moveTo followed by a close, treat it
                           as if it were followed by a zero-length line. Lines without length
                           can have square and round end caps. */
                        stroker.lineTo(stroker.moveToPt());
                        zeroLength = true;
                    } else if (stroker.isCurrentContourEmpty()) {
                        /* If the stroke consists of a moveTo followed by one or more
                           zero-length verbs, then followed by a close, treat is as if it were
                           followed by a zero-length line. Lines without length can have
                           square & round end caps. */
                        zeroLength = true;
                    }
                }
                if (zeroLength) {
                    lastSegment = SkPathVerb::kLine;
                } else {
                    stroker.close(lastSegment == SkPathVerb::kLine);
                }
            } break;
        }
    }
    stroker.done(dst, lastSegment == SkPathVerb::kLine);

    if (fDoFill && !ignoreCenter) {
        const SkPathFirstDirection d = SkPathPriv::ComputeFirstDirection(raw.value());
        if (d == SkPathFirstDirection::kCCW) {
            (*dst).privateReverseAddPath(src);
        } else {
            (*dst).addPath(src);
        }
    }
    //  Otherwise the stroke is not marked convex: even a 2-point src can produce a non-convex
    //  stroke (for example a short round-capped line), so convexity is computed on demand.

    // our answer should preserve the inverseness of the src
    if (src.isInverseFillType()) {
        SkASSERT(!(*dst).isInverseFillType());
        (*dst).toggleInverseFillType();
    }
}

static SkPathDirection reverse_direction(SkPathDirection dir) {
    const SkPathDirection opposite =
            dir == SkPathDirection::kCW ? SkPathDirection::kCCW : SkPathDirection::kCW;
    return opposite;
}

static void addBevel(SkPathBuilder* path, const SkRect& r, const SkRect& outer,
                     SkPathDirection dir) {
    SkPoint pts[8] = {};

    if (SkPathDirection::kCW == dir) {
        pts[0].set(r.fLeft, outer.fTop);
        pts[1].set(r.fRight, outer.fTop);
        pts[2].set(outer.fRight, r.fTop);
        pts[3].set(outer.fRight, r.fBottom);
        pts[4].set(r.fRight, outer.fBottom);
        pts[5].set(r.fLeft, outer.fBottom);
        pts[6].set(outer.fLeft, r.fBottom);
        pts[7].set(outer.fLeft, r.fTop);
    } else {
        pts[7].set(r.fLeft, outer.fTop);
        pts[6].set(r.fRight, outer.fTop);
        pts[5].set(outer.fRight, r.fTop);
        pts[4].set(outer.fRight, r.fBottom);
        pts[3].set(r.fRight, outer.fBottom);
        pts[2].set(r.fLeft, outer.fBottom);
        pts[1].set(outer.fLeft, r.fBottom);
        pts[0].set(outer.fLeft, r.fTop);
    }
    (*path).addPolygon(pts, true);
}

void SkStroke::strokeRect(const SkRect& origRect, SkPathBuilder* dst,
                          SkPathDirection dir) const {
    SkASSERT(dst != nullptr);
    (*dst).reset();

    const float radius = fWidth / 2.f;
    if (radius <= 0) {
        return;
    }

    SkScalar rw = origRect.width();
    SkScalar rh = origRect.height();
    if ((rw < 0) != (rh < 0)) {
        dir = reverse_direction(dir);
    }
    SkRect rect(origRect);
    rect.sort();
    // reassign these, now that we know they'll be >= 0
    rw = rect.width();
    rh = rect.height();

    SkRect r(rect);
    r.outset(radius, radius);

    SkPaint::Join join = static_cast<SkPaint::Join>(fJoin);
    if (SkPaint::kMiter_Join == join && fMiterLimit < SK_ScalarSqrt2) {
        join = SkPaint::kBevel_Join;
    }

    switch (join) {
        case SkPaint::kMiter_Join:
            (*dst).addRect(r, dir);
            break;
        case SkPaint::kBevel_Join:
            addBevel(dst, rect, r, dir);
            break;
        case SkPaint::kRound_Join:
            (*dst).addRRect(SkRRect::MakeRectXY(r, radius, radius), dir);
            break;
        default:
            break;
    }

    if (fWidth < std::min(rw, rh) && !fDoFill) {
        r = rect;
        r.inset(radius, radius);
        (*dst).addRect(r, reverse_direction(dir));
    }
}
