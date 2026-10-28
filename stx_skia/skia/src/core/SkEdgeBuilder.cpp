/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkEdgeBuilder.h"

#include "include/core/SkPath.h"
#include "include/core/SkPoint.h"
#include "include/core/SkTypes.h"
#include "include/private/SkDebug.h"
#include "include/private/SkFixed.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkSafe32.h"
#include "src/core/SkAnalyticEdge.h"
#include "src/core/SkEdge.h"
#include "src/core/SkEdgeClipper.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkLineClipper.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkSafeMath.h"

#include <optional>

SkEdgeBuilder::Combine SkBasicEdgeBuilder::combineVertical(const SkEdge* edge, SkEdge* last) {
    const SkEdge& e = *edge;
    SkEdge& l = *last;
    // We only consider edges that were originally lines to be vertical to avoid numerical issues
    // (crbug.com/1154864).
    if (l.fEdgeType != SkEdge::Type::kLine || l.fDxDy != 0 || e.fX != l.fX) {
        return kNo_Combine;
    }
    if (e.fWinding == l.fWinding) {
        if (e.fLastY + 1 == l.fFirstY) {
            l.fFirstY = e.fFirstY;
            return kPartial_Combine;
        }
        if (e.fFirstY == l.fLastY + 1) {
            l.fLastY = e.fLastY;
            return kPartial_Combine;
        }
        return kNo_Combine;
    }
    if (e.fFirstY == l.fFirstY) {
        if (e.fLastY == l.fLastY) {
            return kTotal_Combine;
        }
        if (e.fLastY < l.fLastY) {
            l.fFirstY = e.fLastY + 1;
            return kPartial_Combine;
        }
        l.fFirstY = l.fLastY + 1;
        l.fLastY = e.fLastY;
        l.fWinding = e.fWinding;
        return kPartial_Combine;
    }
    if (e.fLastY == l.fLastY) {
        if (e.fFirstY > l.fFirstY) {
            l.fLastY = e.fFirstY - 1;
            return kPartial_Combine;
        }
        l.fLastY = l.fFirstY - 1;
        l.fFirstY = e.fFirstY;
        l.fWinding = e.fWinding;
        return kPartial_Combine;
    }
    return kNo_Combine;
}

static bool approximately_equal(SkFixed a, SkFixed b) {
    const bool equal = SkAbs32(a - b) < 0x100;
    return equal;
}

SkEdgeBuilder::Combine SkAnalyticEdgeBuilder::combineVertical(const SkAnalyticEdge* edge,
                                                              SkAnalyticEdge* last) {
    const SkAnalyticEdge& e = *edge;
    SkAnalyticEdge& l = *last;
    // We only consider edges that were originally lines to be vertical to avoid numerical issues
    // (crbug.com/1154864).
    if (l.fEdgeType != SkAnalyticEdge::Type::kLine || l.fDX != 0 || e.fX != l.fX) {
        return kNo_Combine;
    }
    if (e.fWinding == l.fWinding) {
        if (e.fLowerY == l.fUpperY) {
            l.fUpperY = e.fUpperY;
            l.fY = l.fUpperY;
            return kPartial_Combine;
        }
        if (approximately_equal(e.fUpperY, l.fLowerY)) {
            l.fLowerY = e.fLowerY;
            return kPartial_Combine;
        }
        return kNo_Combine;
    }
    if (approximately_equal(e.fUpperY, l.fUpperY)) {
        if (approximately_equal(e.fLowerY, l.fLowerY)) {
            return kTotal_Combine;
        }
        if (e.fLowerY < l.fLowerY) {
            l.fUpperY = e.fLowerY;
            l.fY = l.fUpperY;
            return kPartial_Combine;
        }
        l.fUpperY = l.fLowerY;
        l.fY = l.fUpperY;
        l.fLowerY = e.fLowerY;
        l.fWinding = e.fWinding;
        return kPartial_Combine;
    }
    if (approximately_equal(e.fLowerY, l.fLowerY)) {
        if (e.fUpperY > l.fUpperY) {
            l.fLowerY = e.fUpperY;
            return kPartial_Combine;
        }
        l.fLowerY = l.fUpperY;
        l.fUpperY = e.fUpperY;
        l.fY = l.fUpperY;
        l.fWinding = e.fWinding;
        return kPartial_Combine;
    }
    return kNo_Combine;
}

static bool is_vertical(const SkEdge& edge) {
    // We only consider edges that were originally lines to be vertical to avoid numerical issues
    // (crbug.com/1154864).
    const bool vertical = edge.fDxDy == 0 && edge.fEdgeType == SkEdge::Type::kLine;
    return vertical;
}

static bool is_vertical(const SkAnalyticEdge& edge) {
    // We only consider edges that were originally lines to be vertical to avoid numerical issues
    // (crbug.com/1154864).
    const bool vertical = edge.fDX == 0 && edge.fEdgeType == SkAnalyticEdge::Type::kLine;
    return vertical;
}

// TODO: we can deallocate the edge if edge->setFoo() fails
// or when we don't use it (kPartial_Combine or kTotal_Combine).

void SkBasicEdgeBuilder::addLine(const SkPoint pts[]) {
    SkEdge* edge = fAlloc.make<SkEdge>();
    if ((*edge).setLine(pts[0], pts[1])) {
        const Combine combine = is_vertical(*edge) && !fList.empty()
            ? combineVertical(edge, static_cast<SkEdge*>(fList.back()))
            : kNo_Combine;

        switch (combine) {
            case kTotal_Combine:    fList.pop_back();      break;
            case kPartial_Combine:                         break;
            case kNo_Combine:       fList.push_back(edge); break;
        }
    }
}
void SkAnalyticEdgeBuilder::addLine(const SkPoint pts[]) {
    SkAnalyticEdge* edge = fAlloc.make<SkAnalyticEdge>();
    if ((*edge).setLine(pts[0], pts[1])) {

        const Combine combine = is_vertical(*edge) && !fList.empty()
            ? combineVertical(edge, static_cast<SkAnalyticEdge*>(fList.back()))
            : kNo_Combine;

        switch (combine) {
            case kTotal_Combine:    fList.pop_back();      break;
            case kPartial_Combine:                         break;
            case kNo_Combine:       fList.push_back(edge); break;
        }
    }
}
void SkBasicEdgeBuilder::addQuad(const SkPoint pts[]) {
    SkQuadraticEdge* edge = fAlloc.make<SkQuadraticEdge>();
    if ((*edge).setQuadratic(pts)) {
        fList.push_back(edge);
    }
}
void SkAnalyticEdgeBuilder::addQuad(const SkPoint pts[]) {
    SkAnalyticQuadraticEdge* edge = fAlloc.make<SkAnalyticQuadraticEdge>();
    if ((*edge).setQuadratic(pts)) {
        fList.push_back(edge);
    }
}

void SkBasicEdgeBuilder::addCubic(const SkPoint pts[]) {
    SkCubicEdge* edge = fAlloc.make<SkCubicEdge>();
    if ((*edge).setCubic(pts)) {
        fList.push_back(edge);
    }
}
void SkAnalyticEdgeBuilder::addCubic(const SkPoint pts[]) {
    SkAnalyticCubicEdge* edge = fAlloc.make<SkAnalyticCubicEdge>();
    if ((*edge).setCubic(pts)) {
        fList.push_back(edge);
    }
}

SkRect SkBasicEdgeBuilder::recoverClip(const SkIRect& src) const {
    const SkRect clip = SkRect::Make(src);
    return clip;
}
SkRect SkAnalyticEdgeBuilder::recoverClip(const SkIRect& src) const {
    const SkRect clip = SkRect::Make(src);
    return clip;
}

// TODO: maybe get rid of buildPoly() entirely?
int SkEdgeBuilder::buildPoly(const SkPathRaw& raw, const SkIRect* iclip, bool canCullToTheRight) {
    if (iclip) {
        // clipping can turn 1 line into (up to) kMaxClippedLineSegments, since
        // we turn portions that are clipped out on the left/right into vertical
        // segments. Give up if that bound on the edge count is not representable.
        SkSafeMath safe;
        static_cast<void>(safe.mul(raw.fPoints.size(), SkLineClipper::kMaxClippedLineSegments));
        if (!safe) {
            return 0;
        }
    }

    SkPathEdgeIter iter(raw);
    if (iclip) {
        const SkRect clip = recoverClip(*iclip);

        for (SkPathEdgeIter::Result e = iter.next(); e; e = iter.next()) {
            switch (e.fEdge) {
                case SkPathEdgeIter::Edge::kLine: {
                    SkPoint lines[SkLineClipper::kMaxPoints] = {};
                    const int lineCount =
                            SkLineClipper::ClipLine(e.fPts, clip, lines, canCullToTheRight);
                    SkASSERT(lineCount <= SkLineClipper::kMaxClippedLineSegments);
                    for (int i = 0; i < lineCount; i++) {
                        addLine(&lines[i]);
                    }
                    break;
                }
                default:
                    SkDEBUGFAIL("unexpected verb");
                    break;
            }
        }
    } else {
        for (SkPathEdgeIter::Result e = iter.next(); e; e = iter.next()) {
            switch (e.fEdge) {
                case SkPathEdgeIter::Edge::kLine: {
                    addLine(e.fPts);
                    break;
                }
                default:
                    SkDEBUGFAIL("unexpected verb");
                    break;
            }
        }
    }
    fEdgeList = fList.begin();
    const int count = fList.size();
    return count;
}

namespace {

// State for adding clipped segments to a builder through SkEdgeClipper::ClipPath.
struct ClipRec {
    SkEdgeBuilder* fBuilder = nullptr;
    bool           fIsFinite = true;
};

}  // namespace

void SkEdgeBuilder::AddClippedSegments(SkEdgeClipper* clipper, bool, void* ctx) {
    ClipRec& rec = *static_cast<ClipRec*>(ctx);
    SkPoint pts[4] = {};

    for (std::optional<SkPathVerb> verb = (*clipper).next(pts); verb.has_value();
         verb = (*clipper).next(pts)) {
        const int count = SkPathPriv::PtsInIter(verb.value());
        for (int i = 0; i < count; ++i) {
            if (!SkIsFinite(pts[i].fX, pts[i].fY)) {
                rec.fIsFinite = false;
                return;
            }
        }
        switch (verb.value()) {
            case SkPathVerb::kLine:  (*rec.fBuilder).addLine (pts); break;
            case SkPathVerb::kQuad:  (*rec.fBuilder).addQuad (pts); break;
            case SkPathVerb::kCubic: (*rec.fBuilder).addCubic(pts); break;
            default: break;
        }
    }
}

void SkEdgeBuilder::addMonotonicQuads(const SkPoint pts[3]) {
    SkPoint monoX[5] = {};
    const int n = SkChopQuadAtYExtrema(pts, monoX);
    for (int i = 0; i <= n; i++) {
        addQuad(&monoX[i * 2]);
    }
}

int SkEdgeBuilder::build(const SkPathRaw& raw, const SkIRect* iclip, bool canCullToTheRight) {
    if (iclip) {
        const SkRect clip = recoverClip(*iclip);
        ClipRec rec{};
        rec.fBuilder = this;

        SkEdgeClipper::ClipPath(raw, clip, canCullToTheRight, AddClippedSegments, &rec);
        fEdgeList = fList.begin();
        const int count = rec.fIsFinite ? fList.size() : 0;
        return count;
    }

    SkPathEdgeIter iter(raw);
    SkAutoConicToQuads quadder;
    constexpr float kConicTol = 0.25f;
    SkPoint monoY[10] = {};

    for (SkPathEdgeIter::Result e = iter.next(); e; e = iter.next()) {
        switch (e.fEdge) {
            case SkPathEdgeIter::Edge::kLine:
                addLine(e.fPts);
                break;
            case SkPathEdgeIter::Edge::kQuad: {
                addMonotonicQuads(e.fPts);
                break;
            }
            case SkPathEdgeIter::Edge::kConic: {
                const SkPoint* quadPts =
                        quadder.computeQuads(e.fPts, iter.conicWeight(), kConicTol);
                for (int i = 0; i < quadder.countQuads(); ++i) {
                    addMonotonicQuads(&quadPts[2 * i]);
                }
            } break;
            case SkPathEdgeIter::Edge::kCubic: {
                const int n = SkChopCubicAtYExtrema(e.fPts, monoY);
                for (int i = 0; i <= n; i++) {
                    addCubic(&monoY[i * 3]);
                }
                break;
            }
            default:
                SkDEBUGFAIL("Unknown edge type");
                break;
        }
    }
    fEdgeList = fList.begin();
    const int count = fList.size();
    return count;
}

int SkEdgeBuilder::buildEdges(const SkPathRaw& raw,
                              const SkIRect* shiftedClip) {
    // If we're convex, then we need both edges, even if the right edge is past the clip.
    const bool canCullToTheRight = !raw.isKnownToBeConvex();

    // We can use our buildPoly() optimization if all the segments are lines.
    // (Edges are homogeneous and stored contiguously in memory, no need for indirection.)
    const int count = SkPath::kLine_SegmentMask == raw.segmentMasks()
        ? buildPoly(raw, shiftedClip, canCullToTheRight)
        : build    (raw, shiftedClip, canCullToTheRight);

    SkASSERT(count >= 0);

    // If we can't cull to the right, we should have count > 1 (or 0).
    if (!canCullToTheRight) {
        SkASSERT(count != 1);
    }
    return count;
}

int SkEdgeBuilder::buildEdges(const SkPath& path, const SkIRect* shiftedClip) {
    const std::optional<SkPathRaw> raw = SkPathPriv::Raw(path, SkResolveConvexity::kYes);
    if (raw.has_value()) {
        const int count = buildEdges(raw.value(), shiftedClip);
        return count;
    }
    return 0;   // no edges were built
}
