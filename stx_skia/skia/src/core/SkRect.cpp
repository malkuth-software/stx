/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkRect.h"

#include "include/core/SkString.h"
#include "include/private/SkDebug.h"
#include "src/core/SkRectPriv.h"
#include "src/core/SkStringUtils.h"

bool SkIRect::intersect(const SkIRect& a, const SkIRect& b) {
    const SkIRect overlap{
        std::max(a.fLeft,   b.fLeft),
        std::max(a.fTop,    b.fTop),
        std::min(a.fRight,  b.fRight),
        std::min(a.fBottom, b.fBottom)
    };
    if (overlap.isEmpty()) {
        return false;
    }
    *this = overlap;
    return true;
}

void SkIRect::join(const SkIRect& r) {
    // do nothing if the params are empty
    if (r.fLeft >= r.fRight || r.fTop >= r.fBottom) {
        return;
    }

    // if we are empty, just assign
    if (fLeft >= fRight || fTop >= fBottom) {
        *this = r;
        return;
    }
    fLeft   = std::min(fLeft,   r.fLeft);
    fTop    = std::min(fTop,    r.fTop);
    fRight  = std::max(fRight,  r.fRight);
    fBottom = std::max(fBottom, r.fBottom);
}

/////////////////////////////////////////////////////////////////////////////

// All supported targets are 64-bit. The scalar bounds loop is the form upstream
// measured as faster there; the 32-bit vector alternative is not retained.
static_assert(sizeof(void*) == 8, "stx_skia supports 64-bit targets only");

std::optional<SkRect> SkRect::Bounds(SkSpan<const SkPoint> points) {
    if (points.empty()) {
        const SkRect empty = SkRect::MakeEmpty();
        return empty;
    }

    float L = points[0].fX;
    float T = points[0].fY;
    float R = points[0].fX;
    float B = points[0].fY;
    // Zero times every coordinate stays zero only while all coordinates are finite;
    // an infinity or NaN makes the product NaN.
    float finiteX = 0;
    float finiteY = 0;
    for (const SkPoint& p : points) {
        L = std::fminf(p.fX, L);
        T = std::fminf(p.fY, T);
        R = std::fmaxf(p.fX, R);
        B = std::fmaxf(p.fY, B);

        finiteX *= p.fX;
        finiteY *= p.fY;
    }

    // At least one coordinate was an infinity or NaN: there are no finite bounds.
    if (!(finiteX == 0 && finiteY == 0)) {
        return {};
    }
    const SkRect bounds{L, T, R, B};
    return bounds;
}

bool SkRect::setBoundsCheck(SkSpan<const SkPoint> pts) {
    const std::optional<SkRect> bounds = Bounds(pts);
    if (!bounds.has_value()) {
        *this = MakeEmpty();
        return false;
    }
    *this = bounds.value();
    return true;
}

void SkRect::setBoundsNoCheck(SkSpan<const SkPoint> pts) {
    const std::optional<SkRect> bounds = Bounds(pts);
    if (!bounds.has_value()) {
        setLTRB(SK_FloatNaN, SK_FloatNaN, SK_FloatNaN, SK_FloatNaN);
        return;
    }
    *this = bounds.value();
}

namespace {

// The overlap of two rectangles, before deciding whether it is empty.
struct RectOverlap {
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;
};

}  // namespace

static RectOverlap overlap_of(const SkRect& a, const SkRect& b) {
    const RectOverlap overlap{
        .left = std::max(a.fLeft, b.fLeft),
        .top = std::max(a.fTop, b.fTop),
        .right = std::min(a.fRight, b.fRight),
        .bottom = std::min(a.fBottom, b.fBottom),
    };
    return overlap;
}

// The negated comparison rejects an overlap whose edges include NaN.
static bool overlap_is_empty(const RectOverlap& overlap) {
    const bool empty = !(overlap.left < overlap.right && overlap.top < overlap.bottom);
    return empty;
}

bool SkRect::intersect(const SkRect& r) {
    const RectOverlap overlap = overlap_of(r, *this);
    if (overlap_is_empty(overlap)) {
        return false;
    }
    setLTRB(overlap.left, overlap.top, overlap.right, overlap.bottom);
    return true;
}

bool SkRect::intersect(const SkRect& a, const SkRect& b) {
    const RectOverlap overlap = overlap_of(a, b);
    if (overlap_is_empty(overlap)) {
        return false;
    }
    setLTRB(overlap.left, overlap.top, overlap.right, overlap.bottom);
    return true;
}

void SkRect::join(const SkRect& r) {
    if (r.isEmpty()) {
        return;
    }

    if (isEmpty()) {
        *this = r;
        return;
    }
    fLeft   = std::min(fLeft, r.fLeft);
    fTop    = std::min(fTop, r.fTop);
    fRight  = std::max(fRight, r.fRight);
    fBottom = std::max(fBottom, r.fBottom);
}

////////////////////////////////////////////////////////////////////////////////////////////////

static const char* set_scalar(SkString& storage, float value, SkScalarAsStringType asType) {
    storage.reset();
    SkAppendScalar(&storage, value, asType);
    const char* text = storage.c_str();
    return text;
}

SkString SkRect::dumpToString(bool asHex) const {
    const SkScalarAsStringType asType =
            asHex ? kHex_SkScalarAsStringType : kDec_SkScalarAsStringType;

    SkString line{};
    if (asHex) {
        SkString scalar{};
        line.printf( "SkRect::MakeLTRB(%s, /* %f */\n", set_scalar(scalar, fLeft, asType), fLeft);
        line.appendf("                 %s, /* %f */\n", set_scalar(scalar, fTop, asType), fTop);
        line.appendf("                 %s, /* %f */\n", set_scalar(scalar, fRight, asType), fRight);
        line.appendf("                 %s  /* %f */);", set_scalar(scalar, fBottom, asType), fBottom);
        return line;
    }
    SkString strL{};
    SkString strT{};
    SkString strR{};
    SkString strB{};
    SkAppendScalarDec(&strL, fLeft);
    SkAppendScalarDec(&strT, fTop);
    SkAppendScalarDec(&strR, fRight);
    SkAppendScalarDec(&strB, fBottom);
    line.printf("SkRect::MakeLTRB(%s, %s, %s, %s);",
                strL.c_str(), strT.c_str(), strR.c_str(), strB.c_str());
    return line;
}

void SkRect::dump(bool asHex) const {
    const SkString text = dumpToString(asHex);
    SkDebugf("%s\n", text.c_str());
}

////////////////////////////////////////////////////////////////////////////////////////////////

bool SkRectPriv::Subtract(const SkIRect& a, const SkIRect& b, SkIRect* out) {
    if (a.isEmpty() || b.isEmpty() || !SkIRect::Intersects(a, b)) {
        // Either already empty, or subtracting the empty rect, or there's no intersection, so
        // in all cases the answer is A.
        *out = a;
        return true;
    }

    // 4 rectangles to consider. If the edge in A is contained in B, the resulting difference can
    // be represented exactly as a rectangle. Otherwise the difference is the largest subrectangle
    // that is disjoint from B:
    // 1. Left part of A:   (A.left,  A.top,    B.left,  A.bottom)
    // 2. Right part of A:  (B.right, A.top,    A.right, A.bottom)
    // 3. Top part of A:    (A.left,  A.top,    A.right, B.top)
    // 4. Bottom part of A: (A.left,  B.bottom, A.right, A.bottom)
    //
    // Depending on how B intersects A, there will be 1 to 4 positive areas:
    //  - 4 occur when A contains B
    //  - 3 occur when B intersects a single edge
    //  - 2 occur when B intersects at a corner, or spans two opposing edges
    //  - 1 occurs when B spans two opposing edges and contains a 3rd, resulting in an exact rect
    //  - 0 occurs when B contains A, resulting in the empty rect
    //
    // Compute the relative areas of the 4 rects described above. Since each subrectangle shares
    // either the width or height of A, we only have to divide by the other dimension. A is not
    // empty, so its width and height fit in int32_t; every edge difference below lies strictly
    // inside A and therefore fits too. Even if the float relative areas overflow to infinity,
    // the comparisons work out correctly and (one of) the infinitely large subrects is chosen.
    const float aHeight = static_cast<float>(a.height());
    const float aWidth = static_cast<float>(a.width());
    float leftArea = 0.f;
    float rightArea = 0.f;
    float topArea = 0.f;
    float bottomArea = 0.f;
    int positiveCount = 0;
    if (b.fLeft > a.fLeft) {
        leftArea = static_cast<float>(b.fLeft - a.fLeft) / aWidth;
        ++positiveCount;
    }
    if (a.fRight > b.fRight) {
        rightArea = static_cast<float>(a.fRight - b.fRight) / aWidth;
        ++positiveCount;
    }
    if (b.fTop > a.fTop) {
        topArea = static_cast<float>(b.fTop - a.fTop) / aHeight;
        ++positiveCount;
    }
    if (a.fBottom > b.fBottom) {
        bottomArea = static_cast<float>(a.fBottom - b.fBottom) / aHeight;
        ++positiveCount;
    }

    if (positiveCount == 0) {
        SkASSERT(b.contains(a));
        *out = SkIRect::MakeEmpty();
        return true;
    }

    SkIRect difference = a;
    if (leftArea > rightArea && leftArea > topArea && leftArea > bottomArea) {
        // Left chunk of A, so the new right edge is B's left edge
        difference.fRight = b.fLeft;
    } else if (rightArea > topArea && rightArea > bottomArea) {
        // Right chunk of A, so the new left edge is B's right edge
        difference.fLeft = b.fRight;
    } else if (topArea > bottomArea) {
        // Top chunk of A, so the new bottom edge is B's top edge
        difference.fBottom = b.fTop;
    } else {
        // Bottom chunk of A, so the new top edge is B's bottom edge
        SkASSERT(bottomArea > 0.f);
        difference.fTop = b.fBottom;
    }

    // If we have 1 valid area, the disjoint shape is representable as a rectangle.
    SkASSERT(!SkIRect::Intersects(difference, b));
    const bool exact = positiveCount == 1;
    *out = difference;
    return exact;
}
