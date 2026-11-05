/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SkEdgeBuilder_DEFINED
#define SkEdgeBuilder_DEFINED

#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/private/SkAssert.h"
#include "include/private/SkTDArray.h"
#include "src/core/SkArenaAlloc.h"

#include <cstddef>

class SkEdge;
class SkEdgeClipper;
class SkPath;
struct SkPathRaw;
struct SkAnalyticEdge;

// Builds the edges of a path for scan conversion. Edges live in this builder's arena; the
// edge lists returned by the subclasses borrow them until the builder is destroyed.
class SkEdgeBuilder {
public:
    int buildEdges(const SkPathRaw&, const SkIRect* shiftedClip);
    int buildEdges(const SkPath&, const SkIRect* shiftedClip);

protected:
    SkEdgeBuilder() = default;
    virtual ~SkEdgeBuilder() = default;

    // Points to the head of fList once the edges are built. Every element is the subclass's
    // edge type, stored as void*.
    void**              fEdgeList = nullptr;
    SkTDArray<void*>    fList;
    SkSTArenaAlloc<512> fAlloc;

    enum Combine {
        kNo_Combine,
        kPartial_Combine,
        kTotal_Combine
    };

private:
    int build    (const SkPathRaw&, const SkIRect* clip, bool clipToTheRight);
    int buildPoly(const SkPathRaw&, const SkIRect* clip, bool clipToTheRight);

    // SkEdgeClipper::ClipPath consumer; ctx is the build's clip record.
    static void AddClippedSegments(SkEdgeClipper* clipper, bool newContour, void* ctx);
    // Splits a quadratic into Y-monotonic pieces and adds each.
    void addMonotonicQuads(const SkPoint pts[3]);

    virtual SkRect recoverClip(const SkIRect&) const = 0;
    // Appends the edges for one clipped or monotonic segment.
    virtual void addLine (const SkPoint pts[]) = 0;
    virtual void addQuad (const SkPoint pts[]) = 0;
    virtual void addCubic(const SkPoint pts[]) = 0;
};

class SkBasicEdgeBuilder final : public SkEdgeBuilder {
public:
    SkBasicEdgeBuilder() = default;

    // Borrows this builder's edges; valid until the builder is destroyed.
    SkEdge** edgeList() {
        SkEdge** list = reinterpret_cast<SkEdge**>(fEdgeList);
        return list;
    }

private:
    Combine combineVertical(const SkEdge* edge, SkEdge* last);

    SkRect recoverClip(const SkIRect&) const override;
    void addLine (const SkPoint pts[]) override;
    void addQuad (const SkPoint pts[]) override;
    void addCubic(const SkPoint pts[]) override;
};

class SkAnalyticEdgeBuilder final : public SkEdgeBuilder {
public:
    SkAnalyticEdgeBuilder() = default;

    // Borrows this builder's edges; valid until the builder is destroyed.
    SkAnalyticEdge** analyticEdgeList() {
        SkAnalyticEdge** list = reinterpret_cast<SkAnalyticEdge**>(fEdgeList);
        return list;
    }

private:
    Combine combineVertical(const SkAnalyticEdge* edge, SkAnalyticEdge* last);

    SkRect recoverClip(const SkIRect&) const override;
    void addLine (const SkPoint pts[]) override;
    void addQuad (const SkPoint pts[]) override;
    void addCubic(const SkPoint pts[]) override;
};

#endif
