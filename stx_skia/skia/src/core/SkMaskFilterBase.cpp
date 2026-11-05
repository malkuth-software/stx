/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/core/SkMaskFilterBase.h"

#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkRegion.h"
#include "include/core/SkStrokeRec.h"
#include "include/core/SkTypes.h"
#include "include/private/SkTemplates.h"
#include "src/core/SkAutoMalloc.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkCachedData.h"
#include "src/core/SkDraw.h"
#include "src/core/SkMask.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkRasterClip.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

class SkPaint;
class SkRRect;

SkMaskFilterBase::NinePatch::~NinePatch() {
    if (fCache) SkASSERT(static_cast<const void*>(fMask.fImage) == (*fCache).data());
    // Members release storage after the borrowed descriptor ends its lifetime.
}

bool SkMaskFilterBase::asABlur(BlurRec*) const {
    return false;
}

static SkMask extract_mask_subset(const SkMask& src, SkIRect bounds, int32_t newX, int32_t newY) {
    SkASSERT(src.fBounds.contains(bounds));

    const size_t dx = static_cast<size_t>(static_cast<int64_t>(bounds.left()) - src.fBounds.left());
    const size_t dy = static_cast<size_t>(static_cast<int64_t>(bounds.top()) - src.fBounds.top());
    bounds.offsetTo(newX, newY);
    const size_t offset = dy * src.fRowBytes + dx;
    const SkMask subset(src.fImage + offset, bounds, src.fRowBytes, src.fFormat);
    return subset;
}

static void blit_clipped_mask(SkBlitter* blitter, const SkMask& mask,
                            const SkIRect& bounds, const SkIRect& clipR) {
    SkIRect r = SkIRect::MakeEmpty();
    if (r.intersect(bounds, clipR)) {
        (*blitter).blitMask(mask, r);
    }
}

static void blit_clipped_rect(SkBlitter* blitter, const SkIRect& rect, const SkIRect& clipR) {
    SkIRect r = SkIRect::MakeEmpty();
    if (r.intersect(rect, clipR)) {
        (*blitter).blitRect(r.left(), r.top(), r.width(), r.height());
    }
}

static void draw_nine_clipped(const SkMask& mask, const SkIRect& outerR,
                              const SkIPoint& center, bool fillCenter,
                              const SkIRect& clipR, SkBlitter* blitter) {
    SkASSERT(mask.fFormat == SkMask::kA8_Format);
    SkASSERT(mask.fBounds.fLeft == 0 && mask.fBounds.fTop == 0);
    SkASSERT(mask.fBounds.contains(center.x(), center.y()));
    SkASSERT(outerR.width64() >= mask.fBounds.width64());
    SkASSERT(outerR.height64() >= mask.fBounds.height64());
    const int cx = center.x();
    const int cy = center.y();
    SkIRect bounds = SkIRect::MakeEmpty();

    // top-left
    bounds = mask.fBounds;
    bounds.fRight = cx;
    bounds.fBottom = cy;
    if (bounds.width() > 0 && bounds.height() > 0) {
        SkMask m = extract_mask_subset(mask, bounds, outerR.left(), outerR.top());
        blit_clipped_mask(blitter, m, m.fBounds, clipR);
    }

    // top-right
    bounds = mask.fBounds;
    bounds.fLeft = cx + 1;
    bounds.fBottom = cy;
    if (bounds.width() > 0 && bounds.height() > 0) {
        SkMask m = extract_mask_subset(mask, bounds, outerR.right() - bounds.width(), outerR.top());
        blit_clipped_mask(blitter, m, m.fBounds, clipR);
    }

    // bottom-left
    bounds = mask.fBounds;
    bounds.fRight = cx;
    bounds.fTop = cy + 1;
    if (bounds.width() > 0 && bounds.height() > 0) {
        SkMask m = extract_mask_subset(mask, bounds, outerR.left(), outerR.bottom() - bounds.height());
        blit_clipped_mask(blitter, m, m.fBounds, clipR);
    }

    // bottom-right
    bounds = mask.fBounds;
    bounds.fLeft = cx + 1;
    bounds.fTop = cy + 1;
    if (bounds.width() > 0 && bounds.height() > 0) {
        SkMask m = extract_mask_subset(mask, bounds, outerR.right() - bounds.width(),
                                                   outerR.bottom() - bounds.height());
        blit_clipped_mask(blitter, m, m.fBounds, clipR);
    }

    SkIRect innerR = SkIRect::MakeEmpty();
    innerR.setLTRB(outerR.left() + cx - mask.fBounds.left(),
                   outerR.top() + cy - mask.fBounds.top(),
                   outerR.right() + (cx + 1 - mask.fBounds.right()),
                   outerR.bottom() + (cy + 1 - mask.fBounds.bottom()));
    if (fillCenter) {
        blit_clipped_rect(blitter, innerR, clipR);
    }

    // Only clipped horizontal strips need run storage. A run has a signed
    // 16-bit length; split larger spans instead of narrowing their width.
    SkIRect top = SkIRect::MakeLTRB(innerR.left(), outerR.top(), innerR.right(), innerR.top());
    SkIRect bottom = SkIRect::MakeLTRB(innerR.left(), innerR.bottom(), innerR.right(), outerR.bottom());
    const bool hasTop = top.intersect(clipR);
    const bool hasBottom = bottom.intersect(clipR);
    if (hasTop || hasBottom) {
        const int topWidth = hasTop ? top.width() : 0;
        const int bottomWidth = hasBottom ? bottom.width() : 0;
        const int runWidth = std::min(32767, std::max(topWidth, bottomWidth));
        const size_t slots = static_cast<size_t>(runWidth) + 1;
        const size_t storageSize = slots * (sizeof(int16_t) + sizeof(uint8_t));
        SkAutoSMalloc<4 * 1024> storage(storageSize);
        int16_t* runs = static_cast<int16_t*>(storage.get());
        uint8_t* alpha = reinterpret_cast<uint8_t*>(runs + slots);
        if (hasTop) {
            for (int y = top.top(); y < top.bottom(); ++y) {
                const int maskY = mask.fBounds.top() + y - outerR.top();
                alpha[0] = *mask.getAddr8(cx, maskY);
                for (int x = top.left(); x < top.right();) {
                    const int count = std::min(runWidth, top.right() - x);
                    runs[0] = static_cast<int16_t>(count);
                    runs[count] = 0;
                    (*blitter).blitAntiH(x, y, alpha, runs);
                    x += count;
                }
            }
        }
        if (hasBottom) {
            // Preserve the original bottom-to-top publication order.
            for (int y = bottom.bottom(); y > bottom.top();) {
                --y;
                const int maskY = mask.fBounds.bottom() - (outerR.bottom() - y);
                alpha[0] = *mask.getAddr8(cx, maskY);
                for (int x = bottom.left(); x < bottom.right();) {
                    const int count = std::min(runWidth, bottom.right() - x);
                    runs[0] = static_cast<int16_t>(count);
                    runs[count] = 0;
                    (*blitter).blitAntiH(x, y, alpha, runs);
                    x += count;
                }
            }
        }
    }
    SkIRect r = SkIRect::MakeEmpty();
    // left
    r.setLTRB(outerR.left(), innerR.top(), innerR.left(), innerR.bottom());
    if (r.intersect(clipR)) {
        SkMask leftMask(mask.getAddr8(mask.fBounds.left() + r.left() - outerR.left(),
                                      mask.fBounds.top() + cy),
                        r,
                        0,    // so we repeat the scanline for our height
                        SkMask::kA8_Format);
        (*blitter).blitMask(leftMask, r);
    }
    // right
    r.setLTRB(innerR.right(), innerR.top(), outerR.right(), innerR.bottom());
    if (r.intersect(clipR)) {
        SkMask rightMask(mask.getAddr8(mask.fBounds.right() - outerR.right() + r.left(),
                                       mask.fBounds.top() + cy),
                         r,
                         0,    // so we repeat the scanline for our height
                         SkMask::kA8_Format);
        (*blitter).blitMask(rightMask, r);
    }
}

static void draw_nine(const SkMask& mask, const SkIRect& outerR, const SkIPoint& center,
                      bool fillCenter, const SkRasterClip& clip, SkBlitter* blitter) {
    // if we get here, we need to (possibly) resolve the clip and blitter
    SkAAClipBlitterWrapper wrapper(clip, blitter);
    blitter = wrapper.getBlitter();

    SkRegion::Cliperator clipper(wrapper.getRgn(), outerR);

    if (!clipper.done()) {
        const SkIRect& cr = clipper.rect();
        do {
            draw_nine_clipped(mask, outerR, center, fillCenter, cr, blitter);
            clipper.next();
        } while (!clipper.done());
    }
}

static int countNestedRects(const SkPathRaw& raw, SkRect rects[2]) {
    if (SkPathPriv::IsNestedFillRects(raw, rects)) {
        return 2;
    }
    const std::optional<SkRect> r = raw.isRect();
    if (r.has_value()) {
        rects[0] = *r;
        return 1;
    }
    return 0;
}

bool SkMaskFilterBase::filterRRect(const SkRRect& devRRect,
                                   const SkMatrix& matrix,
                                   const SkRasterClip& clip,
                                   SkBlitter* blitter,
                                   SkResourceCache* cache) const {
    // Attempt to speed up drawing by creating a nine patch. If a nine patch
    // cannot be used, return false to allow our caller to recover and perform
    // the drawing another way.
    std::optional<NinePatch> patch =
            filterRRectToNine(devRRect, matrix, clip.getBounds(), cache);

    if (!patch.has_value()) {
        return false;
    }
    draw_nine((*patch).fMask, (*patch).fOuterRect, (*patch).fCenter, true, clip, blitter);
    return true;
}

SkMaskFilterBase::FilterReturn SkMaskFilterBase::filterRects(SkSpan<const SkRect> devRects,
                                                             const SkMatrix& matrix,
                                                             const SkRasterClip& clip,
                                                             SkBlitter* blitter,
                                                             SkResourceCache* cache) const {
    std::optional<NinePatch> patch;

    FilterReturn filterReturn = filterRectsToNine(
        devRects, matrix, clip.getBounds(), &patch, cache);
    switch (filterReturn) {
        case FilterReturn::kFalse:
            SkASSERT(!patch.has_value());
            break;

        case FilterReturn::kTrue:
            if (!patch.has_value()) return FilterReturn::kFalse;
            draw_nine((*patch).fMask, (*patch).fOuterRect, (*patch).fCenter, 1 == devRects.size(), clip,
                      blitter);
            break;

        case FilterReturn::kUnimplemented:
            SkASSERT(!patch.has_value());
            break;
        default:
            return FilterReturn::kFalse;
    }
    return filterReturn;
}

bool SkMaskFilterBase::filterPath(const SkPathRaw& devRaw,
                                  const SkMatrix& matrix,
                                  const SkRasterClip& clip,
                                  SkBlitter* blitter,
                                  SkStrokeRec::InitStyle style,
                                  SkResourceCache* cache) const {
    SkRect rects[2] = {};
    int rectCount = 0;
    if (SkStrokeRec::kFill_InitStyle == style) {
        rectCount = countNestedRects(devRaw, rects);
    }
    if (rectCount > 0) {
        const SkSpan<const SkRect> rectangles(rects, static_cast<size_t>(rectCount));
        const FilterReturn result = filterRects(rectangles, matrix, clip, blitter, cache);
        switch (result) {
            case FilterReturn::kFalse:
                return false;
            case FilterReturn::kTrue:
                return true;
            case FilterReturn::kUnimplemented:
                break;
            default:
                return false;
        }
    }

    SkAutoMaskFreeImage sourceOwner;
    SkAutoMaskFreeImage destinationOwner;
    SkMaskBuilder srcM;
    SkMaskBuilder dstM;

#if defined(SK_BUILD_FOR_FUZZER)
    if (devRaw.verbs().size() > 1000 || devRaw.points().size() > 1000) {
        return false;
    }
#endif
    const bool drawn = skcpu::DrawToMask(devRaw,
                           clip.getBounds(),
                           this,
                           &matrix,
                           &srcM,
                           SkMaskBuilder::kComputeBoundsAndRenderImage_CreateMode,
                           style);
    sourceOwner.reset(srcM.image());
    if (!drawn) return false;

    const bool filtered = filterMask(&dstM, srcM, matrix, nullptr);
    destinationOwner.reset(dstM.image());
    if (!filtered) return false;

    // if we get here, we need to (possibly) resolve the clip and blitter
    SkAAClipBlitterWrapper wrapper(clip, blitter);
    blitter = wrapper.getBlitter();

    SkRegion::Cliperator clipper(wrapper.getRgn(), dstM.fBounds);

    if (!clipper.done()) {
        const SkIRect& cr = clipper.rect();
        do {
            (*blitter).blitMask(dstM, cr);
            clipper.next();
        } while (!clipper.done());
    }

    return true;
}

std::optional<SkMaskFilterBase::NinePatch> SkMaskFilterBase::filterRRectToNine(
        const SkRRect&, const SkMatrix&, const SkIRect&, SkResourceCache*) const {
    return std::nullopt;
}

SkMaskFilterBase::FilterReturn SkMaskFilterBase::filterRectsToNine(SkSpan<const SkRect>,
                                                                   const SkMatrix&,
                                                                   const SkIRect&,
                                                                   std::optional<NinePatch>*,
                                                                   SkResourceCache*) const {
    return FilterReturn::kUnimplemented;
}

void SkMaskFilterBase::computeFastBounds(const SkRect& src, SkRect* dst) const {
    SkMask srcM(nullptr, src.roundOut(), 0, SkMask::kA8_Format);
    SkAutoMaskFreeImage destinationOwner;
    SkMaskBuilder dstM;

    SkIPoint margin = {0, 0};    // ignored
    const bool filtered = filterMask(&dstM, srcM, SkMatrix::I(), &margin);
    destinationOwner.reset(dstM.image());
    if (filtered) {
        (*dst).set(dstM.fBounds);
    } else {
        (*dst).set(srcM.fBounds);
    }
}
