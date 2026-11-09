/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkLineClipper.h"

#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkScalar.h"
#include "include/core/SkTypes.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkTo.h"

#include <utility>

template <typename T> T pin_unsorted(T value, T limit0, T limit1) {
    if (limit1 < limit0) {
        std::swap(limit0, limit1);
    }
    // now the limits are sorted
    SkASSERT(limit0 <= limit1);

    if (value < limit0) {
        value = limit0;
    } else if (value > limit1) {
        value = limit1;
    }
    return value;
}

// return X coordinate of intersection with horizontal line at Y
static SkScalar sect_with_horizontal(const SkPoint src[2], SkScalar Y) {
    const SkScalar dy = src[1].fY - src[0].fY;
    if (SkScalarNearlyZero(dy)) {
        const SkScalar midpoint = sk_float_midpoint(src[0].fX, src[1].fX);
        return midpoint;
    }
    // need the extra precision so we don't compute a value that exceeds
    // our original limits
    const double X0 = src[0].fX;
    const double Y0 = src[0].fY;
    const double X1 = src[1].fX;
    const double Y1 = src[1].fY;
    const double result = X0 + (static_cast<double>(Y) - Y0) * (X1 - X0) / (Y1 - Y0);

    // The computed X value might still exceed [X0..X1] due to quantum flux
    // when the doubles were added and subtracted, so we have to pin the
    // answer :(
    const SkScalar x = static_cast<float>(pin_unsorted(result, X0, X1));
    return x;
}

// return Y coordinate of intersection with vertical line at X
static SkScalar sect_with_vertical(const SkPoint src[2], SkScalar X) {
    const SkScalar dx = src[1].fX - src[0].fX;
    if (SkScalarNearlyZero(dx)) {
        const SkScalar midpoint = sk_float_midpoint(src[0].fY, src[1].fY);
        return midpoint;
    }
    // need the extra precision so we don't compute a value that exceeds
    // our original limits
    const double X0 = src[0].fX;
    const double Y0 = src[0].fY;
    const double X1 = src[1].fX;
    const double Y1 = src[1].fY;
    const double result = Y0 + (static_cast<double>(X) - X0) * (Y1 - Y0) / (X1 - X0);
    const SkScalar y = static_cast<float>(result);
    return y;
}

static SkScalar sect_clamp_with_vertical(const SkPoint src[2], SkScalar x) {
    const SkScalar y = sect_with_vertical(src, x);
    // Our caller expects y to be between src[0].fY and src[1].fY (unsorted), but due to the
    // numerics of floats/doubles, we might have computed a value slightly outside of that,
    // so we have to manually clamp afterwards.
    // See skbug.com/40038736
    const SkScalar clamped = pin_unsorted(y, src[0].fY, src[1].fY);
    return clamped;
}

///////////////////////////////////////////////////////////////////////////////

static inline bool nestedLT(SkScalar a, SkScalar b, SkScalar dim) {
    const bool less = a <= b && (a < b || dim > 0);
    return less;
}

// returns true if outer contains inner, even if inner is empty.
// note: outer.contains(inner) always returns false if inner is empty.
static inline bool containsNoEmptyCheck(const SkRect& outer,
                                        const SkRect& inner) {
    const bool contains = outer.fLeft <= inner.fLeft && outer.fTop <= inner.fTop &&
                          outer.fRight >= inner.fRight && outer.fBottom >= inner.fBottom;
    return contains;
}

// src and dst may be the same array.
bool SkLineClipper::IntersectLine(const SkPoint src[2], const SkRect& clip,
                                  SkPoint dst[2]) {
    SkRect bounds = SkRect::MakeEmpty();

    bounds.set(src[0], src[1]);
    if (containsNoEmptyCheck(clip, bounds)) {
        if (src != dst) {
            dst[0] = src[0];
            dst[1] = src[1];
        }
        return true;
    }
    // check for no overlap, and only permit coincident edges if the line
    // and the edge are colinear
    if (nestedLT(bounds.fRight, clip.fLeft, bounds.width()) ||
        nestedLT(clip.fRight, bounds.fLeft, bounds.width()) ||
        nestedLT(bounds.fBottom, clip.fTop, bounds.height()) ||
        nestedLT(clip.fBottom, bounds.fTop, bounds.height())) {
        return false;
    }

    int index0 = 0;
    int index1 = 1;

    if (src[0].fY < src[1].fY) {
        index0 = 0;
        index1 = 1;
    } else {
        index0 = 1;
        index1 = 0;
    }

    SkPoint tmp[2] = {src[0], src[1]};

    // now compute Y intersections
    if (tmp[index0].fY < clip.fTop) {
        tmp[index0].set(sect_with_horizontal(src, clip.fTop), clip.fTop);
    }
    if (tmp[index1].fY > clip.fBottom) {
        tmp[index1].set(sect_with_horizontal(src, clip.fBottom), clip.fBottom);
    }

    if (tmp[0].fX < tmp[1].fX) {
        index0 = 0;
        index1 = 1;
    } else {
        index0 = 1;
        index1 = 0;
    }

    // check for quick-reject in X again, now that we may have been chopped
    if ((tmp[index1].fX <= clip.fLeft || tmp[index0].fX >= clip.fRight)) {
        // usually we will return false, but we don't if the line is vertical and coincident
        // with the clip.
        if (tmp[0].fX != tmp[1].fX || tmp[0].fX < clip.fLeft || tmp[0].fX > clip.fRight) {
            return false;
        }
    }

    if (tmp[index0].fX < clip.fLeft) {
        tmp[index0].set(clip.fLeft, sect_with_vertical(tmp, clip.fLeft));
    }
    if (tmp[index1].fX > clip.fRight) {
        tmp[index1].set(clip.fRight, sect_with_vertical(tmp, clip.fRight));
    }
#ifdef SK_DEBUG
    bounds.set(tmp[0], tmp[1]);
    SkASSERT(containsNoEmptyCheck(clip, bounds));
#endif
    dst[0] = tmp[0];
    dst[1] = tmp[1];
    return true;
}

#ifdef SK_DEBUG
// return value between the two limits, where the limits are either ascending
// or descending.
static bool is_between_unsorted(SkScalar value,
                                SkScalar limit0, SkScalar limit1) {
    bool between = false;
    if (limit0 < limit1) {
        between = limit0 <= value && value <= limit1;
    } else {
        between = limit1 <= value && value <= limit0;
    }
    return between;
}
#endif

int SkLineClipper::ClipLine(const SkPoint pts[2], const SkRect& clip, SkPoint lines[kMaxPoints],
                            bool canCullToTheRight) {
    int index0 = 0;
    int index1 = 1;

    if (pts[0].fY < pts[1].fY) {
        index0 = 0;
        index1 = 1;
    } else {
        index0 = 1;
        index1 = 0;
    }

    // Check if we're completely clipped out in Y (above or below

    if (pts[index1].fY <= clip.fTop) {  // we're above the clip
        return 0;
    }
    if (pts[index0].fY >= clip.fBottom) {  // we're below the clip
        return 0;
    }

    // Chop in Y to produce a single segment, stored in tmp[0..1]

    SkPoint tmp[2] = {pts[0], pts[1]};

    // now compute intersections
    if (pts[index0].fY < clip.fTop) {
        tmp[index0].set(sect_with_horizontal(pts, clip.fTop), clip.fTop);
        SkASSERT(is_between_unsorted(tmp[index0].fX, pts[0].fX, pts[1].fX));
    }
    if (tmp[index1].fY > clip.fBottom) {
        tmp[index1].set(sect_with_horizontal(pts, clip.fBottom), clip.fBottom);
        SkASSERT(is_between_unsorted(tmp[index1].fX, pts[0].fX, pts[1].fX));
    }

    // Chop it into 1..3 segments that are wholly within the clip in X.

    // Up to 3 segments; when the line is wholly outside in X only the first two are used.
    SkPoint result[kMaxPoints] = {};
    int lineCount = 1;
    bool reverse = false;

    if (pts[0].fX < pts[1].fX) {
        index0 = 0;
        index1 = 1;
        reverse = false;
    } else {
        index0 = 1;
        index1 = 0;
        reverse = true;
    }

    if (tmp[index1].fX <= clip.fLeft) {  // wholly to the left
        tmp[0].fX = clip.fLeft;
        tmp[1].fX = clip.fLeft;
        result[0] = tmp[0];
        result[1] = tmp[1];
        reverse = false;
    } else if (tmp[index0].fX >= clip.fRight) {    // wholly to the right
        if (canCullToTheRight) {
            return 0;
        }
        tmp[0].fX = clip.fRight;
        tmp[1].fX = clip.fRight;
        result[0] = tmp[0];
        result[1] = tmp[1];
        reverse = false;
    } else {
        int r = 0;

        if (tmp[index0].fX < clip.fLeft) {
            result[r].set(clip.fLeft, tmp[index0].fY);
            r += 1;
            result[r].set(clip.fLeft, sect_clamp_with_vertical(tmp, clip.fLeft));
            SkASSERT(is_between_unsorted(result[r].fY, tmp[0].fY, tmp[1].fY));
        } else {
            result[r] = tmp[index0];
        }
        r += 1;

        if (tmp[index1].fX > clip.fRight) {
            result[r].set(clip.fRight, sect_clamp_with_vertical(tmp, clip.fRight));
            SkASSERT(is_between_unsorted(result[r].fY, tmp[0].fY, tmp[1].fY));
            r += 1;
            result[r].set(clip.fRight, tmp[index1].fY);
        } else {
            result[r] = tmp[index1];
        }

        lineCount = r;
    }

    // Now copy the results into the caller's lines[] parameter
    if (reverse) {
        // copy the pts in reverse order to maintain winding order
        for (int i = 0; i <= lineCount; i++) {
            lines[lineCount - i] = result[i];
        }
    } else {
        for (int i = 0; i <= lineCount; i++) {
            lines[i] = result[i];
        }
    }
    return lineCount;
}
