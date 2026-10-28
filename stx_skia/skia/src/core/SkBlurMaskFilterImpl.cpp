/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkBlurMaskFilterImpl.h"

#include "include/core/SkBlurTypes.h"
#include "include/core/SkFlattenable.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/private/SkAlign.h"
#include "include/private/SkAssert.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkTemplates.h"
#include "src/core/SkBlitter_A8.h"
#include "src/core/SkBlurMask.h"
#include "src/core/SkCachedData.h"
#include "src/core/SkDraw.h"
#include "src/core/SkMask.h"
#include "src/core/SkMaskCache.h"
#include "src/core/SkMaskFilterBase.h"
#include "src/core/SkRasterClip.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkResourceCache.h"
#include "src/core/SkTLazy.h"
#include "src/core/SkWriteBuffer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>
#include <variant>

SkBlurMaskFilterImpl::SkBlurMaskFilterImpl(SkScalar sigma, SkBlurStyle style, bool respectCTM)
    : fSigma(sigma)
    , fBlurStyle(style)
    , fRespectCTM(respectCTM) {
    SkASSERT(fSigma > 0);
    SkASSERT(static_cast<unsigned int>(style) <= kLastEnum_SkBlurStyle);
}

SkMask::Format SkBlurMaskFilterImpl::getFormat() const {
    return SkMask::kA8_Format;
}

bool SkBlurMaskFilterImpl::asABlur(BlurRec* rec) const {
    if (ignoreXform()) {
        return false;
    }

    if (rec) {
        (*rec).fSigma = fSigma;
        (*rec).fStyle = fBlurStyle;
    }
    return true;
}

SkScalar SkBlurMaskFilterImpl::computeXformedSigma(const SkMatrix& ctm) const {
    constexpr SkScalar kMaxBlurSigma = SkIntToScalar(128);
    const SkScalar xformedSigma = ignoreXform() ? fSigma : ctm.mapRadius(fSigma);
    const SkScalar limitedSigma = std::min(xformedSigma, kMaxBlurSigma);
    return limitedSigma;
}

bool SkBlurMaskFilterImpl::filterMask(SkMaskBuilder* dst, const SkMask& src,
                                      const SkMatrix& matrix,
                                      SkIPoint* margin) const {
    const SkScalar sigma = computeXformedSigma(matrix);
    const bool filtered = SkBlurMask::BoxBlur(dst, src, sigma, fBlurStyle, margin);
    return filtered;
}

bool SkBlurMaskFilterImpl::filterRectMask(SkMaskBuilder* dst, const SkRect& r,
                                          const SkMatrix& matrix,
                                          SkIPoint* margin,
                                          SkMaskBuilder::CreateMode createMode) const {
    const SkScalar sigma = computeXformedSigma(matrix);

    const bool filtered = SkBlurMask::BlurRect(sigma, dst, r, fBlurStyle, margin, createMode);
    return filtered;
}

// Each call renders either a rounded rectangle or one/two nested rectangles.
// The span borrows the caller's live rectangle array for this call only.
using MaskShape = std::variant<SkRRect, SkSpan<const SkRect>>;

// Output must be empty. Failure preserves it; success transfers its allocation.
static bool draw_into_mask(const MaskShape& shape, SkMaskBuilder& output) {
    const SkRRect* rounded = std::get_if<SkRRect>(&shape);
    SkSpan<const SkRect> rectangles;
    SkRect bounds = SkRect::MakeEmpty();
    if (rounded != nullptr) {
        bounds = (*rounded).rect();
    } else {
        rectangles = std::get<SkSpan<const SkRect>>(shape);
        if (rectangles.size() != 1 && rectangles.size() != 2) return false;
        bounds = rectangles[0];
    }
    if (!bounds.isFinite() || bounds.isEmpty()) return false;
    const SkIRect integerBounds = bounds.roundOut();
    const int64_t width = integerBounds.width64();
    const int64_t height = integerBounds.height64();
    if (width <= 0 || height <= 0 || width > INT32_MAX - 3 || height > INT32_MAX) return false;
    const uint32_t rowBytes = SkAlign4(static_cast<uint32_t>(width));
    SkAutoMaskFreeImage pixels;
    SkMaskBuilder mask(nullptr, integerBounds, rowBytes, SkMask::kA8_Format);
    const size_t size = mask.computeImageSize();
    if (size == 0) return false;
    pixels.reset(SkMaskBuilder::AllocImage(size, SkMaskBuilder::kZeroInit_Alloc));
    mask.image() = pixels.get();
    {
        // Local bounds avoid negating an extreme signed source coordinate.
        const SkIRect localBounds = SkIRect::MakeWH(static_cast<int>(width), static_cast<int>(height));
        SkRasterClip clip(localBounds);
        const SkImageInfo info = SkImageInfo::MakeA8(static_cast<int>(width), static_cast<int>(height));
        const SkPixmap pixmap(info, pixels.get(), rowBytes);
        const SkMatrix matrix = SkMatrix::Translate(-SkIntToScalar(integerBounds.fLeft),
                                                   -SkIntToScalar(integerBounds.fTop));
        SkPaint paint;
        paint.setAntiAlias(true);
        skcpu::Draw draw;
        draw.fBlitterChooser = SkA8Blitter_Choose;
        draw.fCTM = &matrix;
        draw.fDst = pixmap;
        draw.fRC = &clip;
        if (rounded != nullptr) {
            draw.drawRRect(*rounded, paint);
        } else if (rectangles.size() == 1) {
            draw.drawRect(rectangles[0], paint);
        } else {
            SkPathBuilder builder;
            builder.addRect(rectangles[0]);
            builder.addRect(rectangles[1]);
            builder.setFillType(SkPathFillType::kEvenOdd);
            const SkPath path = builder.detach();
            draw.drawPath(path, paint, nullptr);
        }
    }
    mask.image() = pixels.release();
    output = std::move(mask);
    return true;
}

static bool rect_exceeds(const SkRect& rect, SkScalar limit) {
    const bool exceeds = !rect.isFinite() || rect.fLeft < -limit || rect.fTop < -limit ||
                         rect.fRight > limit || rect.fBottom > limit ||
                         rect.width() > limit || rect.height() > limit;
    return exceeds;
}

// Copy into cache-owned storage before releasing the original allocation. A
// failed cache allocation leaves both the pixel owner and descriptor unchanged.
static sk_sp<SkCachedData> copy_mask_to_cacheddata(SkMaskBuilder& mask,
                                                 SkAutoMaskFreeImage& pixels,
                                                 SkResourceCache* cache) {
    SkASSERT(mask.fImage == pixels.get());
    const size_t size = mask.computeTotalImageSize();
    sk_sp<SkCachedData> data;
    if (cache != nullptr) {
        data.reset((*cache).newCachedData(size));
    } else {
        data.reset(SkResourceCache::NewCachedData(size));
    }
    if (!data) return nullptr;
    uint8_t* writable = static_cast<uint8_t*>((*data).writable_data());
    if (writable == nullptr) return nullptr;
    memcpy(writable, mask.fImage, size);
    mask.image() = writable;
    pixels.reset();
    return data;
}

std::optional<SkMaskFilterBase::NinePatch> SkBlurMaskFilterImpl::filterRRectToNine(
        const SkRRect& rrect,
        const SkMatrix& matrix,
        const SkIRect& clipBounds,
        SkResourceCache* cache) const {
    switch (rrect.getType()) {
        default:
        case SkRRect::kEmpty_Type:
            // Nothing to draw.
            return std::nullopt;

        case SkRRect::kRect_Type:
            // We should have caught this earlier.
            SkDEBUGFAIL("Should use a different special case");
            return std::nullopt;
        case SkRRect::kOval_Type:
            // The nine patch special case does not handle ovals, and we
            // already have code for rectangles.
            return std::nullopt;

        // These three can take advantage of this fast path.
        case SkRRect::kSimple_Type:
        case SkRRect::kNinePatch_Type:
        case SkRRect::kComplex_Type:
            break;
    }

    // TODO: report correct metrics for innerstyle, where we do not grow the
    // total bounds, but we do need an inset the size of our blur-radius
    if (kInner_SkBlurStyle == fBlurStyle) {
        return std::nullopt;
    }

    // TODO: take clipBounds into account to limit our coordinates up front
    // for now, just skip too-large src rects (to take the old code path).
    if (rect_exceeds(rrect.rect(), SkIntToScalar(32767))) {
        return std::nullopt;
    }

    // To better understand the following code, consider the concrete example
    // where the passed in rrect is a 16x12 rounded rectangle with (rX,rY)
    // being (2.5, 2.0) and the sigma is 0.5 (which was turned into matrix).
    // The full non-blurred rrect would look something like this (intensity
    // ranges from 0-F and each letter is one pixel in the bitmap).
    // 4BFFFFFFFFFFFFB4
    // CFFFFFFFFFFFFFFC
    // FFFFFFFFFFFFFFFF
    // FFFFFFFFFFFFFFFF
    // FFFFFFFFFFFFFFFF
    // FFFFFFFFFFFFFFFF
    // FFFFFFFFFFFFFFFF
    // FFFFFFFFFFFFFFFF
    // FFFFFFFFFFFFFFFF
    // FFFFFFFFFFFFFFFF
    // CFFFFFFFFFFFFFFC
    // 4BFFFFFFFFFFFFB4

    // We first figure out how much we need to expand the mask (the margin) to account
    // for the blurred area. In the example, margin will be 1x1
    SkIVector margin{};
    SkAutoMaskFreeImage sourcePixels;
    SkMaskBuilder srcM(nullptr, rrect.rect().roundOut(), 0, SkMask::kA8_Format);
    SkMaskBuilder dstM;
    if (!filterMask(&dstM, srcM, matrix, &margin)) {
        return std::nullopt;
    }

    // Most of the pixels in the center of the bitmap are the same as their neighbors, so
    // blurring is a waste of compute. If we made a smaller nine-patch rrect, blurred
    // that, we could then expand the result later, saving cycles. As a bonus, we can cache
    // that smaller rectangle and re-use it for other rrects with the same radii/sigma
    // combinations.

    // To figure out the appropriate width and height of the nine patch rrect, we use the
    // larger radius per side as well as the margin, to account for inner blur. In the example,
    // the minimum nine patch is 9 x 7
    //
    // width: ceil(2.5) + 1 (margin) + 1 (stretch) + 1 (margin) + ceil(2.5) = 9
    // height: ceil(2.0) + 1 (margin) + 1 (stretch) + 1 (margin) + ceil(2.0) = 7
    //
    // 4BFF F FFB4
    // CFFF F FFFC
    // FFFF F FFFF
    //
    // FFFF F FFFF
    //
    // FFFF F FFFF
    // CFFF F FFFC
    // 4BFF F FFB4
    const SkVector& UL = rrect.radii(SkRRect::kUpperLeft_Corner);
    const SkVector& UR = rrect.radii(SkRRect::kUpperRight_Corner);
    const SkVector& LR = rrect.radii(SkRRect::kLowerRight_Corner);
    const SkVector& LL = rrect.radii(SkRRect::kLowerLeft_Corner);

    // If there's a fractional radii, round up so that our final rrect is an integer width
    // to allow for symmetrical blurring across the x and y axes.
    const int32_t leftUnstretched  = SkScalarCeilToInt(std::max(UL.fX, LL.fX)) + margin.fX;
    const int32_t rightUnstretched = SkScalarCeilToInt(std::max(UR.fX, LR.fX)) + margin.fX;

    // Extra space in the middle to ensure an unchanging piece for stretching.
    const int32_t stretchSize = 1;

    const int32_t totalSmallWidth = leftUnstretched + rightUnstretched + stretchSize;
    if (totalSmallWidth >= rrect.rect().width()) {
        // There is no valid piece to stretch.
        return std::nullopt;
    }

    const int32_t topUnstretched = SkScalarCeilToInt(std::max(UL.fY, UR.fY)) + margin.fY;
    const int32_t botUnstretched = SkScalarCeilToInt(std::max(LL.fY, LR.fY)) + margin.fY;

    const int32_t totalSmallHeight = topUnstretched + botUnstretched + stretchSize;
    if (totalSmallHeight >= rrect.rect().height()) {
        // There is no valid piece to stretch.
        return std::nullopt;
    }

    // Now make that scaled down nine patch rrect.
    const SkRect smallR = SkRect::MakeWH(totalSmallWidth, totalSmallHeight);
    SkRRect smallRR;
    const SkSpan<const SkVector> radii = rrect.radii();
    smallRR.setRectRadii(smallR, radii.data());

    const float sigma = computeXformedSigma(matrix);
    // If we've already blurred this small rrect, pull it out of the cache and we are done
    sk_sp<SkCachedData> cached;
    SkAutoMaskFreeImage uncachedPixels;
    SkTLazy<SkMask> cachedMask;
    cached.reset(SkMaskCache::FindAndRef(sigma, fBlurStyle, smallRR, &cachedMask, cache));
    if (!cached) {
        // Blit the small rrect into a buffer (9x7)
        // 4BFFFFFB4
        // CFFFFFFFC
        // FFFFFFFFF
        // FFFFFFFFF
        // FFFFFFFFF
        // CFFFFFFFC
        // 4BFFFFFB4
        const MaskShape shape(smallRR);
        const bool drawn = draw_into_mask(shape, srcM);
        sourcePixels.reset(srcM.image());
        if (!drawn) {
            return std::nullopt;
        }

        // Blur the small rrect. This will expand the mask on all sides by margin to account for
        // outer blur (11x9)
        // 00111111100
        // 04ADEEEDA40
        // 1BFFFFFFFB1
        // 1EFFFFFFFE1
        // 1EFFFFFFFE1
        // 1EFFFFFFFE1
        // 1BFFFFFFFB1
        // 04ADEEEDA40
        // 00111111100
        SkMaskBuilder filterM;
        const bool filtered = filterMask(&filterM, srcM, matrix, nullptr);
        uncachedPixels.reset(filterM.image());
        if (!filtered) {
            return std::nullopt;
        }
        SkASSERT(filterM.fBounds.width() == (srcM.fBounds.width() + 2*margin.fX));
        SkASSERT(filterM.fBounds.height() == (srcM.fBounds.height() + 2*margin.fY));

        srcM.image() = nullptr;
        sourcePixels.reset();
        cached = copy_mask_to_cacheddata(filterM, uncachedPixels, cache);
        if (cached) SkMaskCache::Add(sigma, fBlurStyle, smallRR, filterM, cached.get(), cache);
        cachedMask.init(filterM);
    }

    // The bounds of the blurred mask are at -margin, so we need to offset it back to 0,0
    SkIRect bounds = (*cachedMask).fBounds;
    bounds.offsetTo(0, 0);
    // The ninepatch could be asymmetrical (e.g. if the left rX are wider than the right), so
    // we must tell the caller where the center stretchy bit is in both directions. We added
    // margin once before to unstretched to account for inner blur, but now we add to account
    // for the outer blur. In the example, this will be (5, 4) [note that it's 0-indexed]
    const SkIPoint center = {margin.fX + leftUnstretched,
                               margin.fY + topUnstretched};
    const SkMask mask((*cachedMask).fImage, bounds, (*cachedMask).fRowBytes, (*cachedMask).fFormat);
    // NinePatch is nonmovable: construct the return object directly. Its
    // constructor cannot throw and adopts exactly one of these storage owners.
    uncachedPixels.release();
    SkCachedData* transferredCache = cached.release();
    return std::optional<NinePatch>(std::in_place, mask, dstM.fBounds, center, transferredCache);
}

SkMaskFilterBase::FilterReturn SkBlurMaskFilterImpl::filterRectsToNine(
        SkSpan<const SkRect> rects,
        const SkMatrix& matrix,
        const SkIRect& clipBounds,
        std::optional<NinePatch>* patch,
        SkResourceCache* cache) const {
    if (patch == nullptr || (rects.size() != 1 && rects.size() != 2)) return FilterReturn::kFalse;

    // TODO: report correct metrics for innerstyle, where we do not grow the
    // total bounds, but we do need an inset the size of our blur-radius
    if (kInner_SkBlurStyle == fBlurStyle || kOuter_SkBlurStyle == fBlurStyle) {
        return FilterReturn::kUnimplemented;
    }

    // TODO: take clipBounds into account to limit our coordinates up front
    // for now, just skip too-large src rects (to take the old code path).
    if (rect_exceeds(rects[0], SkIntToScalar(32767))) {
        return FilterReturn::kUnimplemented;
    }

    SkIPoint margin{};
    SkAutoMaskFreeImage sourcePixels;
    SkMaskBuilder srcM(nullptr, rects[0].roundOut(), 0, SkMask::kA8_Format);
    SkMaskBuilder dstM;

    bool filterResult = false;
    if (rects.size() == 1) {
        // special case for fast rect blur
        // don't actually do the blur the first time, just compute the correct size
        filterResult = filterRectMask(&dstM, rects[0], matrix, &margin,
                                            SkMaskBuilder::kJustComputeBounds_CreateMode);
    } else {
        filterResult = filterMask(&dstM, srcM, matrix, &margin);
    }

    if (!filterResult) {
        return FilterReturn::kFalse;
    }

    /*
     *  smallR is the smallest version of 'rect' that will still guarantee that
     *  we get the same blur results on all edges, plus 1 center row/col that is
     *  representative of the extendible/stretchable edges of the ninepatch.
     *  Since our actual edge may be fractional we inset 1 more to be sure we
     *  don't miss any interior blur.
     *  x is an added pixel of blur, and { and } are the (fractional) edge
     *  pixels from the original rect.
     *
     *   x x { x x .... x x } x x
     *
     *  Thus, in this case, we inset by a total of 5 (on each side) beginning
     *  with our outer-rect (dstM.fBounds)
     */
    SkRect smallR[2]{};
    int rectCount = 0;
    SkIPoint center{};

    // +2 is from +1 for each edge (to account for possible fractional edges
    int smallW = dstM.fBounds.width() - srcM.fBounds.width() + 2;
    int smallH = dstM.fBounds.height() - srcM.fBounds.height() + 2;
    SkIRect innerIR{};

    if (rects.size() == 1) {
        rectCount = 1;
        innerIR = srcM.fBounds;
        center.set(smallW, smallH);
    } else {
        rectCount = 2;
        rects[1].roundIn(&innerIR);
        center.set(smallW + (innerIR.left() - srcM.fBounds.left()),
                   smallH + (innerIR.top() - srcM.fBounds.top()));
    }

    // +1 so we get a clean, stretchable, center row/col
    smallW += 1;
    smallH += 1;

    // we want the inset amounts to be integral, so we don't change any
    // fractional phase on the fRight or fBottom of our smallR.
    const SkScalar dx = SkIntToScalar(innerIR.width() - smallW);
    const SkScalar dy = SkIntToScalar(innerIR.height() - smallH);
    if (dx < 0 || dy < 0) {
        // we're too small, relative to our blur, to break into nine-patch,
        // so we ask to have our normal filterMask() be called.
        return FilterReturn::kUnimplemented;
    }

    smallR[0].setLTRB(rects[0].left(),       rects[0].top(),
                      rects[0].right() - dx, rects[0].bottom() - dy);
    if (smallR[0].width() < 2 || smallR[0].height() < 2) {
        return FilterReturn::kUnimplemented;
    }
    if (rectCount == 2) {
        smallR[1].setLTRB(rects[1].left(), rects[1].top(),
                          rects[1].right() - dx, rects[1].bottom() - dy);
        SkASSERT(!smallR[1].isEmpty());
    }

    const SkScalar sigma = computeXformedSigma(matrix);
    sk_sp<SkCachedData> cached;
    SkAutoMaskFreeImage uncachedPixels;
    SkTLazy<SkMask> cachedMask;
    SkSpan<const SkRect> smallRects(smallR, static_cast<size_t>(rectCount));
    cached.reset(SkMaskCache::FindAndRef(sigma, fBlurStyle, smallRects, &cachedMask, cache));
    if (!cached) {
        SkMaskBuilder filterM;
        if (rectCount == 2) {
            const MaskShape shape(smallRects);
            const bool drawn = draw_into_mask(shape, srcM);
            sourcePixels.reset(srcM.image());
            if (!drawn) {
                return FilterReturn::kFalse;
            }

            filterResult = filterMask(&filterM, srcM, matrix, nullptr);
        } else {
            filterResult = filterRectMask(&filterM, smallR[0], matrix, nullptr,
                                          SkMaskBuilder::kComputeBoundsAndRenderImage_CreateMode);
        }
        uncachedPixels.reset(filterM.image());
        if (!filterResult) return FilterReturn::kFalse;
        srcM.image() = nullptr;
        sourcePixels.reset();
        cached = copy_mask_to_cacheddata(filterM, uncachedPixels, cache);
        if (cached) SkMaskCache::Add(sigma, fBlurStyle, smallRects, filterM, cached.get(), cache);
        cachedMask.init(filterM);
    }
    SkIRect bounds = (*cachedMask).fBounds;
    bounds.offsetTo(0, 0);
    const SkMask mask((*cachedMask).fImage, bounds, (*cachedMask).fRowBytes, (*cachedMask).fFormat);
    uncachedPixels.release();
    SkCachedData* transferredCache = cached.release();
    (*patch).emplace(mask, dstM.fBounds, center, transferredCache);
    return FilterReturn::kTrue;
}

void SkBlurMaskFilterImpl::computeFastBounds(const SkRect& src,
                                             SkRect* dst) const {
    // TODO: if we're doing kInner blur, should we return a different outset?
    //       i.e. pad == 0 ?

    const SkScalar pad = 3.0f * fSigma;

    (*dst).setLTRB(src.fLeft  - pad, src.fTop    - pad,
                 src.fRight + pad, src.fBottom + pad);
}

sk_sp<SkFlattenable> SkBlurMaskFilterImpl::CreateProc(SkReadBuffer& buffer) {
    const SkScalar sigma = buffer.readScalar();
    const SkBlurStyle style = buffer.read32LE(kLastEnum_SkBlurStyle);

    const uint32_t flags = buffer.read32LE(0x3);  // historically we only recorded 2 bits
    const bool respectCTM = (flags & 1) == 0; // historically we stored ignoreCTM in low bit

    sk_sp<SkMaskFilter> filter = SkMaskFilter::MakeBlur(style, sigma, respectCTM);
    sk_sp<SkFlattenable> result = std::move(filter);
    return result;
}

void SkBlurMaskFilterImpl::flatten(SkWriteBuffer& buffer) const {
    buffer.writeScalar(fSigma);
    buffer.writeUInt(fBlurStyle);
    buffer.writeUInt(!fRespectCTM); // historically we recorded ignoreCTM
}


sk_sp<SkMaskFilter> SkMaskFilter::MakeBlur(SkBlurStyle style, SkScalar sigma, bool respectCTM) {
    if (!SkIsFinite(sigma) || sigma <= 0 || static_cast<unsigned int>(style) > kLastEnum_SkBlurStyle) return nullptr;
    sk_sp<SkMaskFilter> filter(new SkBlurMaskFilterImpl(sigma, style, respectCTM));
    return filter;
}
