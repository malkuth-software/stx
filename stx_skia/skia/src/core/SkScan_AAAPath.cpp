/*
 * Copyright 2016 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkColor.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkRect.h"
#include "include/private/SkAlign.h"
#include "include/private/SkAssert.h"
#include "include/private/SkAutoTArray.h"
#include "include/private/SkDebug.h"
#include "include/private/SkFixed.h"
#include "include/private/SkMath.h"
#include "include/private/SkSafe32.h"
#include "include/private/SkTo.h"
#include "src/core/SkAlphaRuns.h"
#include "src/core/SkAnalyticEdge.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkEdgeBuilder.h"
#include "src/core/SkMask.h"
#include "src/core/SkPathRaw.h"
#include "src/core/SkScan.h"
#include "src/core/SkScanPriv.h"
#include "src/core/SkTSort.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

/*

The following is a high-level overview of our analytic anti-aliasing
algorithm. We consider a path as a collection of line segments, as
quadratic/cubic curves are converted to small line segments. Without loss of
generality, let's assume that the draw region is [0, W] x [0, H].

Our algorithm is based on horizontal scan lines (y = c_i) as the previous
sampling-based algorithm did. However, our algorithm uses non-equal-spaced
scan lines, while the previous method always uses equal-spaced scan lines,
such as (y = 1/2 + 0, 1/2 + 1, 1/2 + 2, ...) in the previous non-AA algorithm,
and (y = 1/8 + 1/4, 1/8 + 2/4, 1/8 + 3/4, ...) in the previous
16-supersampling AA algorithm.

Our algorithm contains scan lines y = c_i for c_i that is either:

1. an integer between [0, H]

2. the y value of a line segment endpoint

3. the y value of an intersection of two line segments

For two consecutive scan lines y = c_i, y = c_{i+1}, we analytically computes
the coverage of this horizontal strip of our path on each pixel. This can be
done very efficiently because the strip of our path now only consists of
trapezoids whose top and bottom edges are y = c_i, y = c_{i+1} (this includes
rectangles and triangles as special cases).

We now describe how the coverage of single pixel is computed against such a
trapezoid. That coverage is essentially the intersection area of a rectangle
(e.g., [0, 1] x [c_i, c_{i+1}]) and our trapezoid. However, that intersection
could be complicated, as shown in the example region A below:

+-----------\----+
|            \  C|
|             \  |
\              \ |
|\      A       \|
| \              \
|  \             |
| B \            |
+----\-----------+

However, we don't have to compute the area of A directly. Instead, we can
compute the excluded area, which are B and C, quite easily, because they're
just triangles. In fact, we can prove that an excluded region (take B as an
example) is either itself a simple trapezoid (including rectangles, triangles,
and empty regions), or its opposite (the opposite of B is A + C) is a simple
trapezoid. In any case, we can compute its area efficiently.

In summary, our algorithm has a higher quality because it generates ground-
truth coverages analytically. It is also faster because it has much fewer
unnessasary horizontal scan lines. For example, given a triangle path, the
number of scan lines in our algorithm is only about 3 + H while the
16-supersampling algorithm has about 4H scan lines.

*/

static void add_alpha(SkAlpha* alpha, SkAlpha delta) {
    SkASSERT(*alpha + delta <= 256);
    *alpha = SkAlphaRuns::CatchOverflow(*alpha + delta);
}

static void safely_add_alpha(SkAlpha* alpha, SkAlpha delta) {
    *alpha = static_cast<SkAlpha>(std::min(0xFF, *alpha + delta));
}

class AdditiveBlitter : public SkBlitter {
public:
    ~AdditiveBlitter() override = default;

    virtual SkBlitter* getRealBlitter(bool forceRealBlitter = false) = 0;

    virtual void blitAntiH(int x, int y, const SkAlpha antialias[], int len) = 0;
    virtual void blitAntiH(int x, int y, SkAlpha alpha)                = 0;
    virtual void blitAntiH(int x, int y, int width, SkAlpha alpha)     = 0;

    void blitAntiH(int, int, const SkAlpha[], const int16_t[]) override {
        SkDEBUGFAIL("Please call real blitter's blitAntiH instead.");
    }

    void blitV(int, int, int, SkAlpha) override {
        SkDEBUGFAIL("Please call real blitter's blitV instead.");
    }

    void blitH(int, int, int) override {
        SkDEBUGFAIL("Please call real blitter's blitH instead.");
    }

    void blitRect(int, int, int, int) override {
        SkDEBUGFAIL("Please call real blitter's blitRect instead.");
    }

    void blitAntiRect(int, int, int, int, SkAlpha, SkAlpha) override {
        SkDEBUGFAIL("Please call real blitter's blitAntiRect instead.");
    }

    virtual int getWidth() = 0;

    // Flush the additive alpha cache if floor(y) and floor(nextY) is different
    // (i.e., we'll start working on a new pixel row).
    virtual void flush_if_y_changed(SkFixed y, SkFixed nextY) = 0;
};

// One row of the mask blitter's coverage, addressed by absolute x. The row starts at the mask's
// left edge; x may be one pixel outside the row on either side, which the storage's slack bytes
// absorb. A null row means coverage goes to the blitter instead. It borrows the mask blitter's
// storage.
struct MaskRow {
    SkAlpha* fRow = nullptr;
    int      fLeft = 0;

    explicit operator bool() const { return fRow != nullptr; }
    SkAlpha& operator[](int x) const { return fRow[x - fLeft]; }
};

// We need this mask blitter because it significantly accelerates small path filling.
class MaskAdditiveBlitter : public AdditiveBlitter {
public:
    MaskAdditiveBlitter(SkBlitter*     realBlitter,
                        const SkIRect& ir,
                        const SkIRect& clipBounds,
                        bool           isInverse);
    ~MaskAdditiveBlitter() override { (*fRealBlitter).blitMask(fMask, fClipRect); }

    MaskAdditiveBlitter(const MaskAdditiveBlitter&) = delete;
    MaskAdditiveBlitter& operator=(const MaskAdditiveBlitter&) = delete;

    // Most of the time, we still consider this mask blitter as the real blitter
    // so we can accelerate blitRect and others. But sometimes we want to return
    // the absolute real blitter (e.g., when we fall back to the old code path).
    SkBlitter* getRealBlitter(bool forceRealBlitter) override {
        SkBlitter* blitter = forceRealBlitter ? fRealBlitter : this;
        return blitter;
    }

    // Virtual function is slow. So don't use this. Directly add alpha to the mask instead.
    void blitAntiH(int x, int y, const SkAlpha antialias[], int len) override;

    // Allowing following methods are used to blit rectangles during aaa_walk_convex_edges
    // Since there aren't many rectangles, we can still bear the slow speed of virtual functions.
    void blitAntiH(int x, int y, SkAlpha alpha) override;
    void blitAntiH(int x, int y, int width, SkAlpha alpha) override;
    void blitV(int x, int y, int height, SkAlpha alpha) override;
    void blitRect(int x, int y, int width, int height) override;
    void blitAntiRect(int x, int y, int width, int height, SkAlpha leftAlpha, SkAlpha rightAlpha)
            override;

    // The flush is only needed for RLE (RunBasedAdditiveBlitter)
    void flush_if_y_changed(SkFixed, SkFixed) override {}

    int getWidth() override { return fClipRect.width(); }

    static bool CanHandleRect(const SkIRect& bounds) {
        const int width = bounds.width();
        if (width > MaskAdditiveBlitter::kMAX_WIDTH) {
            return false;
        }
        const int64_t rb = SkAlign4(width);
        // use 64bits to detect overflow
        const int64_t storage = rb * bounds.height();

        const bool fits = (width <= MaskAdditiveBlitter::kMAX_WIDTH) &&
                          (storage <= MaskAdditiveBlitter::kMAX_STORAGE);
        return fits;
    }

    // True when columns [x, x + width) and rows [y, y + height) fit the mask, allowing the one
    // column of slack on either side.
    bool fitsMask(int x, int y, int width, int height) const {
        const SkIRect& b = fMask.fBounds;
        const bool fits = x >= b.fLeft - 1 && x + width <= b.fRight + 1 &&
                          y >= b.fTop && y + height <= b.fBottom;
        return fits;
    }

    // The coverage row for y, cached until a different row is requested.
    MaskRow getRow(int y) {
        if (y != fY) {
            fY   = y;
            fRow = rowAt(y);
        }
        return fRow;
    }

private:
    // so we don't try to do very wide things, where the RLE blitter would be faster
    static constexpr int kMAX_WIDTH   = 32;
    static constexpr int kMAX_STORAGE = 1024;

    MaskRow rowAt(int y) {
        MaskRow row{};
        const ptrdiff_t rowOffset = static_cast<ptrdiff_t>(y - fMask.fBounds.fTop) *
                                    static_cast<ptrdiff_t>(fMask.fRowBytes);
        row.fRow = fMask.image() + rowOffset;
        row.fLeft = fMask.fBounds.fLeft;
        return row;
    }

    SkBlitter* fRealBlitter = nullptr;
    SkMaskBuilder fMask;
    SkIRect    fClipRect = SkIRect::MakeEmpty();
    // The mask starts one byte in, and there is a byte of slack after it, because coverage can
    // land one pixel outside the bounds at either end due to precision error.
    alignas(4) uint8_t fStorage[kMAX_STORAGE + 8] = {};

    MaskRow  fRow;
    int      fY = 0;
};

MaskAdditiveBlitter::MaskAdditiveBlitter(SkBlitter*     realBlitter,
                                         const SkIRect& ir,
                                         const SkIRect& clipBounds,
                                         bool           isInverse)
    : fRealBlitter(realBlitter)
    , fMask(fStorage + 1, ir, ir.width(), SkMask::kA8_Format)
    , fY(ir.fTop - 1)
    {
    SkASSERT(CanHandleRect(ir));
    SkASSERT(!isInverse);
    static_cast<void>(isInverse);

    fClipRect = ir;
    if (!fClipRect.intersect(clipBounds)) {
        SkASSERT(0);
        fClipRect.setEmpty();
    }
}

void MaskAdditiveBlitter::blitAntiH(int, int, const SkAlpha[], int) {
    SK_ABORT("Don't use this; directly add alphas to the mask.");
}

void MaskAdditiveBlitter::blitAntiH(int x, int y, SkAlpha alpha) {
    SkASSERT_RELEASE(fitsMask(x, y, 1, 1));
    add_alpha(&getRow(y)[x], alpha);
}

void MaskAdditiveBlitter::blitAntiH(int x, int y, int width, SkAlpha alpha) {
    SkASSERT_RELEASE(fitsMask(x, y, width, 1));
    const MaskRow row = getRow(y);
    for (int i = 0; i < width; ++i) {
        add_alpha(&row[x + i], alpha);
    }
}

void MaskAdditiveBlitter::blitV(int x, int y, int height, SkAlpha alpha) {
    if (alpha == 0) {
        return;
    }
    SkASSERT_RELEASE(fitsMask(x, y, 1, height));
    // This must be called as if this is a real blitter.
    // So we directly set alpha rather than adding it.
    for (int i = 0; i < height; ++i) {
        rowAt(y + i)[x] = alpha;
    }
}

void MaskAdditiveBlitter::blitRect(int x, int y, int width, int height) {
    SkASSERT_RELEASE(fitsMask(x, y, width, height));
    // This must be called as if this is a real blitter.
    // So we directly set alpha rather than adding it.
    for (int i = 0; i < height; ++i) {
        const MaskRow row = rowAt(y + i);
        for (int j = 0; j < width; ++j) {
            row[x + j] = 0xFF;
        }
    }
}

void MaskAdditiveBlitter::blitAntiRect(int     x,
                                       int     y,
                                       int     width,
                                       int     height,
                                       SkAlpha leftAlpha,
                                       SkAlpha rightAlpha) {
    blitV(x, y, height, leftAlpha);
    blitV(x + 1 + width, y, height, rightAlpha);
    blitRect(x + 1, y, width, height);
}

class RunBasedAdditiveBlitter : public AdditiveBlitter {
public:
    RunBasedAdditiveBlitter(SkBlitter*     realBlitter,
                            const SkIRect& ir,
                            const SkIRect& clipBounds,
                            bool           isInverse);

    ~RunBasedAdditiveBlitter() override { flush(); }

    RunBasedAdditiveBlitter(const RunBasedAdditiveBlitter&) = delete;
    RunBasedAdditiveBlitter& operator=(const RunBasedAdditiveBlitter&) = delete;

    SkBlitter* getRealBlitter(bool) override { return fRealBlitter; }

    void blitAntiH(int x, int y, const SkAlpha antialias[], int len) override;
    void blitAntiH(int x, int y, SkAlpha alpha) override;
    void blitAntiH(int x, int y, int width, SkAlpha alpha) override;

    int getWidth() override { return fWidth; }

    void flush_if_y_changed(SkFixed y, SkFixed nextY) override {
        if (SkFixedFloorToInt(y) != SkFixedFloorToInt(nextY)) {
            flush();
        }
    }

protected:
    SkBlitter* fRealBlitter = nullptr;

    int fCurrY = 0;  // Current y coordinate.
    int fWidth = 0;  // Widest row of region to be blitted
    int fLeft = 0;   // Leftmost x coordinate in any row
    int fTop = 0;    // Initial y coordinate (top of bounds)

    // The next three variables are used to track a circular buffer that
    // contains the values used in SkAlphaRuns. These variables should only
    // ever be updated in advanceRuns(), and fRuns should always point to
    // a valid SkAlphaRuns... The buffer is owned by the real blitter.
    int         fRunsToBuffer = 0;
    uint8_t*    fRunsBuffer = nullptr;
    int         fCurrentRun = 0;
    SkAlphaRuns fRuns;

    int fOffsetX = 0;

    bool check(int x, int width) const {
        const bool inside = x >= 0 && x + width <= fWidth;
        return inside;
    }

    // extra one to store the zero at the end
    int getRunsSz() const {
        const int size = (fWidth + 1 + (fWidth + 2) / 2) * static_cast<int>(sizeof(int16_t));
        return size;
    }

    // This function updates the fRuns variable to point to the next buffer space
    // with adequate storage for a SkAlphaRuns. It mostly just advances fCurrentRun
    // and resets fRuns to point to an empty scanline. Each slot holds fWidth + 1 runs followed
    // by the alphas.
    void advanceRuns() {
        const size_t kRunsSz = static_cast<size_t>(getRunsSz());
        fCurrentRun          = (fCurrentRun + 1) % fRunsToBuffer;
        fRuns.fRuns          = reinterpret_cast<int16_t*>(
                fRunsBuffer + static_cast<size_t>(fCurrentRun) * kRunsSz);
        fRuns.fAlpha         = reinterpret_cast<SkAlpha*>(fRuns.fRuns + fWidth + 1);
        fRuns.reset(fWidth);
    }

    // Blitting 0xFF and 0 is much faster so we snap alphas close to them
    SkAlpha snapAlpha(SkAlpha alpha) {
        const SkAlpha snapped = alpha > 247 ? 0xFF : alpha < 8 ? 0x00 : alpha;
        return snapped;
    }

    void flush() {
        if (fCurrY >= fTop) {
            SkASSERT(fCurrentRun < fRunsToBuffer);
            for (int x = 0; fRuns.fRuns[x] != 0; x += fRuns.fRuns[x]) {
                // It seems that blitting 255 or 0 is much faster than blitting 254 or 1
                fRuns.fAlpha[x] = snapAlpha(fRuns.fAlpha[x]);
            }
            if (!fRuns.empty()) {
                (*fRealBlitter).blitAntiH(fLeft, fCurrY, fRuns.fAlpha, fRuns.fRuns);
                advanceRuns();
                fOffsetX = 0;
            }
            fCurrY = fTop - 1;
        }
    }

    void checkY(int y) {
        if (y != fCurrY) {
            flush();
            fCurrY = y;
        }
    }

    // Clips [x, x + len) of a row to the blitter's columns (x relative to fLeft); returns the
    // offset into the caller's coverage of the first kept pixel.
    int clipRun(int* x, int* len) const {
        int skip = 0;
        if (*x < 0) {
            *len += *x;
            skip = -*x;
            *x = 0;
        }
        *len = std::min(*len, fWidth - *x);
        return skip;
    }

    // Splits [x, x + len) into single-pixel runs, so per-pixel coverage can be added.
    void breakIntoSinglePixels(int x, int len) {
        fOffsetX = fRuns.add(x, 0, len, 0, 0, fOffsetX);  // Break the run
        for (int i = 0; i < len; i += fRuns.fRuns[x + i]) {
            for (int j = 1; j < fRuns.fRuns[x + i]; j++) {
                fRuns.fRuns[x + i + j]  = 1;
                fRuns.fAlpha[x + i + j] = fRuns.fAlpha[x + i];
            }
            fRuns.fRuns[x + i] = 1;
        }
    }
};

RunBasedAdditiveBlitter::RunBasedAdditiveBlitter(SkBlitter*     realBlitter,
                                                 const SkIRect& ir,
                                                 const SkIRect& clipBounds,
                                                 bool           isInverse) {
    fRealBlitter = realBlitter;

    SkIRect sectBounds = SkIRect::MakeEmpty();
    if (isInverse) {
        // We use the clip bounds instead of the ir, since we may be asked to
        // draw outside of the rect when we're a inverse filltype
        sectBounds = clipBounds;
    } else {
        if (!sectBounds.intersect(ir, clipBounds)) {
            sectBounds.setEmpty();
        }
    }

    const int left  = sectBounds.left();
    const int right = sectBounds.right();

    fLeft  = left;
    fWidth = right - left;
    fTop   = sectBounds.top();
    fCurrY = fTop - 1;

    fRunsToBuffer = (*realBlitter).requestRowsPreserved();
    fRunsBuffer   = static_cast<uint8_t*>((*realBlitter).allocBlitMemory(
            static_cast<size_t>(fRunsToBuffer) * static_cast<size_t>(getRunsSz())));
    fCurrentRun   = -1;

    advanceRuns();

    fOffsetX = 0;
}

void RunBasedAdditiveBlitter::blitAntiH(int x, int y, const SkAlpha antialias[], int len) {
    checkY(y);
    x -= fLeft;

    const int skip = clipRun(&x, &len);
    SkASSERT(check(x, len));

    if (x < fOffsetX) {
        fOffsetX = 0;
    }

    breakIntoSinglePixels(x, len);
    for (int i = 0; i < len; ++i) {
        add_alpha(&fRuns.fAlpha[x + i], antialias[skip + i]);
    }
}

void RunBasedAdditiveBlitter::blitAntiH(int x, int y, SkAlpha alpha) {
    checkY(y);
    x -= fLeft;

    if (x < fOffsetX) {
        fOffsetX = 0;
    }

    if (check(x, 1)) {
        fOffsetX = fRuns.add(x, 0, 1, 0, alpha, fOffsetX);
    }
}

void RunBasedAdditiveBlitter::blitAntiH(int x, int y, int width, SkAlpha alpha) {
    checkY(y);
    x -= fLeft;

    if (x < fOffsetX) {
        fOffsetX = 0;
    }

    if (check(x, width)) {
        fOffsetX = fRuns.add(x, 0, width, 0, alpha, fOffsetX);
    }
}

// This exists specifically for concave path filling.
// In those cases, we can easily accumulate alpha greater than 0xFF.
class SafeRLEAdditiveBlitter : public RunBasedAdditiveBlitter {
public:
    SafeRLEAdditiveBlitter(SkBlitter*     realBlitter,
                           const SkIRect& ir,
                           const SkIRect& clipBounds,
                           bool           isInverse)
            : RunBasedAdditiveBlitter(realBlitter, ir, clipBounds, isInverse) {}

    void blitAntiH(int x, int y, const SkAlpha antialias[], int len) override;
    void blitAntiH(int x, int y, SkAlpha alpha) override;
    void blitAntiH(int x, int y, int width, SkAlpha alpha) override;
};

void SafeRLEAdditiveBlitter::blitAntiH(int x, int y, const SkAlpha antialias[], int len) {
    checkY(y);
    x -= fLeft;

    const int skip = clipRun(&x, &len);
    SkASSERT(check(x, len));

    if (x < fOffsetX) {
        fOffsetX = 0;
    }

    breakIntoSinglePixels(x, len);
    for (int i = 0; i < len; ++i) {
        safely_add_alpha(&fRuns.fAlpha[x + i], antialias[skip + i]);
    }
}

void SafeRLEAdditiveBlitter::blitAntiH(int x, int y, SkAlpha alpha) {
    checkY(y);
    x -= fLeft;

    if (x < fOffsetX) {
        fOffsetX = 0;
    }

    if (check(x, 1)) {
        // Break the run
        fOffsetX = fRuns.add(x, 0, 1, 0, 0, fOffsetX);
        safely_add_alpha(&fRuns.fAlpha[x], alpha);
    }
}

void SafeRLEAdditiveBlitter::blitAntiH(int x, int y, int width, SkAlpha alpha) {
    checkY(y);
    x -= fLeft;

    if (x < fOffsetX) {
        fOffsetX = 0;
    }

    if (check(x, width)) {
        // Break the run
        fOffsetX = fRuns.add(x, 0, width, 0, 0, fOffsetX);
        for (int i = x; i < x + width; i += fRuns.fRuns[i]) {
            safely_add_alpha(&fRuns.fAlpha[i], alpha);
        }
    }
}

// Return the alpha of a trapezoid whose height is 1
static SkAlpha trapezoid_to_alpha(SkFixed l1, SkFixed l2) {
    SkASSERT(l1 >= 0 && l2 >= 0);
    const SkFixed area = (l1 + l2) / 2;
    const SkAlpha alpha = SkTo<SkAlpha>(area >> 8);
    return alpha;
}

// The alpha of right-triangle (a, a*b). b may exceed SK_Fixed1 (skbug.com/40040159), so the
// result keeps only its low eight bits.
static SkAlpha partial_triangle_to_alpha(SkFixed a, SkFixed b) {
    SkASSERT(a <= SK_Fixed1);

    // Approximating...
    // SkFixed area = SkFixedMul(a, SkFixedMul(a,b)) / 2;
    const SkFixed area = (a >> 11) * (a >> 11) * (b >> 11);

    const SkAlpha alpha = SkTo<SkAlpha>((area >> 8) & 0xFF);
    return alpha;
}

static SkAlpha get_partial_alpha(SkAlpha alpha, SkFixed partialHeight) {
    const SkAlpha partial = SkToU8(SkFixedRoundToInt(alpha * partialHeight));
    return partial;
}

static SkAlpha get_partial_alpha(SkAlpha alpha, SkAlpha fullAlpha) {
    const SkAlpha partial = static_cast<SkAlpha>((alpha * fullAlpha) >> 8);
    return partial;
}

// For SkFixed that's close to SK_Fixed1, we can't convert it to alpha by just shifting right.
// For example, when f = SK_Fixed1, right shifting 8 will get 256, but we need 255.
// This is rarely the problem so we'll only use this for blitting rectangles.
static SkAlpha fixed_to_alpha(SkFixed f) {
    SkASSERT(f <= SK_Fixed1);
    const SkAlpha alpha = get_partial_alpha(0xFF, f);
    return alpha;
}

// Suppose that line (l1, y)-(r1, y+1) intersects with (l2, y)-(r2, y+1),
// approximate (very coarsely) the x coordinate of the intersection.
static SkFixed approximate_intersection(SkFixed l1, SkFixed r1, SkFixed l2, SkFixed r2) {
    if (l1 > r1) {
        std::swap(l1, r1);
    }
    if (l2 > r2) {
        std::swap(l2, r2);
    }
    const SkFixed intersection = (std::max(l1, l2) + std::min(r1, r2)) / 2;
    return intersection;
}

// Here we always send in l < SK_Fixed1, and the first alpha we want to compute is alphas[0]
static void compute_alpha_above_line(SkAlpha* alphas,
                                     SkFixed  l,
                                     SkFixed  r,
                                     SkFixed  dY,
                                     SkAlpha  fullAlpha) {
    SkASSERT(l <= r);
    SkASSERT(l >> 16 == 0);
    const int R = SkFixedCeilToInt(r);
    if (R == 0) {
        return;
    } else if (R == 1) {
        alphas[0] = get_partial_alpha(static_cast<SkAlpha>(((R << 17) - l - r) >> 9), fullAlpha);
    } else {
        const SkFixed first = SK_Fixed1 - l;   // horizontal edge length of the left-most triangle
        const SkFixed last = r - ((R - 1) << 16);  // horizontal edge length of right-most triangle
        const SkFixed firstH = SkFixedMul(first, dY);  // vertical edge of the left-most triangle
        alphas[0]       = static_cast<SkAlpha>(SkFixedMul(first, firstH) >> 9);  // triangle alpha
        SkFixed alpha16 = Sk32_sat_add(firstH, dY >> 1);                // rectangle plus triangle
        for (int i = 1; i < R - 1; ++i) {
            alphas[i] = static_cast<SkAlpha>(alpha16 >> 8);
            alpha16 = Sk32_sat_add(alpha16, dY);
        }
        alphas[R - 1] = static_cast<SkAlpha>(fullAlpha - partial_triangle_to_alpha(last, dY));
    }
}

// Here we always send in l < SK_Fixed1, and the first alpha we want to compute is alphas[0]
static void compute_alpha_below_line(SkAlpha* alphas,
                                     SkFixed  l,
                                     SkFixed  r,
                                     SkFixed  dY,
                                     SkAlpha  fullAlpha) {
    SkASSERT(l <= r);
    SkASSERT(l >> 16 == 0);
    const int R = SkFixedCeilToInt(r);
    if (R == 0) {
        return;
    } else if (R == 1) {
        alphas[0] = get_partial_alpha(trapezoid_to_alpha(l, r), fullAlpha);
    } else {
        const SkFixed first = SK_Fixed1 - l;   // horizontal edge length of the left-most triangle
        const SkFixed last = r - ((R - 1) << 16);  // horizontal edge length of right-most triangle
        const SkFixed lastH = SkFixedMul(last, dY);    // vertical edge of the right-most triangle
        alphas[R - 1]   = static_cast<SkAlpha>(SkFixedMul(last, lastH) >> 9);  // triangle alpha
        SkFixed alpha16 = Sk32_sat_add(lastH, dY >> 1);             // rectangle plus triangle
        for (int i = R - 2; i > 0; i--) {
            alphas[i] = static_cast<SkAlpha>((alpha16 >> 8) & 0xFF);
            alpha16 = Sk32_sat_add(alpha16, dY);
        }
        alphas[0] = static_cast<SkAlpha>(fullAlpha - partial_triangle_to_alpha(first, dY));
    }
}

// Note that if fullAlpha != 0xFF, we'll multiply alpha by fullAlpha
static void blit_single_alpha(AdditiveBlitter* blitter,
                              int y,
                              int x,
                              SkAlpha alpha,
                              SkAlpha fullAlpha,
                              MaskRow  maskRow,
                              bool noRealBlitter) {
    if (maskRow) {
        if (fullAlpha == 0xFF && !noRealBlitter) {  // noRealBlitter is needed for concave paths
            maskRow[x] = alpha;
        } else {
            safely_add_alpha(&maskRow[x], get_partial_alpha(alpha, fullAlpha));
        }
    } else {
        if (fullAlpha == 0xFF && !noRealBlitter) {
            (*(*blitter).getRealBlitter()).blitV(x, y, 1, alpha);
        } else {
            (*blitter).blitAntiH(x, y, get_partial_alpha(alpha, fullAlpha));
        }
    }
}

static void blit_two_alphas(AdditiveBlitter* blitter,
                            int y,
                            int x,
                            SkAlpha a1,
                            SkAlpha a2,
                            SkAlpha fullAlpha,
                            MaskRow  maskRow,
                            bool noRealBlitter) {
    if (maskRow) {
        safely_add_alpha(&maskRow[x], a1);
        safely_add_alpha(&maskRow[x + 1], a2);
    } else {
        if (fullAlpha == 0xFF && !noRealBlitter) {
            (*(*blitter).getRealBlitter()).blitAntiH2(x, y, a1, a2);
        } else {
            (*blitter).blitAntiH(x, y, a1);
            (*blitter).blitAntiH(x + 1, y, a2);
        }
    }
}

static void blit_full_alpha(AdditiveBlitter* blitter,
                            int y,
                            int x,
                            int len,
                            SkAlpha fullAlpha,
                            MaskRow  maskRow,
                            bool noRealBlitter) {
    if (maskRow) {
        for (int i = 0; i < len; ++i) {
            safely_add_alpha(&maskRow[x + i], fullAlpha);
        }
    } else {
        if (fullAlpha == 0xFF && !noRealBlitter) {
            (*(*blitter).getRealBlitter()).blitH(x, y, len);
        } else {
            (*blitter).blitAntiH(x, y, len, fullAlpha);
        }
    }
}

static void blit_aaa_trapezoid_row(AdditiveBlitter* blitter,
                                   int              y,
                                   SkFixed          ul,
                                   SkFixed          ur,
                                   SkFixed          ll,
                                   SkFixed          lr,
                                   SkFixed          lDY,
                                   SkFixed          rDY,
                                   SkAlpha          fullAlpha,
                                   MaskRow          maskRow,
                                   bool             noRealBlitter) {
    const int L = SkFixedFloorToInt(ul);
    const int R = SkFixedCeilToInt(lr);
    const int len = R - L;

    if (len == 1) {
        const SkAlpha alpha = trapezoid_to_alpha(ur - ul, lr - ll);
        blit_single_alpha(blitter, y, L, alpha, fullAlpha, maskRow, noRealBlitter);
        return;
    }

    // alphas[0..len] and tempAlphas[0..len] (one array), and runs[0..len].
    constexpr int kQuickLen = 31;
    SkAlpha quickAlphas[(kQuickLen + 1) * 2] = {};
    int16_t quickRuns[kQuickLen + 1] = {};
    skia_private::AutoTArray<SkAlpha> heapAlphas;
    skia_private::AutoTArray<int16_t> heapRuns;
    SkAlpha* alphas = quickAlphas;
    int16_t* runs = quickRuns;
    if (len > kQuickLen) {
        heapAlphas.reset(static_cast<size_t>(len + 1) * 2);
        heapRuns.reset(static_cast<size_t>(len + 1));
        alphas = heapAlphas.data();
        runs = heapRuns.data();
    }

    SkAlpha* tempAlphas = alphas + len + 1;

    for (int i = 0; i < len; ++i) {
        runs[i]   = 1;
        alphas[i] = fullAlpha;
    }
    runs[len] = 0;

    const int uL = SkFixedFloorToInt(ul);
    const int lL = SkFixedCeilToInt(ll);
    if (uL + 2 == lL) {  // We only need to compute two triangles, accelerate this special case
        const SkFixed first  = SkIntToFixed(uL) + SK_Fixed1 - ul;
        const SkFixed second = ll - ul - first;
        const SkAlpha a1 = static_cast<SkAlpha>(fullAlpha - partial_triangle_to_alpha(first, lDY));
        const SkAlpha a2 = partial_triangle_to_alpha(second, lDY);
        alphas[0]      = alphas[0] > a1 ? static_cast<SkAlpha>(alphas[0] - a1) : 0;
        alphas[1]      = alphas[1] > a2 ? static_cast<SkAlpha>(alphas[1] - a2) : 0;
    } else {
        compute_alpha_below_line(
                tempAlphas + uL - L, ul - SkIntToFixed(uL), ll - SkIntToFixed(uL), lDY, fullAlpha);
        for (int i = uL; i < lL; ++i) {
            if (alphas[i - L] > tempAlphas[i - L]) {
                alphas[i - L] -= tempAlphas[i - L];
            } else {
                alphas[i - L] = 0;
            }
        }
    }

    const int uR = SkFixedFloorToInt(ur);
    const int lR = SkFixedCeilToInt(lr);
    if (uR + 2 == lR) {  // We only need to compute two triangles, accelerate this special case
        const SkFixed first   = SkIntToFixed(uR) + SK_Fixed1 - ur;
        const SkFixed second  = lr - ur - first;
        const SkAlpha a1 = partial_triangle_to_alpha(first, rDY);
        const SkAlpha a2 = static_cast<SkAlpha>(fullAlpha - partial_triangle_to_alpha(second, rDY));
        alphas[len - 2] = alphas[len - 2] > a1 ? static_cast<SkAlpha>(alphas[len - 2] - a1) : 0;
        alphas[len - 1] = alphas[len - 1] > a2 ? static_cast<SkAlpha>(alphas[len - 1] - a2) : 0;
    } else {
        compute_alpha_above_line(
                tempAlphas + uR - L, ur - SkIntToFixed(uR), lr - SkIntToFixed(uR), rDY, fullAlpha);
        for (int i = uR; i < lR; ++i) {
            if (alphas[i - L] > tempAlphas[i - L]) {
                alphas[i - L] -= tempAlphas[i - L];
            } else {
                alphas[i - L] = 0;
            }
        }
    }

    if (maskRow) {
        for (int i = 0; i < len; ++i) {
            safely_add_alpha(&maskRow[L + i], alphas[i]);
        }
    } else {
        if (fullAlpha == 0xFF && !noRealBlitter) {
            // Real blitter is faster than RunBasedAdditiveBlitter
            (*(*blitter).getRealBlitter()).blitAntiH(L, y, alphas, runs);
        } else {
            (*blitter).blitAntiH(L, y, alphas, len);
        }
    }
}

static void blit_trapezoid_row(AdditiveBlitter* blitter,
                               int y,
                               SkFixed ul,
                               SkFixed ur,
                               SkFixed ll,
                               SkFixed lr,
                               SkFixed lDY,
                               SkFixed rDY,
                               SkAlpha fullAlpha,
                               MaskRow  maskRow,
                               bool noRealBlitter) {
    SkASSERT(lDY >= 0 && rDY >= 0);  // We should only send in the absolte value

    if (ul > ur) {
        return;
    }

    // Edge crosses. Approximate it. This should only happend due to precision limit,
    // so the approximation could be very coarse.
    if (ll > lr) {
        ll = lr = approximate_intersection(ul, ll, ur, lr);
    }

    if (ul == ur && ll == lr) {
        return;  // empty trapzoid
    }

    // We're going to use the left line ul-ll and the rite line ur-lr
    // to exclude the area that's not covered by the path.
    // Swapping (ul, ll) or (ur, lr) won't affect that exclusion
    // so we'll do that for simplicity.
    if (ul > ll) {
        std::swap(ul, ll);
    }
    if (ur > lr) {
        std::swap(ur, lr);
    }

    SkFixed joinLeft = SkFixedCeilToFixed(ll);
    SkFixed joinRite = SkFixedFloorToFixed(ur);
    if (joinLeft <= joinRite) {  // There's a rect from joinLeft to joinRite that we can blit
        if (ul < joinLeft) {
            int len = SkFixedCeilToInt(joinLeft - ul);
            if (len == 1) {
                SkAlpha alpha = trapezoid_to_alpha(joinLeft - ul, joinLeft - ll);
                blit_single_alpha(blitter,
                                  y,
                                  ul >> 16,
                                  alpha,
                                  fullAlpha,
                                  maskRow,
                                  noRealBlitter);
            } else if (len == 2) {
                SkFixed first  = joinLeft - SK_Fixed1 - ul;
                SkFixed second = ll - ul - first;
                SkAlpha a1     = partial_triangle_to_alpha(first, lDY);
                SkAlpha a2     = fullAlpha - partial_triangle_to_alpha(second, lDY);
                blit_two_alphas(blitter,
                                y,
                                ul >> 16,
                                a1,
                                a2,
                                fullAlpha,
                                maskRow,
                                noRealBlitter);
            } else {
                blit_aaa_trapezoid_row(blitter,
                                       y,
                                       ul,
                                       joinLeft,
                                       ll,
                                       joinLeft,
                                       lDY,
                                       SK_MaxS32,
                                       fullAlpha,
                                       maskRow,
                                       noRealBlitter);
            }
        }
        // SkAAClip requires that we blit from left to right.
        // Hence we must blit [ul, joinLeft] before blitting [joinLeft, joinRite]
        if (joinLeft < joinRite) {
            blit_full_alpha(blitter,
                            y,
                            SkFixedFloorToInt(joinLeft),
                            SkFixedFloorToInt(joinRite - joinLeft),
                            fullAlpha,
                            maskRow,
                            noRealBlitter);
        }
        if (lr > joinRite) {
            int len = SkFixedCeilToInt(lr - joinRite);
            if (len == 1) {
                SkAlpha alpha = trapezoid_to_alpha(ur - joinRite, lr - joinRite);
                blit_single_alpha(blitter,
                                  y,
                                  joinRite >> 16,
                                  alpha,
                                  fullAlpha,
                                  maskRow,
                                  noRealBlitter);
            } else if (len == 2) {
                SkFixed first  = joinRite + SK_Fixed1 - ur;
                SkFixed second = lr - ur - first;
                SkAlpha a1     = fullAlpha - partial_triangle_to_alpha(first, rDY);
                SkAlpha a2     = partial_triangle_to_alpha(second, rDY);
                blit_two_alphas(blitter,
                                y,
                                joinRite >> 16,
                                a1,
                                a2,
                                fullAlpha,
                                maskRow,
                                noRealBlitter);
            } else {
                blit_aaa_trapezoid_row(blitter,
                                       y,
                                       joinRite,
                                       ur,
                                       joinRite,
                                       lr,
                                       SK_MaxS32,
                                       rDY,
                                       fullAlpha,
                                       maskRow,
                                       noRealBlitter);
            }
        }
    } else {
        blit_aaa_trapezoid_row(blitter,
                               y,
                               ul,
                               ur,
                               ll,
                               lr,
                               lDY,
                               rDY,
                               fullAlpha,
                               maskRow,
                               noRealBlitter);
    }
}

static bool compare_edges(const SkAnalyticEdge* a, const SkAnalyticEdge* b) {
    bool before = false;
    if ((*a).fUpperY != (*b).fUpperY) {
        before = (*a).fUpperY < (*b).fUpperY;
    } else if ((*a).fX != (*b).fX) {
        before = (*a).fX < (*b).fX;
    } else {
        before = (*a).fDX < (*b).fDX;
    }
    return before;
}

// count > 0. Links list[0..count) in sorted order; returns the first edge and sets *last.
static SkAnalyticEdge* sort_edges(SkAnalyticEdge* list[], int count, SkAnalyticEdge** last) {
    SkASSERT(count > 0);
    SkTQSort(list, list + count, compare_edges);

    // now make the edges linked in sorted order
    for (int i = 1; i < count; ++i) {
        (*list[i - 1]).fNext = list[i];
        (*list[i]).fPrev     = list[i - 1];
    }

    *last = list[count - 1];
    SkAnalyticEdge* first = list[0];
    return first;
}

static void validate_sort(const SkAnalyticEdge* edge) {
#ifdef SK_DEBUG
    SkFixed y = SkIntToFixed(-32768);

    while ((*edge).fUpperY != SK_MaxS32) {
        (*edge).validate();
        SkASSERT(y <= (*edge).fUpperY);

        y    = (*edge).fUpperY;
        edge = (*edge).fNext;
    }
#endif
}

// For an edge, we consider it smooth if the Dx doesn't change much, and Dy is large enough
// For curves that are updating, the Dx is not changing much if fQDx/fCDx and fQDy/fCDy are
// relatively large compared to fQDDx/QCDDx and fQDDy/fCDDy
static bool is_smooth_enough(SkAnalyticEdge* thisEdge, SkAnalyticEdge* nextEdge, int stop_y) {
    if ((*thisEdge).fCurveCount < 0) {
        // A negative curve count is only set by SkAnalyticCubicEdge.
        const SkAnalyticCubicEdge* cEdge = static_cast<SkAnalyticCubicEdge*>(thisEdge);
        const int ddshift = (*cEdge).fCurveShift;
        const bool smooth =
                SkAbs32((*cEdge).fCDx) >> 1 >= SkAbs32((*cEdge).fCDDx) >> ddshift &&
                SkAbs32((*cEdge).fCDy) >> 1 >= SkAbs32((*cEdge).fCDDy) >> ddshift &&
                // current Dy is (fCDy - (fCDDy >> ddshift)) >> dshift
                ((*cEdge).fCDy - ((*cEdge).fCDDy >> ddshift)) >> (*cEdge).fToFixedShift >=
                        SK_Fixed1;
        return smooth;
    } else if ((*thisEdge).fCurveCount > 0) {
        // A positive curve count is only set by SkAnalyticQuadraticEdge.
        const SkAnalyticQuadraticEdge* qEdge = static_cast<SkAnalyticQuadraticEdge*>(thisEdge);
        const bool smooth =
                SkAbs32((*qEdge).fQDx) >> 1 >= SkAbs32((*qEdge).fQDDx) &&
                SkAbs32((*qEdge).fQDy) >> 1 >= SkAbs32((*qEdge).fQDDy) &&
                // current Dy is (fQDy - fQDDy) >> shift
                ((*qEdge).fQDy - (*qEdge).fQDDy) >> (*qEdge).fCurveShift >= SK_Fixed1;
        return smooth;
    }
    // DDx should be small and Dy should be large
    const bool smooth = SkAbs32(Sk32_sat_sub((*nextEdge).fDX, (*thisEdge).fDX)) <= SK_Fixed1 &&
                        (*nextEdge).fLowerY - (*nextEdge).fUpperY >= SK_Fixed1;
    return smooth;
}

// Check if the leftE and riteE are changing smoothly in terms of fDX.
// If yes, we can later skip the fractional y and directly jump to integer y.
static bool is_smooth_enough(SkAnalyticEdge* leftE,
                             SkAnalyticEdge* riteE,
                             SkAnalyticEdge* currE,
                             int             stop_y) {
    if ((*currE).fUpperY >= SkLeftShift(stop_y, 16)) {
        return false;  // We're at the end so we won't skip anything
    }
    if ((*leftE).fLowerY + SK_Fixed1 < (*riteE).fLowerY) {
        return is_smooth_enough(leftE, currE, stop_y);  // Only leftE is changing
    } else if ((*leftE).fLowerY > (*riteE).fLowerY + SK_Fixed1) {
        return is_smooth_enough(riteE, currE, stop_y);  // Only riteE is changing
    }

    // Now both edges are changing, find the second next edge
    SkAnalyticEdge* nextCurrE = (*currE).fNext;
    if ((*nextCurrE).fUpperY >= stop_y << 16) {  // Check if we're at the end
        return false;
    }
    // Ensure that currE is the next left edge and nextCurrE is the next right edge. Swap if not.
    if ((*nextCurrE).fUpperX < (*currE).fUpperX) {
        std::swap(currE, nextCurrE);
    }
    return is_smooth_enough(leftE, currE, stop_y) && is_smooth_enough(riteE, nextCurrE, stop_y);
}

static void aaa_walk_convex_edges(SkAnalyticEdge*  prevHead,
                                  AdditiveBlitter* blitter,
                                  int              start_y,
                                  int              stop_y,
                                  SkFixed          leftBound,
                                  SkFixed          riteBound,
                                  bool             isUsingMask) {
    validate_sort((*prevHead).fNext);

    SkAnalyticEdge* leftE = (*prevHead).fNext;
    SkAnalyticEdge* riteE = (*leftE).fNext;
    SkAnalyticEdge* currE = (*riteE).fNext;

    SkFixed y = std::max((*leftE).fUpperY, (*riteE).fUpperY);

    for (;;) {
        // We have to check fLowerY first because some edges might be alone (e.g., there's only
        // a left edge but no right edge in a given y scan line) due to precision limit.
        while ((*leftE).fLowerY <= y) {  // Due to smooth jump, we may pass multiple short edges
            if (!(*leftE).update(y)) {
                if (SkFixedFloorToInt((*currE).fUpperY) >= stop_y) {
                    return;
                }
                leftE = currE;
                currE = (*currE).fNext;
            }
        }
        while ((*riteE).fLowerY <= y) {  // Due to smooth jump, we may pass multiple short edges
            if (!(*riteE).update(y)) {
                if (SkFixedFloorToInt((*currE).fUpperY) >= stop_y) {
                    return;
                }
                riteE = currE;
                currE = (*currE).fNext;
            }
        }

        SkASSERT(leftE);
        SkASSERT(riteE);

        // check our bottom clip
        if (SkFixedFloorToInt(y) >= stop_y) {
            break;
        }

        SkASSERT(SkFixedFloorToInt((*leftE).fUpperY) <= stop_y);
        SkASSERT(SkFixedFloorToInt((*riteE).fUpperY) <= stop_y);

        (*leftE).goY(y);
        (*riteE).goY(y);

        if ((*leftE).fX > (*riteE).fX || ((*leftE).fX == (*riteE).fX && (*leftE).fDX > (*riteE).fDX)) {
            std::swap(leftE, riteE);
        }

        SkFixed local_bot_fixed = std::min((*leftE).fLowerY, (*riteE).fLowerY);
        if (is_smooth_enough(leftE, riteE, currE, stop_y)) {
            local_bot_fixed = SkFixedCeilToFixed(local_bot_fixed);
        }
        local_bot_fixed = std::min(local_bot_fixed, SkIntToFixed(stop_y));

        SkFixed left  = std::max(leftBound, (*leftE).fX);
        SkFixed dLeft = (*leftE).fDX;
        SkFixed rite  = std::min(riteBound, (*riteE).fX);
        SkFixed dRite = (*riteE).fDX;
        if (0 == (dLeft | dRite)) {
            int     fullLeft    = SkFixedCeilToInt(left);
            int     fullRite    = SkFixedFloorToInt(rite);
            SkFixed partialLeft = SkIntToFixed(fullLeft) - left;
            SkFixed partialRite = rite - SkIntToFixed(fullRite);
            int     fullTop     = SkFixedCeilToInt(y);
            int     fullBot     = SkFixedFloorToInt(local_bot_fixed);
            SkFixed partialTop  = SkIntToFixed(fullTop) - y;
            SkFixed partialBot  = local_bot_fixed - SkIntToFixed(fullBot);
            if (fullTop > fullBot) {  // The rectangle is within one pixel height...
                partialTop -= (SK_Fixed1 - partialBot);
                partialBot = 0;
            }

            if (fullRite >= fullLeft) {
                if (partialTop > 0) {  // blit first partial row
                    if (partialLeft > 0) {
                        (*blitter).blitAntiH(fullLeft - 1,
                                           fullTop - 1,
                                           fixed_to_alpha(SkFixedMul(partialTop, partialLeft)));
                    }
                    (*blitter).blitAntiH(
                            fullLeft, fullTop - 1, fullRite - fullLeft, fixed_to_alpha(partialTop));
                    if (partialRite > 0) {
                        (*blitter).blitAntiH(fullRite,
                                           fullTop - 1,
                                           fixed_to_alpha(SkFixedMul(partialTop, partialRite)));
                    }
                    (*blitter).flush_if_y_changed(y, y + partialTop);
                }

                // Blit all full-height rows from fullTop to fullBot
                if (fullBot > fullTop &&
                    // SkAAClip cannot handle the empty rect so check the non-emptiness here
                    // (bug chromium:662800)
                    (fullRite > fullLeft || fixed_to_alpha(partialLeft) > 0 ||
                     fixed_to_alpha(partialRite) > 0)) {
                    (*(*blitter).getRealBlitter()).blitAntiRect(fullLeft - 1,
                                                            fullTop,
                                                            fullRite - fullLeft,
                                                            fullBot - fullTop,
                                                            fixed_to_alpha(partialLeft),
                                                            fixed_to_alpha(partialRite));
                }

                if (partialBot > 0) {  // blit last partial row
                    if (partialLeft > 0) {
                        (*blitter).blitAntiH(fullLeft - 1,
                                           fullBot,
                                           fixed_to_alpha(SkFixedMul(partialBot, partialLeft)));
                    }
                    (*blitter).blitAntiH(
                            fullLeft, fullBot, fullRite - fullLeft, fixed_to_alpha(partialBot));
                    if (partialRite > 0) {
                        (*blitter).blitAntiH(fullRite,
                                           fullBot,
                                           fixed_to_alpha(SkFixedMul(partialBot, partialRite)));
                    }
                }
            } else {
                // Normal conditions, this means left and rite are within the same pixel, but if
                // both left and rite were < leftBounds or > rightBounds, both edges are clipped and
                // we should not do any blitting (particularly since the negative width saturates to
                // full alpha).
                SkFixed width = rite - left;
                if (width > 0) {
                    if (partialTop > 0) {
                        (*blitter).blitAntiH(fullLeft - 1,
                                           fullTop - 1,
                                           1,
                                           fixed_to_alpha(SkFixedMul(partialTop, width)));
                        (*blitter).flush_if_y_changed(y, y + partialTop);
                    }
                    if (fullBot > fullTop) {
                        (*(*blitter).getRealBlitter()).blitV(
                                fullLeft - 1, fullTop, fullBot - fullTop, fixed_to_alpha(width));
                    }
                    if (partialBot > 0) {
                        (*blitter).blitAntiH(fullLeft - 1,
                                           fullBot,
                                           1,
                                           fixed_to_alpha(SkFixedMul(partialBot, width)));
                    }
                }
            }

            y = local_bot_fixed;
        } else {
            // The following constant are used to snap X
            // We snap X mainly for speedup (no tiny triangle) and
            // avoiding edge cases caused by precision errors
            const SkFixed kSnapDigit = SK_Fixed1 >> 4;
            const SkFixed kSnapHalf  = kSnapDigit >> 1;
            const SkFixed kSnapMask  = (-1 ^ (kSnapDigit - 1));
            left += kSnapHalf;
            rite += kSnapHalf;  // For fast rounding

            // Number of blit_trapezoid_row calls we'll have
            int count = SkFixedCeilToInt(local_bot_fixed) - SkFixedFloorToInt(y);

            // If we're using mask blitter, we advance the mask row in this function
            // to save some "if" condition checks.
            MaskRow maskRow = isUsingMask
                                       ? (*static_cast<MaskAdditiveBlitter*>(blitter)).getRow(y >> 16)
                                       : MaskRow{};

            // Instead of writing one loop that handles both partial-row blit_trapezoid_row
            // and full-row trapezoid_row together, we use the following 3-stage flow to
            // handle partial-row blit and full-row blit separately. It will save us much time
            // on changing y, left, and rite.
            if (count > 1) {
                if (static_cast<int>(y & 0xFFFF0000) != y) {  // There's a partial-row on the top
                    count--;
                    SkFixed nextY    = SkFixedCeilToFixed(y + 1);
                    SkFixed dY       = nextY - y;
                    SkFixed nextLeft = left + SkFixedMul(dLeft, dY);
                    SkFixed nextRite = rite + SkFixedMul(dRite, dY);
                    SkASSERT((left & kSnapMask) >= leftBound && (rite & kSnapMask) <= riteBound &&
                             (nextLeft & kSnapMask) >= leftBound &&
                             (nextRite & kSnapMask) <= riteBound);
                    blit_trapezoid_row(blitter,
                                       y >> 16,
                                       left & kSnapMask,
                                       rite & kSnapMask,
                                       nextLeft & kSnapMask,
                                       nextRite & kSnapMask,
                                       (*leftE).fDY,
                                       (*riteE).fDY,
                                       get_partial_alpha(0xFF, dY),
                                       maskRow,
                                       /*noRealBlitter=*/false);
                    (*blitter).flush_if_y_changed(y, nextY);
                    left = nextLeft;
                    rite = nextRite;
                    y    = nextY;
                }

                while (count > 1) {  // Full rows in the middle
                    count--;
                    if (isUsingMask) {
                        maskRow = (*static_cast<MaskAdditiveBlitter*>(blitter)).getRow(y >> 16);
                    }
                    const SkFixed nextY = y + SK_Fixed1;
                    const SkFixed nextLeft = left + dLeft;
                    const SkFixed nextRite = rite + dRite;
                    SkASSERT((left & kSnapMask) >= leftBound && (rite & kSnapMask) <= riteBound &&
                             (nextLeft & kSnapMask) >= leftBound &&
                             (nextRite & kSnapMask) <= riteBound);
                    blit_trapezoid_row(blitter,
                                       y >> 16,
                                       left & kSnapMask,
                                       rite & kSnapMask,
                                       nextLeft & kSnapMask,
                                       nextRite & kSnapMask,
                                       (*leftE).fDY,
                                       (*riteE).fDY,
                                       0xFF,
                                       maskRow,
                                       /*noRealBlitter=*/false);
                    (*blitter).flush_if_y_changed(y, nextY);
                    left = nextLeft;
                    rite = nextRite;
                    y    = nextY;
                }
            }

            if (isUsingMask) {
                maskRow = (*static_cast<MaskAdditiveBlitter*>(blitter)).getRow(y >> 16);
            }

            SkFixed dY = local_bot_fixed - y;  // partial-row on the bottom
            SkASSERT(dY <= SK_Fixed1);
            // Smooth jumping to integer y may make the last nextLeft/nextRite out of bound.
            // Take them back into the bound here.
            // Note that we substract kSnapHalf later so we have to add them to leftBound/riteBound
            SkFixed nextLeft = std::max(left + SkFixedMul(dLeft, dY), leftBound + kSnapHalf);
            SkFixed nextRite = std::min(rite + SkFixedMul(dRite, dY), riteBound + kSnapHalf);
            SkASSERT((left & kSnapMask) >= leftBound && (rite & kSnapMask) <= riteBound &&
                     (nextLeft & kSnapMask) >= leftBound && (nextRite & kSnapMask) <= riteBound);
            blit_trapezoid_row(blitter,
                               y >> 16,
                               left & kSnapMask,
                               rite & kSnapMask,
                               nextLeft & kSnapMask,
                               nextRite & kSnapMask,
                               (*leftE).fDY,
                               (*riteE).fDY,
                               get_partial_alpha(0xFF, dY),
                               maskRow,
                               /*noRealBlitter=*/false);
            (*blitter).flush_if_y_changed(y, local_bot_fixed);
            left = nextLeft;
            rite = nextRite;
            y    = local_bot_fixed;
            left -= kSnapHalf;
            rite -= kSnapHalf;
        }

        (*leftE).fX = left;
        (*riteE).fX = rite;
        (*leftE).fY = y;
        (*riteE).fY = y;
    }
}

static void update_next_next_y(SkFixed y, SkFixed nextY, SkFixed* nextNextY) {
    *nextNextY = y > nextY && y < *nextNextY ? y : *nextNextY;
}

static void check_intersection(const SkAnalyticEdge* edge, SkFixed nextY, SkFixed* nextNextY) {
    if ((*(*edge).fPrev).fPrev && (*(*edge).fPrev).fX + (*(*edge).fPrev).fDX > (*edge).fX + (*edge).fDX) {
        *nextNextY = nextY + (SK_Fixed1 >> SkAnalyticEdge::kDefaultAccuracy);
    }
}

static void check_intersection_fwd(const SkAnalyticEdge* edge, SkFixed nextY, SkFixed* nextNextY) {
    if ((*(*edge).fNext).fNext && (*edge).fX + (*edge).fDX > (*(*edge).fNext).fX + (*(*edge).fNext).fDX) {
        *nextNextY = nextY + (SK_Fixed1 >> SkAnalyticEdge::kDefaultAccuracy);
    }
}

static void insert_new_edges(SkAnalyticEdge* newEdge, SkFixed y, SkFixed* nextNextY) {
    if ((*newEdge).fUpperY > y) {
        update_next_next_y((*newEdge).fUpperY, y, nextNextY);
        return;
    }
    SkAnalyticEdge* prev = (*newEdge).fPrev;
    if ((*prev).fX <= (*newEdge).fX) {
        while ((*newEdge).fUpperY <= y) {
            check_intersection(newEdge, y, nextNextY);
            update_next_next_y((*newEdge).fLowerY, y, nextNextY);
            newEdge = (*newEdge).fNext;
        }
        update_next_next_y((*newEdge).fUpperY, y, nextNextY);
        return;
    }
    // find first x pos to insert
    SkAnalyticEdge* start = backward_insert_start(prev, (*newEdge).fX);
    // insert the lot, fixing up the links as we go
    do {
        SkAnalyticEdge* next = (*newEdge).fNext;
        bool alreadyInPlace = false;
        for (;;) {
            if ((*start).fNext == newEdge) {
                alreadyInPlace = true;
                break;
            }
            SkAnalyticEdge* after = (*start).fNext;
            if ((*after).fX >= (*newEdge).fX) {
                break;
            }
            SkASSERT(start != after);
            start = after;
        }
        if (!alreadyInPlace) {
            remove_edge(newEdge);
            insert_edge_after(newEdge, start);
        }
        check_intersection(newEdge, y, nextNextY);
        check_intersection_fwd(newEdge, y, nextNextY);
        update_next_next_y((*newEdge).fLowerY, y, nextNextY);
        start   = newEdge;
        newEdge = next;
    } while ((*newEdge).fUpperY <= y);
    update_next_next_y((*newEdge).fUpperY, y, nextNextY);
}

static void validate_edges_for_y(const SkAnalyticEdge* edge, SkFixed y) {
#ifdef SK_DEBUG
    while ((*edge).fUpperY <= y) {
        SkASSERT((*edge).fPrev && (*edge).fNext);
        SkASSERT((*(*edge).fPrev).fNext == edge);
        SkASSERT((*(*edge).fNext).fPrev == edge);
        SkASSERT((*edge).fUpperY <= (*edge).fLowerY);
        SkASSERT((*(*edge).fPrev).fPrev == nullptr || (*(*edge).fPrev).fX <= (*edge).fX);
        edge = (*edge).fNext;
    }
#endif
}

// Return true if prev->fX, next->fX are too close in the current pixel row.
static bool edges_too_close(SkAnalyticEdge* prev, SkAnalyticEdge* next, SkFixed lowerY) {
    // When next->fDX == 0, prev->fX >= next->fX - SkAbs32(next->fDX) would be false
    // even if prev->fX and next->fX are close and within one pixel (e.g., prev->fX == 0.1,
    // next->fX == 0.9). Adding SLACK = 1 to the formula would guarantee it to be true if two
    // edges prev and next are within one pixel.
    constexpr SkFixed SLACK = SK_Fixed1;

    // Note that even if the following test failed, the edges might still be very close to each
    // other at some point within the current pixel row because of prev->fDX and next->fDX.
    // However, to handle that case, we have to sacrafice more performance.
    // I think the current quality is good enough (mainly by looking at Nebraska-StateSeal.svg)
    // so I'll ignore fDX for performance tradeoff.
    // Compared in 64 bits: a saturated slope makes the magnitude SK_FixedMax, and subtracting
    // that from a negative position overflowed in 32.
    const bool tooClose = next && prev && (*next).fUpperY < lowerY &&
                          static_cast<int64_t>((*prev).fX) + SLACK >=
                                  static_cast<int64_t>((*next).fX) - SkAbs32((*next).fDX);
    return tooClose;
    // The following is more accurate but also slower.
    // return (prev && prev->fPrev && next && next->fNext != nullptr && next->fUpperY < lowerY &&
    //     prev->fX + SkAbs32(prev->fDX) + SLACK >= next->fX - SkAbs32(next->fDX));
}

// This function exists for the case where the previous rite edge is removed because
// its fLowerY <= nextY
static bool edges_too_close(int prevRite, SkFixed ul, SkFixed ll) {
    const bool tooClose = prevRite > SkFixedFloorToInt(ul) || prevRite > SkFixedFloorToInt(ll);
    return tooClose;
}

static void aaa_walk_edges(SkAnalyticEdge*  prevHead,
                           SkAnalyticEdge*  nextTail,
                           SkPathFillType   fillType,
                           AdditiveBlitter* blitter,
                           int              start_y,
                           int              stop_y,
                           SkFixed          leftClip,
                           SkFixed          rightClip,
                           bool             isUsingMask,
                           bool             forceRLE,
                           bool             skipIntersect) {
    (*prevHead).fX = leftClip;
    (*prevHead).fUpperX = leftClip;
    (*nextTail).fX = rightClip;
    (*nextTail).fUpperX = rightClip;
    SkFixed y                        = std::max((*(*prevHead).fNext).fUpperY, SkIntToFixed(start_y));
    SkFixed nextNextY                = SK_MaxS32;

    {
        SkAnalyticEdge* edge = nullptr;
        for (edge = (*prevHead).fNext; (*edge).fUpperY <= y; edge = (*edge).fNext) {
            (*edge).goY(y);
            update_next_next_y((*edge).fLowerY, y, &nextNextY);
        }
        update_next_next_y((*edge).fUpperY, y, &nextNextY);
    }

    int windingMask = SkPathFillType_IsEvenOdd(fillType) ? 1 : -1;
    bool isInverse  = SkPathFillType_IsInverse(fillType);

    if (isInverse && SkIntToFixed(start_y) != y) {
        int width = SkFixedFloorToInt(rightClip - leftClip);
        if (SkFixedFloorToInt(y) != start_y) {
            (*(*blitter).getRealBlitter()).blitRect(
                    SkFixedFloorToInt(leftClip), start_y, width, SkFixedFloorToInt(y) - start_y);
            start_y = SkFixedFloorToInt(y);
        }
        const MaskRow maskRow =
                isUsingMask ? (*static_cast<MaskAdditiveBlitter*>(blitter)).getRow(start_y)
                            : MaskRow{};
        blit_full_alpha(blitter,
                        start_y,
                        SkFixedFloorToInt(leftClip),
                        width,
                        fixed_to_alpha(y - SkIntToFixed(start_y)),
                        maskRow,
                        false);
    }

    while (true) {
        int             w               = 0;
        bool            in_interval     = isInverse;
        SkFixed         prevX           = (*prevHead).fX;
        SkFixed         nextY           = std::min(nextNextY, SkFixedCeilToFixed(y + 1));
        SkAnalyticEdge* currE           = (*prevHead).fNext;
        SkAnalyticEdge* leftE           = prevHead;
        SkFixed         left            = leftClip;
        SkFixed         leftDY          = 0;
        int             prevRite        = SkFixedFloorToInt(leftClip);

        nextNextY = SK_MaxS32;

        SkASSERT((nextY & ((SK_Fixed1 >> 2) - 1)) == 0);
        int yShift = 0;
        if ((nextY - y) & (SK_Fixed1 >> 2)) {
            yShift = 2;
            nextY  = y + (SK_Fixed1 >> 2);
        } else if ((nextY - y) & (SK_Fixed1 >> 1)) {
            yShift = 1;
            SkASSERT(nextY == y + (SK_Fixed1 >> 1));
        }

        SkAlpha fullAlpha = fixed_to_alpha(nextY - y);

        // If we're using mask blitter, we advance the mask row in this function
        // to save some "if" condition checks.
        const MaskRow maskRow =
                isUsingMask
                        ? (*static_cast<MaskAdditiveBlitter*>(blitter)).getRow(SkFixedFloorToInt(y))
                        : MaskRow{};

        SkASSERT((*currE).fPrev == prevHead);
        validate_edges_for_y(currE, y);

        // Even if next - y == SK_Fixed1, we can still break the left-to-right order requirement
        // of the SKAAClip: |\| (two trapezoids with overlapping middle wedges)
        bool noRealBlitter = forceRLE;  // forceRLE && (nextY - y != SK_Fixed1);

        while ((*currE).fUpperY <= y) {
            SkASSERT((*currE).fLowerY >= nextY);
            SkASSERT((*currE).fY == y);

            w += static_cast<int>((*currE).fWinding);
            bool prev_in_interval = in_interval;
            in_interval           = !(w & windingMask) == isInverse;

            bool isLeft   = in_interval && !prev_in_interval;
            bool isRite   = !in_interval && prev_in_interval;

            if (isRite) {
                SkFixed rite = (*currE).fX;
                (*currE).goY(nextY, yShift);
                SkFixed nextLeft = std::max(leftClip, (*leftE).fX);
                rite = std::min(rightClip, rite);
                SkFixed nextRite = std::min(rightClip, (*currE).fX);
                blit_trapezoid_row(
                        blitter,
                        y >> 16,
                        left,
                        rite,
                        nextLeft,
                        nextRite,
                        leftDY,
                        (*currE).fDY,
                        fullAlpha,
                        maskRow,
                        noRealBlitter || (fullAlpha == 0xFF &&
                                          (edges_too_close(prevRite, left, (*leftE).fX) ||
                                           edges_too_close(currE, (*currE).fNext, nextY))));
                prevRite = SkFixedCeilToInt(std::max(rite, (*currE).fX));
            } else {
                if (isLeft) {
                    left     = std::max((*currE).fX, leftClip);
                    leftDY   = (*currE).fDY;
                    leftE    = currE;
                }
                (*currE).goY(nextY, yShift);
            }

            SkAnalyticEdge* next = (*currE).fNext;
            SkFixed         newX = 0;

            while ((*currE).fLowerY <= nextY) {
                if ((*currE).fCurveCount < 0) {
                    SkAnalyticCubicEdge* cubicEdge = (SkAnalyticCubicEdge*)currE;
                    (*cubicEdge).keepContinuous();
                    if (!(*cubicEdge).updateCubic()) {
                        break;
                    }
                } else if ((*currE).fCurveCount > 0) {
                    SkAnalyticQuadraticEdge* quadEdge = (SkAnalyticQuadraticEdge*)currE;
                    (*quadEdge).keepContinuous();
                    if (!(*quadEdge).updateQuadratic()) {
                        break;
                    }
                } else {
                    break;
                }
            }
            SkASSERT((*currE).fY == nextY);

            if ((*currE).fLowerY <= nextY) {
                remove_edge(currE);
            } else {
                update_next_next_y((*currE).fLowerY, nextY, &nextNextY);
                newX = (*currE).fX;
                SkASSERT((*currE).fLowerY > nextY);
                if (newX < prevX) {
                    // ripple currE backwards until it is x-sorted
                    backward_insert_edge_based_on_x(currE);
                } else {
                    prevX = newX;
                }
                if (!skipIntersect) {
                    check_intersection(currE, nextY, &nextNextY);
                }
            }

            currE = next;
            SkASSERT(currE);
        }

        // was our right-edge culled away?
        if (in_interval) {
            blit_trapezoid_row(blitter,
                               y >> 16,
                               left,
                               rightClip,
                               std::max(leftClip, (*leftE).fX),
                               rightClip,
                               leftDY,
                               0,
                               fullAlpha,
                               maskRow,
                               noRealBlitter || (fullAlpha == 0xFF &&
                                                 edges_too_close((*leftE).fPrev, leftE, nextY)));
        }

        if (forceRLE) {
            // forceRLE never uses the mask blitter.
            (*static_cast<RunBasedAdditiveBlitter*>(blitter)).flush_if_y_changed(y, nextY);
        }

        y = nextY;
        if (y >= SkIntToFixed(stop_y)) {
            break;
        }

        // now currE points to the first edge with a fUpperY larger than the previous y
        insert_new_edges(currE, y, &nextNextY);
    }
}

static void aaa_fill_path(const SkPathRaw& path,
                          const SkIRect& clipRect,
                          AdditiveBlitter* blitter,
                          int start_y,
                          int stop_y,
                          bool pathContainedInClip,
                          bool isUsingMask,
                          bool forceRLE) {  // forceRLE implies that SkAAClip is calling us
    SkASSERT(blitter);

    SkAnalyticEdgeBuilder builder;
    int              count = builder.buildEdges(path, pathContainedInClip ? nullptr : &clipRect);
    SkAnalyticEdge** list  = builder.analyticEdgeList();

    SkIRect rect = clipRect;
    if (0 == count) {
        if (path.isInverseFillType()) {
            /*
             *  Since we are in inverse-fill, our caller has already drawn above
             *  our top (start_y) and will draw below our bottom (stop_y). Thus
             *  we need to restrict our drawing to the intersection of the clip
             *  and those two limits.
             */
            if (rect.fTop < start_y) {
                rect.fTop = start_y;
            }
            if (rect.fBottom > stop_y) {
                rect.fBottom = stop_y;
            }
            if (!rect.isEmpty()) {
                (*(*blitter).getRealBlitter()).blitRect(
                        rect.fLeft, rect.fTop, rect.width(), rect.height());
            }
        }
        return;
    }

    SkAnalyticEdge headEdge;
    SkAnalyticEdge tailEdge;
    SkAnalyticEdge* last = nullptr;
    // this returns the first and last edge after they're sorted into a dlink list
    SkAnalyticEdge* edge = sort_edges(list, count, &last);

    headEdge.fPrev   = nullptr;
    headEdge.fNext   = edge;
    headEdge.fUpperY = SK_MinS32;
    headEdge.fLowerY = SK_MinS32;
    headEdge.fX                         = SK_MinS32;
    headEdge.fDX                        = 0;
    headEdge.fDY                        = SK_MaxS32;
    headEdge.fUpperX                    = SK_MinS32;
    (*edge).fPrev                         = &headEdge;

    tailEdge.fPrev   = last;
    tailEdge.fNext   = nullptr;
    tailEdge.fUpperY = SK_MaxS32;
    tailEdge.fLowerY = SK_MaxS32;
    tailEdge.fX                         = SK_MaxS32;
    tailEdge.fDX                        = 0;
    tailEdge.fDY                        = SK_MaxS32;
    tailEdge.fUpperX                    = SK_MaxS32;
    (*last).fNext                         = &tailEdge;

    // now edge is the head of the sorted linklist

    if (!pathContainedInClip && start_y < clipRect.fTop) {
        start_y = clipRect.fTop;
    }
    if (!pathContainedInClip && stop_y > clipRect.fBottom) {
        stop_y = clipRect.fBottom;
    }

    SkFixed leftBound  = SkIntToFixed(rect.fLeft);
    SkFixed rightBound = SkIntToFixed(rect.fRight);
    if (isUsingMask) {
        // If we're using mask, then we have to limit the bound within the path bounds.
        // Otherwise, the edge drift may access an invalid address inside the mask.
        SkIRect ir = SkIRect::MakeEmpty();
        path.bounds().roundOut(&ir);
        leftBound  = std::max(leftBound, SkIntToFixed(ir.fLeft));
        rightBound = std::min(rightBound, SkIntToFixed(ir.fRight));
    }

    if (!path.isInverseFillType() && path.isKnownToBeConvex() && count >= 2) {
        aaa_walk_convex_edges(
                &headEdge, blitter, start_y, stop_y, leftBound, rightBound, isUsingMask);
    } else {
        // We skip intersection computation if there are many points which probably already
        // give us enough fractional scan lines.
        bool skipIntersect = path.points().size() > SkToSizeT((stop_y - start_y) * 2);

        aaa_walk_edges(&headEdge,
                       &tailEdge,
                       path.fillType(),
                       blitter,
                       start_y,
                       stop_y,
                       leftBound,
                       rightBound,
                       isUsingMask,
                       forceRLE,
                       skipIntersect);
    }
}

// Check if the path is a rect and fat enough after clipping; if so, blit it.
static inline bool try_blit_fat_anti_rect(SkBlitter* blitter,
                                          const SkPathRaw& raw,
                                          const SkIRect& clip) {
    const std::optional<SkRect> isRect = raw.isRect();
    if (!isRect.has_value()) {
        return false;
    }
    SkRect rect = isRect.value();
    if (!rect.intersect(SkRect::Make(clip))) {
        return true; // The intersection is empty. Hence consider it done.
    }
    const SkIRect bounds = rect.roundOut();
    if (bounds.width() < 3) {
        return false; // not fat
    }
    (*blitter).blitFatAntiRect(rect);
    return true;
}

void SkScan::AAAFillPath(const SkPathRaw&  path,
                         SkBlitter*     blitter,
                         const SkIRect& ir,
                         const SkIRect& clipBounds,
                         bool           forceRLE) {
    bool containedInClip = clipBounds.contains(ir);
    bool isInverse       = path.isInverseFillType();

    // The mask blitter (where we store intermediate alpha values directly in a mask, and then call
    // the real blitter once in the end to blit the whole mask) is faster than the RLE blitter when
    // the blit region is small enough (i.e., CanHandleRect(ir)). When isInverse is true, the blit
    // region is no longer the rectangle ir so we won't use the mask blitter. The caller may also
    // use the forceRLE flag to force not using the mask blitter. Also, when the path is a simple
    // rect, preparing a mask and blitting it might have too much overhead. Hence we'll use
    // blitFatAntiRect to avoid the mask and its overhead.
    if (MaskAdditiveBlitter::CanHandleRect(ir) && !isInverse && !forceRLE) {
        // blitFatAntiRect is slower than the normal AAA flow without MaskAdditiveBlitter.
        // Hence only tryBlitFatAntiRect when MaskAdditiveBlitter would have been used.
        if (!try_blit_fat_anti_rect(blitter, path, clipBounds)) {
            MaskAdditiveBlitter additiveBlitter(blitter, ir, clipBounds, isInverse);
            aaa_fill_path(path,
                          clipBounds,
                          &additiveBlitter,
                          ir.fTop,
                          ir.fBottom,
                          containedInClip,
                          true,
                          forceRLE);
        }
    } else if (!isInverse && path.isKnownToBeConvex()) {
        // If the filling area is convex (i.e., path.isConvex && !isInverse), our simpler
        // aaa_walk_convex_edges won't generate alphas above 255. Hence we don't need
        // SafeRLEAdditiveBlitter (which is slow due to clamping). The basic RLE blitter
        // RunBasedAdditiveBlitter would suffice.
        RunBasedAdditiveBlitter additiveBlitter(blitter, ir, clipBounds, isInverse);
        aaa_fill_path(path,
                      clipBounds,
                      &additiveBlitter,
                      ir.fTop,
                      ir.fBottom,
                      containedInClip,
                      false,
                      forceRLE);
    } else {
        // If the filling area might not be convex, the more involved aaa_walk_edges would
        // be called and we have to clamp the alpha downto 255. The SafeRLEAdditiveBlitter
        // does that at a cost of performance.
        SafeRLEAdditiveBlitter additiveBlitter(blitter, ir, clipBounds, isInverse);
        aaa_fill_path(path,
                      clipBounds,
                      &additiveBlitter,
                      ir.fTop,
                      ir.fBottom,
                      containedInClip,
                      false,
                      forceRLE);
    }
}
