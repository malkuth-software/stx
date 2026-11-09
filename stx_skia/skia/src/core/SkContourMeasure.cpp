/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkContourMeasure.h"

#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathTypes.h"
#include "include/private/SkDebug.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkTo.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkPathMeasurePriv.h"
#include "src/core/SkPathPriv.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

static constexpr int kMaxTValue = 0x3FFFFFFF;

constexpr static inline SkScalar tValue2Scalar(int t) {
    SkASSERT(static_cast<unsigned>(t) <= static_cast<unsigned>(kMaxTValue));
    // 1/kMaxTValue can't be represented as a float, but it's close and the limits work fine.
    const SkScalar kMaxTReciprocal = 1.0f / static_cast<SkScalar>(kMaxTValue);
    const SkScalar value = static_cast<SkScalar>(t) * kMaxTReciprocal;
    return value;
}

static_assert(0.0f == tValue2Scalar(         0), "Lower limit should be exact.");
static_assert(1.0f == tValue2Scalar(kMaxTValue), "Upper limit should be exact.");

SkScalar SkContourMeasure::Segment::getScalarT() const {
    const SkScalar t = tValue2Scalar(static_cast<int>(fTValue));
    return t;
}

// Number of points a segment type reads from the measure's point array.
static int segment_point_count(unsigned segType) {
    int count = 0;
    switch (segType) {
        case kLine_SegType:
            count = 2;
            break;
        case kQuad_SegType:
            count = 3;
            break;
        case kCubic_SegType:
        case kConic_SegType:
            count = 4;
            break;
        default:
            count = 0;
            break;
    }
    return count;
}

// True when the points a segment of segType reads from ptIndex lie inside pointCount points.
static bool segment_points_fit(unsigned ptIndex, unsigned segType, int pointCount) {
    const int needed = segment_point_count(segType);
    const bool fits = needed > 0 && pointCount >= needed &&
                      static_cast<size_t>(ptIndex) <= static_cast<size_t>(pointCount - needed);
    return fits;
}

// Appends the part of one verb between startT and stopT to dst. pts holds the verb's points
// in the measure's layout (a conic stores its weight in pts[1].fX).
static void SkContourMeasure_segTo(const SkPoint pts[], unsigned segType,
                                   SkScalar startT, SkScalar stopT, SkPathBuilder* dst) {
    SkASSERT(startT >= 0 && startT <= SK_Scalar1);
    SkASSERT(stopT >= 0 && stopT <= SK_Scalar1);
    SkASSERT(startT <= stopT);

    if (startT == stopT) {
        if (!(*dst).isEmpty()) {
            /* if the dash as a zero-length on segment, add a corresponding zero-length line.
               The stroke code will add end caps to zero length lines as appropriate */
            const std::optional<SkPoint> lastPt = (*dst).getLastPt();
            SkASSERT(lastPt.has_value());
            if (lastPt.has_value()) {
                (*dst).lineTo(lastPt.value());
            }
        }
        return;
    }

    SkPoint tmp0[7] = {};
    SkPoint tmp1[7] = {};

    switch (segType) {
        case kLine_SegType:
            if (SK_Scalar1 == stopT) {
                (*dst).lineTo(pts[1]);
            } else {
                (*dst).lineTo(SkScalarInterp(pts[0].fX, pts[1].fX, stopT),
                              SkScalarInterp(pts[0].fY, pts[1].fY, stopT));
            }
            break;
        case kQuad_SegType:
            if (0 == startT) {
                if (SK_Scalar1 == stopT) {
                    (*dst).quadTo(pts[1], pts[2]);
                } else {
                    SkChopQuadAt(pts, tmp0, stopT);
                    (*dst).quadTo(tmp0[1], tmp0[2]);
                }
            } else {
                SkChopQuadAt(pts, tmp0, startT);
                if (SK_Scalar1 == stopT) {
                    (*dst).quadTo(tmp0[3], tmp0[4]);
                } else {
                    SkChopQuadAt(&tmp0[2], tmp1, (stopT - startT) / (1 - startT));
                    (*dst).quadTo(tmp1[1], tmp1[2]);
                }
            }
            break;
        case kConic_SegType: {
            const SkConic conic(pts[0], pts[2], pts[3], pts[1].fX);

            if (0 == startT) {
                if (SK_Scalar1 == stopT) {
                    (*dst).conicTo(conic.fPts[1], conic.fPts[2], conic.fW);
                } else {
                    SkConic tmp[2];
                    if (conic.chopAt(stopT, tmp)) {
                        (*dst).conicTo(tmp[0].fPts[1], tmp[0].fPts[2], tmp[0].fW);
                    }
                }
            } else {
                if (SK_Scalar1 == stopT) {
                    SkConic tmp[2];
                    if (conic.chopAt(startT, tmp)) {
                        (*dst).conicTo(tmp[1].fPts[1], tmp[1].fPts[2], tmp[1].fW);
                    }
                } else {
                    SkConic tmp;
                    conic.chopAt(startT, stopT, &tmp);
                    (*dst).conicTo(tmp.fPts[1], tmp.fPts[2], tmp.fW);
                }
            }
        } break;
        case kCubic_SegType:
            if (0 == startT) {
                if (SK_Scalar1 == stopT) {
                    (*dst).cubicTo(pts[1], pts[2], pts[3]);
                } else {
                    SkChopCubicAt(pts, tmp0, stopT);
                    (*dst).cubicTo(tmp0[1], tmp0[2], tmp0[3]);
                }
            } else {
                SkChopCubicAt(pts, tmp0, startT);
                if (SK_Scalar1 == stopT) {
                    (*dst).cubicTo(tmp0[4], tmp0[5], tmp0[6]);
                } else {
                    SkChopCubicAt(&tmp0[3], tmp1, (stopT - startT) / (1 - startT));
                    (*dst).cubicTo(tmp1[1], tmp1[2], tmp1[3]);
                }
            }
            break;
        default:
            // Segment types are a two-bit field holding one of the four types above.
            SkDEBUGFAIL("unknown segType");
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////

static inline int tspan_big_enough(int tspan) {
    SkASSERT(static_cast<unsigned>(tspan) <= static_cast<unsigned>(kMaxTValue));
    const int bigEnough = tspan >> 10;
    return bigEnough;
}

// can't use tangents, since we need [0..1..................2] to be seen
// as definitely not a line (it is when drawn, but not parametrically)
// so we compare midpoints
static constexpr SkScalar kCheapDistLimit = SK_Scalar1/2;  // just made this value up

static bool quad_too_curvy(const SkPoint pts[3], SkScalar tolerance) {
    // diff = (a/4 + b/2 + c/4) - (a/2 + c/2)
    // diff = -a/4 + b/2 - c/4
    const float dx = (pts[1].fX / 2.f) -
                     (((pts[0].fX + pts[2].fX) / 2.f) / 2.f);
    const float dy = (pts[1].fY / 2.f) -
                     (((pts[0].fY + pts[2].fY) / 2.f) / 2.f);

    const float dist = std::max(std::abs(dx), std::abs(dy));
    const bool curvy = dist > tolerance;
    return curvy;
}

static bool conic_too_curvy(const SkPoint& firstPt, const SkPoint& midTPt,
                            const SkPoint& lastPt, SkScalar tolerance) {
    SkPoint midEnds = firstPt + lastPt;
    midEnds *= 0.5f;
    const SkVector dxy = midTPt - midEnds;
    const SkScalar dist = std::max(SkScalarAbs(dxy.fX), SkScalarAbs(dxy.fY));
    const bool curvy = dist > tolerance;
    return curvy;
}

static bool cheap_dist_exceeds_limit(const SkPoint& pt, SkScalar x, SkScalar y,
                                     SkScalar tolerance) {
    const SkScalar dist = std::max(SkScalarAbs(x - pt.fX), SkScalarAbs(y - pt.fY));
    // just made up the 1/2
    const bool exceeds = dist > tolerance;
    return exceeds;
}

static bool cubic_too_curvy(const SkPoint pts[4], SkScalar tolerance) {
    const bool firstExceeds = cheap_dist_exceeds_limit(pts[1],
                         SkScalarInterp(pts[0].fX, pts[3].fX, SK_Scalar1/3),
                         SkScalarInterp(pts[0].fY, pts[3].fY, SK_Scalar1/3), tolerance);
    if (firstExceeds) {
        return true;
    }
    const bool secondExceeds = cheap_dist_exceeds_limit(pts[2],
                         SkScalarInterp(pts[0].fX, pts[3].fX, SK_Scalar1*2/3),
                         SkScalarInterp(pts[0].fY, pts[3].fY, SK_Scalar1*2/3), tolerance);
    return secondExceeds;
}

// puts a cap on the total size of our output, since the client can pass in
// arbitrarily large values for resScale.
constexpr int kMaxRecursionDepth = 8;

// Owns a copy of the path; fIter walks that copy's storage.
class SkContourMeasureIter::Impl {
public:
    Impl(const SkPath& path, bool forceClosed, SkScalar resScale)
        : fPath(path)
        , fIter(SkPathPriv::Iterate(fPath).begin())
        , fTolerance(kCheapDistLimit * sk_ieee_float_divide(1.0f, resScale))
        , fForceClosed(forceClosed) {}

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    bool hasNextSegments() const {
        const bool more = fIter != SkPathPriv::Iterate(fPath).end();
        return more;
    }
    sk_sp<SkContourMeasure> buildSegments();

private:
    SkPath                fPath;
    SkPathPriv::RangeIter fIter;
    SkScalar              fTolerance = 0;
    bool                  fForceClosed = false;

    // temporary
    SkTDArray<SkContourMeasure::Segment>  fSegments;
    SkTDArray<SkPoint>  fPts; // Points used to define the segments

    SkDEBUGCODE(void validate() const;)
    void appendSegment(SkScalar distance, unsigned ptIndex, SkSegType type, int tValue);
    SkScalar compute_line_seg(SkPoint p0, SkPoint p1, SkScalar distance, unsigned ptIndex);
    SkScalar compute_quad_segs(const SkPoint pts[3], SkScalar distance,
                               int mint, int maxt, unsigned ptIndex, int recursionDepth = 0);
    SkScalar compute_conic_segs(const SkConic& conic, SkScalar distance,
                                                         int mint, const SkPoint& minPt,
                                                         int maxt, const SkPoint& maxPt,
                                unsigned ptIndex, int recursionDepth = 0);
    SkScalar compute_cubic_segs(const SkPoint pts[4], SkScalar distance,
                                int mint, int maxt, unsigned ptIndex, int recursionDepth = 0);
};

void SkContourMeasureIter::Impl::appendSegment(SkScalar distance, unsigned ptIndex,
                                               SkSegType type, int tValue) {
    SkASSERT(fPts.size() > 0 && static_cast<size_t>(ptIndex) < static_cast<size_t>(fPts.size()));
    SkContourMeasure::Segment segment{};
    segment.fDistance = distance;
    segment.fPtIndex = ptIndex;
    segment.fType = static_cast<unsigned>(type);
    segment.fTValue = static_cast<unsigned>(tValue);
    fSegments.push_back(segment);
}

SkScalar SkContourMeasureIter::Impl::compute_quad_segs(const SkPoint pts[3], SkScalar distance,
                                                       int mint, int maxt, unsigned ptIndex,
                                                       int recursionDepth) {
    if (recursionDepth < kMaxRecursionDepth &&
        tspan_big_enough(maxt - mint) && quad_too_curvy(pts, fTolerance)) {
        SkPoint tmp[5] = {};
        const int halft = (mint + maxt) >> 1;

        SkChopQuadAtHalf(pts, tmp);
        recursionDepth += 1;
        distance = compute_quad_segs(tmp, distance, mint, halft, ptIndex, recursionDepth);
        distance = compute_quad_segs(&tmp[2], distance, halft, maxt, ptIndex, recursionDepth);
    } else {
        const SkScalar d = SkPoint::Distance(pts[0], pts[2]);
        const SkScalar prevD = distance;
        distance += d;
        if (distance > prevD) {
            appendSegment(distance, ptIndex, kQuad_SegType, maxt);
        }
    }
    return distance;
}

SkScalar SkContourMeasureIter::Impl::compute_conic_segs(const SkConic& conic, SkScalar distance,
                                                        int mint, const SkPoint& minPt,
                                                        int maxt, const SkPoint& maxPt,
                                                        unsigned ptIndex, int recursionDepth) {
    const int halft = (mint + maxt) >> 1;
    const SkPoint halfPt = conic.evalAt(tValue2Scalar(halft));
    if (!halfPt.isFinite()) {
        return distance;
    }
    if (recursionDepth < kMaxRecursionDepth &&
        tspan_big_enough(maxt - mint) && conic_too_curvy(minPt, halfPt, maxPt, fTolerance))
    {
        recursionDepth += 1;
        distance = compute_conic_segs(conic, distance, mint, minPt, halft, halfPt,
                                      ptIndex, recursionDepth);
        distance = compute_conic_segs(conic, distance, halft, halfPt, maxt, maxPt,
                                      ptIndex, recursionDepth);
    } else {
        const SkScalar d = SkPoint::Distance(minPt, maxPt);
        const SkScalar prevD = distance;
        distance += d;
        if (distance > prevD) {
            appendSegment(distance, ptIndex, kConic_SegType, maxt);
        }
    }
    return distance;
}

SkScalar SkContourMeasureIter::Impl::compute_cubic_segs(const SkPoint pts[4], SkScalar distance,
                                                        int mint, int maxt, unsigned ptIndex,
                                                        int recursionDepth) {
    if (recursionDepth < kMaxRecursionDepth &&
        tspan_big_enough(maxt - mint) && cubic_too_curvy(pts, fTolerance))
    {
        SkPoint tmp[7] = {};
        const int halft = (mint + maxt) >> 1;

        SkChopCubicAtHalf(pts, tmp);
        recursionDepth += 1;
        distance = compute_cubic_segs(tmp, distance, mint, halft,
                                      ptIndex, recursionDepth);
        distance = compute_cubic_segs(&tmp[3], distance, halft, maxt,
                                      ptIndex, recursionDepth);
    } else {
        const SkScalar d = SkPoint::Distance(pts[0], pts[3]);
        const SkScalar prevD = distance;
        distance += d;
        if (distance > prevD) {
            appendSegment(distance, ptIndex, kCubic_SegType, maxt);
        }
    }
    return distance;
}

SkScalar SkContourMeasureIter::Impl::compute_line_seg(SkPoint p0, SkPoint p1, SkScalar distance,
                                                      unsigned ptIndex) {
    const SkScalar d = SkPoint::Distance(p0, p1);
    SkASSERT(d >= 0);
    const SkScalar prevD = distance;
    distance += d;
    if (distance > prevD) {
        appendSegment(distance, ptIndex, kLine_SegType, kMaxTValue);
    }
    return distance;
}

#ifdef SK_DEBUG
void SkContourMeasureIter::Impl::validate() const {
    const int stop = fSegments.size();
    unsigned ptIndex = 0;
    SkScalar distance = 0;
    // limit the loop to a reasonable number; pathological cases can run for minutes
    int maxChecks = 10000000;  // set to INT_MAX to defeat the check
    for (int index = 0; index < stop; ++index) {
        const SkContourMeasure::Segment& seg = fSegments[index];
        SkASSERT(seg.fDistance > distance);
        SkASSERT(seg.fPtIndex >= ptIndex);
        SkASSERT(seg.fTValue > 0);

        int s = index;
        while (s < stop - 1 && fSegments[s].fPtIndex == fSegments[s + 1].fPtIndex &&
               --maxChecks > 0) {
            SkASSERT(fSegments[s].fType == fSegments[s + 1].fType);
            SkASSERT(fSegments[s].fTValue < fSegments[s + 1].fTValue);
            s += 1;
        }

        distance = seg.fDistance;
        ptIndex = seg.fPtIndex;
    }
}
#endif

sk_sp<SkContourMeasure> SkContourMeasureIter::Impl::buildSegments() {
    int         ptIndex = -1;
    SkScalar    distance = 0;
    bool        haveSeenClose = fForceClosed;
    bool        haveSeenMoveTo = false;

    /*  Note:
     *  as we accumulate distance, we have to check that the result of +=
     *  actually made it larger, since a very small delta might be > 0, but
     *  still have no effect on distance (if distance >>> delta).
     *
     *  We do this check below, and in compute_quad_segs and compute_cubic_segs
     */

    fSegments.reset();
    fPts.reset();

    const SkPathPriv::RangeIter end = SkPathPriv::Iterate(fPath).end();
    for (; fIter != end; ++fIter) {
        const SkPathPriv::VerbRecord record = *fIter;
        const SkPoint* pts = record.fPoints;
        if (haveSeenMoveTo && record.fVerb == SkPathVerb::kMove) {
            break;
        }
        switch (record.fVerb) {
            case SkPathVerb::kMove:
                ptIndex += 1;
                fPts.push_back(pts[0]);
                SkASSERT(!haveSeenMoveTo);
                haveSeenMoveTo = true;
                break;

            case SkPathVerb::kLine: {
                SkASSERT(haveSeenMoveTo);
                const SkScalar prevD = distance;
                distance = compute_line_seg(pts[0], pts[1], distance,
                                            static_cast<unsigned>(ptIndex));
                if (distance > prevD) {
                    fPts.push_back(pts[1]);
                    ptIndex++;
                }
            } break;

            case SkPathVerb::kQuad: {
                SkASSERT(haveSeenMoveTo);
                const SkScalar prevD = distance;
                distance = compute_quad_segs(pts, distance, 0, kMaxTValue,
                                             static_cast<unsigned>(ptIndex));
                if (distance > prevD) {
                    fPts.push_back(pts[1]);
                    fPts.push_back(pts[2]);
                    ptIndex += 2;
                }
            } break;

            case SkPathVerb::kConic: {
                SkASSERT(haveSeenMoveTo);
                const SkConic conic(pts, record.fWeights[0]);
                const SkScalar prevD = distance;
                distance = compute_conic_segs(conic, distance, 0, conic.fPts[0],
                                              kMaxTValue, conic.fPts[2],
                                              static_cast<unsigned>(ptIndex));
                if (distance > prevD) {
                    // we store the conic weight in our next point, followed by the last 2 pts
                    // thus to reconstitue a conic, you'd need to say
                    // SkConic(pts[0], pts[2], pts[3], weight = pts[1].fX)
                    fPts.push_back(SkPoint{conic.fW, 0});
                    fPts.push_back(pts[1]);
                    fPts.push_back(pts[2]);
                    ptIndex += 3;
                }
            } break;

            case SkPathVerb::kCubic: {
                SkASSERT(haveSeenMoveTo);
                const SkScalar prevD = distance;
                distance = compute_cubic_segs(pts, distance, 0, kMaxTValue,
                                              static_cast<unsigned>(ptIndex));
                if (distance > prevD) {
                    fPts.push_back(pts[1]);
                    fPts.push_back(pts[2]);
                    fPts.push_back(pts[3]);
                    ptIndex += 3;
                }
            } break;

            case SkPathVerb::kClose:
                haveSeenClose = true;
                break;
        }

    }

    if (!SkIsFinite(distance)) {
        return nullptr;
    }
    if (fSegments.empty()) {
        return nullptr;
    }

    if (haveSeenClose) {
        const SkScalar prevD = distance;
        const SkPoint firstPt = fPts[0];
        distance = compute_line_seg(fPts[ptIndex], firstPt, distance,
                                    static_cast<unsigned>(ptIndex));
        if (distance > prevD) {
            fPts.push_back(firstPt);
        }
    }

    SkDEBUGCODE(validate();)

    const sk_sp<SkContourMeasure> measure(
            new SkContourMeasure(std::move(fSegments), std::move(fPts), distance, haveSeenClose));
    return measure;
}

static void compute_pos_tan(const SkPoint pts[], unsigned segType,
                            SkScalar t, SkPoint* pos, SkVector* tangent) {
    switch (segType) {
        case kLine_SegType:
            if (pos) {
                (*pos).set(SkScalarInterp(pts[0].fX, pts[1].fX, t),
                           SkScalarInterp(pts[0].fY, pts[1].fY, t));
            }
            if (tangent) {
                // A zero-length tangent is left at zero.
                static_cast<void>((*tangent).setNormalize(pts[1].fX - pts[0].fX,
                                                          pts[1].fY - pts[0].fY));
            }
            break;
        case kQuad_SegType:
            SkEvalQuadAt(pts, t, pos, tangent);
            if (tangent) {
                static_cast<void>((*tangent).normalize());
            }
            break;
        case kConic_SegType: {
            const SkConic conic(pts[0], pts[2], pts[3], pts[1].fX);
            conic.evalAt(t, pos, tangent);
            if (tangent) {
                static_cast<void>((*tangent).normalize());
            }
        } break;
        case kCubic_SegType:
            SkEvalCubicAt(pts, t, pos, tangent, nullptr);
            if (tangent) {
                static_cast<void>((*tangent).normalize());
            }
            break;
        default:
            SkDEBUGFAIL("unknown segType");
            break;
    }
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

SkContourMeasureIter::SkContourMeasureIter() = default;

SkContourMeasureIter::SkContourMeasureIter(const SkPath& path, bool forceClosed,
                                           SkScalar resScale) {
    reset(path, forceClosed, resScale);
}

SkContourMeasureIter::~SkContourMeasureIter() = default;

SkContourMeasureIter::SkContourMeasureIter(SkContourMeasureIter&&) = default;
SkContourMeasureIter& SkContourMeasureIter::operator=(SkContourMeasureIter&&) = default;

/** Assign a new path, or null to have none.
*/
void SkContourMeasureIter::reset(const SkPath& path, bool forceClosed, SkScalar resScale) {
    if (path.isFinite()) {
        fImpl = std::make_unique<Impl>(path, forceClosed, resScale);
    } else {
        fImpl.reset();
    }
}

sk_sp<SkContourMeasure> SkContourMeasureIter::next() {
    if (!fImpl) {
        return nullptr;
    }
    while ((*fImpl).hasNextSegments()) {
        sk_sp<SkContourMeasure> measure = (*fImpl).buildSegments();
        if (measure) {
            return measure;
        }
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SkContourMeasure::SkContourMeasure(SkTDArray<Segment>&& segs, SkTDArray<SkPoint>&& pts, SkScalar length, bool isClosed)
    : fSegments(std::move(segs))
    , fPts(std::move(pts))
    , fLength(length)
    , fIsClosed(isClosed)
    {}

int SkContourMeasure::distanceToSegment(SkScalar distance, SkScalar* t) const {
    SkDEBUGCODE(const SkScalar contourLength = ) length();
    SkASSERT(distance >= 0 && distance <= contourLength);

    const int count = fSegments.size();
    SkASSERT(count > 0);

    // Binary search for the first segment whose cumulative distance is not below distance.
    // An exact match and an insertion point are treated alike.
    unsigned lo = 0;
    unsigned hi = count > 0 ? static_cast<unsigned>(count - 1) : 0;
    while (lo < hi) {
        const unsigned mid = (hi + lo) >> 1;
        if (fSegments[static_cast<int>(mid)].fDistance < distance) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (count > 0 && fSegments[static_cast<int>(hi)].fDistance < distance) {
        hi += 1;
    }
    int index = static_cast<int>(hi);
    // distance <= length() == the last segment's distance, so the search stays in range;
    // keep the last segment if rounding ever disagrees.
    if (index >= count) {
        index = count - 1;
    }
    const Segment& seg = fSegments[index];

    // now interpolate t-values with the prev segment (if possible)
    SkScalar startT = 0;
    SkScalar startD = 0;
    // check if the prev segment is legal, and references the same set of points
    if (index > 0) {
        const Segment& prev = fSegments[index - 1];
        startD = prev.fDistance;
        if (prev.fPtIndex == seg.fPtIndex) {
            SkASSERT(prev.fType == seg.fType);
            startT = prev.getScalarT();
        }
    }

    SkASSERT(seg.getScalarT() > startT);
    SkASSERT(distance >= startD);
    SkASSERT(seg.fDistance > startD);

    *t = startT + (seg.getScalarT() - startT) * (distance - startD) / (seg.fDistance - startD);
    return index;
}

bool SkContourMeasure::getPosTan(SkScalar distance, SkPoint* pos, SkVector* tangent) const {
    if (SkIsNaN(distance)) {
        return false;
    }

    const SkScalar contourLength = length();
    SkASSERT(contourLength > 0);
    if (fSegments.empty()) {
        return false;
    }

    // pin the distance to a legal range
    if (distance < 0) {
        distance = 0;
    } else if (distance > contourLength) {
        distance = contourLength;
    }

    SkScalar t = 0;
    const int index = distanceToSegment(distance, &t);
    if (SkIsNaN(t)) {
        return false;
    }

    const Segment& seg = fSegments[index];
    if (!segment_points_fit(seg.fPtIndex, seg.fType, fPts.size())) {
        return false;
    }
    compute_pos_tan(&fPts[static_cast<int>(seg.fPtIndex)], seg.fType, t, pos, tangent);
    return true;
}

bool SkContourMeasure::getMatrix(SkScalar distance, SkMatrix* matrix, MatrixFlags flags) const {
    SkPoint     position{0, 0};
    SkVector    tangent{0, 0};

    if (getPosTan(distance, &position, &tangent)) {
        if (matrix) {
            if ((flags & kGetTangent_MatrixFlag) != 0) {
                (*matrix).setSinCos(tangent.fY, tangent.fX, 0, 0);
            } else {
                (*matrix).reset();
            }
            if ((flags & kGetPosition_MatrixFlag) != 0) {
                (*matrix).postTranslate(position.fX, position.fY);
            }
        }
        return true;
    }
    return false;
}

bool SkContourMeasure::getSegment(SkScalar startD, SkScalar stopD, SkPathBuilder* dst,
                                  bool startWithMoveTo) const {
    SkASSERT(dst);

    const SkScalar contourLength = length();

    if (startD < 0) {
        startD = 0;
    }
    if (stopD > contourLength) {
        stopD = contourLength;
    }
    if (!(startD <= stopD)) {   // catch NaN values as well
        return false;
    }
    if (fSegments.empty()) {
        return false;
    }

    SkPoint  p{0, 0};
    SkScalar startT = 0;
    SkScalar stopT = 0;
    int segIndex = distanceToSegment(startD, &startT);
    if (!SkIsFinite(startT)) {
        return false;
    }
    const int stopIndex = distanceToSegment(stopD, &stopT);
    if (!SkIsFinite(stopT)) {
        return false;
    }
    // Distances are non-decreasing along the segments, so startD <= stopD orders the indices.
    if (segIndex > stopIndex) {
        return false;
    }
    // Every segment between them must reference points inside the point array.
    for (int i = segIndex; i <= stopIndex; ++i) {
        if (!segment_points_fit(fSegments[i].fPtIndex, fSegments[i].fType, fPts.size())) {
            return false;
        }
    }
    if (startWithMoveTo) {
        const Segment& seg = fSegments[segIndex];
        compute_pos_tan(&fPts[static_cast<int>(seg.fPtIndex)], seg.fType, startT, &p, nullptr);
        (*dst).moveTo(p);
    }

    const unsigned stopPtIndex = fSegments[stopIndex].fPtIndex;
    if (fSegments[segIndex].fPtIndex == stopPtIndex) {
        const Segment& seg = fSegments[segIndex];
        SkContourMeasure_segTo(&fPts[static_cast<int>(seg.fPtIndex)], seg.fType, startT, stopT,
                               dst);
    } else {
        do {
            const Segment& seg = fSegments[segIndex];
            SkContourMeasure_segTo(&fPts[static_cast<int>(seg.fPtIndex)], seg.fType, startT,
                                   SK_Scalar1, dst);
            // Advance to the first segment of the next verb. Point indices never decrease, so
            // this stops at or before stopIndex.
            const unsigned verbPtIndex = seg.fPtIndex;
            do {
                ++segIndex;
            } while (segIndex < stopIndex && fSegments[segIndex].fPtIndex == verbPtIndex);
            startT = 0;
        } while (fSegments[segIndex].fPtIndex < stopPtIndex);
        const Segment& last = fSegments[segIndex];
        SkContourMeasure_segTo(&fPts[static_cast<int>(last.fPtIndex)], last.fType, 0, stopT,
                               dst);
    }

    return true;
}

SkContourMeasure::VerbMeasure SkContourMeasure::ForwardVerbIterator::operator*() const {
    static constexpr size_t seg_pt_count[] = {
        2, // kLine  (current_pt, 1 line pt)
        3, // kQuad  (current_pt, 2 quad pts)
        4, // kCubic (current_pt, 3 cubic pts)
        4, // kConic (current_pt, {weight, 0}, 2 conic pts)
    };
    static constexpr SkPathVerb seg_verb[] = {
        SkPathVerb::kLine,
        SkPathVerb::kQuad,
        SkPathVerb::kCubic,
        SkPathVerb::kConic,
    };
    static_assert(std::size(seg_pt_count) == std::size(seg_verb));
    static_assert(static_cast<size_t>(kLine_SegType)  < std::size(seg_pt_count));
    static_assert(static_cast<size_t>(kQuad_SegType)  < std::size(seg_pt_count));
    static_assert(static_cast<size_t>(kCubic_SegType) < std::size(seg_pt_count));
    static_assert(static_cast<size_t>(kConic_SegType) < std::size(seg_pt_count));

    const Segment& front = fSegments.front();
    const size_t type = SkToSizeT(front.fType);
    SkASSERT(type < std::size(seg_pt_count));
    SkASSERT(front.fPtIndex + seg_pt_count[type] <= fPts.size());

    const VerbMeasure measure{
        front.fDistance,
        seg_verb[type],
        fPts.subspan(front.fPtIndex, seg_pt_count[type]),
    };
    return measure;
}
