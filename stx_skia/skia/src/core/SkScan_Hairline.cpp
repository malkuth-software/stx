/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPixmap.h"
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
#include "src/core/SkFDot6.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkLineClipper.h"
#include "src/core/SkMathPriv.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPointPriv.h"
#include "src/core/SkRasterClip.h"
#include "src/core/SkScan.h"
#include "src/core/SkUtils.h"
#include "src/core/SkVx.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

// Address of pixel (x, y) in a pixmap whose pixels are T.
template <typename T>
static T* pixel_address(const SkPixmap& pm, int x, int y) {
    T* address = nullptr;
    if constexpr (sizeof(T) == 1) {
        address = pm.writable_addr8(x, y);
    } else if constexpr (sizeof(T) == 2) {
        address = pm.writable_addr16(x, y);
    } else if constexpr (sizeof(T) == 4) {
        address = pm.writable_addr32(x, y);
    } else {
        address = pm.writable_addr64(x, y);
    }
    return address;
}

// Writes value at (x, fy >> 16) for x in [x, stopx), stepping fy by dy.
template <typename T>
static void direct_horiline(const SkPixmap& pm, uint64_t value, int x, int stopx, SkFixed fy,
                            SkFixed dy) {
    const T pixel = static_cast<T>(value);
    do {
        *pixel_address<T>(pm, x, fy >> 16) = pixel;
        fy += dy;
    } while (++x < stopx);
}

// Writes value at (fx >> 16, y) for y in [y, stopy), stepping fx by dx.
template <typename T>
static void direct_vertline(const SkPixmap& pm, uint64_t value, int y, int stopy, SkFixed fx,
                            SkFixed dx) {
    const T pixel = static_cast<T>(value);
    do {
        *pixel_address<T>(pm, fx >> 16, y) = pixel;
        fx += dx;
    } while (++y < stopy);
}

static void horiline(int x, int stopx, SkFixed fy, SkFixed dy,
                     SkBlitter* blitter) {
    SkASSERT(x < stopx);

    const std::optional<SkBlitter::DirectBlit> direct = (*blitter).canDirectBlit();
    if (direct.has_value()) {
        const SkPixmap& pm = direct.value().pm;
        const uint64_t value = direct.value().value;
        // Other pixel sizes are never offered for direct blits.
        switch (pm.info().bytesPerPixel()) {
            case 1: direct_horiline<uint8_t>(pm, value, x, stopx, fy, dy); break;
            case 2: direct_horiline<uint16_t>(pm, value, x, stopx, fy, dy); break;
            case 4: direct_horiline<uint32_t>(pm, value, x, stopx, fy, dy); break;
            case 8: direct_horiline<uint64_t>(pm, value, x, stopx, fy, dy); break;
            default: break;
        }
    } else {
        do {
            (*blitter).blitH(x, fy >> 16, 1);
            fy += dy;
        } while (++x < stopx);
    }
}

static void vertline(int y, int stopy, SkFixed fx, SkFixed dx,
                     SkBlitter* blitter) {
    SkASSERT(y < stopy);

    const std::optional<SkBlitter::DirectBlit> direct = (*blitter).canDirectBlit();
    if (direct.has_value()) {
        const SkPixmap& pm = direct.value().pm;
        const uint64_t value = direct.value().value;
        // Other pixel sizes are never offered for direct blits.
        switch (pm.info().bytesPerPixel()) {
            case 1: direct_vertline<uint8_t>(pm, value, y, stopy, fx, dx); break;
            case 2: direct_vertline<uint16_t>(pm, value, y, stopy, fx, dx); break;
            case 4: direct_vertline<uint32_t>(pm, value, y, stopy, fx, dx); break;
            case 8: direct_vertline<uint64_t>(pm, value, y, stopy, fx, dx); break;
            default: break;
        }
    } else {
        do {
            (*blitter).blitH(fx >> 16, y, 1);
            fx += dx;
        } while (++y < stopy);
    }
}

//////////////////////////////////////////////////////

#ifdef SK_DEBUG
static bool canConvertFDot6ToFixed(SkFDot6 x) {
    const int maxDot6 = SK_MaxS32 >> (16 - 6);
    const bool convertible = SkAbs32(x) <= maxDot6;
    return convertible;
}
#endif

void SkScan::HairLineRgn(SkSpan<const SkPoint> src, const SkRegion* clip, SkBlitter* origBlitter) {
    if (src.empty()) {
        return;
    }
    SkBlitterClipper    clipper;
    SkIRect clipR = SkIRect::MakeEmpty();
    SkIRect ptsR = SkIRect::MakeEmpty();

    const SkScalar max = SkIntToScalar(32767);
    const SkRect fixedBounds = SkRect::MakeLTRB(-max, -max, max, max);

    SkRect clipBounds = SkRect::MakeEmpty();
    if (clip) {
        clipBounds.set((*clip).getBounds());
    }

    for (size_t i = 0; i < src.size() - 1; ++i) {
        SkBlitter* blitter = origBlitter;

        SkPoint pts[2] = {};

        // We have to pre-clip the line to fit in a SkFixed, so we just chop
        // the line. TODO find a way to actually draw beyond that range.
        if (!SkLineClipper::IntersectLine(&src[i], fixedBounds, pts)) {
            continue;
        }

        // Perform a clip in scalar space, so we catch huge values which might
        // be missed after we convert to SkFDot6 (overflow)
        if (clip && !SkLineClipper::IntersectLine(pts, clipBounds, pts)) {
            continue;
        }

        SkFDot6 x0 = SkScalarToFDot6(pts[0].fX);
        SkFDot6 y0 = SkScalarToFDot6(pts[0].fY);
        SkFDot6 x1 = SkScalarToFDot6(pts[1].fX);
        SkFDot6 y1 = SkScalarToFDot6(pts[1].fY);

        SkASSERT(canConvertFDot6ToFixed(x0));
        SkASSERT(canConvertFDot6ToFixed(y0));
        SkASSERT(canConvertFDot6ToFixed(x1));
        SkASSERT(canConvertFDot6ToFixed(y1));

        if (clip) {
            // now perform clipping again, as the rounding to dot6 can wiggle us
            // our rects are really dot6 rects, but since we've already used
            // lineclipper, we know they will fit in 32bits (26.6)
            const SkIRect& bounds = (*clip).getBounds();

            clipR.setLTRB(SkIntToFDot6(bounds.fLeft), SkIntToFDot6(bounds.fTop),
                          SkIntToFDot6(bounds.fRight), SkIntToFDot6(bounds.fBottom));
            ptsR.setLTRB(x0, y0, x1, y1);
            ptsR.sort();

            // outset the right and bottom, to account for how hairlines are
            // actually drawn, which may hit the pixel to the right or below of
            // the coordinate
            ptsR.fRight += SK_FDot6One;
            ptsR.fBottom += SK_FDot6One;

            if (!SkIRect::Intersects(ptsR, clipR)) {
                continue;
            }
            if (!(*clip).isRect() || !clipR.contains(ptsR)) {
                blitter = clipper.apply(origBlitter, clip);
            }
        }

        const SkFDot6 dx = x1 - x0;
        const SkFDot6 dy = y1 - y0;

        if (SkAbs32(dx) > SkAbs32(dy)) { // mostly horizontal
            if (x0 > x1) {   // we want to go left-to-right
                std::swap(x0, x1);
                std::swap(y0, y1);
            }
            const int ix0 = SkFDot6Round(x0);
            const int ix1 = SkFDot6Round(x1);
            if (ix0 == ix1) {// too short to draw
                continue;
            }
#if defined(SK_BUILD_FOR_FUZZER)
            if ((ix1 - ix0) > 100000 || (ix1 - ix0) < 0) {
                continue; // too big to draw
            }
#endif
            const SkFixed slope = SkFixedDiv(dy, dx);
            const SkFixed startY = SkFDot6ToFixed(y0) + (slope * ((32 - x0) & 63) >> 6);

            horiline(ix0, ix1, startY, slope, blitter);
        } else {              // mostly vertical
            if (y0 > y1) {   // we want to go top-to-bottom
                std::swap(x0, x1);
                std::swap(y0, y1);
            }
            const int iy0 = SkFDot6Round(y0);
            const int iy1 = SkFDot6Round(y1);
            if (iy0 == iy1) { // too short to draw
                continue;
            }
#if defined(SK_BUILD_FOR_FUZZER)
            if ((iy1 - iy0) > 100000 || (iy1 - iy0) < 0) {
                continue; // too big to draw
            }
#endif
            const SkFixed slope = SkFixedDiv(dx, dy);
            const SkFixed startX = SkFDot6ToFixed(x0) + (slope * ((32 - y0) & 63) >> 6);

            vertline(iy0, iy1, startX, slope, blitter);
        }
    }
}

// The clip and cull rectangles borrow storage owned by hair_path for the duration of a draw.
struct DrawingParameters {
    const SkRegion* clip = nullptr;
    const SkRect* insetClip = nullptr;
    const SkRect* outsetClip = nullptr;
    SkBlitter* blitter = nullptr;
    SkScan::HairRgnProc lineproc = nullptr;
};

// we don't just draw 4 lines, 'cause that can leave a gap in the bottom-right
// and double-hit the top-left.
void SkScan::HairRect(const SkRect& rect, const SkRasterClip& clip, SkBlitter* blitter) {
    SkAAClipBlitterWrapper wrapper;
    SkBlitterClipper clipper;
    // Create the enclosing bounds of the hairrect. i.e. we will stroke the interior of r.
    SkIRect r = SkIRect::MakeLTRB(SkScalarFloorToInt(rect.fLeft),
                                  SkScalarFloorToInt(rect.fTop),
                                  SkScalarFloorToInt(rect.fRight + 1),
                                  SkScalarFloorToInt(rect.fBottom + 1));

    // Note: r might be crazy big, if rect was huge, possibly getting pinned to max/min s32.
    // We need to trim it back to something reasonable before we can query its width etc.
    // since r.fRight - r.fLeft might wrap around to negative even if fRight > fLeft.
    //
    // We outset the clip bounds by 1 before intersecting, since r is being stroked and not filled
    // so we don't want to pin an edge of it to the clip. The intersect's job is mostly to just
    // get the actual edge values into a reasonable range (e.g. so width() can't overflow).
    if (!r.intersect(clip.getBounds().makeOutset(1, 1))) {
        return;
    }

    if (clip.quickReject(r)) {
        return;
    }
    if (!clip.quickContains(r)) {
        const SkRegion* clipRgn = nullptr;
        if (clip.isBW()) {
            clipRgn = &clip.bwRgn();
        } else {
            wrapper.init(clip, blitter);
            clipRgn = &wrapper.getRgn();
            blitter = wrapper.getBlitter();
        }
        blitter = clipper.apply(blitter, clipRgn);
    }

    const int width = r.width();
    const int height = r.height();

    if ((width | height) == 0) {
        return;
    }
    if (width <= 2 || height <= 2) {
        (*blitter).blitRect(r.fLeft, r.fTop, width, height);
        return;
    }
    // if we get here, we know we have 4 segments to draw
    (*blitter).blitH(r.fLeft, r.fTop, width);                     // top
    (*blitter).blitRect(r.fLeft, r.fTop + 1, 1, height - 2);      // left
    (*blitter).blitRect(r.fRight - 1, r.fTop + 1, 1, height - 2); // right
    (*blitter).blitH(r.fLeft, r.fBottom - 1, width);              // bottom
}

///////////////////////////////////////////////////////////////////////////////

static constexpr int kMaxCubicSubdivideLevel = 9;
static constexpr int kMaxQuadSubdivideLevel = 5;

using float2 = skvx::float2;

static uint32_t compute_int_quad_dist(const SkPoint pts[3]) {
    // compute the vector between the control point ([1]) and the middle of the
    // line connecting the start and end ([0] and [2])
    float dx = ((pts[0].fX + pts[2].fX) / 2.f) - pts[1].fX;
    float dy = ((pts[0].fY + pts[2].fY) / 2.f) - pts[1].fY;
    // we want everyone to be positive
    dx = std::abs(dx);
    dy = std::abs(dy);
    // convert to whole pixel values (use ceiling to be conservative).
    // assign to unsigned so we can safely add 1/2 of the smaller and still fit in
    // uint32_t, since SkScalarCeilToInt() returns 31 bits at most.
    const uint32_t idx = static_cast<uint32_t>(SkScalarCeilToInt(dx));
    const uint32_t idy = static_cast<uint32_t>(SkScalarCeilToInt(dy));
    // use the cheap approx for distance
    const uint32_t distance = idx > idy ? idx + (idy >> 1) : idy + (idx >> 1);
    return distance;
}

using mask2 = skvx::Vec<2, uint32_t>;

// Lanes whose float has a finite exponent; compares the IEEE bits directly.
static inline mask2 float2_is_finite(const float2& x) {
    const mask2 exp_mask = mask2(0xFF << 23);
    const mask2 finite = (sk_bit_cast<mask2>(x) & exp_mask) != exp_mask;
    return finite;
}

// Draw a quadratic by subdividing it into a series of line segments.
// Assuming none of those points are infinite/nan, then draw the line.
static void hair_quad(const SkPoint pts[3],
                      const SkRegion* clip,
                      SkBlitter* blitter,
                      int level,
                      SkScan::HairRgnProc lineproc) {
    SkASSERT(level >= 0 && level <= kMaxQuadSubdivideLevel);
    // compute_quad_level limits the level; keep the point storage in range regardless.
    level = std::clamp(level, 0, kMaxQuadSubdivideLevel);

    // Convert the quadratic points into coefficients for the form: p(t) = At^2 + Bt + C
    const SkQuadCoeff coeff(pts);

    const unsigned lines = 1u << level;
    float2 t(0);
    const float2 dt(1.0f / lines);

    SkPoint tmp[(1 << kMaxQuadSubdivideLevel) + 1] = {};

    tmp[0] = pts[0];
    const float2 A = coeff.fA;
    const float2 B = coeff.fB;
    const float2 C = coeff.fC;
    mask2 is_finite(~0);  // start out as true
    for (unsigned i = 1; i < lines; ++i) {
        t = t + dt;
        const float2 p = (A * t + B) * t + C;
        is_finite &= float2_is_finite(p);
        tmp[i] = to_point(p);
    }
    if (all(is_finite)) {
        tmp[lines] = pts[2];
        lineproc(SkSpan<const SkPoint>(tmp, lines + 1), clip, blitter);
    }
}

static SkRect compute_nocheck_quad_bounds(const SkPoint pts[3]) {
    SkASSERT(SkPointPriv::AreFinite(pts, 3));

    float2 min = from_point(pts[0]);
    float2 max = min;
    for (int i = 1; i < 3; ++i) {
        const float2 pair = from_point(pts[i]);
        min = skvx::min(min, pair);
        max = skvx::max(max, pair);
    }
    const SkRect bounds = SkRect::MakeLTRB(min[0], min[1], max[0], max[1]);
    return bounds;
}

static bool is_inverted(const SkRect& r) {
    const bool inverted = r.fLeft > r.fRight || r.fTop > r.fBottom;
    return inverted;
}

// Can't call SkRect::intersects, since it cares about empty, and we don't (since we tracking
// something to be stroked, so empty can still draw something (e.g. horizontal line)
static bool geometric_overlap(const SkRect& a, const SkRect& b) {
    SkASSERT(!is_inverted(a) && !is_inverted(b));
    const bool overlaps = a.fLeft < b.fRight && b.fLeft < a.fRight &&
                          a.fTop < b.fBottom && b.fTop < a.fBottom;
    return overlaps;
}

// Can't call SkRect::contains, since it cares about empty, and we don't (since we tracking
// something to be stroked, so empty can still draw something (e.g. horizontal line)
static bool geometric_contains(const SkRect& outer, const SkRect& inner) {
    SkASSERT(!is_inverted(outer) && !is_inverted(inner));
    const bool contains = inner.fRight <= outer.fRight && inner.fLeft >= outer.fLeft &&
                          inner.fBottom <= outer.fBottom && inner.fTop >= outer.fTop;
    return contains;
}

static inline void hairquad(const SkPoint pts[3], DrawingParameters d, int level) {
    if (d.insetClip) {
        SkASSERT(d.outsetClip);
        const SkRect bounds = compute_nocheck_quad_bounds(pts);
        if (!geometric_overlap(*d.outsetClip, bounds)) {
            return;
        } else if (geometric_contains(*d.insetClip, bounds)) {
            d.clip = nullptr;
        }
    }

    hair_quad(pts, d.clip, d.blitter, level, d.lineproc);
}

static inline SkScalar max_component(const float2& value) {
    const SkScalar largest = std::max(value[0], value[1]);
    return largest;
}

static inline int compute_cubic_segs(const SkPoint pts[4]) {
    const float2 p0 = from_point(pts[0]);
    const float2 p1 = from_point(pts[1]);
    const float2 p2 = from_point(pts[2]);
    const float2 p3 = from_point(pts[3]);

    const float2 oneThird(1.0f / 3.0f);
    const float2 twoThird(2.0f / 3.0f);

    const float2 p13 = oneThird * p3 + twoThird * p0;
    const float2 p23 = oneThird * p0 + twoThird * p3;

    const SkScalar diff = max_component(max(abs(p1 - p13), abs(p2 - p23)));
    SkScalar tol = SK_Scalar1 / 8;

    for (int i = 0; i < kMaxCubicSubdivideLevel; ++i) {
        if (diff < tol) {
            return 1 << i;
        }
        tol *= 4;
    }
    return 1 << kMaxCubicSubdivideLevel;
}

static bool lt_90(SkPoint p0, SkPoint pivot, SkPoint p2) {
    const bool acute = SkVector::DotProduct(p0 - pivot, p2 - pivot) >= 0;
    return acute;
}

// The off-curve points are "inside" the limits of the on-curve pts
static bool quick_cubic_niceness_check(const SkPoint pts[4]) {
    const bool nice = lt_90(pts[1], pts[0], pts[3]) &&
                      lt_90(pts[2], pts[0], pts[3]) &&
                      lt_90(pts[1], pts[3], pts[0]) &&
                      lt_90(pts[2], pts[3], pts[0]);
    return nice;
}

// Draw a cubic by subdividing it into a series of line segments.
// Assuming none of those points are infinite/nan, then draw the line.
static void hair_cubic(const SkPoint pts[4],
                       const SkRegion* clip,
                       SkBlitter* blitter,
                       SkScan::HairRgnProc lineproc) {
    constexpr size_t kMaxLines = static_cast<size_t>(1) << kMaxCubicSubdivideLevel;
    // compute_cubic_segs returns at most kMaxLines.
    const size_t lines = std::min(static_cast<size_t>(compute_cubic_segs(pts)), kMaxLines);
    SkASSERT(lines > 0);
    if (1 == lines) {
        const SkPoint ends[2] = {pts[0], pts[3]};
        lineproc(SkSpan<const SkPoint>(ends, 2), clip, blitter);
        return;
    }
    SkPoint tmp[kMaxLines + 1] = {};

    // Convert the cubic points into coefficients for the form: p(t) = At^3 + Bt^2 + Ct + D
    const SkCubicCoeff coeff(pts);

    float2 t(0);
    const float2 dt(1.0f / lines);

    tmp[0] = pts[0];
    const float2 A = coeff.fA;
    const float2 B = coeff.fB;
    const float2 C = coeff.fC;
    const float2 D = coeff.fD;
    mask2 is_finite(~0);  // start out as true
    for (size_t i = 1; i < lines; ++i) {
        t = t + dt;
        const float2 p = ((A * t + B) * t + C) * t + D;
        is_finite &= float2_is_finite(p);
        tmp[i] = to_point(p);
    }
    if (all(is_finite)) {
        tmp[lines] = pts[3];
        lineproc(SkSpan<const SkPoint>(tmp, lines + 1), clip, blitter);
    } // else some point(s) are non-finite, so don't draw
}

static SkRect compute_nocheck_cubic_bounds(const SkPoint pts[4]) {
    SkASSERT(SkPointPriv::AreFinite(pts, 4));

    float2 min = from_point(pts[0]);
    float2 max = min;
    for (int i = 1; i < 4; ++i) {
        const float2 pair = from_point(pts[i]);
        min = skvx::min(min, pair);
        max = skvx::max(max, pair);
    }
    const SkRect bounds = SkRect::MakeLTRB(min[0], min[1], max[0], max[1]);
    return bounds;
}

static inline void haircubic(const SkPoint pts[4], DrawingParameters d, int /*level*/) {
    if (d.insetClip) {
        SkASSERT(d.outsetClip);
        const SkRect bounds = compute_nocheck_cubic_bounds(pts);
        if (!geometric_overlap(*d.outsetClip, bounds)) {
            return;
        } else if (geometric_contains(*d.insetClip, bounds)) {
            d.clip = nullptr;
        }
    }

    if (quick_cubic_niceness_check(pts)) {
        hair_cubic(pts, d.clip, d.blitter, d.lineproc);
    } else {
        SkPoint tmp[13] = {};
        SkScalar tValues[3] = {0, 0, 0};

        const int count = SkChopCubicAtMaxCurvature(pts, tmp, tValues);
        for (int i = 0; i < count; i++) {
            hair_cubic(
                    &tmp[i * 3], d.clip, d.blitter, d.lineproc);
        }
    }
}

static int compute_quad_level(const SkPoint pts[3]) {
    const uint32_t d = compute_int_quad_dist(pts);
    /*  quadratics approach the line connecting their start and end points
     4x closer with each subdivision, so we compute the number of
     subdivisions to be the minimum need to get that distance to be less
     than a pixel.
     */
    int level = (33 - SkCLZ(d)) >> 1;
    // safety check on level (from the previous version)
    if (level > kMaxQuadSubdivideLevel) {
        level = kMaxQuadSubdivideLevel;
    }
    return level;
}

template <typename T> bool optional_eq(std::optional<T> opt, T other) {
    const bool equal = opt.has_value() && (opt.value() == other);
    return equal;
}

/* Extend the points in the direction of the starting or ending tangent by 1/2 unit to
   account for a round or square cap. If there's no distance between the end point and
   the control point, use the next control point to create a tangent. If the curve
   is degenerate, move the cap out 1/2 unit horizontally. pts has at least two points. */
template <SkPaint::Cap capStyle>
void extend_pts(std::optional<SkPathVerb> prevVerb, std::optional<SkPathVerb> nextVerb,
                SkSpan<SkPoint> pts) {
    SkASSERT(SkPaint::kSquare_Cap == capStyle || SkPaint::kRound_Cap == capStyle);
    SkASSERT(pts.size() >= 2);
    // The area of a circle is PI*R*R. For a unit circle, R=1/2, and the cap covers half of that.
    const SkScalar capOutset = SkPaint::kSquare_Cap == capStyle ? 0.5f : SK_ScalarPI / 8;
    const size_t count = pts.size();
    if (optional_eq(prevVerb, SkPathVerb::kMove)) {
        // Find the first control point that differs from the start.
        size_t ctrl = 0;
        size_t controls = count - 1;
        SkVector tangent{0, 0};
        for (;;) {
            ++ctrl;
            tangent = pts[0] - pts[ctrl];
            if (!tangent.isZero()) {
                break;
            }
            --controls;
            if (controls == 0) {
                break;
            }
        }
        if (tangent.isZero()) {
            tangent.set(1, 0);
            controls = count - 1;  // If all points are equal, move all but one
        } else {
            static_cast<void>(tangent.normalize());
        }
        // If the end point and control points are equal, move them in tandem.
        size_t index = 0;
        do {
            pts[index].fX += tangent.fX * capOutset;
            pts[index].fY += tangent.fY * capOutset;
            ++index;
            ++controls;
        } while (controls < count);
    }
    if (!nextVerb.has_value() ||
        nextVerb.value() == SkPathVerb::kMove ||
        nextVerb.value() == SkPathVerb::kClose)
    {
        // Find the last control point that differs from the end.
        const size_t lastIndex = count - 1;
        size_t ctrl = lastIndex;
        size_t controls = count - 1;
        SkVector tangent{0, 0};
        for (;;) {
            --ctrl;
            tangent = pts[lastIndex] - pts[ctrl];
            if (!tangent.isZero()) {
                break;
            }
            --controls;
            if (controls == 0) {
                break;
            }
        }
        if (tangent.isZero()) {
            tangent.set(-1, 0);
            controls = count - 1;
        } else {
            static_cast<void>(tangent.normalize());
        }
        size_t index = lastIndex;
        for (;;) {
            pts[index].fX += tangent.fX * capOutset;
            pts[index].fY += tangent.fY * capOutset;
            ++controls;
            if (controls >= count) {
                break;
            }
            --index;
        }
    }
}

static inline void hairconic(const SkPoint* p, DrawingParameters d, float conicWeight) {
    SkAutoConicToQuads converter;
    // how close should the quads be to the original conic?
    const SkScalar tol = SK_Scalar1 / 4;
    const SkPoint* quadPts = converter.computeQuads(p, conicWeight, tol);
    for (int i = 0; i < converter.countQuads(); ++i) {
        const SkPoint* quad = &quadPts[2 * i];
        const int level = compute_quad_level(quad);
        hairquad(quad, d, level);
    }
}

// This function assumes that iter is currently ON a SkPathVerb::kMove verb
static inline bool is_next_contour_closed(SkPathIter scanner) {
    // we assume the first verb is already a move, so do scanner.next() to proceed to the next verb.
    // This will ideally be a contour verb or a close. A move that ends the verbs (possible after
    // repeated moves) has no contour.
    const std::optional<SkPathIter::Rec> rec = scanner.next();
    if (!rec.has_value()) {
        return false;
    }
    if (rec.value().fVerb == SkPathVerb::kClose) {
        return true;
    }
    if (rec.value().fVerb == SkPathVerb::kMove) {
        return false;
    }

    while (scanner.peekNextVerb().has_value()) {
        const SkPathVerb next_verb = scanner.peekNextVerb().value();
        // The current contour ends here, before this kMove.
        if (next_verb == SkPathVerb::kMove) {
            return false;
        }
        // This kClose ends the current contour.
        if (next_verb == SkPathVerb::kClose) {
            return true;
        }
        static_cast<void>(scanner.next());  // Consume the record
    }
    return false;
}

template <SkPaint::Cap capStyle>
void hair_path(const SkPathRaw& raw,
               const SkRasterClip& rclip,
               SkBlitter* blitter,
               SkScan::HairRgnProc lineproc) {
    if (raw.empty()) {
        return;
    }

    SkAAClipBlitterWrapper wrap;
    const SkRegion* clip = nullptr;
    SkRect insetStorage = SkRect::MakeEmpty();
    SkRect outsetStorage = SkRect::MakeEmpty();
    const SkRect* insetClip = nullptr;
    const SkRect* outsetClip = nullptr;

    {
        const int capOut = SkPaint::kButt_Cap == capStyle ? 1 : 2;
        const SkIRect ibounds = raw.bounds().roundOut().makeOutset(capOut, capOut);
        if (rclip.quickReject(ibounds)) {
            return;
        }
        if (!rclip.quickContains(ibounds)) {
            if (rclip.isBW()) {
                clip = &rclip.bwRgn();
            } else {
                wrap.init(rclip, blitter);
                blitter = wrap.getBlitter();
                clip = &wrap.getRgn();
            }

            /*
             *  We now cache two scalar rects, to use for culling per-segment (e.g. cubic).
             *  Since we're hairlining, the "bounds" of the control points isn't necessairly the
             *  limit of where a segment can draw (it might draw up to 1 pixel beyond in aa-hairs).
             *
             *  Compute the pt-bounds per segment is easy, so we do that, and then inversely adjust
             *  the culling bounds so we can just do a straight compare per segment.
             *
             *  insetClip is use for quick-accept (i.e. the segment is not clipped), so we inset
             *  it from the clip-bounds (since segment bounds can be off by 1).
             *
             *  outsetClip is used for quick-reject (i.e. the segment is entirely outside), so we
             *  outset it from the clip-bounds.
             */
            insetStorage.set((*clip).getBounds());
            outsetStorage = insetStorage.makeOutset(1, 1);
            insetStorage.inset(1, 1);
            if (is_inverted(insetStorage)) {
                /*
                 *  our bounds checks assume the rects are never inverted. If insetting has
                 *  created that, we assume that the area is too small to safely perform a
                 *  quick-accept, so we just mark the rect as empty (so the quick-accept check
                 *  will always fail.
                 */
                insetStorage.setEmpty();  // just so we don't pass an inverted rect
            }
            if (rclip.isRect()) {
                insetClip = &insetStorage;
            }
            outsetClip = &outsetStorage;
        }
    }

    SkPoint pts[4] = {};
    SkPoint firstPt{0, 0};
    SkPoint lastPt{0, 0};
    bool isClosed = false;
    DrawingParameters params{};
    params.clip = clip;
    params.insetClip = insetClip;
    params.outsetClip = outsetClip;
    params.blitter = blitter;
    params.lineproc = lineproc;
    const bool isButtCap = capStyle == SkPaint::kButt_Cap;

    std::optional<SkPathVerb> prevVerb;
    SkPathIter iter = raw.iter();
    for (std::optional<SkPathIter::Rec> rec = iter.next(); rec.has_value(); rec = iter.next()) {
        const SkPathIter::Rec& record = rec.value();
        const SkSpan<const SkPoint> srcPts = record.fPoints;
        const SkPathVerb verb = record.fVerb;
        const std::optional<SkPathVerb> nextVerb = iter.peekNextVerb();
        switch (verb) {
            case SkPathVerb::kMove:
                firstPt = srcPts[0];
                lastPt = srcPts[0];
                isClosed = !isButtCap && is_next_contour_closed(iter);
                break;
            case SkPathVerb::kLine: {
                constexpr size_t kNumLinePts = 2;
                std::copy(srcPts.begin(), srcPts.begin() + kNumLinePts, pts);
                if (!isButtCap && (!isClosed || SkPath::IsLineDegenerate(pts[0], pts[1], true))) {
                    extend_pts<capStyle>(prevVerb, nextVerb, SkSpan<SkPoint>(pts, kNumLinePts));
                }
                lineproc(SkSpan<const SkPoint>(pts, kNumLinePts), clip, blitter);
                lastPt = pts[kNumLinePts - 1];
                break;
            }
            case SkPathVerb::kQuad: {
                constexpr size_t kNumQuadPts = 3;
                std::copy(srcPts.begin(), srcPts.begin() + kNumQuadPts, pts);
                if (!isButtCap &&
                    (!isClosed || SkPath::IsQuadDegenerate(pts[0], pts[1], pts[2], true))) {
                    extend_pts<capStyle>(prevVerb, nextVerb, SkSpan<SkPoint>(pts, kNumQuadPts));
                }
                hairquad(pts, params, compute_quad_level(pts));
                lastPt = pts[kNumQuadPts - 1];
                break;
            }
            case SkPathVerb::kConic: {
                constexpr size_t kNumConicPts = 3;
                std::copy(srcPts.begin(), srcPts.begin() + kNumConicPts, pts);
                if (!isButtCap &&
                    (!isClosed || SkPath::IsQuadDegenerate(pts[0], pts[1], pts[2], true))) {
                    extend_pts<capStyle>(prevVerb, nextVerb, SkSpan<SkPoint>(pts, kNumConicPts));
                }
                hairconic(pts, params, record.conicWeight());
                lastPt = pts[kNumConicPts - 1];
                break;
            }
            case SkPathVerb::kCubic: {
                constexpr size_t kNumCubicPts = 4;
                std::copy(srcPts.begin(), srcPts.begin() + kNumCubicPts, pts);
                if (!isButtCap && (!isClosed || SkPath::IsCubicDegenerate(
                                                        pts[0], pts[1], pts[2], pts[3], true))) {
                    extend_pts<capStyle>(prevVerb, nextVerb, SkSpan<SkPoint>(pts, kNumCubicPts));
                }
                haircubic(pts, params, kMaxCubicSubdivideLevel);
                lastPt = pts[kNumCubicPts - 1];
            } break;
            case SkPathVerb::kClose:
                pts[0] = lastPt;
                pts[1] = firstPt;
                if (!isButtCap && optional_eq(prevVerb, SkPathVerb::kMove)) {
                    // cap moveTo/close to match svg expectations for degenerate segments
                    extend_pts<capStyle>(prevVerb, nextVerb, SkSpan<SkPoint>(pts, 2));
                }
                lineproc(SkSpan<const SkPoint>(pts, 2), clip, blitter);
                break;
        }
        if (!isButtCap) {
            if (optional_eq(prevVerb, SkPathVerb::kMove) && verb >= SkPathVerb::kLine &&
                verb <= SkPathVerb::kCubic) {
                firstPt = pts[0];  // the curve moved the initial point, so close to it instead
            }
            prevVerb = verb;
        }
    }
}

void SkScan::HairPath(const SkPathRaw& raw, const SkRasterClip& clip, SkBlitter* blitter) {
    hair_path<SkPaint::kButt_Cap>(raw, clip, blitter, SkScan::HairLineRgn);
}

void SkScan::AntiHairPath(const SkPathRaw& raw, const SkRasterClip& clip, SkBlitter* blitter) {
    hair_path<SkPaint::kButt_Cap>(raw, clip, blitter, SkScan::AntiHairLineRgn);
}

void SkScan::HairSquarePath(const SkPathRaw& raw, const SkRasterClip& clip, SkBlitter* blitter) {
    hair_path<SkPaint::kSquare_Cap>(raw, clip, blitter, SkScan::HairLineRgn);
}

void SkScan::AntiHairSquarePath(const SkPathRaw& raw, const SkRasterClip& clip, SkBlitter* blitter) {
    hair_path<SkPaint::kSquare_Cap>(raw, clip, blitter, SkScan::AntiHairLineRgn);
}

void SkScan::HairRoundPath(const SkPathRaw& raw, const SkRasterClip& clip, SkBlitter* blitter) {
    hair_path<SkPaint::kRound_Cap>(raw, clip, blitter, SkScan::HairLineRgn);
}

void SkScan::AntiHairRoundPath(const SkPathRaw& raw, const SkRasterClip& clip, SkBlitter* blitter) {
    hair_path<SkPaint::kRound_Cap>(raw, clip, blitter, SkScan::AntiHairLineRgn);
}

///////////////////////////////////////////////////////////////////////////////

void SkScan::FrameRect(const SkRect& r, const SkPoint& strokeSize,
                       const SkRasterClip& clip, SkBlitter* blitter) {
    SkASSERT(strokeSize.fX >= 0 && strokeSize.fY >= 0);

    if (strokeSize.fX < 0 || strokeSize.fY < 0) {
        return;
    }

    const float dx = strokeSize.fX;
    const float dy = strokeSize.fY;
    const float rx = dx / 2.f;
    const float ry = dy / 2.f;
    SkRect   outer = SkRect::MakeEmpty();
    SkRect   tmp = SkRect::MakeEmpty();

    outer.setLTRB(r.fLeft - rx, r.fTop - ry, r.fRight + rx, r.fBottom + ry);

    if (r.width() <= dx || r.height() <= dy) {
        // If we're empty on either axis, we remove the outset amount, to be sure
        // we stroke the same way a polygon would (i.e. it would just see a "line"
        // and not extend it for the miter join).
        if (r.width() == 0) {
            outer.fTop = r.fTop;
            outer.fBottom = r.fBottom;
        }
        if (r.height() == 0) {
            outer.fLeft = r.fLeft;
            outer.fRight = r.fRight;
        }
        SkScan::FillRect(outer, clip, blitter);
        return;
    }

    tmp.setLTRB(outer.fLeft, outer.fTop, outer.fRight, outer.fTop + dy);
    SkScan::FillRect(tmp, clip, blitter);
    tmp.fTop = outer.fBottom - dy;
    tmp.fBottom = outer.fBottom;
    SkScan::FillRect(tmp, clip, blitter);

    tmp.setLTRB(outer.fLeft, outer.fTop + dy, outer.fLeft + dx, outer.fBottom - dy);
    SkScan::FillRect(tmp, clip, blitter);
    tmp.fLeft = outer.fRight - dx;
    tmp.fRight = outer.fRight;
    SkScan::FillRect(tmp, clip, blitter);
}

void SkScan::HairLine(SkSpan<const SkPoint> pts, const SkRasterClip& clip, SkBlitter* blitter) {
    if (clip.isBW()) {
        HairLineRgn(pts, &clip.bwRgn(), blitter);
    } else {
        const SkRegion* clipRgn = nullptr;

        const SkRect r = SkRect::BoundsOrEmpty(pts).makeOutset(SK_ScalarHalf, SK_ScalarHalf);

        SkAAClipBlitterWrapper wrap;
        if (!clip.quickContains(r.roundOut())) {
            wrap.init(clip, blitter);
            blitter = wrap.getBlitter();
            clipRgn = &wrap.getRgn();
        }
        HairLineRgn(pts, clipRgn, blitter);
    }
}

void SkScan::AntiHairLine(SkSpan<const SkPoint> pts, const SkRasterClip& clip, SkBlitter* blitter) {
    if (clip.isBW()) {
        AntiHairLineRgn(pts, &clip.bwRgn(), blitter);
    } else {
        const SkRegion* clipRgn = nullptr;

        const SkRect r = SkRect::BoundsOrEmpty(pts);

        SkAAClipBlitterWrapper wrap;
        if (!clip.quickContains(r.roundOut().makeOutset(1, 1))) {
            wrap.init(clip, blitter);
            blitter = wrap.getBlitter();
            clipRgn = &wrap.getRgn();
        }
        AntiHairLineRgn(pts, clipRgn, blitter);
    }
}
