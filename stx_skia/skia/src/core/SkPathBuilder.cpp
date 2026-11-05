/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPathBuilder.h"

#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkRRect.h"
#include "include/core/SkTypes.h"
#include "include/private/SkAssert.h" // IWYU pragma: keep
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkPathRef.h"
#include "include/private/SkSafe32.h"
#include "include/private/SkTArray.h"
#include "include/private/SkTo.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkMatrixPriv.h"
#include "src/core/SkPathData.h"
#include "src/core/SkPathEnums.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPathRawShapes.h"
#include "src/core/SkVx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

namespace {

// A borrowed input may point into a builder array. Preserve it before an
// append can move the array; disjoint callers keep the direct copy path.
template <typename T>
bool spans_overlap(SkSpan<const T> source, SkSpan<const T> stored) {
    if (source.empty()) {
        return false;
    }
    if (stored.empty()) {
        return false;
    }
    const std::less<const T*> less{};
    const T* sourceEnd = &source.back() + 1;
    const T* storedEnd = &stored.back() + 1;
    const bool overlaps = less(source.data(), storedEnd) && less(stored.data(), sourceEnd);
    return overlaps;
}

// Appends the cubic in pts[0..3] as 2^level halved cubics; pts[0] is the current point.
void subdivide_cubic_to(SkPathBuilder* path, const SkPoint pts[4], int level = 2) {
    --level;
    if (level >= 0) {
        SkPoint tmp[7] = {};

        SkChopCubicAtHalf(pts, tmp);
        subdivide_cubic_to(path, &tmp[0], level);
        subdivide_cubic_to(path, &tmp[3], level);
    } else {
        (*path).cubicTo(pts[1], pts[2], pts[3]);
    }
}

bool has_trailing_move(SkSpan<const SkPathVerb> vbs) {
    const bool trailing = vbs.size() > 0 && vbs.back() == SkPathVerb::kMove;
    return trailing;
}

bool scalar_is_integer(SkScalar scalar) {
    const bool integer = scalar == SkScalarFloorToScalar(scalar);
    return integer;
}

}  // namespace

SkPathBuilder::SkPathBuilder() {
    reset();
}

SkPathBuilder::SkPathBuilder(const SkPathBuilder&) = default;
SkPathBuilder& SkPathBuilder::operator=(const SkPathBuilder&) = default;
SkPathBuilder::SkPathBuilder(SkPathBuilder&&) = default;
SkPathBuilder& SkPathBuilder::operator=(SkPathBuilder&&) = default;
SkPathBuilder::~SkPathBuilder() = default;

SkPathBuilder::SkPathBuilder(SkPathFillType ft) {
    reset();
    fFillType = ft;
}

SkPathBuilder::SkPathBuilder(const SkPath& src) {
    *this = src;
}

SkPathBuilder& SkPathBuilder::operator=(const SkPath& src) {
    reset();
    setFillType(src.getFillType());
    setIsVolatile(src.isVolatile());

    if (src.isEmpty()) {
        return *this;
    }

    const SkPathData& data = *src.fPathData;
    addRaw(data.raw(src.getFillType(), SkResolveConvexity::kYes), Reserve::kExact);

    // These are not part of SkPathRaw, so we set them separately
    fLastMoveIndex = SkPathPriv::FindLastMoveToIndex(fVerbs, fPts.size());
    SkASSERT(fLastMoveIndex < fPts.size());
    fType = data.fType;
    fIsA  = data.fIsA;

    return *this;
}

SkPathBuilder& SkPathBuilder::reset() {
    fPts.clear();
    fVerbs.clear();
    fConicWeights.clear();
    fFillType = SkPathFillType::kDefault;
    fIsVolatile = false;

    // these are internal state

    fSegmentMask = 0;
    fLastMoveIndex = -1;        // illegal

    fType      = SkPathIsAType::kGeneral;
    fConvexity = SkPathConvexity::kUnknown;

    return *this;
}

bool SkPathBuilder::operator==(const SkPathBuilder& o) const {
    // quick-accept
    if (this == &o) {
        return true;
    }
    // quick-reject
    if (fSegmentMask != o.fSegmentMask || fFillType != o.fFillType) {
        return false;
    }
    // deep compare
    const bool equal = fVerbs == o.fVerbs && fPts == o.fPts && fConicWeights == o.fConicWeights;
    return equal;
}

void SkPathBuilder::incReserve(int extraPtCount, int extraVbCount, int extraCnCount) {
    fPts.reserve(Sk32_sat_add(fPts.size(), extraPtCount));
    fVerbs.reserve(Sk32_sat_add(fVerbs.size(), extraVbCount));
    fConicWeights.reserve(Sk32_sat_add(fConicWeights.size(), extraCnCount));
}

SkPathBuilder::GrownStorage SkPathBuilder::growForVerbsInPath(const SkPath& path) {
    fSegmentMask |= path.getSegmentMasks();

    const SkSpan<const SkPathVerb> verbs = path.verbs();
    if (!verbs.empty()) {
         // TODO(borenet): If the current builder is empty or JustMoves, we can use the type of the
         // path. If the path is empty, we can keep the current type.
        fType = SkPathIsAType::kGeneral;
        fVerbs.push_back_n(verbs.size(), verbs.data());
    }

    SkPoint* pts = nullptr;
    const int numPts = path.countPoints();
    if (numPts != 0) {
        pts = fPts.push_back_n(numPts);
    }

    SkScalar* weights = nullptr;
    const int numConics = SkToInt(path.conicWeights().size());
    if (numConics != 0) {
        weights = fConicWeights.push_back_n(numConics);
    }

    const GrownStorage storage{pts, weights};
    return storage;
}

/*
 *  Some old behavior in SkPath -- should we keep it?
 *
 *  After each edit (i.e. adding a verb)
        setConvexityType(SkPathConvexity::kUnknown);
        setFirstDirection(SkPathPriv::kUnknown_FirstDirection);
 */

SkPathBuilder& SkPathBuilder::moveTo(SkPoint pt) {
    if (!fVerbs.empty() && fVerbs.back() == SkPathVerb::kMove) {
        fPts.back() = pt;

        SkASSERT(fType != SkPathIsAType::kOval && fType != SkPathIsAType::kRRect);
        SkASSERT(fConvexity == SkPathConvexity::kUnknown);
        SkASSERT(fLastMoveIndex == SkToInt(fPts.size()) - 1);
    } else {
        fLastMoveIndex = SkToInt(fPts.size());

        fPts.push_back(pt);
        fVerbs.push_back(SkPathVerb::kMove);

        if (fType == SkPathIsAType::kOval || fType == SkPathIsAType::kRRect) {
            fType = SkPathIsAType::kGeneral;
        }
        fConvexity = SkPathConvexity::kUnknown;
    }

    return *this;
}

SkPathBuilder& SkPathBuilder::lineTo(SkPoint pt) {
    ensureMove();

    fPts.push_back(pt);
    fVerbs.push_back(SkPathVerb::kLine);

    fSegmentMask |= kLine_SkPathSegmentMask;
    return *this;
}

SkPathBuilder& SkPathBuilder::quadTo(SkPoint pt1, SkPoint pt2) {
    ensureMove();

    SkPoint* p = fPts.push_back_n(2);
    p[0] = pt1;
    p[1] = pt2;
    fVerbs.push_back(SkPathVerb::kQuad);

    fSegmentMask |= kQuad_SkPathSegmentMask;
    return *this;
}

// A non-positive weight is a line to pt2, a unit weight is a quad, and a non-finite weight is
// two lines through pt1.
SkPathBuilder& SkPathBuilder::conicTo(SkPoint pt1, SkPoint pt2, SkScalar w) {
    ensureMove();

    if (w <= 0) {
        lineTo(pt2);
        return *this;
    }
    SkPoint* p = fPts.push_back_n(2);
    p[0] = pt1;
    p[1] = pt2;
    if (w == 1) {
        fVerbs.push_back(SkPathVerb::kQuad);
        fSegmentMask |= kQuad_SkPathSegmentMask;
    } else if (SkIsFinite(w)) {
        fVerbs.push_back(SkPathVerb::kConic);
        fConicWeights.push_back(w);
        fSegmentMask |= kConic_SkPathSegmentMask;
    } else {
        fVerbs.push_back(SkPathVerb::kLine);
        fVerbs.push_back(SkPathVerb::kLine);
        fSegmentMask |= kLine_SkPathSegmentMask;
    }

    return *this;
}

SkPathBuilder& SkPathBuilder::cubicTo(SkPoint pt1, SkPoint pt2, SkPoint pt3) {
    ensureMove();

    SkPoint* p = fPts.push_back_n(3);
    p[0] = pt1;
    p[1] = pt2;
    p[2] = pt3;
    fVerbs.push_back(SkPathVerb::kCubic);

    fSegmentMask |= kCubic_SkPathSegmentMask;
    return *this;
}

SkPathBuilder& SkPathBuilder::close() {
    // If this is a 2nd 'close', we just ignore it
    if (!fVerbs.empty() && fVerbs.back() != SkPathVerb::kClose) {
        ensureMove();
        fVerbs.push_back(SkPathVerb::kClose);
    }
    return *this;
}

///////////////////////////////////////////////////////////////////////////////////////////

SkPathBuilder& SkPathBuilder::rMoveTo(SkVector pt) {
    SkPoint lastPt = {0,0}; // in case we're empty
    if (!fPts.empty()) {
        // A builder with points always has a move; fall back to the last point otherwise.
        if (fVerbs.back() == SkPathVerb::kClose && fLastMoveIndex >= 0 &&
            fLastMoveIndex < fPts.size()) {
            lastPt = fPts[fLastMoveIndex];
        } else {
            lastPt = fPts.back();
        }
    }
    moveTo(lastPt + pt);
    return *this;
}

SkPathBuilder& SkPathBuilder::rLineTo(SkVector p1) {
    ensureMove();
    const SkPoint base = fPts.back();
    lineTo(base + p1);
    return *this;
}

SkPathBuilder& SkPathBuilder::rQuadTo(SkVector p1, SkVector p2) {
    ensureMove();
    const SkPoint base = fPts.back();
    quadTo(base + p1, base + p2);
    return *this;
}

SkPathBuilder& SkPathBuilder::rConicTo(SkVector p1, SkVector p2, SkScalar w) {
    ensureMove();
    const SkPoint base = fPts.back();
    conicTo(base + p1, base + p2, w);
    return *this;
}

SkPathBuilder& SkPathBuilder::rCubicTo(SkVector p1, SkVector p2, SkVector p3) {
    ensureMove();
    const SkPoint base = fPts.back();
    cubicTo(base + p1, base + p2, base + p3);
    return *this;
}

///////////////////////////////////////////////////////////////////////////////////////////

SkPath SkPathBuilder::snapshot(const SkMatrix* mx) const {
    const SkMatrix& matrix = mx ? *mx : SkMatrix::I();

    sk_sp<SkPathData> pdata{};
    const std::optional<SkPathRaw> raw = SkPathPriv::Raw(*this, SkResolveConvexity::kNo);
    if (raw.has_value()) {
        pdata = SkPathData::MakeTransform(raw.value(), matrix);
    }
    if (pdata && fType != SkPathIsAType::kGeneral) {
        (*pdata).setupIsA(fType, fIsA.fDirection, fIsA.fStartIndex);
    }
    const SkPath path = SkPath::MakeNullCheck(std::move(pdata), fFillType, fIsVolatile);
    return path;
}

SkPath SkPathBuilder::detach(const SkMatrix* mx) {
    const SkPath path = snapshot(mx);
    reset();
    return path;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

// Returns null when an oval or rounded rectangle has non-finite bounds.
sk_sp<SkPathData> SkPathBuilder::snapshotData() const {
    if (fVerbs.size() <= 1) {
        sk_sp<SkPathData> empty = SkPathData::Empty();
        return empty;
    }

    switch (fType) {
        case SkPathIsAType::kGeneral:
            break;
        case SkPathIsAType::kOval: {
            const std::optional<SkRect> r = SkRect::Bounds(fPts);
            if (!r.has_value()) {
                return nullptr;
            }
            sk_sp<SkPathData> oval = SkPathData::Oval(r.value(), fIsA.fDirection,
                                                      fIsA.fStartIndex);
            return oval;
        }
        case SkPathIsAType::kRRect: {
            const std::optional<SkRect> r = SkRect::Bounds(fPts);
            if (!r.has_value()) {
                return nullptr;
            }
            const SkRRect rrect = SkPathPriv::DeduceRRectFromContour(r.value(), fPts, fVerbs);
            sk_sp<SkPathData> rounded = SkPathData::RRect(rrect, fIsA.fDirection,
                                                          fIsA.fStartIndex);
            return rounded;
        }
    }
    SkASSERT(fType == SkPathIsAType::kGeneral);

    sk_sp<SkPathData> general = SkPathData::Make(fPts, fVerbs, fConicWeights);
    return general;
}

sk_sp<SkPathData> SkPathBuilder::detachData() {
    sk_sp<SkPathData> data = snapshotData();
    reset();
    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static bool arc_is_lone_point(const SkRect& oval, SkScalar startAngle, SkScalar sweepAngle,
                              SkPoint* pt) {
    if (0 == sweepAngle && (0 == startAngle || SkIntToScalar(360) == startAngle)) {
        // Chrome uses this path to move into and out of ovals. If not
        // treated as a special case the moves can distort the oval's
        // bounding box (and break the circle special case).
        (*pt).set(oval.fRight, oval.centerY());
        return true;
    }
    if (0 == oval.width() && 0 == oval.height()) {
        // Chrome will sometimes create 0 radius round rects. Having degenerate
        // quad segments in the path prevents the path from being recognized as
        // a rect.
        // TODO: optimizing the case where only one of width or height is zero
        // should also be considered. This case, however, doesn't seem to be
        // as common as the single point case.
        (*pt).set(oval.fRight, oval.fTop);
        return true;
    }
    return false;
}

// Return the unit vectors pointing at the start/stop points for the given start/sweep angles
//
static void angles_to_unit_vectors(SkScalar startAngle, SkScalar sweepAngle,
                                   SkVector* startV, SkVector* stopV, SkPathDirection* dir) {
    const SkScalar startRad = SkDegreesToRadians(startAngle);
    SkScalar stopRad = SkDegreesToRadians(startAngle + sweepAngle);

    (*startV).fY = SkScalarSinSnapToZero(startRad);
    (*startV).fX = SkScalarCosSnapToZero(startRad);
    (*stopV).fY = SkScalarSinSnapToZero(stopRad);
    (*stopV).fX = SkScalarCosSnapToZero(stopRad);

    /*  If the sweep angle is nearly (but less than) 360, then due to precision
     loss in radians-conversion and/or sin/cos, we may end up with coincident
     vectors, which will fool SkBuildQuadArc into doing nothing (bad) instead
     of drawing a nearly complete circle (good).
     e.g. canvas.drawArc(0, 359.99, ...)
     -vs- canvas.drawArc(0, 359.9, ...)
     We try to detect this edge case, and tweak the stop vector
     */
    if (*startV == *stopV) {
        const SkScalar sw = SkScalarAbs(sweepAngle);
        if (sw < SkIntToScalar(360) && sw > SkIntToScalar(359)) {
            // make a guess at a tiny angle (in radians) to tweak by
            const SkScalar deltaRad = SkScalarCopySign(SK_Scalar1/512, sweepAngle);
            // not sure how much will be enough, so we use a loop; stopRad is at most
            // a few multiples of pi, so each step changes it
            do {
                stopRad -= deltaRad;
                (*stopV).fY = SkScalarSinSnapToZero(stopRad);
                (*stopV).fX = SkScalarCosSnapToZero(stopRad);
            } while (*startV == *stopV);
        }
    }
    *dir = sweepAngle > 0 ? SkPathDirection::kCW : SkPathDirection::kCCW;
}

/**
 *  If this returns 0, then the caller should just line-to the singlePt, else it should
 *  ignore singlePt and append the specified number of conics.
 */
static int build_arc_conics(const SkRect& oval, const SkVector& start, const SkVector& stop,
                            SkPathDirection dir, SkConic conics[SkConic::kMaxConicsForArc],
                            SkPoint* singlePt) {
    SkMatrix matrix{};

    matrix.setScale(oval.width() / 2.f, oval.height() / 2.f);
    matrix.postTranslate(oval.centerX(), oval.centerY());

    const int count = SkConic::BuildUnitArc(start, stop, dir, &matrix, conics);
    if (0 == count) {
        *singlePt = matrix.mapPoint(stop);
    }
    return count;
}

static bool nearly_equal(const SkPoint& a, const SkPoint& b) {
    const bool nearly = SkScalarNearlyEqual(a.fX, b.fX) && SkScalarNearlyEqual(a.fY, b.fY);
    return nearly;
}

// Adds a move-to to 'pt' if forceMoveTo is true. Otherwise a lineTo unless we're sufficiently
// close to 'pt' currently. This prevents spurious lineTos when adding a series of contiguous
// arcs from the same oval. Without forceMoveTo the builder has a current point.
static void add_arc_point(SkPathBuilder& builder, const SkPoint& pt, bool forceMoveTo) {
    if (forceMoveTo) {
        builder.moveTo(pt);
        return;
    }
    const SkPoint last = builder.points().back();
    if (!nearly_equal(last, pt)) {
        builder.lineTo(pt);
    }
}

SkPathBuilder& SkPathBuilder::arcTo(const SkRect& oval, SkScalar startAngle, SkScalar sweepAngle,
                                    bool forceMoveTo) {
    if (oval.width() < 0 || oval.height() < 0) {
        return *this;
    }

    startAngle = SkScalarMod(startAngle, 360.0f);

    if (fVerbs.empty()) {
        forceMoveTo = true;
    }

    SkPoint lonePt{0, 0};
    if (arc_is_lone_point(oval, startAngle, sweepAngle, &lonePt)) {
        if (forceMoveTo) {
            moveTo(lonePt);
        } else {
            lineTo(lonePt);
        }
        return *this;
    }

    SkVector startV{0, 0};
    SkVector stopV{0, 0};
    SkPathDirection dir = SkPathDirection::kCW;
    angles_to_unit_vectors(startAngle, sweepAngle, &startV, &stopV, &dir);

    SkPoint singlePt{0, 0};

    // At this point, we know that the arc is not a lone point, but startV == stopV
    // indicates that the sweepAngle is too small such that angles_to_unit_vectors
    // cannot handle it.
    if (startV == stopV) {
        const SkScalar endAngle = SkDegreesToRadians(startAngle + sweepAngle);
        const SkScalar radiusX = oval.width() / 2;
        const SkScalar radiusY = oval.height() / 2;
        // We do not use SkScalar[Sin|Cos]SnapToZero here. When sin(startAngle) is 0 and sweepAngle
        // is very small and radius is huge, the expected behavior here is to draw a line. But
        // calling SkScalarSinSnapToZero will make sin(endAngle) be 0 which will then draw a dot.
        singlePt.set(oval.centerX() + radiusX * SkScalarCos(endAngle),
                     oval.centerY() + radiusY * SkScalarSin(endAngle));
        add_arc_point(*this, singlePt, forceMoveTo);
        return *this;
    }

    SkConic conics[SkConic::kMaxConicsForArc];
    const int count = build_arc_conics(oval, startV, stopV, dir, conics, &singlePt);
    if (count) {
        incReserve(count * 2 + 1);
        const SkPoint& pt = conics[0].fPts[0];
        add_arc_point(*this, pt, forceMoveTo);
        for (int i = 0; i < count; ++i) {
            conicTo(conics[i].fPts[1], conics[i].fPts[2], conics[i].fW);
        }
    } else {
        add_arc_point(*this, singlePt, forceMoveTo);
    }
    return *this;
}

SkPathBuilder& SkPathBuilder::rArcTo(SkPoint r, SkScalar xAxisRotate, ArcSize largeArc,
                                     SkPathDirection sweep, SkVector dxdy) {
    const SkPoint currentPoint = getLastPt().value_or(SkPoint{0, 0});
    arcTo(r, xAxisRotate, largeArc, sweep, currentPoint + dxdy);
    return *this;
}

SkPathBuilder& SkPathBuilder::addArc(const SkRect& oval, SkScalar startAngle, SkScalar sweepAngle) {
    if (oval.isEmpty() || 0 == sweepAngle) {
        return *this;
    }

    const SkScalar kFullCircleAngle = SkIntToScalar(360);

    if (sweepAngle >= kFullCircleAngle || sweepAngle <= -kFullCircleAngle) {
        // We can treat the arc as an oval if it begins at one of our legal starting positions.
        // See SkPath::addOval() docs.
        const SkScalar startOver90 = startAngle / 90.f;
        const SkScalar startOver90I = SkScalarRoundToScalar(startOver90);
        const SkScalar error = startOver90 - startOver90I;
        if (SkScalarNearlyEqual(error, 0)) {
            // Index 1 is at startAngle == 0. startOver90I is finite here, so the remainder
            // lies in (-4, 4) and the start index in [0, 4).
            SkScalar startIndex = std::fmod(startOver90I + 1.f, 4.f);
            startIndex = startIndex < 0 ? startIndex + 4.f : startIndex;
            const SkPathDirection direction = sweepAngle > 0 ? SkPathDirection::kCW
                                                             : SkPathDirection::kCCW;
            addOval(oval, direction, static_cast<unsigned>(startIndex));
            return *this;
        }
    }
    arcTo(oval, startAngle, sweepAngle, true);
    return *this;
}

SkPathBuilder& SkPathBuilder::arcTo(SkPoint p1, SkPoint p2, SkScalar radius) {
    ensureMove();

    if (radius == 0) {
        lineTo(p1);
        return *this;
    }

    // need to know our prev pt so we can construct tangent vectors
    const SkPoint start = fPts.back();

    // need double precision for these calcs.
    const skvx::double2 befored = normalize(skvx::double2{p1.fX - start.fX, p1.fY - start.fY});
    const skvx::double2 afterd = normalize(skvx::double2{p2.fX - p1.fX, p2.fY - p1.fY});
    const double cosh = dot(befored, afterd);
    const double sinh = cross(befored, afterd);

    // If the previous point equals the first point, befored will be denormalized.
    // If the two points equal, afterd will be denormalized.
    // If the second point equals the first point, sinh will be zero.
    // In all these cases, we cannot construct an arc, so we construct a line to the first point.
    if (!isfinite(befored) || !isfinite(afterd) || SkScalarNearlyZero(SkDoubleToScalar(sinh))) {
        lineTo(p1);
        return *this;
    }

    // safe to convert back to floats now
    const SkScalar dist = SkScalarAbs(SkDoubleToScalar(radius * (1 - cosh) / sinh));
    const SkScalar xx = p1.fX - dist * befored[0];
    const SkScalar yy = p1.fY - dist * befored[1];

    SkVector after = SkVector::Make(afterd[0], afterd[1]);
    after.setLength(dist);
    lineTo(xx, yy);
    const SkScalar weight = SkScalarSqrt(SkDoubleToScalar(SK_ScalarHalf + cosh * 0.5));
    conicTo(p1, p1 + after, weight);
    return *this;
}

// This converts the SVG arc to conics.
// Partly adapted from Niko's code in kdelibs/kdecore/svgicons.
// Then transcribed from webkit/chrome's SVGPathNormalizer::decomposeArcToCubic()
// See also SVG implementation notes:
// http://www.w3.org/TR/SVG/implnote.html#ArcConversionEndpointToCenter
// Note that arcSweep bool value is flipped from the original implementation.
SkPathBuilder& SkPathBuilder::arcTo(SkPoint rad, SkScalar angle, SkPathBuilder::ArcSize arcLarge,
                                    SkPathDirection arcSweep, SkPoint endPt) {
    ensureMove();

    const SkPoint srcPts[2] = { fPts.back(), endPt };

    // If rx = 0 or ry = 0 then this arc is treated as a straight line segment (a "lineto")
    // joining the endpoints.
    // http://www.w3.org/TR/SVG/implnote.html#ArcOutOfRangeParameters
    if (!rad.fX || !rad.fY) {
        lineTo(endPt);
        return *this;
    }
    // If the current point and target point for the arc are identical, it should be treated as a
    // zero length path. This ensures continuity in animations.
    if (srcPts[0] == srcPts[1]) {
        lineTo(endPt);
        return *this;
    }
    SkScalar rx = SkScalarAbs(rad.fX);
    SkScalar ry = SkScalarAbs(rad.fY);
    SkVector midPointDistance = srcPts[0] - srcPts[1];
    midPointDistance *= 0.5f;

    SkMatrix pointTransform{};
    pointTransform.setRotate(-angle);

    const SkPoint transformedMidPoint = pointTransform.mapPoint(midPointDistance);
    const SkScalar squareRx = rx * rx;
    const SkScalar squareRy = ry * ry;
    const SkScalar squareX = transformedMidPoint.fX * transformedMidPoint.fX;
    const SkScalar squareY = transformedMidPoint.fY * transformedMidPoint.fY;

    // Check if the radii are big enough to draw the arc, scale radii if not.
    // http://www.w3.org/TR/SVG/implnote.html#ArcCorrectionOutOfRangeRadii
    SkScalar radiiScale = squareX / squareRx + squareY / squareRy;
    if (radiiScale > 1) {
        radiiScale = SkScalarSqrt(radiiScale);
        rx *= radiiScale;
        ry *= radiiScale;
    }

    pointTransform.setScale(1 / rx, 1 / ry);
    pointTransform.preRotate(-angle);

    SkPoint unitPts[2] = {{0, 0}, {0, 0}};
    pointTransform.mapPoints(unitPts, srcPts);
    SkVector delta = unitPts[1] - unitPts[0];

    const SkScalar d = delta.fX * delta.fX + delta.fY * delta.fY;
    const SkScalar scaleFactorSquared = std::max(1 / d - 0.25f, 0.f);

    SkScalar scaleFactor = SkScalarSqrt(scaleFactorSquared);
    const bool largeArc = arcLarge == SkPathBuilder::kLarge_ArcSize;
    if ((arcSweep == SkPathDirection::kCCW) != largeArc) {  // flipped from the original implementation
        scaleFactor = -scaleFactor;
    }
    delta.scale(scaleFactor);
    SkPoint centerPoint = unitPts[0] + unitPts[1];
    centerPoint *= 0.5f;
    centerPoint.offset(-delta.fY, delta.fX);
    unitPts[0] -= centerPoint;
    unitPts[1] -= centerPoint;
    const SkScalar theta1 = SkScalarATan2(unitPts[0].fY, unitPts[0].fX);
    const SkScalar theta2 = SkScalarATan2(unitPts[1].fY, unitPts[1].fX);
    SkScalar thetaArc = theta2 - theta1;
    if (thetaArc < 0 && (arcSweep == SkPathDirection::kCW)) {  // arcSweep flipped from the original implementation
        thetaArc += SK_ScalarPI * 2;
    } else if (thetaArc > 0 && (arcSweep != SkPathDirection::kCW)) {  // arcSweep flipped from the original implementation
        thetaArc -= SK_ScalarPI * 2;
    }

    // Very tiny angles cause our subsequent math to go wonky (skbug.com/40040578)
    // so we do a quick check here. The precise tolerance amount is just made up.
    // PI/million happens to fix the bug in 9272, but a larger value is probably
    // ok too.
    if (SkScalarAbs(thetaArc) < (SK_ScalarPI / (1000 * 1000))) {
        lineTo(endPt);
        return *this;
    }

    pointTransform.setRotate(angle);
    pointTransform.preScale(rx, ry);

    // the arc may be slightly bigger than 1/4 circle, so allow up to 1/3rd. A NaN arc
    // saturates the count here and is rejected by the finite-tangent check below.
    const int segments = SkScalarCeilToInt(SkScalarAbs(thetaArc / (2 * SK_ScalarPI / 3)));
    const SkScalar thetaWidth = thetaArc / segments;
    const SkScalar t = SkScalarTan(0.5f * thetaWidth);
    if (!SkIsFinite(t)) {
        return *this;
    }
    SkScalar startTheta = theta1;
    const SkScalar w = SkScalarSqrt(SK_ScalarHalf + SkScalarCos(thetaWidth) * SK_ScalarHalf);
    const bool expectIntegers = SkScalarNearlyZero(SK_ScalarPI/2 - SkScalarAbs(thetaWidth)) &&
        scalar_is_integer(rx) && scalar_is_integer(ry) &&
        scalar_is_integer(endPt.fX) && scalar_is_integer(endPt.fY);

    for (int i = 0; i < segments; ++i) {
        const SkScalar endTheta    = startTheta + thetaWidth;
        const SkScalar sinEndTheta = SkScalarSinSnapToZero(endTheta);
        const SkScalar cosEndTheta = SkScalarCosSnapToZero(endTheta);

        unitPts[1].set(cosEndTheta, sinEndTheta);
        unitPts[1] += centerPoint;
        unitPts[0] = unitPts[1];
        unitPts[0].offset(t * sinEndTheta, -t * cosEndTheta);
        SkPoint mapped[2] = {{0, 0}, {0, 0}};
        pointTransform.mapPoints(mapped, unitPts);
        /*
        Computing the arc width introduces rounding errors that cause arcs to start
        outside their marks. A round rect may lose convexity as a result. If the input
        values are on integers, place the conic on integers as well.
         */
        if (expectIntegers) {
            for (SkPoint& point : mapped) {
                point.fX = SkScalarRoundToScalar(point.fX);
                point.fY = SkScalarRoundToScalar(point.fY);
            }
        }
        conicTo(mapped[0], mapped[1], w);
        startTheta = endTheta;
    }

    // The final point should match the input point (by definition); replace it to
    // ensure that rounding errors in the above math don't cause any problems.
    fPts.back() = endPt;
    return *this;
}

///////////////////////////////////////////////////////////////////////////////////////////

SkPathIter SkPathBuilder::iter() const & {
    const SkPathIter iterator(fPts, fVerbs, fConicWeights);
    return iterator;
}

SkPathBuilder& SkPathBuilder::addRaw(const SkPathRaw& raw, Reserve reserve) {
    // The raw view can borrow this builder's arrays. Capture each aliased
    // range before reserve or the replayed verbs can invalidate its inputs.
    SkPathRaw input = raw;
    PointsArray stablePoints{};
    VerbsArray stableVerbs{};
    ConicWeightsArray stableConics{};
    if (spans_overlap(raw.points(), SkSpan<const SkPoint>(fPts))) {
        stablePoints = PointsArray(raw.points());
        input.fPoints = stablePoints;
    }
    if (spans_overlap(raw.verbs(), SkSpan<const SkPathVerb>(fVerbs))) {
        stableVerbs = VerbsArray(raw.verbs());
        input.fVerbs = stableVerbs;
    }
    if (spans_overlap(raw.conics(), SkSpan<const float>(fConicWeights))) {
        stableConics = ConicWeightsArray(raw.conics());
        input.fConics = stableConics;
    }

    if (reserve == Reserve::kGrow) {
        incReserve(input.points().size(), input.verbs().size(), input.conics().size());
    } else {
        fPts         .reserve_exact(Sk32_sat_add(fPts.size()         , input.points().size()));
        fVerbs       .reserve_exact(Sk32_sat_add(fVerbs.size()       , input.verbs().size()));
        fConicWeights.reserve_exact(Sk32_sat_add(fConicWeights.size(), input.conics().size()));
    }

    SkPathIter iter = input.iter();
    for (std::optional<SkPathIter::Rec> rec = iter.next(); rec.has_value(); rec = iter.next()) {
        const SkSpan<const SkPoint> pts = rec.value().fPoints;
        switch (rec.value().fVerb) {
            case SkPathVerb::kMove:  moveTo( pts[0]); break;
            case SkPathVerb::kLine:  lineTo( pts[1]); break;
            case SkPathVerb::kQuad:  quadTo( pts[1], pts[2]); break;
            case SkPathVerb::kConic: conicTo(pts[1], pts[2], rec.value().fConicWeight); break;
            case SkPathVerb::kCubic: cubicTo(pts[1], pts[2], pts[3]); break;
            case SkPathVerb::kClose: close(); break;
        }
    }

    // if the iterator 'trimmed' off a trialing move, we restore it here
    if (has_trailing_move(input.verbs()) && !has_trailing_move(verbs())) {
        moveTo(input.points().back());
    }

    return *this;
}

SkPathBuilder& SkPathBuilder::addRect(const SkRect& rect, SkPathDirection dir, unsigned index) {
    const bool wasEmpty = (fSegmentMask == 0);

    addRaw(SkPathRawShapes::Rect(rect, dir, index), Reserve::kGrow);

    if (wasEmpty) {
        // now we're a rect
        fConvexity = SkPathDirection_ToConvexity(dir);
    }
    return *this;
}

SkPathBuilder& SkPathBuilder::addOval(const SkRect& oval, SkPathDirection dir, unsigned index) {
    const bool wasEmpty = (fSegmentMask == 0);

    addRaw(SkPathRawShapes::Oval(oval, dir, index), Reserve::kGrow);

    if (wasEmpty) {
        fType            = SkPathIsAType::kOval;
        fIsA.fDirection  = dir;
        fIsA.fStartIndex = index % 4;
        fConvexity = SkPathDirection_ToConvexity(dir);
    }

    return *this;
}

SkPathBuilder& SkPathBuilder::addRRect(const SkRRect& rrect, SkPathDirection dir, unsigned index) {
    const SkRect& bounds = rrect.getBounds();

    const SkPathPriv::SimplifiedRRect simplified = SkPathPriv::SimplifyRRect(rrect, index);
    switch (simplified.fShape) {
        case SkPathPriv::RRectAsEnum::kRect:
            addRect(bounds, dir, simplified.fStartIndex);
            return *this;
        case SkPathPriv::RRectAsEnum::kOval:
            addOval(bounds, dir, simplified.fStartIndex);
            return *this;
        case SkPathPriv::RRectAsEnum::kRRect:
            break;
    }

    const bool wasEmpty = (fSegmentMask == 0);

    addRaw(SkPathRawShapes::RRect(rrect, dir, index), Reserve::kGrow);

    if (wasEmpty) {
        fType            = SkPathIsAType::kRRect;
        fIsA.fDirection  = dir;
        fIsA.fStartIndex = index % 8;
        fConvexity = SkPathDirection_ToConvexity(dir);
    }
    return *this;
}

SkPathBuilder& SkPathBuilder::addCircle(SkPoint center, float r, SkPathDirection dir) {
    if (r >= 0) {
        addOval(SkRect::MakeLTRB(center.fX - r, center.fY - r, center.fX + r, center.fY + r),
                dir);
    }
    return *this;
}

SkPathBuilder& SkPathBuilder::addPolygon(SkSpan<const SkPoint> pts, bool isClosed) {
    if (pts.empty()) {
        return *this;
    }

    PointsArray stablePoints{};
    if (spans_overlap(pts, SkSpan<const SkPoint>(fPts))) {
        stablePoints = PointsArray(pts);
        pts = stablePoints;
    }

    moveTo(pts[0]);
    polylineTo(pts.last(pts.size() - 1));
    if (isClosed) {
        close();
    }
    return *this;
}

SkPathBuilder& SkPathBuilder::polylineTo(SkSpan<const SkPoint> pts) {
    if (!pts.empty()) {
        PointsArray stablePoints{};
        if (spans_overlap(pts, SkSpan<const SkPoint>(fPts))) {
            stablePoints = PointsArray(pts);
            pts = stablePoints;
        }

        ensureMove();

        const size_t count = pts.size();
        incReserve(count, count, 0);
        SkPoint* newPoints = fPts.push_back_n(count);
        std::memcpy(newPoints, pts.data(), count * sizeof(SkPoint));
        SkPathVerb* newVerbs = fVerbs.push_back_n(count);
        std::fill(newVerbs, newVerbs + count, SkPathVerb::kLine);
        fSegmentMask |= kLine_SkPathSegmentMask;
    }
    return *this;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

SkPathBuilder& SkPathBuilder::offset(SkScalar dx, SkScalar dy) {
    const SkVector delta{dx, dy};
    for (SkPoint& p : fPts) {
        p += delta;
    }
    return *this;
}

SkPathBuilder& SkPathBuilder::addPath(const SkPath& path, SkScalar dx, SkScalar dy,
                                      SkPath::AddPathMode mode) {
    const SkMatrix matrix = SkMatrix::Translate(dx, dy);
    addPath(path, matrix, mode);
    return *this;
}

// src is an immutable path with its own storage; it never aliases this builder's arrays.
SkPathBuilder& SkPathBuilder::addPath(const SkPath& src, const SkMatrix& matrix,
                                      SkPath::AddPathMode mode) {
    if (src.isEmpty()) {
        return *this;
    }

    const bool canReplaceThis = (mode == SkPath::AddPathMode::kAppend_AddPathMode &&
                                 verbs().size() <= 1)
                              || verbs().empty();
    if (canReplaceThis && matrix.isIdentity()) {
        const SkPathFillType fillType = fFillType;
        *this = src;
        fFillType = fillType;
        return *this;
    }

    // We're about to append - clear convexity.
    fConvexity = SkPathConvexity::kUnknown;

    if (SkPath::AddPathMode::kAppend_AddPathMode == mode && !matrix.hasPerspective()) {
        // If the current builder ends with a moveTo and src starts with one (which is always
        // true if non-empty), we must discard the builder moveTo in order to maintain
        // internal consistency after append (no repeating moveTos).
        if (!fVerbs.empty() && fVerbs.back() == SkPathVerb::kMove && !src.isEmpty()) {
            SkASSERT(src.verbs().front() == SkPathVerb::kMove);
            fVerbs.pop_back();
            fPts.pop_back();
            SkASSERT(fVerbs.empty() || fVerbs.back() != SkPathVerb::kMove);
        }

        const int lastMoveToIndex =
            SkPathPriv::FindLastMoveToIndex(src.verbs(), src.points().size());
        SkASSERT(lastMoveToIndex >= 0);
        fLastMoveIndex = lastMoveToIndex + countPoints();

        const GrownStorage grown = growForVerbsInPath(src);
        const size_t count = src.points().size();
        matrix.mapPoints(SkSpan<SkPoint>(grown.fPoints, count), src.points());
        const SkSpan<const float> conics = src.conicWeights();
        if (!conics.empty()) {
            std::memcpy(grown.fWeights, conics.data(), conics.size_bytes());
        }
        return *this;
    }

    const SkMatrixPriv::MapPtsProc mapPtsProc = SkMatrixPriv::GetMapPtsProc(matrix);
    bool firstVerb = true;
    for (const SkPathPriv::VerbRecord record : SkPathPriv::Iterate(src)) {
        const SkPoint* pts = record.fPoints;
        SkPoint mappedPts[3] = {{0, 0}, {0, 0}, {0, 0}};
        switch (record.fVerb) {
            case SkPathVerb::kMove:
                mapPtsProc(matrix, mappedPts, &pts[0], 1);
                if (firstVerb && mode == SkPath::kExtend_AddPathMode && !isEmpty()) {
                    ensureMove(); // In case last contour is closed
                    const std::optional<SkPoint> lastPt = getLastPt();
                    // don't add lineTo if it is degenerate
                    if (!lastPt.has_value() || lastPt.value() != mappedPts[0]) {
                        lineTo(mappedPts[0]);
                    }
                } else {
                    moveTo(mappedPts[0]);
                }
                break;
            case SkPathVerb::kLine:
                mapPtsProc(matrix, mappedPts, &pts[1], 1);
                lineTo(mappedPts[0]);
                break;
            case SkPathVerb::kQuad:
                mapPtsProc(matrix, mappedPts, &pts[1], 2);
                quadTo(mappedPts[0], mappedPts[1]);
                break;
            case SkPathVerb::kConic:
                mapPtsProc(matrix, mappedPts, &pts[1], 2);
                conicTo(mappedPts[0], mappedPts[1], *record.fWeights);
                break;
            case SkPathVerb::kCubic:
                mapPtsProc(matrix, mappedPts, &pts[1], 3);
                cubicTo(mappedPts[0], mappedPts[1], mappedPts[2]);
                break;
            case SkPathVerb::kClose:
                close();
                break;
        }
        firstVerb = false;
    }
    return *this;
}

// Appends the first contour of path in reverse, as segments from the current point; the
// last point of that contour is not repeated. Stops at the contour's moveTo.
SkPathBuilder& SkPathBuilder::privateReversePathTo(const SkPath& path) {
    const SkSpan<const SkPathVerb> verbs = path.verbs();
    if (verbs.empty()) {
        return *this;
    }

    const SkSpan<const SkPoint> points = path.points();
    const SkSpan<const float> conicWeights = path.conicWeights();
    // Index of the first point of the current segment, walking backwards from the last point.
    size_t pointIndex = points.size() - 1;
    size_t weightIndex = conicWeights.size();

    for (size_t verbIndex = verbs.size(); verbIndex > 0; --verbIndex) {
        const SkPathVerb v = verbs[verbIndex - 1];
        if (v == SkPathVerb::kMove) {
            // if the path has multiple contours, stop after reversing the last
            return *this;
        }
        pointIndex -= SkPathPriv::PtsInVerb(v);
        const SkPoint* pts = points.data() + pointIndex;
        switch (v) {
            case SkPathVerb::kMove:
                return *this;
            case SkPathVerb::kLine:
                lineTo(pts[0]);
                break;
            case SkPathVerb::kQuad:
                quadTo(pts[1], pts[0]);
                break;
            case SkPathVerb::kConic:
                --weightIndex;
                conicTo(pts[1], pts[0], conicWeights[weightIndex]);
                break;
            case SkPathVerb::kCubic:
                cubicTo(pts[2], pts[1], pts[0]);
                break;
            case SkPathVerb::kClose:
                break;
        }
    }
    return *this;
}

// Appends every contour of src in reverse order and direction.
SkPathBuilder& SkPathBuilder::privateReverseAddPath(const SkPath& src) {
    const SkSpan<const SkPathVerb> verbs = src.verbs();
    if (verbs.empty()) {
        return *this;
    }

    const SkSpan<const SkPoint> points = src.points();
    const SkSpan<const float> conicWeights = src.conicWeights();
    // One past the last unconsumed point, walking backwards.
    size_t pointEnd = points.size();
    size_t weightIndex = conicWeights.size();

    bool needMove = true;
    bool needClose = false;
    for (size_t verbIndex = verbs.size(); verbIndex > 0; --verbIndex) {
        const SkPathVerb v = verbs[verbIndex - 1];
        const int n = SkPathPriv::PtsInVerb(v);

        if (needMove) {
            --pointEnd;
            const SkPoint start = points[pointEnd];
            moveTo(start.fX, start.fY);
            needMove = false;
        }
        if (v == SkPathVerb::kMove) {
            // The move's point stays unconsumed so the next contour starts there.
            if (needClose) {
                close();
                needClose = false;
            }
            needMove = true;
            continue;
        }
        pointEnd -= n;
        const SkPoint* pts = points.data() + pointEnd;
        switch (v) {
            case SkPathVerb::kMove:
                break;
            case SkPathVerb::kLine:
                lineTo(pts[0]);
                break;
            case SkPathVerb::kQuad:
                quadTo(pts[1], pts[0]);
                break;
            case SkPathVerb::kConic:
                --weightIndex;
                conicTo(pts[1], pts[0], conicWeights[weightIndex]);
                break;
            case SkPathVerb::kCubic:
                cubicTo(pts[2], pts[1], pts[0]);
                break;
            case SkPathVerb::kClose:
                needClose = true;
                break;
        }
    }
    return *this;
}

std::optional<SkPoint> SkPathBuilder::getLastPt() const {
    const size_t count = fPts.size();
    if (count > 0) {
        const SkPoint last = fPts.at(count - 1);
        return last;
    }
    return std::nullopt;
}

#ifdef SK_SUPPORT_LEGACY_PATHBUILDER_SETLASTPT
void SkPathBuilder::setLastPt(SkPoint pt) {
    const size_t count = fPts.size();
    if (count == 0) {
        moveTo(pt);
    } else {
        setPoint(count - 1, pt);
    }
}
#endif

// An index outside the points changes nothing.
void SkPathBuilder::setPoint(size_t index, SkPoint p) {
    if (index < static_cast<size_t>(fPts.size())) {
        fPts[index] = p;
        fType = SkPathIsAType::kGeneral;
    }
}

SkPathBuilder& SkPathBuilder::transform(const SkMatrix& matrix) {
    if (matrix.isIdentity() || isEmpty()) {
        return *this;
    }

    if (matrix.hasPerspective()) {
        SkPath src = detach();

        // remember this from before the detach()
        setFillType(src.getFillType());

        SkPath clipped{};
        if (SkPathPriv::PerspectiveClip(src, matrix, &clipped)) {
            src = std::move(clipped);
        }

        for (const SkPathPriv::VerbRecord record : SkPathPriv::Iterate(src)) {
            const SkPoint* pts = record.fPoints;
            switch (record.fVerb) {
                case SkPathVerb::kMove:
                    moveTo(pts[0]);
                    break;
                case SkPathVerb::kLine:
                    lineTo(pts[1]);
                    break;
                case SkPathVerb::kQuad:
                    // promote the quad to a conic
                    conicTo(pts[1], pts[2], SkConic::TransformW(pts, SK_Scalar1, matrix));
                    break;
                case SkPathVerb::kConic:
                    conicTo(pts[1], pts[2], SkConic::TransformW(pts, record.fWeights[0], matrix));
                    break;
                case SkPathVerb::kCubic:
                    subdivide_cubic_to(this, pts);
                    break;
                case SkPathVerb::kClose:
                    close();
                    break;
            }
        }
    } else {

        // Can we maintain our special case shape?
        if (!matrix.rectStaysRect() || !SkPathPriv::IsAxisAligned(fPts)) {
            fType = SkPathIsAType::kGeneral;
            // lose convexity (just to be numerically safe)
            if (SkPathConvexity_IsConvex(fConvexity)) {
                fConvexity = SkPathConvexity::kUnknown;
            }
        }

        // If we're still a special case, check if we need to reverse our winding
        if (fType == SkPathIsAType::kOval || fType == SkPathIsAType::kRRect) {
            const SkPathPriv::DirAndStart mapped =
                    SkPathPriv::TransformDirAndStart(matrix, fType == SkPathIsAType::kRRect,
                                                     fIsA.fDirection, fIsA.fStartIndex);
            fIsA.fDirection  = mapped.fDirection;
            fIsA.fStartIndex = mapped.fStartIndex;
        }

    }
    matrix.mapPoints(fPts);

    return *this;
}

std::optional<SkRect> SkPathBuilder::computeFiniteBounds() const {
    const std::optional<SkRect> bounds = SkPathPriv::TrimmedBounds(points(), verbs());
    return bounds;
}

std::optional<SkRect> SkPathBuilder::computeTightBounds() const {
    if (!isFinite()) {
        return {};
    }
    const SkRect bounds = SkPathPriv::ComputeTightBounds(points(), verbs(), conicWeights());
    return bounds;
}

bool SkPathBuilder::isFinite() const {
    for (const SkPoint& p : fPts) {
        if (!p.isFinite()) {
            return false;
        }
    }
    return true;
}

bool SkPathBuilder::contains(SkPoint p) const {
    const std::optional<SkPathRaw> raw = SkPathPriv::Raw(*this, SkResolveConvexity::kNo);
    const bool inside = raw.has_value() && SkPathPriv::Contains(raw.value(), p);
    return inside;
}
