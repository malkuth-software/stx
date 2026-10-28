/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSpan.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkTArray.h"
#include "include/private/SkTo.h"
#include "src/core/SkEdgeClipper.h"
#include "src/core/SkFloatBits.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkPathEnums.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPathRawShapes.h"
#include "src/core/SkPointPriv.h"
#include "src/core/SkSpanPriv.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits.h>
#include <utility>

SkPath::SkPath(sk_sp<SkPathData> pd, SkPathFillType ft, bool isVolatile)
    : fPathData(std::move(pd))
    , fFillType(ft)
    , fIsVolatile(isVolatile)
{
    SkASSERT(fPathData);
}

SkPath::SkPath(SkPathFillType ft)
    : fPathData(SkPathData::Empty())
    , fFillType(ft)
    , fIsVolatile(false)
{}

SkPath::SkPath(const SkPath&) = default;
SkPath& SkPath::operator=(const SkPath&) = default;
SkPath::SkPath(SkPath&&) = default;
SkPath& SkPath::operator=(SkPath&&) = default;

SkPath::~SkPath() {
    SkDEBUGCODE(validate();)
}

void SkPath::setConvexity(SkPathConvexity c) const {
    (*fPathData).setConvexity(c);
}

SkPathConvexity SkPath::getConvexityOrUnknown() const {
    const SkPathConvexity convexity = (*fPathData).getConvexityOrUnknown();
    return convexity;
}

bool operator==(const SkPath& a, const SkPath& b) {
    const bool equal = &a == &b ||
                       (a.fFillType == b.fFillType && *a.fPathData == *b.fPathData);
    return equal;
}

void SkPath::swap(SkPath& that) {
    if (this != &that) {
        fPathData.swap(that.fPathData);
        std::swap(fFillType, that.fFillType);
        std::swap(fIsVolatile, that.fIsVolatile);
    }
}

SkPath& SkPath::reset() {
    *this = SkPath();
    return *this;
}

const SkRect& SkPath::getBounds() const & {
    return (*fPathData).bounds();
}

uint32_t SkPath::getSegmentMasks() const {
    const uint32_t mask = (*fPathData).segmentMask();
    return mask;
}

bool SkPath::isFinite() const {
    const bool finite = fPathData.get() != PeekErrorSingleton();
    return finite;
}

bool SkPath::isValid() const {
    const bool valid = isFinite();
    return valid;
}

uint32_t SkPath::getGenerationID() const {
    const uint32_t id = (*fPathData).uniqueID();
    return id;
}

#ifdef SK_DEBUG
void SkPath::validate() const {}
#endif

std::optional<SkPathOvalInfo> SkPath::getOvalInfo() const {
    const std::optional<SkPathOvalInfo> info = (*fPathData).asOval();
    return info;
}

std::optional<SkPathRRectInfo> SkPath::getRRectInfo() const {
    const std::optional<SkPathRRectInfo> info = (*fPathData).asRRect();
    return info;
}

SkSpan<const SkPoint> SkPath::points() const & {
    const SkSpan<const SkPoint> view = (*fPathData).points();
    return view;
}

SkSpan<const SkPathVerb> SkPath::verbs() const & {
    const SkSpan<const SkPathVerb> view = (*fPathData).verbs();
    return view;
}

SkSpan<const float> SkPath::conicWeights() const & {
    const SkSpan<const float> view = (*fPathData).conics();
    return view;
}

SkPath SkPath::Raw(SkSpan<const SkPoint> pts, SkSpan<const SkPathVerb> vbs,
                   SkSpan<const float> ws, SkPathFillType ft, bool isVolatile) {
    const SkPath path = MakeNullCheck(SkPathData::Make(pts, vbs, ws), ft, isVolatile);
    return path;
}

SkPath SkPath::Rect(const SkRect& r, SkPathFillType ft, SkPathDirection dir, unsigned startIndex) {
    startIndex &= 3;    // keep it legal
    const SkPath path = MakeNullCheck(SkPathData::Rect(r, dir, startIndex), ft, false);
    return path;
}

SkPath SkPath::Oval(const SkRect& r, SkPathDirection dir, unsigned startIndex) {
    startIndex &= 3;    // keep it legal
    const SkPath path = MakeNullCheck(SkPathData::Oval(r, dir, startIndex),
                                      SkPathFillType::kDefault, false);
    return path;
}

SkPath SkPath::RRect(const SkRRect& rr, SkPathDirection dir, unsigned startIndex) {
    startIndex &= 7;    // keep it legal

    // Simplify rrects that are really rects or ovals
    const SkRect& bounds = rr.getBounds();
    const SkPathPriv::SimplifiedRRect simplified = SkPathPriv::SimplifyRRect(rr, startIndex);
    switch (simplified.fShape) {
        case SkPathPriv::RRectAsEnum::kRect: {
            const SkPath rect = SkPath::Rect(bounds, SkPathFillType::kDefault, dir,
                                             simplified.fStartIndex);
            return rect;
        }
        case SkPathPriv::RRectAsEnum::kOval: {
            const SkPath oval = SkPath::Oval(bounds, dir, simplified.fStartIndex);
            return oval;
        }
        case SkPathPriv::RRectAsEnum::kRRect:
            // fall out of the switch
            break;
    }
    const SkPath path = MakeNullCheck(SkPathData::RRect(rr, dir, simplified.fStartIndex),
                                      SkPathFillType::kDefault, false);
    return path;
}

SkPath SkPath::Polygon(SkSpan<const SkPoint> pts, bool isClosed,
                       SkPathFillType ft, bool isVolatile) {
    const SkPath path = MakeNullCheck(SkPathData::Polygon(pts, isClosed), ft, isVolatile);
    return path;
}

std::optional<SkPath> SkPath::tryMakeTransform(const SkMatrix& matrix) const {
    sk_sp<SkPathData> pdata = (*fPathData).makeTransform(matrix);
    if (!pdata) {
        return {};
    }
    const SkPath transformed(std::move(pdata), fFillType, fIsVolatile);
    return transformed;
}

SkPath SkPath::makeTransform(const SkMatrix& matrix) const {
    if (!isFinite()) {
        return *this;
    }
    const std::optional<SkPath> transformed = tryMakeTransform(matrix);
    if (transformed.has_value()) {
        return transformed.value();
    }
    const SkPath error(sk_ref_sp(PeekErrorSingleton()), fFillType, false);
    return error;
}

std::optional<SkPathRaw> SkPath::raw(SkResolveConvexity rc) const {
    const std::optional<SkPathRaw> view = (*fPathData).raw(fFillType, rc);
    return view;
}

// Returns false when the edge from p0 to p1 (in direction dir) leaves rect on its inner side.
static inline bool check_edge_against_rect(const SkPoint& p0,
                                           const SkPoint& p1,
                                           const SkRect& rect,
                                           SkPathDirection dir) {
    const SkPoint* edgeBegin = nullptr;
    SkVector v{0, 0};
    if (SkPathDirection::kCW == dir) {
        v = p1 - p0;
        edgeBegin = &p0;
    } else {
        v = p0 - p1;
        edgeBegin = &p1;
    }
    if (v.fX || v.fY) {
        // check the cross product of v with the vec from edgeBegin to each rect corner
        const SkScalar yL = v.fY * (rect.fLeft - (*edgeBegin).fX);
        const SkScalar xT = v.fX * (rect.fTop - (*edgeBegin).fY);
        const SkScalar yR = v.fY * (rect.fRight - (*edgeBegin).fX);
        const SkScalar xB = v.fX * (rect.fBottom - (*edgeBegin).fY);
        if ((xT < yL) || (xT < yR) || (xB < yL) || (xB < yR)) {
            return false;
        }
    }
    return true;
}

bool SkPath::conservativelyContainsRect(const SkRect& rect) const {
    const SkPathConvexity convexity = getConvexity();
    if (!SkPathConvexity_IsConvex(convexity)) {
        return false;
    }

    const std::optional<SkPathDirection> direction = SkPathConvexity_ToDirection(convexity);
    if (!direction.has_value()) {
        return false;
    }

    SkPoint firstPt{0, 0};
    SkPoint prevPt{0, 0};
    int segmentCount = 0;
    SkDEBUGCODE(int moveCnt = 0;)

    for (const SkPathPriv::VerbRecord record : SkPathPriv::Iterate(*this)) {
        const SkPathVerb verb = record.fVerb;
        const SkPoint* pts = record.fPoints;
        if (verb == SkPathVerb::kClose || (segmentCount > 0 && verb == SkPathVerb::kMove)) {
            // Closing the current contour; but since convexity is a precondition, it's the only
            // contour that matters.
            SkASSERT(moveCnt);
            segmentCount++;
            break;
        } else if (verb == SkPathVerb::kMove) {
            // A move at the start of the contour (or multiple leading moves, in which case we
            // keep the last one before a non-move verb).
            SkASSERT(!segmentCount);
            SkDEBUGCODE(++moveCnt);
            firstPt = pts[0];
            prevPt = pts[0];
        } else {
            const int pointCount = SkPathPriv::PtsInVerb(static_cast<unsigned>(verb));
            SkASSERT(pointCount > 0);

            const SkSpan<const SkPoint> segment(pts, static_cast<size_t>(pointCount) + 1);
            if (!SkPathPriv::AllPointsEq(segment)) {
                SkASSERT(moveCnt);
                const int nextPt = pointCount;
                segmentCount++;

                if (prevPt == pts[nextPt]) {
                    // A pre-condition to getting here is that the path is convex, so if a
                    // verb's start and end points are the same, it means it's the only
                    // verb in the contour (and the only contour). While it's possible for
                    // such a single-verb path to be a convex curve, we do not have any non-zero
                    // length edges to conservatively test against without splitting or
                    // evaluating the curve. For simplicity, just reject the rectangle.
                    return false;
                } else if (SkPathVerb::kConic == verb) {
                    SkConic orig{};
                    orig.set(pts, *record.fWeights);
                    SkPoint quadPts[5] = {};
                    const int count = orig.chopIntoQuadsPOW2(quadPts, 1);
                    SkASSERT_RELEASE(2 == count);

                    if (!check_edge_against_rect(quadPts[0], quadPts[2], rect, direction.value())) {
                        return false;
                    }
                    if (!check_edge_against_rect(quadPts[2], quadPts[4], rect, direction.value())) {
                        return false;
                    }
                } else {
                    if (!check_edge_against_rect(prevPt, pts[nextPt], rect, direction.value())) {
                        return false;
                    }
                }
                prevPt = pts[nextPt];
            }
        }
    }

    if (segmentCount) {
        const bool contained = check_edge_against_rect(prevPt, firstPt, rect, direction.value());
        return contained;
    }
    return false;
}

bool SkPath::isLastContourClosed() const {
    const SkSpan<const SkPathVerb> vbs = verbs();
    const bool closed = !vbs.empty() && vbs.back() == SkPathVerb::kClose;
    return closed;
}

bool SkPath::isLine(SkPoint line[2]) const {
    const SkSpan<const SkPathVerb> vbs = verbs();
    if (vbs.size() == 2 && vbs[1] == SkPathVerb::kLine) {
        SkASSERT(vbs[0] == SkPathVerb::kMove);
        const SkSpan<const SkPoint> pts = points();
        SkASSERT(pts.size() == 2);
        if (line) {
            line[0] = pts[0];
            line[1] = pts[1];
        }
        return true;
    }
    return false;
}

bool SkPath::isEmpty() const {
    SkDEBUGCODE(validate();)
    const bool empty = verbs().empty();
    return empty;
}

bool SkPath::isConvex() const {
    const bool convex = SkPathConvexity_IsConvex(getConvexity());
    return convex;
}

bool SkPath::isRect(SkRect* rect, bool* isClosed, SkPathDirection* direction) const {
    SkDEBUGCODE(validate();)
    const SkSpan<const SkPoint> pts = points();
    const SkSpan<const SkPathVerb> vbs = verbs();
    const std::optional<SkPathPriv::RectContour> rc =
            SkPathPriv::IsRectContour(pts, vbs, getSegmentMasks(), false);
    if (!rc.has_value()) {
        return false;
    }
    if (rect) {
        *rect = rc.value().fRect;
    }
    if (isClosed) {
        *isClosed = rc.value().fIsClosed;
    }
    if (direction) {
        *direction = rc.value().fDirection;
    }
    return true;
}

bool SkPath::isOval(SkRect* bounds) const {
    const std::optional<SkPathOvalInfo> info = getOvalInfo();
    if (!info.has_value()) {
        return false;
    }
    if (bounds) {
        *bounds = info.value().fBounds;
    }
    return true;
}

bool SkPath::isRRect(SkRRect* rrect) const {
    const std::optional<SkPathRRectInfo> info = getRRectInfo();
    if (!info.has_value()) {
        return false;
    }
    if (rrect) {
        *rrect = info.value().fRRect;
    }
    return true;
}

#ifdef SK_LEGACY_PATH_ACCESSORS
// Copies as many points as fit and returns the path's point count.
size_t SkPath::getPoints(SkSpan<SkPoint> dst) const {
    SkDEBUGCODE(validate();)
    const SkSpan<const SkPoint> src = points();

    const size_t n = std::min(dst.size(), src.size());
    sk_careful_memcpy(dst.data(), src.data(), n * sizeof(SkPoint));
    return src.size();
}

// Returns (0, 0) for an index outside the points.
SkPoint SkPath::getPoint(int index) const {
    const SkSpan<const SkPoint> pts = points();
    if (index >= 0 && static_cast<size_t>(index) < pts.size()) {
        return pts[static_cast<size_t>(index)];
    }
    const SkPoint origin = SkPoint::Make(0, 0);
    return origin;
}

// Copies as many verbs as fit and returns the path's verb count.
size_t SkPath::getVerbs(SkSpan<uint8_t> dst) const {
    SkDEBUGCODE(validate();)
    const SkSpan<const SkPathVerb> src = verbs();

    const size_t n = std::min(dst.size(), src.size());
    sk_careful_memcpy(dst.data(), src.data(), n);
    return src.size();
}
#endif

size_t SkPath::approximateBytesUsed() const {
    const size_t bytes = sizeof(SkPath)
                       + points().size_bytes()
                       + verbs().size_bytes()
                       + conicWeights().size_bytes();
    return bytes;
}

std::optional<SkPoint> SkPath::getLastPt() const {
    SkDEBUGCODE(validate();)
    const SkSpan<const SkPoint> pts = points();
    if (!pts.empty()) {
        return pts.back();
    }
    return {};
}

SkPathConvexity SkPath::getConvexity() const {
    // Enable once we fix all the bugs
    // SkDEBUGCODE(this->validate());
    SkPathConvexity convexity = getConvexityOrUnknown();
    if (convexity == SkPathConvexity::kUnknown) {
        convexity = computeConvexity();
    }
    SkASSERT(convexity != SkPathConvexity::kUnknown);
    return convexity;
}

SkPathIter SkPath::iter() const & {
    const SkPathIter iterator(points(), verbs(), conicWeights());
    return iterator;
}

///////////////////////////////////////////////////////////////////////////////

// A default iterator has no verbs, so next() returns kDone_Verb.
SkPath::Iter::Iter() = default;

SkPath::Iter::Iter(const SkPath& path, bool forceClose) {
    setPath(path, forceClose);
}

void SkPath::Iter::setPath(const SkPath& path, bool forceClose) {
    fPts = path.points().data();

    const SkSpan<const SkPathVerb> vbs = path.verbs();
    // An empty path has no verb storage; both cursors stay null.
    fVerbs = nullptr;
    fVerbStop = nullptr;
    if (!vbs.empty()) {
        fVerbs = vbs.data();
        fVerbStop = &vbs.back() + 1;
    }

    fConicWeights = path.conicWeights().data();
    fConicWeight = 0;
    fLastPt = {0, 0};
    fMoveTo = {0, 0};
    fForceClose = forceClose;
    fNeedClose = false;
    fCloseLine = false;
}

bool SkPath::Iter::isClosedContour() const {
    if (fVerbs == nullptr || fVerbs == fVerbStop) {
        return false;
    }
    if (fForceClose) {
        return true;
    }

    const SkPathVerb* verbs = fVerbs;
    const SkPathVerb* stop = fVerbStop;

    if (SkPathVerb::kMove == *verbs) {
        verbs += 1; // skip the initial moveto
    }

    while (verbs < stop) {
        const SkPathVerb v = *verbs;
        ++verbs;
        if (SkPathVerb::kMove == v) {
            break;
        }
        if (SkPathVerb::kClose == v) {
            return true;
        }
    }
    return false;
}

SkPathVerb SkPath::Iter::autoClose(SkPoint pts[2]) {
    SkASSERT(pts);
    if (fLastPt != fMoveTo) {
        // A special case: if both points are NaN, SkPoint::operation== returns
        // false, but the iterator expects that they are treated as the same.
        // (consider SkPoint is a 2-dimension float point).
        if (SkIsNaN(fLastPt.fX) || SkIsNaN(fLastPt.fY) ||
            SkIsNaN(fMoveTo.fX) || SkIsNaN(fMoveTo.fY)) {
            return SkPathVerb::kClose;
        }

        pts[0] = fLastPt;
        pts[1] = fMoveTo;
        fLastPt = fMoveTo;
        fCloseLine = true;
        return SkPathVerb::kLine;
    }
    pts[0] = fMoveTo;
    return SkPathVerb::kClose;
}

// ptsParam receives up to four points; it must not address the path's own storage.
SkPath::Verb SkPath::Iter::next(SkPoint ptsParam[4]) {
    SkASSERT(ptsParam);
    SkASSERT(!!fVerbs == !!fVerbStop);

    if (fVerbs == fVerbStop) {
        // Close the curve if requested and if there is some curve to close
        if (fNeedClose) {
            if (SkPathVerb::kLine == autoClose(ptsParam)) {
                return kLine_Verb;
            }
            fNeedClose = false;
            return kClose_Verb;
        }
        return kDone_Verb;
    }

    SkPathVerb verb = *fVerbs;
    ++fVerbs;
    const SkPoint* srcPts = fPts;
    SkPoint* pts = ptsParam;

    switch (verb) {
        case SkPathVerb::kMove:
            if (fNeedClose) {
                fVerbs--; // move back one verb
                verb = autoClose(pts);
                if (verb == SkPathVerb::kClose) {
                    fNeedClose = false;
                }
                return static_cast<Verb>(verb);
            }
            if (fVerbs == fVerbStop) {    // might be a trailing moveto
                return kDone_Verb;
            }
            fMoveTo = *srcPts;
            pts[0] = *srcPts;
            srcPts += 1;
            fLastPt = fMoveTo;
            fNeedClose = fForceClose;
            break;
        case SkPathVerb::kLine:
            pts[0] = fLastPt;
            pts[1] = srcPts[0];
            fLastPt = srcPts[0];
            fCloseLine = false;
            srcPts += 1;
            break;
        case SkPathVerb::kConic:
            fConicWeight = *fConicWeights;
            fConicWeights += 1;
            [[fallthrough]];
        case SkPathVerb::kQuad:
            pts[0] = fLastPt;
            pts[1] = srcPts[0];
            pts[2] = srcPts[1];
            fLastPt = srcPts[1];
            srcPts += 2;
            break;
        case SkPathVerb::kCubic:
            pts[0] = fLastPt;
            pts[1] = srcPts[0];
            pts[2] = srcPts[1];
            pts[3] = srcPts[2];
            fLastPt = srcPts[2];
            srcPts += 3;
            break;
        case SkPathVerb::kClose:
            verb = autoClose(pts);
            if (verb == SkPathVerb::kLine) {
                fVerbs--; // move back one verb
            } else {
                fNeedClose = false;
            }
            fLastPt = fMoveTo;
            break;
    }
    fPts = srcPts;
    return static_cast<Verb>(verb);
}

// Points reported for each verb by SkPath::Iter, including the start point of segments.
static inline uint8_t SkPathIterPointsPerVerb(SkPathVerb verb) {
    switch (verb) {
        case SkPathVerb::kMove:  return 1;
        case SkPathVerb::kLine:  return 2;
        case SkPathVerb::kQuad:  return 3;
        case SkPathVerb::kConic: return 3;
        case SkPathVerb::kCubic: return 4;
        case SkPathVerb::kClose: return 0;
    }
    return 0;
}

std::optional<SkPath::IterRec> SkPath::Iter::next() {
    const Verb legacyVerb = next(fStorage.data());
    if (legacyVerb == kDone_Verb) {
        return {};
    }

    const SkPathVerb verb = static_cast<SkPathVerb>(legacyVerb);
    const IterRec record{
        verb,
        SkSpan<const SkPoint>(fStorage.data(), SkPathIterPointsPerVerb(verb)),
        verb == SkPathVerb::kConic ? fConicWeight : 1,
    };
    return record;
}

void SkPath::RawIter::setPath(const SkPath& path) {
    SkPathPriv::Iterate iterate(path);
    fIter = iterate.begin();
    fEnd = iterate.end();
}

SkPath::Verb SkPath::RawIter::next(SkPoint pts[4]) {
    if (!(fIter != fEnd)) {
        return kDone_Verb;
    }
    const VerbRecord record = *fIter;
    const int numPts = SkPathIterPointsPerVerb(record.fVerb);
    if (record.fVerb == SkPathVerb::kConic) {
        fConicWeight = *record.fWeights;
    }
    for (int i = 0; i < numPts; ++i) {
        pts[i] = record.fPoints[i];
    }
    ++fIter;
    const Verb verb = static_cast<Verb>(record.fVerb);
    return verb;
}

std::optional<SkPath::IterRec> SkPath::RawIter::next() {
    if (fIter == fEnd) {
        return {};
    }

    const VerbRecord current = *fIter;
    ++fIter;
    const IterRec record{
        current.fVerb,
        SkSpan<const SkPoint>(current.fPoints, SkPathIterPointsPerVerb(current.fVerb)),
        current.fVerb == SkPathVerb::kConic ? *current.fWeights : 1
    };
    return record;
}

///////////////////////////////////////////////////////////////////////////////

SkPath SkPath::makeFillType(SkPathFillType ft) const {
    SkPath copy = *this;
    copy.setFillType(ft);
    return copy;
}

SkPath SkPath::makeToggleInverseFillType() const {
    const SkPath toggled = makeFillType(SkPathFillType_ToggleInverse(fFillType));
    return toggled;
}

SkPath SkPath::makeIsVolatile(bool v) const {
    SkPath copy = *this;
    copy.fIsVolatile = v;
    return copy;
}

SkPathConvexity SkPath::computeConvexity() const {
    const SkPathConvexity cached = getConvexityOrUnknown();
    if (cached != SkPathConvexity::kUnknown) {
        return cached;
    }

    SkPathConvexity convexity = SkPathConvexity::kConcave;

    if (isFinite()) {
        convexity = SkPathPriv::ComputeConvexity(points(),
                                                 verbs(),
                                                 conicWeights());
    }

    SkASSERT(convexity != SkPathConvexity::kUnknown);
    setConvexity(convexity);
    return convexity;
}

bool SkPath::contains(SkPoint p) const {
    const std::optional<SkPathRaw> raw = SkPathPriv::Raw(*this, SkResolveConvexity::kNo);
    const bool inside = raw.has_value() && SkPathPriv::Contains(raw.value(), p);
    return inside;
}

// pts must hold 1 + 2 * 2^pow2 points.
int SkPath::ConvertConicToQuads(const SkPoint& p0, const SkPoint& p1, const SkPoint& p2,
                                SkScalar w, SkPoint pts[], int pow2) {
    const SkConic conic(p0, p1, p2, w);
    const int count = conic.chopIntoQuadsPOW2(pts, pow2);
    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

SkRect SkPath::computeTightBounds() const {
    // If we're only lines, then our (quick) bounds is also tight.
    if (getSegmentMasks() == SkPath::kLine_SegmentMask) {
        return getBounds();
    }

    const SkRect bounds = SkPathPriv::ComputeTightBounds(points(),
                                                         verbs(),
                                                         conicWeights());
    return bounds;
}

bool SkPath::IsLineDegenerate(const SkPoint& p1, const SkPoint& p2, bool exact) {
    const bool degenerate = exact ? p1 == p2 : SkPointPriv::EqualsWithinTolerance(p1, p2);
    return degenerate;
}

bool SkPath::IsQuadDegenerate(const SkPoint& p1, const SkPoint& p2,
                                const SkPoint& p3, bool exact) {
    const bool degenerate = exact ? p1 == p2 && p2 == p3
                                  : SkPointPriv::EqualsWithinTolerance(p1, p2) &&
                                    SkPointPriv::EqualsWithinTolerance(p2, p3);
    return degenerate;
}

bool SkPath::IsCubicDegenerate(const SkPoint& p1, const SkPoint& p2,
                                const SkPoint& p3, const SkPoint& p4, bool exact) {
    const bool degenerate = exact ? p1 == p2 && p2 == p3 && p3 == p4
                                  : SkPointPriv::EqualsWithinTolerance(p1, p2) &&
                                    SkPointPriv::EqualsWithinTolerance(p2, p3) &&
                                    SkPointPriv::EqualsWithinTolerance(p3, p4);
    return degenerate;
}

SkPath SkPath::RRect(const SkRRect& rr, SkPathDirection dir) {
    // legacy start indices: 6 (CW) and 7 (CCW)
    const unsigned start = dir == SkPathDirection::kCW ? 6 : 7;
    const SkPath path = RRect(rr, dir, start);
    return path;
}

SkPath SkPath::Oval(const SkRect& r, SkPathDirection dir) {
    // legacy start index: 1
    const SkPath path = Oval(r, dir, 1);
    return path;
}

SkPath SkPath::Circle(SkScalar x, SkScalar y, SkScalar r, SkPathDirection dir) {
    if (r >= 0) {
        const SkPath circle = Oval(SkRect::MakeLTRB(x - r, y - r, x + r, y + r), dir);
        return circle;
    }
    const SkPath empty{};
    return empty;
}

SkPath SkPath::RRect(const SkRect& r, SkScalar rx, SkScalar ry, SkPathDirection dir) {
    const SkPath path = RRect(SkRRect::MakeRectXY(r, rx, ry), dir);
    return path;
}

SkPathFirstDirection SkPathPriv::ComputeFirstDirection(const SkPath& path) {
    const SkPathConvexity convexity = path.getConvexityOrUnknown();
    if (SkPathConvexity_IsConvex(convexity)) {
        // Note, this can return kUnknown. That is valid. If we've determined that the
        // path is convex, then we've already tried to compute its first-direction. If
        // that failed, then kUnknown is the right answer.
        const SkPathFirstDirection first = SkPathConvexity_ToFirstDirection(convexity);
        return first;
    }

    // Note, this can compute a 'first' direction, even for non-convex shapes.
    const std::optional<SkPathRaw> raw = SkPathPriv::Raw(path, SkResolveConvexity::kNo);
    if (!raw.has_value()) {
        return SkPathFirstDirection::kUnknown;
    }
    const SkPathFirstDirection first = ComputeFirstDirection(raw.value());
    return first;
}

/*
 *  This returns a singleton instance which SkPath uses to signify that its pathdata is in error:
 *  either because the inputs were invalid (e.g. bad verbs), or its coordintes were non-finite
 *  (either from the client, or after a makeTransform() call). The singleton is intentionally
 *  never released.
 */
SkPathData* SkPath::PeekErrorSingleton() {
    static SkPathData* gErrorSingleton = SkPathData::MakeNoCheck({}, {}, {}, {}, {}).release();

    // Make sure MakeNoCheck() didn't alias us to the standard Empty instance. We want our
    // pointer to be distinct from that one.
    SkASSERT(gErrorSingleton != SkPathData::Empty().get());

    return gErrorSingleton;
}

SkPath SkPath::MakeNullCheck(sk_sp<SkPathData> pdata, SkPathFillType ft, bool isVolatile) {
    if (!pdata) {
        pdata = sk_ref_sp(PeekErrorSingleton());
    }
    const SkPath path(std::move(pdata), ft, isVolatile);
    return path;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

// The half-plane a*x + b*y + c >= 0, normalized so that (a, b) is a unit normal.
struct SkHalfPlane {
    SkScalar fA = 0;
    SkScalar fB = 0;
    SkScalar fC = 0;

    SkScalar eval(SkScalar x, SkScalar y) const {
        const SkScalar value = fA * x + fB * y + fC;
        return value;
    }

    // Returns false (and the always-positive plane) when the normal cannot be normalized.
    bool normalize() {
        double a = fA;
        double b = fB;
        double c = fC;
        const double dmag = sqrt(a * a + b * b);
        // length of initial plane normal is zero
        if (dmag == 0) {
           fA = 0;
           fB = 0;
           fC = SK_Scalar1;
           return true;
        }
        const double dscale = sk_ieee_double_divide(1.0, dmag);
        a *= dscale;
        b *= dscale;
        c *= dscale;
        // check if we're not finite, or normal is zero-length
        if (!SkIsFinite(a, b, c) ||
            (a == 0 && b == 0)) {
            fA = 0;
            fB = 0;
            fC = SK_Scalar1;
            return false;
        }
        fA = static_cast<SkScalar>(a);
        fB = static_cast<SkScalar>(b);
        fC = static_cast<SkScalar>(c);
        return true;
    }

    enum Result {
        kAllNegative,
        kAllPositive,
        kMixed
    };
    Result test(const SkRect& bounds) const {
        // check whether the diagonal aligned with the normal crosses the plane
        SkPoint diagMin{0, 0};
        SkPoint diagMax{0, 0};
        if (fA >= 0) {
            diagMin.fX = bounds.fLeft;
            diagMax.fX = bounds.fRight;
        } else {
            diagMin.fX = bounds.fRight;
            diagMax.fX = bounds.fLeft;
        }
        if (fB >= 0) {
            diagMin.fY = bounds.fTop;
            diagMax.fY = bounds.fBottom;
        } else {
            diagMin.fY = bounds.fBottom;
            diagMax.fY = bounds.fTop;
        }
        const SkScalar test = eval(diagMin.fX, diagMin.fY);
        const SkScalar sign = test*eval(diagMax.fX, diagMax.fY);
        if (sign > 0) {
            // the path is either all on one side of the half-plane or the other
            if (test < 0) {
                return kAllNegative;
            }
            return kAllPositive;
        }
        return kMixed;
    }
};

// Accumulates clipped contours into a builder.
struct ClipAccumulator {
    SkPathBuilder fResult{};
    SkPoint       fPrev = {0,0};
};

// Edge-clipper callback: appends one clipped contour of segments to the accumulator in ctx.
void append_clipped_contour(SkEdgeClipper* clipper, bool newCtr, void* ctx) {
    ClipAccumulator& rec = *static_cast<ClipAccumulator*>(ctx);

    bool addLineTo = false;
    SkPoint pts[4] = {};
    for (std::optional<SkPathVerb> verb = (*clipper).next(pts); verb.has_value();
         verb = (*clipper).next(pts)) {
        if (newCtr) {
            rec.fResult.moveTo(pts[0]);
            rec.fPrev = pts[0];
            newCtr = false;
        }

        if (addLineTo || pts[0] != rec.fPrev) {
            rec.fResult.lineTo(pts[0]);
        }

        switch (verb.value()) {
            case SkPathVerb::kLine:
                rec.fResult.lineTo(pts[1]);
                rec.fPrev = pts[1];
                break;
            case SkPathVerb::kQuad:
                rec.fResult.quadTo(pts[1], pts[2]);
                rec.fPrev = pts[2];
                break;
            case SkPathVerb::kCubic:
                rec.fResult.cubicTo(pts[1], pts[2], pts[3]);
                rec.fPrev = pts[3];
                break;
            default:
                // The clipper emits only lines, quads and cubics.
                break;
        }
        addLineTo = true;
    }
}

}  // namespace

// assumes plane is pre-normalized
static std::optional<SkPath> clip(const SkPath& path, const SkHalfPlane& plane) {
    SkMatrix mx{};
    const SkPoint p0 = { -plane.fA*plane.fC, -plane.fB*plane.fC };
    mx.setAll( plane.fB, plane.fA, p0.fX,
              -plane.fA, plane.fB, p0.fY,
                      0,        0,     1);
    const std::optional<SkMatrix> inv = mx.invert();
    if (!inv.has_value()) {
        return {};
    }

    const std::optional<SkPath> rotated = path.tryMakeTransform(inv.value());
    if (!rotated.has_value()) {
        return {};
    }
    const std::optional<SkPathRaw> raw = SkPathPriv::Raw(rotated.value(), SkResolveConvexity::kNo);
    if (!raw.has_value()) {
        SkASSERT(false);    // if rotated was valid, so should the raw
        return {};
    }

    const SkScalar big = SK_ScalarMax;
    const SkRect clip = {-big, 0, big, big };

    ClipAccumulator rec{};
    SkEdgeClipper::ClipPath(raw.value(), clip, false, append_clipped_contour, &rec);

    rec.fResult.setFillType(path.getFillType());
    const SkPath result = rec.fResult.detach(&mx);
    if (!result.isFinite()) {
        return {};
    }
    return result;
}

// true means we have written to clippedPath
bool SkPathPriv::PerspectiveClip(const SkPath& path, const SkMatrix& matrix, SkPath* clippedPath) {
    if (!matrix.hasPerspective()) {
        return false;
    }

    SkHalfPlane plane{
        matrix[SkMatrix::kMPersp0],
        matrix[SkMatrix::kMPersp1],
        matrix[SkMatrix::kMPersp2] - kW0PlaneDistance
    };
    if (plane.normalize()) {
        switch (plane.test(path.getBounds())) {
            case SkHalfPlane::kAllPositive:
                return false;
            case SkHalfPlane::kMixed: {
                const std::optional<SkPath> result = clip(path, plane);
                // An empty path means clipped out (or failed).
                *clippedPath = result.value_or(SkPath());
                return true;
            }
            case SkHalfPlane::kAllNegative:
                break; // handled outside of the switch
        }
    }
    // clipped out (or failed)
    *clippedPath = SkPath();
    return true;
}

std::optional<SkPathRectInfo> SkPathPriv::IsSimpleRect(const SkPath& path, bool isSimpleFill) {
    if (path.getSegmentMasks() != SkPath::kLine_SegmentMask) {
        return {};
    }
    SkPoint rectPts[5] = {};
    int rectPtCnt = 0;
    bool needsClose = !isSimpleFill;
    for (const SkPathPriv::VerbRecord record : SkPathPriv::Iterate(path)) {
        const SkPoint* verbPts = record.fPoints;
        switch (record.fVerb) {
            case SkPathVerb::kMove:
                if (0 != rectPtCnt) {
                    return {};
                }
                rectPts[0] = verbPts[0];
                ++rectPtCnt;
                break;
            case SkPathVerb::kLine:
                if (5 == rectPtCnt) {
                    return {};
                }
                rectPts[rectPtCnt] = verbPts[1];
                ++rectPtCnt;
                break;
            case SkPathVerb::kClose:
                if (4 == rectPtCnt) {
                    rectPts[4] = rectPts[0];
                    rectPtCnt = 5;
                }
                needsClose = false;
                break;
            case SkPathVerb::kQuad:
            case SkPathVerb::kConic:
            case SkPathVerb::kCubic:
                return {};
        }
    }
    if (needsClose) {
        return {};
    }
    if (rectPtCnt < 5) {
        return {};
    }
    if (rectPts[0] != rectPts[4]) {
        return {};
    }
    // Check for two cases of rectangles: pts 0 and 3 form a vertical edge or a horizontal edge (
    // and pts 1 and 2 the opposite vertical or horizontal edge).
    bool vec03IsVertical = false;
    if (rectPts[0].fX == rectPts[3].fX && rectPts[1].fX == rectPts[2].fX &&
        rectPts[0].fY == rectPts[1].fY && rectPts[3].fY == rectPts[2].fY) {
        // Make sure it has non-zero width and height
        if (rectPts[0].fX == rectPts[1].fX || rectPts[0].fY == rectPts[3].fY) {
            return {};
        }
        vec03IsVertical = true;
    } else if (rectPts[0].fY == rectPts[3].fY && rectPts[1].fY == rectPts[2].fY &&
               rectPts[0].fX == rectPts[1].fX && rectPts[3].fX == rectPts[2].fX) {
        // Make sure it has non-zero width and height
        if (rectPts[0].fY == rectPts[1].fY || rectPts[0].fX == rectPts[3].fX) {
            return {};
        }
        vec03IsVertical = false;
    } else {
        return {};
    }

    SkPathRectInfo info{};

    // Set sortFlags so that it has the low bit set if pt index 0 is on right edge and second bit
    // set if it is on the bottom edge.
    const unsigned sortFlags =
            ((rectPts[0].fX < rectPts[2].fX) ? 0b00 : 0b01) |
            ((rectPts[0].fY < rectPts[2].fY) ? 0b00 : 0b10);
    switch (sortFlags) {
        case 0b00:
            info.fRect.setLTRB(rectPts[0].fX, rectPts[0].fY, rectPts[2].fX, rectPts[2].fY);
            info.fDirection = vec03IsVertical ? SkPathDirection::kCW : SkPathDirection::kCCW;
            info.fStartIndex = 0;
            break;
        case 0b01:
            info.fRect.setLTRB(rectPts[2].fX, rectPts[0].fY, rectPts[0].fX, rectPts[2].fY);
            info.fDirection = vec03IsVertical ? SkPathDirection::kCCW : SkPathDirection::kCW;
            info.fStartIndex = 1;
            break;
        case 0b10:
            info.fRect.setLTRB(rectPts[0].fX, rectPts[2].fY, rectPts[2].fX, rectPts[0].fY);
            info.fDirection = vec03IsVertical ? SkPathDirection::kCCW : SkPathDirection::kCW;
            info.fStartIndex = 3;
            break;
        default:
            // sortFlags has two bits; 0b11 is the remaining case.
            info.fRect.setLTRB(rectPts[2].fX, rectPts[2].fY, rectPts[0].fX, rectPts[0].fY);
            info.fDirection = vec03IsVertical ? SkPathDirection::kCW : SkPathDirection::kCCW;
            info.fStartIndex = 2;
            break;
    }
    return info;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

SkPathEdgeIter::SkPathEdgeIter(const SkPathRaw& raw) {
    fPts = raw.fPoints.data();
    fMoveToPtr = fPts;
    fVerbs = raw.fVerbs.data();
    fVerbsStop = fVerbs + raw.fVerbs.size();
    fConicWeights = raw.fConics.data();
    fConicIndex = 0;

    fNeedsCloseLine = false;
    fNextIsNewContour = false;
    SkDEBUGCODE(fIsConic = false;)
}

SkPathEdgeIter::SkPathEdgeIter(const SkPath& path)
    : SkPathEdgeIter(SkPathPriv::Raw(path, SkResolveConvexity::kNo).value_or(SkPathRaw::Empty()))
{}
