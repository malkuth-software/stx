/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkColor.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkScalar.h"
#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h"
#include "include/private/SkFixed.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkMath.h"
#include "include/private/SkSafe32.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkEdge.h"
#include "src/core/SkEdgeBuilder.h"
#include "src/core/SkFDot6.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPathRawShapes.h"
#include "src/core/SkRasterClip.h"
#include "src/core/SkRectPriv.h"
#include "src/core/SkScan.h"
#include "src/core/SkScanPriv.h"
#include "src/core/SkTSort.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

struct SkMask;

// First rows of the sentinels bracketing the sorted edge list.
static constexpr int kEDGE_HEAD_Y = SK_MinS32;
static constexpr int kEDGE_TAIL_Y = SK_MaxS32;

#ifdef SK_DEBUG
    static void validate_sort(const SkEdge* edge) {
        int y = kEDGE_HEAD_Y;

        while ((*edge).fFirstY != SK_MaxS32) {
            (*edge).validate();
            SkASSERT(y <= (*edge).fFirstY);

            y = (*edge).fFirstY;
            edge = (*edge).fNext;
        }
    }
#else
    static void validate_sort(const SkEdge*) {}
#endif

// Moves the edges whose first row is curr_y (starting at newEdge) into x order.
static void insert_new_edges(SkEdge* newEdge, int curr_y) {
    if ((*newEdge).fFirstY != curr_y) {
        return;
    }
    SkEdge* prev = (*newEdge).fPrev;
    if ((*prev).fX <= (*newEdge).fX) {
        return;
    }
    // find first x pos to insert
    SkEdge* start = backward_insert_start(prev, (*newEdge).fX);
    // insert the lot, fixing up the links as we go
    do {
        SkEdge* next = (*newEdge).fNext;
        bool alreadyInPlace = false;
        for (;;) {
            if ((*start).fNext == newEdge) {
                alreadyInPlace = true;
                break;
            }
            SkEdge* after = (*start).fNext;
            if ((*after).fX >= (*newEdge).fX) {
                break;
            }
            start = after;
        }
        if (!alreadyInPlace) {
            remove_edge(newEdge);
            insert_edge_after(newEdge, start);
        }
        start = newEdge;
        newEdge = next;
    } while ((*newEdge).fFirstY == curr_y);
}

#ifdef SK_DEBUG
static void validate_edges_for_y(const SkEdge* edge, int curr_y) {
    while ((*edge).fFirstY <= curr_y) {
        SkASSERT((*edge).fPrev && (*edge).fNext);
        SkASSERT((*(*edge).fPrev).fNext == edge);
        SkASSERT((*(*edge).fNext).fPrev == edge);
        SkASSERT((*edge).fFirstY <= (*edge).fLastY);

        SkASSERT((*(*edge).fPrev).fX <= (*edge).fX);
        edge = (*edge).fNext;
    }
}
#else
    static void validate_edges_for_y(const SkEdge*, int) {}
#endif

using PrePostProc = void (*)(SkBlitter* blitter, int y, bool isStartOfScanline);
static constexpr bool kPrePostStart = true;
static constexpr bool kPrePostEnd = false;

static void walk_edges(SkEdge* prevHead, SkPathFillType fillType,
                       SkBlitter* blitter, int start_y, int stop_y,
                       PrePostProc proc, int rightClip) {
    validate_sort((*prevHead).fNext);

    int curr_y = start_y;
    const int windingMask = SkPathFillType_IsEvenOdd(fillType) ? 1 : -1;

    for (;;) {
        int     w = 0;
        int     left = 0;
        SkEdge* currE = (*prevHead).fNext;
        SkFixed prevX = (*prevHead).fX;

        validate_edges_for_y(currE, curr_y);

        if (proc) {
            proc(blitter, curr_y, kPrePostStart);    // pre-proc
        }

        while ((*currE).fFirstY <= curr_y) {
            SkASSERT((*currE).fLastY >= curr_y);

            const int x = SkFixedRoundToInt((*currE).fX);

            if ((w & windingMask) == 0) { // we're starting interval
                left = x;
            }

            w += static_cast<int>((*currE).fWinding);

            if ((w & windingMask) == 0) { // we finished an interval
                const int width = x - left;
                SkASSERT(width >= 0);
                if (width > 0) {
                    (*blitter).blitH(left, curr_y, width);
                }
            }

            SkEdge* next = (*currE).fNext;
            SkFixed newX = 0;
            bool stillActive = true;

            if ((*currE).fLastY == curr_y) {    // are we done with this edge?
                if ((*currE).hasNextSegment() && (*currE).nextSegment()) {
                    SkASSERT((*currE).fFirstY == curr_y + 1);
                    newX = (*currE).fX;
                } else {
                    remove_edge(currE);
                    stillActive = false;
                }
            } else {
                SkASSERT((*currE).fLastY > curr_y);
                newX = (*currE).fX + (*currE).fDxDy;
                (*currE).fX = newX;
            }
            if (stillActive) {
                if (newX < prevX) { // ripple currE backwards until it is x-sorted
                    backward_insert_edge_based_on_x(currE);
                } else {
                    prevX = newX;
                }
            }
            currE = next;
            SkASSERT(currE);
        }

        if ((w & windingMask) != 0) { // was our right-edge culled away?
            const int width = rightClip - left;
            if (width > 0) {
                (*blitter).blitH(left, curr_y, width);
            }
        }

        if (proc) {
            proc(blitter, curr_y, kPrePostEnd);    // post-proc
        }

        curr_y += 1;
        if (curr_y >= stop_y) {
            break;
        }
        // now currE points to the first edge with a Yint larger than curr_y
        insert_new_edges(currE, curr_y);
    }
}

// return true if we're NOT done with this edge
static bool update_edge(SkEdge* edge, int last_y) {
    SkASSERT((*edge).fLastY >= last_y);
    if (last_y != (*edge).fLastY) {
        return true;
    }
    if (!(*edge).hasNextSegment()) {
        return false;
    }
    if ((*edge).nextSegment()) {
        SkASSERT((*edge).fFirstY == last_y + 1);
        return true;
    }
    return false;
}

// Needs Y to only change once (looser than convex in X). Unexpected edge configurations stop
// the walk (and fail in Debug).
static void walk_simple_edges(SkEdge* prevHead, SkBlitter* blitter, int start_y, int stop_y) {
    validate_sort((*prevHead).fNext);

    SkEdge* leftE = (*prevHead).fNext;
    SkEdge* riteE = (*leftE).fNext;
    SkEdge* currE = (*riteE).fNext;

    // our edge choppers for curves can result in the initial edges
    // not lining up, so we take the max.
    int local_top = std::max((*leftE).fFirstY, (*riteE).fFirstY);
    if (!(local_top >= start_y)) {
        SkDEBUGFAIL("assert(local_top >= start_y)");
        return;
    }

    while (local_top < stop_y) {
        SkASSERT((*leftE).fFirstY <= stop_y);
        SkASSERT((*riteE).fFirstY <= stop_y);

        int local_bot = std::min((*leftE).fLastY, (*riteE).fLastY);
        local_bot = std::min(local_bot, stop_y - 1);
        if (!(local_top <= local_bot)) {
            SkDEBUGFAIL("assert(local_top <= local_bot)");
            return;
        }

        SkFixed left = (*leftE).fX;
        const SkFixed dLeft = (*leftE).fDxDy;
        SkFixed rite = (*riteE).fX;
        const SkFixed dRite = (*riteE).fDxDy;
        int count = local_bot - local_top;
        if (!(count >= 0)) {
            SkDEBUGFAIL("assert(count >= 0)");
            return;
        }

        if (dLeft == 0 && dRite == 0) {
            int L = SkFixedRoundToInt(left);
            int R = SkFixedRoundToInt(rite);
            if (L > R) {
                std::swap(L, R);
            }
            if (L < R) {
                count += 1;
                (*blitter).blitRect(L, local_top, R - L, count);
            }
            local_top = local_bot + 1;
        } else {
            do {
                int L = SkFixedRoundToInt(left);
                int R = SkFixedRoundToInt(rite);
                if (L > R) {
                    std::swap(L, R);
                }
                if (L < R) {
                    (*blitter).blitH(L, local_top, R - L);
                }
                // Either/both of these might overflow, since we perform this step even if
                // (later) we determine that we are done with the edge, and so the computed
                // left or rite edge will not be used (see update_edge). Use this helper to
                // silence UBSAN when we perform the add.
                left = Sk32_can_overflow_add(left, dLeft);
                rite = Sk32_can_overflow_add(rite, dRite);
                local_top += 1;
            } while (--count >= 0);
        }

        (*leftE).fX = left;
        (*riteE).fX = rite;

        if (!update_edge(leftE, local_bot)) {
            if ((*currE).fFirstY >= stop_y) {
                return; // we're done
            }
            leftE = currE;
            currE = (*currE).fNext;
            if (!((*leftE).fFirstY == local_top)) {
                SkDEBUGFAIL("assert(leftE.fFirstY == local_top)");
                return;
            }
        }
        if (!update_edge(riteE, local_bot)) {
            if ((*currE).fFirstY >= stop_y) {
                return; // we're done
            }
            riteE = currE;
            currE = (*currE).fNext;
            if (!((*riteE).fFirstY == local_top)) {
                SkDEBUGFAIL("assert(riteE.fFirstY == local_top)");
                return;
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

// this overrides blitH, and will call its proxy blitter with the inverse
// of the spans it is given (clipped to the left/right of the cliprect)
//
// used to implement inverse filltypes on paths. Borrows the proxy blitter, which must outlive
// the fill.
//
class InverseBlitter : public SkBlitter {
public:
    void setBlitter(SkBlitter* blitter, const SkIRect& clip) {
        fBlitter = blitter;
        fFirstX = clip.fLeft;
        fLastX = clip.fRight;
    }
    void prepost(int y, bool isStart) {
        if (isStart) {
            fPrevX = fFirstX;
        } else {
            const int invWidth = fLastX - fPrevX;
            if (invWidth > 0) {
                (*fBlitter).blitH(fPrevX, y, invWidth);
            }
        }
    }

    // overrides
    void blitH(int x, int y, int width) override {
        const int invWidth = x - fPrevX;
        if (invWidth > 0) {
            (*fBlitter).blitH(fPrevX, y, invWidth);
        }
        fPrevX = x + width;
    }

    // we do not expect to get called with these entrypoints
    void blitAntiH(int, int, const SkAlpha[], const int16_t[]) override {
        SkDEBUGFAIL("blitAntiH unexpected");
    }
    void blitV(int, int, int, SkAlpha) override {
        SkDEBUGFAIL("blitV unexpected");
    }
    void blitRect(int, int, int, int) override {
        SkDEBUGFAIL("blitRect unexpected");
    }
    void blitMask(const SkMask&, const SkIRect&) override {
        SkDEBUGFAIL("blitMask unexpected");
    }

private:
    SkBlitter*  fBlitter = nullptr;
    int         fFirstX = 0;
    int         fLastX = 0;
    int         fPrevX = 0;
};

// Only installed together with an InverseBlitter.
static void PrePostInverseBlitterProc(SkBlitter* blitter, int y, bool isStart) {
    (*static_cast<InverseBlitter*>(blitter)).prepost(y, isStart);
}

///////////////////////////////////////////////////////////////////////////////

static bool compare_edges(const SkEdge* a, const SkEdge* b) {
    if ((*a).fFirstY != (*b).fFirstY) {
        const bool before = (*a).fFirstY < (*b).fFirstY;
        return before;
    }
    const bool before = (*a).fX < (*b).fX;
    return before;
}

// count > 0. Links list[0..count) in sorted order; returns the first edge and sets *last.
static SkEdge* sort_edges(SkEdge* list[], int count, SkEdge** last) {
    SkASSERT(count > 0);
    SkTQSort(list, list + count, compare_edges);

    // now make the edges linked in sorted order
    for (int i = 1; i < count; i++) {
        (*list[i - 1]).fNext = list[i];
        (*list[i]).fPrev = list[i - 1];
    }

    *last = list[count - 1];
    SkEdge* first = list[0];
    return first;
}

// Brackets the sorted list first..last with head and tail sentinels.
static void link_sentinels(SkEdge* headEdge, SkEdge* tailEdge, SkEdge* first, SkEdge* last) {
    (*headEdge).fPrev = nullptr;
    (*headEdge).fNext = first;
    (*headEdge).fFirstY = kEDGE_HEAD_Y;
    (*headEdge).fX = SK_MinS32;
    (*first).fPrev = headEdge;

    (*tailEdge).fPrev = last;
    (*tailEdge).fNext = nullptr;
    (*tailEdge).fFirstY = kEDGE_TAIL_Y;
    (*last).fNext = tailEdge;
}

static void sk_fill_path(const SkPathRaw& raw, const SkIRect& clipRect, SkBlitter* blitter,
                         int start_y, int stop_y, bool pathContainedInClip) {
    SkASSERT(blitter);

    SkBasicEdgeBuilder builder;
    const int count = builder.buildEdges(raw, pathContainedInClip ? nullptr : &clipRect);
    SkEdge** list = builder.edgeList();

    if (0 == count) {
        if (raw.isInverseFillType()) {
            /*
             *  Since we are in inverse-fill, our caller has already drawn above
             *  our top (start_y) and will draw below our bottom (stop_y). Thus
             *  we need to restrict our drawing to the intersection of the clip
             *  and those two limits.
             */
            SkIRect rect = clipRect;
            if (rect.fTop < start_y) {
                rect.fTop = start_y;
            }
            if (rect.fBottom > stop_y) {
                rect.fBottom = stop_y;
            }
            if (!rect.isEmpty()) {
                (*blitter).blitRect(rect.fLeft,
                                    rect.fTop,
                                    rect.width(),
                                    rect.height());
            }
        }
        return;
    }

    SkEdge headEdge;
    SkEdge tailEdge;
    SkEdge* last = nullptr;
    // this returns the first and last edge after they're sorted into a dlink list
    SkEdge* edge = sort_edges(list, count, &last);
    link_sentinels(&headEdge, &tailEdge, edge, last);

    // now edge is the head of the sorted linklist
    if (!pathContainedInClip && start_y < clipRect.fTop) {
        start_y = clipRect.fTop;
    }
    if (!pathContainedInClip && stop_y > clipRect.fBottom) {
        stop_y = clipRect.fBottom;
    }

    InverseBlitter  ib;
    PrePostProc     proc = nullptr;

    if (raw.isInverseFillType()) {
        ib.setBlitter(blitter, clipRect);
        blitter = &ib;
        proc = PrePostInverseBlitterProc;
    }

    // count >= 2 is required as the convex walker does not handle missing right edges
    if (raw.isKnownToBeConvex() && (nullptr == proc) && count >= 2) {
        walk_simple_edges(&headEdge, blitter, start_y, stop_y);
    } else {
        walk_edges(&headEdge, raw.fillType(), blitter, start_y, stop_y, proc,
                   clipRect.right());
    }
}

void sk_blit_above(SkBlitter* blitter, const SkIRect& ir, const SkRegion& clip) {
    const SkIRect& cr = clip.getBounds();
    const SkIRect tmp = SkIRect::MakeLTRB(cr.fLeft, cr.fTop, cr.fRight, ir.fTop);
    if (!tmp.isEmpty()) {
        (*blitter).blitRectRegion(tmp, clip);
    }
}

void sk_blit_below(SkBlitter* blitter, const SkIRect& ir, const SkRegion& clip) {
    const SkIRect& cr = clip.getBounds();
    const SkIRect tmp = SkIRect::MakeLTRB(cr.fLeft, ir.fBottom, cr.fRight, cr.fBottom);
    if (!tmp.isEmpty()) {
        (*blitter).blitRectRegion(tmp, clip);
    }
}

///////////////////////////////////////////////////////////////////////////////

/**
 *  If the caller is drawing an inverse-fill path, then it pass true for
 *  skipRejectTest, so we don't abort drawing just because the src bounds (ir)
 *  is outside of the clip.
 */
SkScanClipper::SkScanClipper(SkBlitter* blitter, const SkRegion* clip,
                             const SkIRect& ir, bool skipRejectTest, bool irPreClipped) {
    fBlitter = nullptr;     // null means blit nothing
    fClipRect = nullptr;

    if (clip) {
        fClipRect = &(*clip).getBounds();
        if (!skipRejectTest && !SkIRect::Intersects(*fClipRect, ir)) { // completely clipped out
            return;
        }

        if ((*clip).isRect()) {
            if (!irPreClipped && (*fClipRect).contains(ir)) {
#ifdef SK_DEBUG
                fRectClipCheckBlitter.init(blitter, *fClipRect);
                blitter = &fRectClipCheckBlitter;
#endif
                fClipRect = nullptr;
            } else {
                // only need a wrapper blitter if we're horizontally clipped
                if (irPreClipped ||
                    (*fClipRect).fLeft > ir.fLeft || (*fClipRect).fRight < ir.fRight) {
                    fRectBlitter.init(blitter, *fClipRect);
                    blitter = &fRectBlitter;
                } else {
#ifdef SK_DEBUG
                    fRectClipCheckBlitter.init(blitter, *fClipRect);
                    blitter = &fRectClipCheckBlitter;
#endif
                }
            }
        } else {
            fRgnBlitter.init(blitter, clip);
            blitter = &fRgnBlitter;
        }
    }
    fBlitter = blitter;
}

///////////////////////////////////////////////////////////////////////////////

static bool clip_to_limit(const SkRegion& orig, SkRegion* reduced) {
    // need to limit coordinates such that the width/height of our rect can be represented
    // in SkFixed (16.16). See skbug.com/40039252
    const int32_t limit = 32767 >> 1;

    const SkIRect limitR = SkIRect::MakeLTRB(-limit, -limit, limit, limit);
    if (limitR.contains(orig.getBounds())) {
        return false;
    }
    (*reduced).op(orig, limitR, SkRegion::kIntersect_Op);
    return true;
}

// Bias used for conservative rounding of float rects to int rects, to nudge the irects a little
// larger, so we don't "think" a path's bounds are inside a clip, when (due to numeric drift in
// the scan-converter) we might walk beyond the predicted limits.
//
// This value has been determined trial and error: pick the smallest value (after the 0.5) that
// fixes any problematic cases (e.g. crbug.com/844457)
// NOTE: cubics appear to be the main reason for needing this slop. If we could (perhaps) have a
// more accurate walker for cubics, we may be able to reduce this fudge factor.
static constexpr double kConservativeRoundBias = 0.5 + 1.5 / SK_FDot6One;

/**
 *  Round the value down. This is used to round the top and left of a rectangle,
 *  and corresponds to the way the scan converter treats the top and left edges.
 *  It has a slight bias to make the "rounded" int smaller than a normal round, to create a more
 *  conservative int-bounds (larger) from a float rect.
 */
static inline int round_down_to_int(SkScalar x) {
    double xx = x;
    xx -= kConservativeRoundBias;
    const int rounded = sk_double_saturate2int(ceil(xx));
    return rounded;
}

/**
 *  Round the value up. This is used to round the right and bottom of a rectangle.
 *  It has a slight bias to make the "rounded" int smaller than a normal round, to create a more
 *  conservative int-bounds (larger) from a float rect.
  */
static inline int round_up_to_int(SkScalar x) {
    double xx = x;
    xx += kConservativeRoundBias;
    const int rounded = sk_double_saturate2int(floor(xx));
    return rounded;
}

/*
 *  Conservative rounding function, which effectively nudges the int-rect to be slightly larger
 *  than SkRect::round() might have produced. This is a safety-net for the scan-converter, which
 *  inspects the returned int-rect, and may disable clipping (for speed) if it thinks all of the
 *  edges will fit inside the clip's bounds. The scan-converter introduces slight numeric errors
 *  due to accumulated += of the slope, so this function is used to return a conservatively large
 *  int-bounds, and thus we will only disable clipping if we're sure the edges will stay in-bounds.
  */
static SkIRect conservative_round_to_int(const SkRect& src) {
    const SkIRect rounded = SkIRect::MakeLTRB(round_down_to_int(src.fLeft),
                                              round_down_to_int(src.fTop),
                                              round_up_to_int(src.fRight),
                                              round_up_to_int(src.fBottom));
    return rounded;
}

void SkScan::FillPath(const SkPathRaw& raw, const SkRegion& origClip, SkBlitter* blitter) {
    if (origClip.isEmpty()) {
        return;
    }

    // Our edges are fixed-point, and don't like the bounds of the clip to
    // exceed that. Here we trim the clip just so we don't overflow later on
    const SkRegion* clipPtr = &origClip;
    SkRegion finiteClip;
    if (clip_to_limit(origClip, &finiteClip)) {
        if (finiteClip.isEmpty()) {
            return;
        }
        clipPtr = &finiteClip;
    }
    // don't reference "origClip" any more, just use clipPtr


    SkRect bounds = raw.bounds();
    bool irPreClipped = false;
    if (!SkRectPriv::MakeLargeS32().contains(bounds)) {
        if (!bounds.intersect(SkRectPriv::MakeLargeS32())) {
            bounds.setEmpty();
        }
        irPreClipped = true;
    }

    const SkIRect ir = conservative_round_to_int(bounds);
    if (ir.isEmpty()) {
        if (raw.isInverseFillType()) {
            (*blitter).blitRegion(*clipPtr);
        }
        return;
    }

    SkScanClipper clipper(blitter, clipPtr, ir, raw.isInverseFillType(), irPreClipped);

    blitter = clipper.getBlitter();
    // Without a blitter nothing is drawn (the path is entirely clipped out).
    if (blitter) {
        // we have to keep our calls to blitter in sorted order, so we
        // must blit the above section first, then the middle, then the bottom.
        if (raw.isInverseFillType()) {
            sk_blit_above(blitter, ir, *clipPtr);
        }
        SkASSERT(clipper.getClipRect() == nullptr ||
                *clipper.getClipRect() == (*clipPtr).getBounds());
        sk_fill_path(raw, (*clipPtr).getBounds(), blitter, ir.fTop, ir.fBottom,
                     clipper.getClipRect() == nullptr);
        if (raw.isInverseFillType()) {
            sk_blit_below(blitter, ir, *clipPtr);
        }
    }
}

bool SkScan::PathRequiresTiling(const SkIRect& bounds) {
    SkRegion out;  // ignored
    const bool needsTiling = clip_to_limit(SkRegion(bounds), &out);
    return needsTiling;
}

///////////////////////////////////////////////////////////////////////////////

// Sets up to three edges of the triangle in edge[] and lists the non-empty ones; returns how
// many were listed.
static int build_tri_edges(SkEdge edge[3], const SkPoint pts[3],
                           const SkIRect* clipRect, SkEdge* list[3]) {
    int count = 0;
    for (int side = 0; side < 3; ++side) {
        const SkPoint& from = pts[side];
        const SkPoint& to = pts[(side + 1) % 3];
        if (edge[count].setLine(from, to, clipRect)) {
            list[count] = &edge[count];
            ++count;
        }
    }
    return count;
}


static void sk_fill_triangle(const SkPoint pts[], const SkIRect* clipRect,
                             SkBlitter* blitter, const SkIRect& ir) {
    SkASSERT(pts && blitter);

    SkEdge edgeStorage[3];
    SkEdge* list[3] = {nullptr, nullptr, nullptr};

    const int count = build_tri_edges(edgeStorage, pts, clipRect, list);
    if (count < 2) {
        return;
    }

    SkEdge headEdge;
    SkEdge tailEdge;
    SkEdge* last = nullptr;

    // this returns the first and last edge after they're sorted into a dlink list
    SkEdge* edge = sort_edges(list, count, &last);
    link_sentinels(&headEdge, &tailEdge, edge, last);

    // now edge is the head of the sorted linklist
    int stop_y = ir.fBottom;
    if (clipRect && stop_y > (*clipRect).fBottom) {
        stop_y = (*clipRect).fBottom;
    }
    int start_y = ir.fTop;
    if (clipRect && start_y < (*clipRect).fTop) {
        start_y = (*clipRect).fTop;
    }
    walk_simple_edges(&headEdge, blitter, start_y, stop_y);
}

void SkScan::FillTriangle(const SkPoint pts[], const SkRasterClip& clip,
                          SkBlitter* blitter) {
    if (clip.isEmpty()) {
        return;
    }

    const std::optional<SkRect> r = SkRect::Bounds(SkSpan<const SkPoint>(pts, 3));
    if (!r.has_value()) {
        return;
    }

    // If r is too large (larger than can easily fit in SkFixed) then we need perform geometric
    // clipping. This is a bit of work, so we just call the general FillPath() to handle it.
    // Use FixedMax/2 as the limit so we can subtract two edges and still store that in Fixed.
    const SkScalar limit = SK_MaxS16 >> 1;
    if (!SkRect::MakeLTRB(-limit, -limit, limit, limit).contains(r.value())) {
        const SkPathRawShapes::Triangle tri(SkSpan<const SkPoint>(pts, 3), r.value());
        FillPath(tri, clip, blitter);
        return;
    }

    const SkIRect ir = conservative_round_to_int(r.value());
    if (ir.isEmpty() || !SkIRect::Intersects(ir, clip.getBounds())) {
        return;
    }

    SkAAClipBlitterWrapper wrap;
    const SkRegion* clipRgn = nullptr;
    if (clip.isBW()) {
        clipRgn = &clip.bwRgn();
    } else {
        wrap.init(clip, blitter);
        clipRgn = &wrap.getRgn();
        blitter = wrap.getBlitter();
    }

    SkScanClipper clipper(blitter, clipRgn, ir);
    blitter = clipper.getBlitter();
    if (blitter) {
        sk_fill_triangle(pts, clipper.getClipRect(), blitter, ir);
    }
}

void SkScan::FillPath(const SkPathRaw& raw, const SkRasterClip& clip, SkBlitter* blitter) {
    if (clip.isEmpty()) {
        return;
    }

    if (clip.isBW()) {
        SkScan::FillPath(raw, clip.bwRgn(), blitter);
    } else {
        SkRegion        tmp;
        SkAAClipBlitter aaBlitter;

        tmp.setRect(clip.getBounds());
        aaBlitter.init(blitter, &clip.aaRgn());
        SkScan::FillPath(raw, tmp, &aaBlitter);
    }
}
