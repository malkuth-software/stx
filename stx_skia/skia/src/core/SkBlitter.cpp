/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkBlitter.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkScalar.h"
#include "include/core/SkShader.h"
#include "include/private/SkDebug.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkTemplates.h"
#include "include/private/SkTo.h"
#include "src/core/SkAlphaRuns.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkBlendModePriv.h"
#include "src/core/SkBlitter_A8.h"
#include "src/core/SkCoreBlitters.h"
#include "src/core/SkDrawTypes.h"
#include "src/core/SkMask.h"
#include "src/core/SkMaskFilterBase.h"
#include "src/core/SkMemset.h"
#include "src/core/SkPaintPriv.h"
#include "src/core/SkRegionPriv.h"
#include "src/core/SkTLazy.h"
#include "src/shaders/SkShaderBase.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

using namespace skia_private;

// Hacks for testing.
bool gSkForceRasterPipelineBlitter{false};

SkBlitter::~SkBlitter() = default;

// Converts a coverage fraction to an alpha, snapping values near 0 or 1. Out-of-range and NaN
// fractions saturate.
inline static SkAlpha scalar_to_alpha(SkScalar a) {
    const int alpha = sk_float_saturate2int(a * 255);
    if (alpha > 247) {
        return 0xFF;
    }
    if (alpha < 8) {
        return 0;
    }
    return static_cast<SkAlpha>(alpha);
}

void SkBlitter::blitFatAntiRect(const SkRect& rect) {
    const SkIRect bounds = rect.roundOut();
    SkASSERT(bounds.width() >= 3);

    // skbug.com/40039068
    // To ensure consistency of the threaded backend (a rect that's considered fat in the init-once
    // phase must also be considered fat in the draw phase), we have to deal with rects with small
    // heights because the horizontal tiling in the threaded backend may change the height.
    //
    // This also implies that we cannot do vertical tiling unless we can blit any rect (not just the
    // fat one.)
    if (bounds.height() == 0) {
        return;
    }

    // The runs are followed by the coverage in one allocation owned by this blitter.
    const int runSize = bounds.width() + 1; // +1 so we can set runs[bounds.width()] = 0
    const size_t entryBytes = sizeof(int16_t) + sizeof(SkAlpha);
    void* storage = (*this).allocBlitMemory(static_cast<size_t>(runSize) * entryBytes);
    int16_t* runs = reinterpret_cast<int16_t*>(storage);
    SkAlpha* alphas = reinterpret_cast<SkAlpha*>(runs + runSize);

    runs[0] = 1;
    runs[1] = static_cast<int16_t>(bounds.width() - 2);
    runs[bounds.width() - 1] = 1;
    runs[bounds.width()]  = 0;

    const SkScalar partialL = bounds.fLeft + 1 - rect.fLeft;
    const SkScalar partialR = rect.fRight - (bounds.fRight - 1);
    SkScalar partialT = bounds.fTop + 1 - rect.fTop;
    const SkScalar partialB = rect.fBottom - (bounds.fBottom - 1);

    if (bounds.height() == 1) {
        partialT = rect.fBottom - rect.fTop;
    }

    alphas[0] = scalar_to_alpha(partialL * partialT);
    alphas[1] = scalar_to_alpha(partialT);
    alphas[bounds.width() - 1] = scalar_to_alpha(partialR * partialT);
    (*this).blitAntiH(bounds.fLeft, bounds.fTop, alphas, runs);

    if (bounds.height() > 2) {
        (*this).blitAntiRect(bounds.fLeft, bounds.fTop + 1, bounds.width() - 2,
                             bounds.height() - 2, scalar_to_alpha(partialL),
                             scalar_to_alpha(partialR));
    }

    if (bounds.height() > 1) {
        alphas[0] = scalar_to_alpha(partialL * partialB);
        alphas[1] = scalar_to_alpha(partialB);
        alphas[bounds.width() - 1] = scalar_to_alpha(partialR * partialB);
        (*this).blitAntiH(bounds.fLeft, bounds.fBottom - 1, alphas, runs);
    }
}

void SkBlitter::blitV(int x, int y, int height, SkAlpha alpha) {
    if (alpha == 255) {
        (*this).blitRect(x, y, 1, height);
    } else {
        int16_t runs[2] = {1, 0};
        SkAlpha coverage = alpha;
        for (int row = 0; row < height; ++row) {
            (*this).blitAntiH(x, y + row, &coverage, runs);
        }
    }
}

void SkBlitter::blitRect(int x, int y, int width, int height) {
    SkASSERT(width > 0);
    for (int row = 0; row < height; ++row) {
        (*this).blitH(x, y + row, width);
    }
}

/// Default implementation doesn't check for easy optimizations
/// such as alpha == 255; also uses blitV(), which some subclasses
/// may not support.
void SkBlitter::blitAntiRect(int x, int y, int width, int height,
                             SkAlpha leftAlpha, SkAlpha rightAlpha) {
    if (leftAlpha > 0) { // we may send in x = -1 with leftAlpha = 0
        (*this).blitV(x, y, height, leftAlpha);
    }
    int column = x + 1;
    if (width > 0) {
        (*this).blitRect(column, y, width, height);
        column += width;
    }
    if (rightAlpha > 0) {
        (*this).blitV(column, y, height, rightAlpha);
    }
}

//////////////////////////////////////////////////////////////////////////////

// Blits the set bits of rowBytes mask bytes as spans. The first byte is masked by left_mask and
// the last by right_mask; x is the column of the first byte's high bit.
static inline void bits_to_runs(SkBlitter* blitter, int x, int y,
                                const uint8_t bits[],
                                uint8_t left_mask, ptrdiff_t rowBytes,
                                uint8_t right_mask) {
    bool inFill = false;
    int pos = 0;
    int column = x;

    for (ptrdiff_t byte = 0; byte < rowBytes; ++byte) {
        uint8_t b = bits[byte];
        if (byte == 0) {
            b &= left_mask;
        }
        if (byte == rowBytes - 1) {
            b &= right_mask;
        }

        for (unsigned test = 0x80U; test != 0; test >>= 1) {
            if (b & test) {
                if (!inFill) {
                    pos = column;
                    inFill = true;
                }
            } else {
                if (inFill) {
                    (*blitter).blitH(pos, y, column - pos);
                    inFill = false;
                }
            }
            column += 1;
        }
    }

    // final cleanup
    if (inFill) {
        (*blitter).blitH(pos, y, column - pos);
    }
}

// maskBitCount is the number of 1's to place in the mask. It must be in the range between 1 and 8.
static uint8_t generate_right_mask(int maskBitCount) {
    return static_cast<uint8_t>((0xFF00U >> maskBitCount) & 0xFF);
}

void SkBlitter::blitMask(const SkMask& mask, const SkIRect& clip) {
    SkASSERT(mask.fBounds.contains(clip));

    if (mask.fFormat == SkMask::kLCD16_Format) {
        return; // needs to be handled by subclass
    }

    // Each row's address comes from the mask, so no address is formed past its last row.
    if (mask.fFormat == SkMask::kBW_Format) {
        const int cx = clip.fLeft;
        const int maskLeft = mask.fBounds.fLeft;

#ifdef SK_DEBUG
        const uint8_t* endOfImage = mask.fImage +
                                    (mask.fBounds.height() - 1) * mask.fRowBytes +
                                    ((mask.fBounds.width() + 7) >> 3);
#endif

        if (cx == maskLeft && clip.fRight == mask.fBounds.fRight) {
            const int affectedRightBit = mask.fBounds.width() - 1;
            const ptrdiff_t rowBytes = (affectedRightBit >> 3) + 1;
            const uint8_t rightMask = generate_right_mask((affectedRightBit & 7) + 1);
            for (int cy = clip.fTop; cy < clip.fBottom; ++cy) {
                const uint8_t* bits = mask.getAddr1(cx, cy);
                SkASSERT(bits + rowBytes <= endOfImage);
                bits_to_runs(this, cx, cy, bits, 0xFF, rowBytes, rightMask);
            }
        } else {
            // Bits is calculated as the offset into the mask at the point {cx, cy} therefore, all
            // addressing into the bit mask is relative to that point. Since this is an address
            // calculated from a arbitrary bit in that byte, calculate the left most bit.
            const int bitsLeft = cx - ((cx - maskLeft) & 7);

            // Everything is relative to the bitsLeft.
            const int leftEdge = cx - bitsLeft;
            SkASSERT(leftEdge >= 0);
            const int rightEdge = clip.fRight - bitsLeft;
            SkASSERT(rightEdge > leftEdge);

            // Calculate left byte and mask
            const uint8_t leftMask = static_cast<uint8_t>(0xFFU >> (leftEdge & 7));

            // Calculate right byte and mask
            const int affectedRightBit = rightEdge - 1;
            const uint8_t rightMask = generate_right_mask((affectedRightBit & 7) + 1);

            // The left byte holds bit 0 and the right byte holds affectedRightBit, so the count
            // of bytes is one more than the index of the right byte.
            const ptrdiff_t rowBytes = (affectedRightBit >> 3) + 1;

            for (int cy = clip.fTop; cy < clip.fBottom; ++cy) {
                const uint8_t* bits = mask.getAddr1(cx, cy);
                SkASSERT(bits + rowBytes <= endOfImage);
                bits_to_runs(this, bitsLeft, cy, bits, leftMask, rowBytes, rightMask);
            }
        }
    } else {
        const int width = clip.width();
        AutoSTMalloc<64, int16_t> runStorage(width + 1);
        int16_t* runs = runStorage.get();

        // Signed and unsigned 16-bit values may share storage.
        SkOpts::memset16(reinterpret_cast<uint16_t*>(runs), 1, width);
        runs[width] = 0;

        for (int y = clip.fTop; y < clip.fBottom; ++y) {
            const uint8_t* aa = mask.getAddr8(clip.fLeft, y);
            (*this).blitAntiH(clip.fLeft, y, aa, runs);
        }
    }
}

/////////////////////// these are not virtual, just helpers
void SkBlitter::blitMaskRegion(const SkMask& mask, const SkRegion& clip) {
    if (clip.quickReject(mask.fBounds)) {
        return;
    }

    SkRegion::Cliperator clipper(clip, mask.fBounds);

    while (!clipper.done()) {
        const SkIRect& cr = clipper.rect();
        (*this).blitMask(mask, cr);
        clipper.next();
    }
}

void SkBlitter::blitRectRegion(const SkIRect& rect, const SkRegion& clip) {
    SkRegion::Cliperator clipper(clip, rect);

    while (!clipper.done()) {
        const SkIRect& cr = clipper.rect();
        (*this).blitRect(cr.fLeft, cr.fTop, cr.width(), cr.height());
        clipper.next();
    }
}

namespace {

// Blits each span of a region as a rectangle. The blitter is borrowed for the visit.
class RegionSpanBlitter final : public SkRegionSpanVisitor {
public:
    explicit RegionSpanBlitter(SkBlitter* blitter) : fBlitter(blitter) {}

    void visitSpan(const SkIRect& span) override {
        (*fBlitter).blitRect(span.left(), span.top(), span.width(), span.height());
    }

private:
    SkBlitter* fBlitter;
};

}  // namespace

void SkBlitter::blitRegion(const SkRegion& clip) {
    RegionSpanBlitter visitor(this);
    SkRegionPriv::VisitSpans(clip, visitor);
}

///////////////////////////////////////////////////////////////////////////////

static int compute_anti_width(const int16_t runs[]) {
    int width = 0;

    while (true) {
        const int count = runs[width];

        SkASSERT(count >= 0);
        if (count == 0) {
            break;
        }
        width += count;
    }
    return width;
}

// Unsigned differences make the range tests exact for every int input.
static inline bool y_in_rect(int y, const SkIRect& rect) {
    const uint32_t offset = static_cast<uint32_t>(y) - static_cast<uint32_t>(rect.fTop);
    const uint32_t height = static_cast<uint32_t>(rect.fBottom) - static_cast<uint32_t>(rect.fTop);
    const bool inside = offset < height;
    return inside;
}

static inline bool x_in_rect(int x, const SkIRect& rect) {
    const uint32_t offset = static_cast<uint32_t>(x) - static_cast<uint32_t>(rect.fLeft);
    const uint32_t width = static_cast<uint32_t>(rect.fRight) - static_cast<uint32_t>(rect.fLeft);
    const bool inside = offset < width;
    return inside;
}

void SkRectClipBlitter::blitH(int left, int y, int width) {
    SkASSERT(width > 0);

    if (!y_in_rect(y, fClipRect)) {
        return;
    }

    const int clippedLeft = std::max(left, fClipRect.fLeft);
    const int clippedRight = std::min(left + width, fClipRect.fRight);

    const int clippedWidth = clippedRight - clippedLeft;
    if (clippedWidth > 0) {
        (*fBlitter).blitH(clippedLeft, y, clippedWidth);
    }
}

void SkRectClipBlitter::blitAntiH(int left, int y, const SkAlpha const_aa[],
                                  const int16_t const_runs[]) {
    // The runs are split in place; see the class comment of SkBlitter.
    SkAlpha* aa = const_cast<SkAlpha*>(const_aa);
    int16_t* runs = const_cast<int16_t*>(const_runs);

    if (!y_in_rect(y, fClipRect) || left >= fClipRect.fRight) {
        return;
    }

    int x0 = left;
    int x1 = left + compute_anti_width(runs);

    if (x1 <= fClipRect.fLeft) {
        return;
    }

    SkASSERT(x0 < x1);
    if (x0 < fClipRect.fLeft) {
        const int dx = fClipRect.fLeft - x0;
        SkAlphaRuns::BreakAt(runs, aa, dx);
        runs += dx;
        aa += dx;
        x0 = fClipRect.fLeft;
    }

    SkASSERT(x0 < x1 && runs[x1 - x0] == 0);
    if (x1 > fClipRect.fRight) {
        x1 = fClipRect.fRight;
        SkAlphaRuns::BreakAt(runs, aa, x1 - x0);
        runs[x1 - x0] = 0;
    }

    SkASSERT(x0 < x1 && runs[x1 - x0] == 0);
    SkASSERT(compute_anti_width(runs) == x1 - x0);

    (*fBlitter).blitAntiH(x0, y, aa, runs);
}

void SkRectClipBlitter::blitV(int x, int y, int height, SkAlpha alpha) {
    SkASSERT(height > 0);

    if (!x_in_rect(x, fClipRect)) {
        return;
    }

    const int y0 = std::max(y, fClipRect.fTop);
    const int y1 = std::min(y + height, fClipRect.fBottom);

    if (y0 < y1) {
        (*fBlitter).blitV(x, y0, y1 - y0, alpha);
    }
}

void SkRectClipBlitter::blitRect(int left, int y, int width, int height) {
    SkIRect r = SkIRect::MakeLTRB(left, y, left + width, y + height);
    if (r.intersect(fClipRect)) {
        (*fBlitter).blitRect(r.fLeft, r.fTop, r.width(), r.height());
    }
}

void SkRectClipBlitter::blitAntiRect(int left, int y, int width, int height,
                                     SkAlpha leftAlpha, SkAlpha rightAlpha) {
    // The *true* width of the rectangle blitted is width+2:
    SkIRect r = SkIRect::MakeLTRB(left, y, left + width + 2, y + height);
    if (r.intersect(fClipRect)) {
        SkAlpha clippedLeftAlpha = leftAlpha;
        SkAlpha clippedRightAlpha = rightAlpha;
        if (r.fLeft != left) {
            SkASSERT(r.fLeft > left);
            clippedLeftAlpha = 255;
        }
        if (r.fRight != left + width + 2) {
            SkASSERT(r.fRight < left + width + 2);
            clippedRightAlpha = 255;
        }
        if (255 == clippedLeftAlpha && 255 == clippedRightAlpha) {
            (*fBlitter).blitRect(r.fLeft, r.fTop, r.width(), r.height());
        } else if (1 == r.width()) {
            if (r.fLeft == left) {
                (*fBlitter).blitV(r.fLeft, r.fTop, r.height(), clippedLeftAlpha);
            } else {
                SkASSERT(r.fLeft == left + width + 1);
                (*fBlitter).blitV(r.fLeft, r.fTop, r.height(), clippedRightAlpha);
            }
        } else {
            (*fBlitter).blitAntiRect(r.fLeft, r.fTop, r.width() - 2, r.height(),
                                     clippedLeftAlpha, clippedRightAlpha);
        }
    }
}

void SkRectClipBlitter::blitMask(const SkMask& mask, const SkIRect& clip) {
    SkASSERT(mask.fBounds.contains(clip));

    SkIRect r = clip;

    if (r.intersect(fClipRect)) {
        (*fBlitter).blitMask(mask, r);
    }
}

///////////////////////////////////////////////////////////////////////////////

void SkRgnClipBlitter::blitH(int x, int y, int width) {
    SkRegion::Spanerator span(*fRgn, y, x, x + width);
    int left = 0;
    int right = 0;

    while (span.next(&left, &right)) {
        SkASSERT(left < right);
        (*fBlitter).blitH(left, y, right - left);
    }
}

void SkRgnClipBlitter::blitAntiH(int x, int y, const SkAlpha const_aa[],
                                 const int16_t const_runs[]) {
    // The runs are split in place; see the class comment of SkBlitter.
    SkAlpha* aa = const_cast<SkAlpha*>(const_aa);
    int16_t* runs = const_cast<int16_t*>(const_runs);

    const int width = compute_anti_width(runs);
    SkRegion::Spanerator span(*fRgn, y, x, x + width);
    int left = 0;
    int right = 0;
#ifdef SK_DEBUG
    const SkIRect& bounds = (*fRgn).getBounds();
#endif

    int prevRite = x;
    while (span.next(&left, &right)) {
        SkASSERT(x <= left);
        SkASSERT(left < right);
        SkASSERT(left >= bounds.fLeft && right <= bounds.fRight);

        SkAlphaRuns::Break(runs, aa, left - x, right - left);

        // now zero before left
        if (left > prevRite) {
            const int index = prevRite - x;
            aa[index] = 0;   // skip runs after right
            runs[index] = SkToS16(left - prevRite);
        }

        prevRite = right;
    }

    if (prevRite > x) {
        runs[prevRite - x] = 0;

        int start = x;
        if (x < 0) {
            const int skip = runs[0];
            SkASSERT(skip >= -x);
            aa += skip;
            runs += skip;
            start += skip;
        }
        (*fBlitter).blitAntiH(start, y, aa, runs);
    }
}

void SkRgnClipBlitter::blitV(int x, int y, int height, SkAlpha alpha) {
    const SkIRect bounds = SkIRect::MakeXYWH(x, y, 1, height);

    SkRegion::Cliperator iter(*fRgn, bounds);

    while (!iter.done()) {
        const SkIRect& r = iter.rect();
        SkASSERT(bounds.contains(r));

        (*fBlitter).blitV(x, r.fTop, r.height(), alpha);
        iter.next();
    }
}

void SkRgnClipBlitter::blitRect(int x, int y, int width, int height) {
    const SkIRect bounds = SkIRect::MakeXYWH(x, y, width, height);

    SkRegion::Cliperator iter(*fRgn, bounds);

    while (!iter.done()) {
        const SkIRect& r = iter.rect();
        SkASSERT(bounds.contains(r));

        (*fBlitter).blitRect(r.fLeft, r.fTop, r.width(), r.height());
        iter.next();
    }
}

void SkRgnClipBlitter::blitAntiRect(int x, int y, int width, int height,
                                    SkAlpha leftAlpha, SkAlpha rightAlpha) {
    // The *true* width of the rectangle to blit is width + 2
    const SkIRect bounds = SkIRect::MakeXYWH(x, y, width + 2, height);

    SkRegion::Cliperator iter(*fRgn, bounds);

    while (!iter.done()) {
        const SkIRect& r = iter.rect();
        SkASSERT(bounds.contains(r));
        SkASSERT(r.fLeft >= x);
        SkASSERT(r.fRight <= x + width + 2);

        const SkAlpha effectiveLeftAlpha = (r.fLeft == x) ? leftAlpha : 255;
        const SkAlpha effectiveRightAlpha = (r.fRight == x + width + 2) ? rightAlpha : 255;

        if (255 == effectiveLeftAlpha && 255 == effectiveRightAlpha) {
            (*fBlitter).blitRect(r.fLeft, r.fTop, r.width(), r.height());
        } else if (1 == r.width()) {
            if (r.fLeft == x) {
                (*fBlitter).blitV(r.fLeft, r.fTop, r.height(), effectiveLeftAlpha);
            } else {
                SkASSERT(r.fLeft == x + width + 1);
                (*fBlitter).blitV(r.fLeft, r.fTop, r.height(), effectiveRightAlpha);
            }
        } else {
            (*fBlitter).blitAntiRect(r.fLeft, r.fTop, r.width() - 2, r.height(),
                                     effectiveLeftAlpha, effectiveRightAlpha);
        }
        iter.next();
    }
}


void SkRgnClipBlitter::blitMask(const SkMask& mask, const SkIRect& clip) {
    SkASSERT(mask.fBounds.contains(clip));

    SkRegion::Cliperator iter(*fRgn, clip);

    while (!iter.done()) {
        (*fBlitter).blitMask(mask, iter.rect());
        iter.next();
    }
}

///////////////////////////////////////////////////////////////////////////////

SkBlitter* SkBlitterClipper::apply(SkBlitter* blitter, const SkRegion* clip,
                                   const SkIRect* ir) {
    if (!clip) {
        return blitter;
    }
    const SkIRect& clipR = (*clip).getBounds();

    if ((*clip).isEmpty() || (ir && !SkIRect::Intersects(clipR, *ir))) {
        return &fNullBlitter;
    }
    if ((*clip).isRect()) {
        if (ir == nullptr || !clipR.contains(*ir)) {
            fRectBlitter.init(blitter, clipR);
            return &fRectBlitter;
        }
        return blitter;
    }
    fRgnBlitter.init(blitter, clip);
    return &fRgnBlitter;
}

///////////////////////////////////////////////////////////////////////////////

bool SkBlitter::UseLegacyBlitter(const SkPixmap& device,
                                 const SkPaint& paint,
                                 const SkMatrix& matrix) {
    if (gSkForceRasterPipelineBlitter) {
        return false;
    }
#if defined(SK_FORCE_RASTER_PIPELINE_BLITTER)
    return false;
#else

    if (paint.isDither()) {
        return false;
    }

    const SkMaskFilterBase* mf = as_MFB(paint.getMaskFilter());

    // The legacy blitters cannot handle any of these "complex" features (anymore).
    if (device.alphaType() == kUnpremul_SkAlphaType   ||
        !paint.isSrcOver()                            ||
        (mf && (*mf).getFormat() == SkMask::k3D_Format)) {
        return false;
    }

    const SkColorSpace* cs = device.colorSpace();
    // We check (indirectly via makeContext()) later on if the shader can handle the colorspace
    // in legacy mode, so here we just focus on if a single color needs raster-pipeline.
    if (cs && !paint.getShader()) {
        if (!paint.getColor4f().fitsInBytes() || !(*cs).isSRGB()) {
            return false;
        }
    }

    // Only kN32 is handled by legacy blitters now
    const bool legacy = device.colorType() == kN32_SkColorType;
    return legacy;
#endif
}

// The raster pipeline blitter for the paint, or a blitter that draws nothing when the paint
// cannot be drawn with the raster pipeline. The blitter lives in alloc.
static SkBlitter* make_raster_pipeline_blitter(const SkPixmap& device,
                                               const SkPaint& paint,
                                               const SkMatrix& ctm,
                                               SkArenaAlloc* alloc,
                                               const sk_sp<SkShader>& clipShader,
                                               const SkSurfaceProps& props,
                                               const SkRect& devBounds) {
    SkBlitter* blitter = SkCreateRasterPipelineBlitter(device, paint, ctm, alloc, clipShader, props,
                                                       devBounds);
    if (blitter) {
        return blitter;
    }
    SkBlitter* nothing = (*alloc).make<SkNullBlitter>();
    return nothing;
}

SkBlitter* SkBlitter::Choose(const SkPixmap& device,
                             const SkMatrix& ctm,
                             const SkPaint& origPaint,
                             SkArenaAlloc* alloc,
                             SkDrawCoverage drawCoverage,
                             sk_sp<SkShader> clipShader,
                             const SkSurfaceProps& props,
                             const SkRect& devBounds) {
    SkASSERT(alloc);

    if (kUnknown_SkColorType == device.colorType()) {
        return (*alloc).make<SkNullBlitter>();
    }

    // We may tweak the original paint as we go.
    SkTCopyOnFirstWrite<SkPaint> paint(origPaint);

    const std::optional<SkBlendMode> mode = (*paint).asBlendMode();
    if (mode.has_value()) {
        // We have the most fast-paths for SrcOver, so see if we can act like SrcOver.
        if (mode.value() != SkBlendMode::kSrcOver) {
            switch (CheckFastPath(*paint, SkColorTypeIsAlwaysOpaque(device.colorType()))) {
                case SkBlendFastPath::kSrcOver:
                    (*paint.writable()).setBlendMode(SkBlendMode::kSrcOver);
                    break;
                case SkBlendFastPath::kSkipDrawing:
                    return (*alloc).make<SkNullBlitter>();
                default:
                    break;
            }
        }

        // A Clear blend mode will ignore the entire color pipeline, as if Src mode with 0x00000000.
        if (mode.value() == SkBlendMode::kClear) {
            SkPaint* p = paint.writable();
            (*p).setShader(nullptr);
            (*p).setColorFilter(nullptr);
            (*p).setBlendMode(SkBlendMode::kSrc);
            (*p).setColor(0x00000000);
        }
    }

    if ((*paint).getColorFilter()) {
        SkPaintPriv::RemoveColorFilter(paint.writable(), device.colorSpace());
    }
    SkASSERT(!(*paint).getColorFilter());

    if (drawCoverage == SkDrawCoverage::kYes) {
        if (device.colorType() == kAlpha_8_SkColorType) {
            SkASSERT(!(*paint).getShader());
            SkASSERT((*paint).isSrcOver());
            return (*alloc).make<SkA8_Coverage_Blitter>(device, *paint);
        }
        return (*alloc).make<SkNullBlitter>();
    }

    if ((*paint).isDither() && !SkPaintPriv::ShouldDither(*paint, device.colorType())) {
        (*paint.writable()).setDither(false);
    }

    // We'll end here for many interesting cases: color spaces, color filters, most color types.
    if (clipShader || !UseLegacyBlitter(device, *paint, ctm)) {
        return make_raster_pipeline_blitter(device, *paint, ctm, alloc, clipShader, props,
                                            devBounds);
    }

    // Everything but legacy kN32_SkColorType should already be handled.
    SkASSERT(device.colorType() == kN32_SkColorType);

    // And we should be blending with SrcOver
    SkASSERT((*paint).asBlendMode() == SkBlendMode::kSrcOver);

    // Legacy blitters keep their shader state on a shader context.
    SkShaderBase::Context* shaderContext = nullptr;
    if ((*paint).getShader()) {
        const SkShaderBase::ContextRec rec((*paint).getAlpha(), SkShaders::MatrixRec(ctm),
                                           device.colorType(), device.colorSpace(), props);
        shaderContext = (*as_SB((*paint).getShader())).makeContext(rec, alloc);

        // Creating the context isn't always possible... try fallbacks before giving up.
        if (!shaderContext) {
            return make_raster_pipeline_blitter(device, *paint, ctm, alloc, clipShader, props,
                                                devBounds);
        }
    }

    if (shaderContext) {
        return (*alloc).make<SkARGB32_Shader_Blitter>(device, *paint, shaderContext);
    }
    if ((*paint).getColor() == SK_ColorBLACK) {
        return (*alloc).make<SkARGB32_Black_Blitter>(device, *paint);
    }
    if ((*paint).getAlpha() == 0xFF) {
        return (*alloc).make<SkARGB32_Opaque_Blitter>(device, *paint);
    }
    return (*alloc).make<SkARGB32_Blitter>(device, *paint);
}

///////////////////////////////////////////////////////////////////////////////

SkShaderBlitter::SkShaderBlitter(const SkPixmap& device,
                                 const SkPaint& paint,
                                 SkShaderBase::Context* shaderContext)
        : SkRasterBlitter(device), fShader(paint.refShader()), fShaderContext(shaderContext) {
    SkASSERT(fShader);
    SkASSERT(fShaderContext);
}

SkShaderBlitter::~SkShaderBlitter() = default;

///////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef SK_DEBUG

void SkRectClipCheckBlitter::blitH(int x, int y, int width) {
    SkASSERT(fClipRect.contains(SkIRect::MakeXYWH(x, y, width, 1)));
    (*fBlitter).blitH(x, y, width);
}

void SkRectClipCheckBlitter::blitAntiH(int x, int y, const SkAlpha aa[], const int16_t runs[]) {
    const int width = compute_anti_width(runs);
    SkASSERT(fClipRect.contains(SkIRect::MakeXYWH(x, y, width, 1)));
    (*fBlitter).blitAntiH(x, y, aa, runs);
}

void SkRectClipCheckBlitter::blitV(int x, int y, int height, SkAlpha alpha) {
    SkASSERT(fClipRect.contains(SkIRect::MakeXYWH(x, y, 1, height)));
    (*fBlitter).blitV(x, y, height, alpha);
}

void SkRectClipCheckBlitter::blitRect(int x, int y, int width, int height) {
    SkASSERT(fClipRect.contains(SkIRect::MakeXYWH(x, y, width, height)));
    (*fBlitter).blitRect(x, y, width, height);
}

void SkRectClipCheckBlitter::blitAntiRect(int x, int y, int width, int height,
                                     SkAlpha leftAlpha, SkAlpha rightAlpha) {
    const int skipLeft = leftAlpha ? 0 : 1;
    const int skipRight = rightAlpha ? 0 : 1;
    const SkIRect r = SkIRect::MakeXYWH(x + skipLeft, y, width + 2 - skipRight - skipLeft, height);
    SkASSERT(r.isEmpty() || fClipRect.contains(r));
    (*fBlitter).blitAntiRect(x, y, width, height, leftAlpha, rightAlpha);
}

void SkRectClipCheckBlitter::blitMask(const SkMask& mask, const SkIRect& clip) {
    SkASSERT(mask.fBounds.contains(clip));
    SkASSERT(fClipRect.contains(clip));
    (*fBlitter).blitMask(mask, clip);
}

void SkRectClipCheckBlitter::blitAntiH2(int x, int y, U8CPU a0, U8CPU a1) {
    SkASSERT(fClipRect.contains(SkIRect::MakeXYWH(x, y, 2, 1)));
    (*fBlitter).blitAntiH2(x, y, a0, a1);
}

void SkRectClipCheckBlitter::blitAntiV2(int x, int y, U8CPU a0, U8CPU a1) {
    SkASSERT(fClipRect.contains(SkIRect::MakeXYWH(x, y, 1, 2)));
    (*fBlitter).blitAntiV2(x, y, a0, a1);
}

#endif
