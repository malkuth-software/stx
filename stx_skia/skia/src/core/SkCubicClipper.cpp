/*
 * Copyright 2009 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkCubicClipper.h"

#include "include/core/SkPoint.h"
#include "src/core/SkGeometry.h"

#include <utility>

void SkCubicClipper::setClip(const SkIRect& clip) {
    // conver to scalars, since that's where we'll see the points
    fClip.set(clip);
}

// Bisection: linear convergence, typically 16 iterations. (Upstream also carried an
// uncompiled Newton-Raphson variant behind NEWTON_RAPHSON; it is not retained.)
bool SkCubicClipper::ChopMonoAtY(const SkPoint pts[4], SkScalar y, SkScalar* t) {
    const SkScalar ycrv[4] = {pts[0].fY - y, pts[1].fY - y, pts[2].fY - y, pts[3].fY - y};

    // Check that the endpoints straddle zero.
    SkScalar tNeg = 0;    // Negative and positive function parameters.
    SkScalar tPos = 0;
    if (ycrv[0] < 0) {
        if (ycrv[3] < 0) {
            return false;
        }
        tNeg = 0;
        tPos = SK_Scalar1;
    } else if (ycrv[0] > 0) {
        if (ycrv[3] > 0) {
            return false;
        }
        tNeg = SK_Scalar1;
        tPos = 0;
    } else {
        *t = 0;
        return true;
    }

    const SkScalar tol = SK_Scalar1 / 65536;  // 1 for fixed, 1e-5 for float.
    do {
        const SkScalar tMid = (tPos + tNeg) / 2;
        const SkScalar y01   = SkScalarInterp(ycrv[0], ycrv[1], tMid);
        const SkScalar y12   = SkScalarInterp(ycrv[1], ycrv[2], tMid);
        const SkScalar y23   = SkScalarInterp(ycrv[2], ycrv[3], tMid);
        const SkScalar y012  = SkScalarInterp(y01,     y12,     tMid);
        const SkScalar y123  = SkScalarInterp(y12,     y23,     tMid);
        const SkScalar y0123 = SkScalarInterp(y012,    y123,    tMid);
        if (y0123 == 0) {
            *t = tMid;
            return true;
        }
        if (y0123 < 0) {
            tNeg = tMid;
        } else {
            tPos = tMid;
        }
    } while (!(SkScalarAbs(tPos - tNeg) <= tol));   // Nan-safe

    *t = (tNeg + tPos) / 2;
    return true;
}


bool SkCubicClipper::clipCubic(const SkPoint srcPts[4], SkPoint dst[4]) {
    bool reverse = false;

    // we need the data to be monotonically descending in Y
    if (srcPts[0].fY > srcPts[3].fY) {
        dst[0] = srcPts[3];
        dst[1] = srcPts[2];
        dst[2] = srcPts[1];
        dst[3] = srcPts[0];
        reverse = true;
    } else {
        for (int i = 0; i < 4; ++i) {
            dst[i] = srcPts[i];
        }
        reverse = false;
    }

    // are we completely above or below
    const SkScalar ctop = fClip.fTop;
    const SkScalar cbot = fClip.fBottom;
    if (dst[3].fY <= ctop || dst[0].fY >= cbot) {
        return false;
    }

    SkScalar t = 0;
    SkPoint tmp[7] = {}; // for SkChopCubicAt

    // are we partially above
    if (dst[0].fY < ctop && ChopMonoAtY(dst, ctop, &t)) {
        SkChopCubicAt(dst, tmp, t);
        dst[0] = tmp[3];
        dst[1] = tmp[4];
        dst[2] = tmp[5];
    }

    // are we partially below
    if (dst[3].fY > cbot && ChopMonoAtY(dst, cbot, &t)) {
        SkChopCubicAt(dst, tmp, t);
        dst[1] = tmp[1];
        dst[2] = tmp[2];
        dst[3] = tmp[3];
    }

    if (reverse) {
        std::swap(dst[0], dst[3]);
        std::swap(dst[1], dst[2]);
    }
    return true;
}
