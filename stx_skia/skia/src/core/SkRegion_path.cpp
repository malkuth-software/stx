/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkColor.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkScalar.h"
#include "include/core/SkTypes.h"
#include "include/private/SkDebug.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkMath.h"
#include "include/private/SkTDArray.h"
#include "include/private/SkTFitsIn.h"
#include "include/private/SkTo.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPathRaw.h"
#include "src/core/SkRegionPriv.h"
#include "src/core/SkSafeMath.h"
#include "src/core/SkScan.h"
#include "src/core/SkTSort.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

// The rgnbuilder caller *seems* to pass short counts, possible often seens early failure, so
// we may not want to promote this to a "std" routine just yet.
// The builder compares two distinct scanlines of its storage, so the ranges never overlap.
static bool sk_memeq32(const int32_t* SK_RESTRICT a, const int32_t* SK_RESTRICT b, int count) {
    for (int i = 0; i < count; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

/*
 *  Accumulates the spans of a non-anti-aliased path fill, which arrive in Y then X order, and
 *  converts them to region runs.
 *
 *  The working storage holds a sequence of scanlines. A scanline mimics a row in the region,
 *  nearly. A row in a region is:
 *      [Bottom IntervalCount [L R]... Sentinel]
 *  while a scanline is
 *      [LastY XCount [L R]... uninitialized]
 *  The two are the same length (which is good), but we have to transmute
 *  the scanline a little when we convert it to a region-row. A scanline is identified by the
 *  index of its LastY value in the storage.
 */
class SkRgnBuilder : public SkBlitter {
public:
    SkRgnBuilder() = default;
    ~SkRgnBuilder() override;

    SkRgnBuilder(const SkRgnBuilder&) = delete;
    SkRgnBuilder& operator=(const SkRgnBuilder&) = delete;

    // returns true if it could allocate the working storage needed
    bool init(int maxHeight, int maxTransitions, bool pathIsInverse);

    // Completes the current scanline after the last span.
    void done() {
        if (fCurrScanline != kNoScanline) {
            fStorage[fCurrScanline + kXCountOffset] = fCurrX - (fCurrScanline + kFirstXOffset);
            if (!(*this).collapsWithPrev()) { // flush the last line
                fCurrScanline = (*this).nextScanline(fCurrScanline);
            }
        }
    }

    int     computeRunCount() const;
    void    copyToRect(SkIRect*) const;
    void    copyToRgn(SkRegion::RunType runs[]) const;

    void blitH(int x, int y, int width) override;
    void blitAntiH(int x, int y, const SkAlpha antialias[], const int16_t runs[]) override {
        SkDEBUGFAIL("blitAntiH not implemented");
    }

private:
    static constexpr int kNoScanline = -1;
    static constexpr int kLastYOffset = 0;
    static constexpr int kXCountOffset = 1;
    static constexpr int kFirstXOffset = 2;

    // Owned working storage of fStorageCount values, released by the destructor.
    SkRegion::RunType*  fStorage = nullptr;
    int fStorageCount = 0;
    // The scanline receiving spans, or kNoScanline before the first span; after done(), the
    // index just past the last scanline.
    int fCurrScanline = kNoScanline;
    int fPrevScanline = kNoScanline;
    // Index of the next available x in fCurrScanline.
    int fCurrX = 0;
    SkRegion::RunType   fTop = 0;           // first Y value

    // The scanline after the one at line: its header, x values and the x-sentinel slot.
    int nextScanline(int line) const {
        const int next = line + kFirstXOffset + fStorage[line + kXCountOffset] + 1;
        return next;
    }

    bool collapsWithPrev() {
        if (fPrevScanline == kNoScanline) {
            return false;
        }
        const int xCount = fStorage[fCurrScanline + kXCountOffset];
        if (fStorage[fPrevScanline + kLastYOffset] + 1 == fStorage[fCurrScanline + kLastYOffset] &&
            fStorage[fPrevScanline + kXCountOffset] == xCount &&
            sk_memeq32(&fStorage[fPrevScanline + kFirstXOffset],
                       &fStorage[fCurrScanline + kFirstXOffset], xCount)) {
            // update the height of fPrevScanline
            fStorage[fPrevScanline + kLastYOffset] = fStorage[fCurrScanline + kLastYOffset];
            return true;
        }
        return false;
    }
};

SkRgnBuilder::~SkRgnBuilder() {
    sk_free(fStorage);
}

bool SkRgnBuilder::init(int maxHeight, int maxTransitions, bool pathIsInverse) {
    if ((maxHeight | maxTransitions) < 0) {
        return false;
    }

    SkSafeMath  safe;

    int transitions = maxTransitions;
    if (pathIsInverse) {
        // allow for additional X transitions to "invert" each scanline
        // [ L' ... normal transitions ... R' ]
        //
        transitions = safe.addInt(transitions, 2);
    }

    // compute the count with +1 and +3 slop for the working buffer
    size_t count = safe.mul(safe.addInt(maxHeight, 1), safe.addInt(3, transitions));

    if (pathIsInverse) {
        // allow for two "empty" rows for the top and bottom
        //      [ Y, 1, L, R, S] == 5 (*2 for top and bottom)
        count = safe.add(count, 10);
    }

    if (!safe || !SkTFitsIn<int32_t>(count)) {
        return false;
    }
    fStorageCount = SkToS32(count);

    void* storage = sk_malloc_canfail(static_cast<size_t>(fStorageCount),
                                      sizeof(SkRegion::RunType));
    fStorage = static_cast<SkRegion::RunType*>(storage);
    if (nullptr == fStorage) {
        return false;
    }

    fCurrScanline = kNoScanline;    // signal empty collection
    fPrevScanline = kNoScanline;    // signal first scanline
    return true;
}

void SkRgnBuilder::blitH(int x, int y, int width) {
    if (fCurrScanline == kNoScanline) {  // first time
        fTop = y;
        fCurrScanline = 0;
        fStorage[fCurrScanline + kLastYOffset] = y;
        fCurrX = fCurrScanline + kFirstXOffset;
    } else {
        SkASSERT(y >= fStorage[fCurrScanline + kLastYOffset]);

        if (y > fStorage[fCurrScanline + kLastYOffset]) {
            // if we get here, we're done with fCurrScanline
            fStorage[fCurrScanline + kXCountOffset] = fCurrX - (fCurrScanline + kFirstXOffset);

            const int prevLastY = fStorage[fCurrScanline + kLastYOffset];
            if (!(*this).collapsWithPrev()) {
                fPrevScanline = fCurrScanline;
                fCurrScanline = (*this).nextScanline(fCurrScanline);
            }
            if (y - 1 > prevLastY) {  // insert empty run
                fStorage[fCurrScanline + kLastYOffset] = y - 1;
                fStorage[fCurrScanline + kXCountOffset] = 0;
                fCurrScanline = (*this).nextScanline(fCurrScanline);
            }
            // setup for the new curr line
            fStorage[fCurrScanline + kLastYOffset] = y;
            fCurrX = fCurrScanline + kFirstXOffset;
        }
    }
    //  check if we should extend the current run, or add a new one
    if (fCurrX > fCurrScanline + kFirstXOffset && fStorage[fCurrX - 1] == x) {
        fStorage[fCurrX - 1] = x + width;
    } else {
        // The new pair and the scanline's sentinel slot after it must fit. This also covers the
        // scanline headers written above, which precede the pair.
        SkASSERT_RELEASE(fCurrX + 2 < fStorageCount);
        fStorage[fCurrX] = x;
        fStorage[fCurrX + 1] = x + width;
        fCurrX += 2;
    }
}

int SkRgnBuilder::computeRunCount() const {
    if (fCurrScanline == kNoScanline) {
        return 0;
    }

    // The top value and the final y-sentinel, plus scanlines that are as long as region rows.
    const int count = 2 + fCurrScanline;
    return count;
}

void SkRgnBuilder::copyToRect(SkIRect* r) const {
    SkASSERT(fCurrScanline != kNoScanline);
    // A rect's scanline is [bottom intervals left right sentinel] == 5
    SkASSERT(fCurrScanline == 5);

    const int line = 0;
    SkASSERT(fStorage[line + kXCountOffset] == 2);

    (*r).setLTRB(fStorage[line + kFirstXOffset], fTop, fStorage[line + kFirstXOffset + 1],
                 fStorage[line + kLastYOffset] + 1);
}

// Writes computeRunCount() values into runs.
void SkRgnBuilder::copyToRgn(SkRegion::RunType runs[]) const {
    SkASSERT(fCurrScanline != kNoScanline);
    SkASSERT(fCurrScanline > 4);

    int line = 0;
    const int stop = fCurrScanline;
    int out = 0;

    runs[out] = fTop;
    out += 1;
    do {
        runs[out] = fStorage[line + kLastYOffset] + 1;
        out += 1;
        const int count = fStorage[line + kXCountOffset];
        runs[out] = count >> 1;   // intervalCount
        out += 1;
        if (count) {
            const size_t xBytes = static_cast<size_t>(count) * sizeof(SkRegion::RunType);
            memcpy(&runs[out], &fStorage[line + kFirstXOffset], xBytes);
            out += count;
        }
        runs[out] = SkRegion_kRunTypeSentinel;
        out += 1;
        line = (*this).nextScanline(line);
    } while (line < stop);
    SkASSERT(line == stop);
    runs[out] = SkRegion_kRunTypeSentinel;
}

// The index of the verb's last point, which ends the segment.
static unsigned verb_to_initial_last_index(SkPathVerb verb) {
    switch (verb) {
        case SkPathVerb::kMove:  return 0;
        case SkPathVerb::kLine:  return 1;
        case SkPathVerb::kQuad:  return 2;
        case SkPathVerb::kConic: return 2;
        case SkPathVerb::kCubic: return 3;
        case SkPathVerb::kClose: return 0;
    }
    // An unknown verb contributes no points.
    return 0;
}

// The most monotonic edges the verb's segment can produce.
static unsigned verb_to_max_edges(SkPathVerb verb) {
    switch (verb) {
        case SkPathVerb::kMove:  return 0;
        case SkPathVerb::kLine:  return 1;
        case SkPathVerb::kQuad:  return 2;
        case SkPathVerb::kConic: return 2;
        case SkPathVerb::kCubic: return 3;
        case SkPathVerb::kClose: return 0;
    }
    // An unknown verb contributes no edges.
    return 0;
}

// If returns 0, ignore itop and ibot
//
// The forced close revisits each contour's first point as a line end, so the extreme values
// reach both tests below even though each point updates only one of them.
static int count_path_runtype_values(const SkPath& path, int* itop, int* ibot) {
    int maxEdges = 0;
    SkScalar    top = SkIntToScalar(SK_MaxS16);
    SkScalar    bot = SkIntToScalar(SK_MinS16);

    SkPath::Iter iter(path, true);
    std::optional<SkPath::IterRec> rec = iter.next();
    while (rec.has_value()) {
        const SkPathVerb verb = (*rec).fVerb;
        const SkSpan<const SkPoint> pts = (*rec).fPoints;

        maxEdges += verb_to_max_edges(verb);

        const int lastIndex = verb_to_initial_last_index(verb);
        if (lastIndex > 0) {
            for (int i = 1; i <= lastIndex; i++) {
                if (top > pts[i].fY) {
                    top = pts[i].fY;
                } else if (bot < pts[i].fY) {
                    bot = pts[i].fY;
                }
            }
        } else if (SkPathVerb::kMove == verb) {
            if (top > pts[0].fY) {
                top = pts[0].fY;
            } else if (bot < pts[0].fY) {
                bot = pts[0].fY;
            }
        }
        rec = iter.next();
    }
    if (0 == maxEdges) {
        return 0;   // we have only moves+closes
    }

    SkASSERT(top <= bot);
    *itop = SkScalarRoundToInt(top);
    *ibot = SkScalarRoundToInt(bot);
    return maxEdges;
}

static bool check_inverse_on_empty_return(SkRegion* dst, const SkPath& path, const SkRegion& clip) {
    if (path.isInverseFillType()) {
        const bool nonEmpty = (*dst).set(clip);
        return nonEmpty;
    }
    const bool nonEmpty = (*dst).setEmpty();
    return nonEmpty;
}

bool SkRegion::setPath(const SkPath& path, const SkRegion& clip) {
#ifdef SK_DEBUG
    SkRegionPriv::Validate(*this);
#endif

    const std::optional<SkPathRaw> raw = SkPathPriv::Raw(path, SkResolveConvexity::kYes);

    if (clip.isEmpty() || !raw.has_value() || path.isEmpty()) {
        // This treats non-finite paths (no raw) as empty as well, so this returns empty or 'clip'
        // if it's inverse-filled. If clip is also empty, path's fill type doesn't really matter
        // and this region ends up empty.
        return check_inverse_on_empty_return(this, path, clip);
    }

    // Our builder is very fragile, and can't be called with spans/rects out of Y->X order.
    // To ensure this, we only "fill" clipped to a rect (the clip's bounds), and if the
    // clip is more complex than that, we just post-intersect the result with the clip.
    const SkIRect clipBounds = clip.getBounds();
    if (clip.isComplex()) {
        if (!(*this).setPath(path, SkRegion(clipBounds))) {
            return false;
        }
        const bool nonEmpty = (*this).op(clip, kIntersect_Op);
        return nonEmpty;
    }

    // SkScan::FillPath has limits on the coordinate range of the clipping SkRegion. If it's too
    // big, tile the clip bounds and union the pieces back together.
    if (SkScan::PathRequiresTiling(clipBounds)) {
        static constexpr int kTileSize = 32767 >> 1; // Limit so coords can fit into SkFixed (16.16)
        const SkIRect pathBounds = path.getBounds().roundOut();

        (*this).setEmpty();

        SkIRect intersection = {0, 0, 0, 0};
        if (!intersection.intersect(pathBounds, clipBounds)) {
            return false;
        }

        // Note: With large integers some intermediate calculations can overflow, but the
        // end results will still be in integer range. Using int64_t for the intermediate
        // values will handle this situation.
        for (int64_t top = intersection.fTop; top < intersection.fBottom; top += kTileSize) {
            const int64_t bot = std::min(top + kTileSize, static_cast<int64_t>(intersection.fBottom));
            for (int64_t left = intersection.fLeft; left < intersection.fRight; left += kTileSize) {
                const int64_t right =
                        std::min(left + kTileSize, static_cast<int64_t>(intersection.fRight));

                SkIRect tileClipBounds = {static_cast<int>(left), static_cast<int>(top),
                                          static_cast<int>(right), static_cast<int>(bot)};
                SkASSERT(SkIRect::Intersects(pathBounds, tileClipBounds));

                // Shift coordinates so the top left is (0,0) during scan conversion and then
                // translate the SkRegion afterwards.
                tileClipBounds.offset(static_cast<int32_t>(-left), static_cast<int32_t>(-top));
                SkASSERT(!SkScan::PathRequiresTiling(tileClipBounds));
                const std::optional<SkPath> newpath =
                        path.tryMakeOffset(static_cast<float>(-left), static_cast<float>(-top));
                if (!newpath.has_value()) {
                    return false;
                }
                SkRegion tile;
                tile.setPath(*newpath, SkRegion(tileClipBounds));
                tile.translate(static_cast<int>(left), static_cast<int>(top));
                (*this).op(tile, kUnion_Op);
            }
        }
        // During tiling we only applied the bounds of the tile, now that we have a full SkRegion,
        // apply the original clip.
        const bool nonEmpty = (*this).op(clip, kIntersect_Op);
        return nonEmpty;
    }

    //  compute worst-case rgn-size for the path
    int pathTop = 0;
    int pathBot = 0;
    const int pathTransitions = count_path_runtype_values(path, &pathTop, &pathBot);
    if (0 == pathTransitions) {
        return check_inverse_on_empty_return(this, path, clip);
    }

    int clipTop = 0;
    int clipBot = 0;
    const int clipTransitions = clip.count_runtype_values(&clipTop, &clipBot);

    const int top = std::max(pathTop, clipTop);
    const int bot = std::min(pathBot, clipBot);
    if (top >= bot) {
        return check_inverse_on_empty_return(this, path, clip);
    }

    SkRgnBuilder builder;

    if (!builder.init(bot - top,
                      std::max(pathTransitions, clipTransitions),
                      path.isInverseFillType())) {
        // can't allocate working space, so return false
        return (*this).setEmpty();
    }

    SkScan::FillPath(*raw, clip, &builder);
    builder.done();

    const int count = builder.computeRunCount();
    if (count == 0) {
        return (*this).setEmpty();
    }
    if (count == kRectRegionRuns) {
        builder.copyToRect(&fBounds);
        (*this).setRect(fBounds);
    } else {
        SkRegion tmp;

        tmp.fRunHead = RunHead::Alloc(count);
        builder.copyToRgn((*tmp.fRunHead).writable_runs());
        (*tmp.fRunHead).computeRunBounds(&tmp.fBounds);
        (*this).swap(tmp);
    }
#ifdef SK_DEBUG
    SkRegionPriv::Validate(*this);
#endif
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////

// A vertical side of one region rectangle, directed so the boundary keeps the region on one
// side. fNext links the sides of one boundary contour in order.
struct Edge {
    enum {
        kY0Link = 0x01,
        kY1Link = 0x02,

        kCompleteLink = (kY0Link | kY1Link)
    };

    SkRegionPriv::RunType fX = 0;
    SkRegionPriv::RunType fY0 = 0;
    SkRegionPriv::RunType fY1 = 0;
    uint8_t fFlags = 0;
    // Borrows another edge of the same array.
    Edge*   fNext = nullptr;

    void set(int x, int y0, int y1) {
        SkASSERT(y0 != y1);

        fX = x;
        fY0 = y0;
        fY1 = y1;
        fFlags = 0;
        fNext = nullptr;
    }

    int top() const {
        const int edgeTop = std::min(fY0, fY1);
        return edgeTop;
    }
};

// Links base to the following edges that continue its ends. Edges come from a valid region, so
// each unlinked end has a partner later in the sorted array; the searches are bounded by stop
// and stop in Release if the partner is missing.
static void find_link(Edge* base, Edge* stop) {
    SkASSERT(base < stop);

    if ((*base).fFlags == Edge::kCompleteLink) {
        SkASSERT((*base).fNext);
        return;
    }

    SkASSERT(base + 1 < stop);

    const int y0 = (*base).fY0;
    const int y1 = (*base).fY1;

    if (((*base).fFlags & Edge::kY0Link) == 0) {
        bool linked = false;
        for (Edge* e = base + 1; e < stop; ++e) {
            if (((*e).fFlags & Edge::kY1Link) == 0 && y0 == (*e).fY1) {
                SkASSERT(nullptr == (*e).fNext);
                (*e).fNext = base;
                (*e).fFlags = SkToU8((*e).fFlags | Edge::kY1Link);
                linked = true;
                break;
            }
        }
        SkASSERT_RELEASE(linked);
    }

    if (((*base).fFlags & Edge::kY1Link) == 0) {
        bool linked = false;
        for (Edge* e = base + 1; e < stop; ++e) {
            if (((*e).fFlags & Edge::kY0Link) == 0 && y1 == (*e).fY0) {
                SkASSERT(nullptr == (*base).fNext);
                (*base).fNext = e;
                (*e).fFlags = SkToU8((*e).fFlags | Edge::kY0Link);
                linked = true;
                break;
            }
        }
        SkASSERT_RELEASE(linked);
    }

    (*base).fFlags = Edge::kCompleteLink;
}

// Appends the first contour whose edges are still unused and marks its edges used (flags 0).
// Returns the number of edges in the contour.
static int extract_path(Edge* edge, Edge* stop, SkPathBuilder* builder) {
    while (edge < stop && 0 == (*edge).fFlags) {
        edge++; // skip over "used" edges
    }

    SkASSERT_RELEASE(edge < stop);

    Edge* base = edge;
    Edge* prev = edge;
    edge = (*edge).fNext;
    SkASSERT(edge != base);

    int count = 1;
    (*builder).moveTo(SkIntToScalar((*prev).fX), SkIntToScalar((*prev).fY0));
    (*prev).fFlags = 0;
    do {
        if ((*prev).fX != (*edge).fX || (*prev).fY1 != (*edge).fY0) { // skip collinear
            (*builder).lineTo(SkIntToScalar((*prev).fX), SkIntToScalar((*prev).fY1));    // V
            (*builder).lineTo(SkIntToScalar((*edge).fX), SkIntToScalar((*edge).fY0));    // H
        }
        prev = edge;
        edge = (*edge).fNext;
        count += 1;
        (*prev).fFlags = 0;
    } while (edge != base);
    (*builder).lineTo(SkIntToScalar((*prev).fX), SkIntToScalar((*prev).fY1));    // V
    (*builder).close();
    return count;
}

// Orders edges by x, then by top.
struct EdgeLT {
    bool operator()(const Edge& a, const Edge& b) const {
        if (a.fX == b.fX) {
            const bool less = a.top() < b.top();
            return less;
        }
        const bool less = a.fX < b.fX;
        return less;
    }
};

bool SkRegion::addBoundaryPath(SkPathBuilder* builder) const {
    // path could safely be nullptr if we're empty, but the caller shouldn't
    // *know* that
    SkASSERT(builder);

    if ((*this).isEmpty()) {
        return false;
    }

    const SkIRect& bounds = (*this).getBounds();

    if ((*this).isRect()) {
        const SkRect r = SkRect::Make(bounds);      // this converts the ints to scalars
        (*builder).addRect(r);
        return true;
    }

    SkRegion::Iterator  iter(*this);
    SkTDArray<Edge>     edges;

    while (!iter.done()) {
        const SkIRect& r = iter.rect();
        Edge* edge = edges.append(2);
        edge[0].set(r.fLeft, r.fBottom, r.fTop);
        edge[1].set(r.fRight, r.fTop, r.fBottom);
        iter.next();
    }

    int count = edges.size();
    Edge* start = edges.begin();
    Edge* stop = start + count;
    SkTQSort<Edge>(start, stop, EdgeLT());

    for (Edge* e = start; e != stop; e++) {
        find_link(e, stop);
    }

#ifdef SK_DEBUG
    for (Edge* e = start; e != stop; e++) {
        SkASSERT((*e).fNext != nullptr);
        SkASSERT((*e).fFlags == Edge::kCompleteLink);
    }
#endif

    (*builder).incReserve(count << 1);
    do {
        SkASSERT(count > 1);
        count -= extract_path(start, stop, builder);
    } while (count > 0);

    return true;
}

SkPath SkRegion::getBoundaryPath() const {
    SkPathBuilder builder;
    (void)(*this).addBoundaryPath(&builder);
    SkPath path = builder.detach();
    return path;
}
