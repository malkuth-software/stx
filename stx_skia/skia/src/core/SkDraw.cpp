/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkDraw.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkPathUtils.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSpan.h"
#include "include/core/SkStrokeRec.h"
#include "include/core/SkTileMode.h"
#include "include/private/SkAlign.h"
#include "include/private/SkAssert.h"
#include "include/private/SkCPUTypes.h"
#include "include/private/SkDebug.h"
#include "include/private/SkFixed.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkTemplates.h"
#include "include/private/SkTo.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkAutoBlitterChoose.h"
#include "src/core/SkBlendModePriv.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkBlitter_A8.h"
#include "src/core/SkDevice.h"
#include "src/core/SkDrawProcs.h"
#include "src/core/SkDrawTypes.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkMask.h"
#include "src/core/SkMaskFilterBase.h"
#include "src/core/SkMatrixUtils.h"
#include "src/core/SkMipmap.h"
#include "src/core/SkPathData.h"
#include "src/core/SkPathEffectBase.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkRasterClip.h"
#include "src/core/SkRectPriv.h"
#include "src/core/SkScan.h"
#include "src/core/SkTLazy.h"
#include "src/core/SkZip.h"
#include "src/image/SkImage_Raster.h"
#include "src/shaders/SkImageShader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

class SkResourceCache;

using namespace skia_private;

namespace skcpu {

static SkPaint make_paint_with_image_and_mips(const SkPaint& origPaint,
                                              const SkBitmap& bitmap,
                                              const SkSamplingOptions& sampling,
                                              SkMatrix* matrix,
                                              sk_sp<SkMipmap> mips) {
    SkPaint paint(origPaint);
    const sk_sp<SkImage_Raster> img =
            SkImage_Raster::MakeFromBitmap(bitmap, SkCopyPixelsMode::kNever, std::move(mips));
    paint.setShader((*img).makeShaderForPaint(
            origPaint, SkTileMode::kClamp, SkTileMode::kClamp, sampling, matrix));
    return paint;
}

static SkPaint make_paint_with_image(const SkPaint& origPaint,
                                     const SkBitmap& bitmap,
                                     const SkSamplingOptions& sampling,
                                     SkMatrix* matrix) {
    SkPaint paint = make_paint_with_image_and_mips(origPaint, bitmap, sampling, matrix, nullptr);
    return paint;
}

Draw::Draw() : fBlitterChooser(SkBlitter::Choose) {}

// The point procedure and the state it reads. The paint and clip are borrowed for one
// drawPoints() call.
struct PtProcRec {
    SkCanvas::PointMode fMode = SkCanvas::kPoints_PointMode;
    const SkPaint*  fPaint = nullptr;
    const SkRegion* fClip = nullptr;
    const SkRasterClip* fRC = nullptr;

    // computed values
    SkRect   fClipBounds = SkRect::MakeEmpty();
    SkScalar fRadius = 0;

    using Proc = void (*)(const PtProcRec&, SkSpan<const SkPoint> devPts, SkBlitter*);

    bool init(SkCanvas::PointMode, const SkPaint&, const SkMatrix* matrix,
              const SkRasterClip*);
    Proc chooseProc(SkBlitter** blitter);

private:
    SkAAClipBlitterWrapper fWrapper;
};

// Writes value to the pixel under each point that lies in the clip rectangle cr.
template <typename Pixel>
static void direct_blit_points(const SkPixmap& pm, const SkIRect& cr,
                               SkSpan<const SkPoint> devPts, uint64_t value) {
    const Pixel pixel = static_cast<Pixel>(value);
    for (const SkPoint& p : devPts) {
        const int x = SkScalarFloorToInt(p.fX);
        const int y = SkScalarFloorToInt(p.fY);
        if (cr.contains(x, y)) {
            *static_cast<Pixel*>(pm.writable_addr(x, y)) = pixel;
        }
    }
}

static void bw_pt_hair_proc(const PtProcRec& rec, SkSpan<const SkPoint> devPts,
                            SkBlitter* blitter) {
    const std::optional<SkBlitter::DirectBlit> direct = (*blitter).canDirectBlit();
    if (direct.has_value() && (*rec.fClip).isRect()) {
        const SkIRect cr = (*rec.fClip).getBounds();
        const SkPixmap& pm = (*direct).pm;
        const uint64_t v = (*direct).value;
        switch (pm.info().bytesPerPixel()) {
            case 1: direct_blit_points<uint8_t>(pm, cr, devPts, v); break;
            case 2: direct_blit_points<uint16_t>(pm, cr, devPts, v); break;
            case 4: direct_blit_points<uint32_t>(pm, cr, devPts, v); break;
            case 8: direct_blit_points<uint64_t>(pm, cr, devPts, v); break;
            default:
                // No direct blitter has another pixel size; draw nothing.
                SkASSERT(false);
                break;
        }
    } else {
        for (const SkPoint& p : devPts) {
            const int x = SkScalarFloorToInt(p.fX);
            const int y = SkScalarFloorToInt(p.fY);
            if ((*rec.fClip).contains(x, y)) {
                (*blitter).blitH(x, y, 1);
            }
        }
    }
}

static void bw_line_hair_proc(const PtProcRec& rec, SkSpan<const SkPoint> devPts,
                              SkBlitter* blitter) {
    for (size_t i = 0; i+1 < devPts.size(); i += 2) {
        SkScan::HairLine({&devPts[i], 2}, *rec.fRC, blitter);
    }
}

static void bw_poly_hair_proc(const PtProcRec& rec, SkSpan<const SkPoint> devPts,
                              SkBlitter* blitter) {
    SkScan::HairLine(devPts, *rec.fRC, blitter);
}

// aa versions

static void aa_line_hair_proc(const PtProcRec& rec, SkSpan<const SkPoint> devPts,
                              SkBlitter* blitter) {
    for (size_t i = 0; i+1 < devPts.size(); i += 2) {
        SkScan::AntiHairLine({&devPts[i], 2}, *rec.fRC, blitter);
    }
}

static void aa_poly_hair_proc(const PtProcRec& rec, SkSpan<const SkPoint> devPts,
                              SkBlitter* blitter) {
    SkScan::AntiHairLine(devPts, *rec.fRC, blitter);
}

// square procs (strokeWidth > 0 but matrix is square-scale (sx == sy)

static SkRect make_square_rad(SkPoint center, SkScalar radius) {
    const SkRect square = SkRect::MakeLTRB(center.fX - radius, center.fY - radius,
                                           center.fX + radius, center.fY + radius);
    return square;
}

static SkXRect make_xrect(const SkRect& r) {
    SkASSERT(SkRectPriv::FitsInFixed(r));
    const SkXRect fixed = {
        SkScalarToFixed(r.fLeft), SkScalarToFixed(r.fTop),
        SkScalarToFixed(r.fRight), SkScalarToFixed(r.fBottom)
    };
    return fixed;
}

static void bw_square_proc(const PtProcRec& rec, SkSpan<const SkPoint> devPts,
                           SkBlitter* blitter) {
    for (const SkPoint& p : devPts) {
        SkRect r = make_square_rad(p, rec.fRadius);
        if (r.intersect(rec.fClipBounds)) {
            SkScan::FillXRect(make_xrect(r), *rec.fRC, blitter);
        }
    }
}

static void aa_square_proc(const PtProcRec& rec, SkSpan<const SkPoint> devPts,
                           SkBlitter* blitter) {
    for (const SkPoint& p : devPts) {
        SkRect r = make_square_rad(p, rec.fRadius);
        if (r.intersect(rec.fClipBounds)) {
            SkScan::AntiFillXRect(make_xrect(r), *rec.fRC, blitter);
        }
    }
}

// If this returns true, then chooseProc() must return a valid proc
bool PtProcRec::init(SkCanvas::PointMode mode, const SkPaint& paint,
                     const SkMatrix* matrix, const SkRasterClip* rc) {
    if (static_cast<unsigned>(mode) > static_cast<unsigned>(SkCanvas::kPolygon_PointMode)) {
        return false;
    }
    if (paint.getPathEffect() || paint.getMaskFilter()) {
        return false;
    }
    const float width = paint.getStrokeWidth();
    float radius = -1;   // sentinel value, a "valid" value must be > 0

    if (0 == width) {
        radius = 0.5f;
    } else if (paint.getStrokeCap() != SkPaint::kRound_Cap &&
               (*matrix).isScaleTranslate() && SkCanvas::kPoints_PointMode == mode) {
        const float sx = (*matrix).get(SkMatrix::kMScaleX);
        const float sy = (*matrix).get(SkMatrix::kMScaleY);
        if (SkScalarNearlyZero(sx - sy)) {
            radius = (width * std::abs(sx)) / 2.f;
        }
    }
    if (radius > 0) {
        const SkRect clipBounds = SkRect::Make((*rc).getBounds());
        // if we return true, the caller may assume that the constructed shapes can be represented
        // using SkFixed (after clipping), so we preflight that here.
        if (!SkRectPriv::FitsInFixed(clipBounds)) {
            return false;
        }
        fMode = mode;
        fPaint = &paint;
        fClip = nullptr;
        fRC = rc;
        fClipBounds = clipBounds;
        fRadius = radius;
        return true;
    }
    return false;
}

PtProcRec::Proc PtProcRec::chooseProc(SkBlitter** blitterPtr) {
    Proc proc = nullptr;

    SkBlitter* blitter = *blitterPtr;
    if ((*fRC).isBW()) {
        fClip = &(*fRC).bwRgn();
    } else {
        fWrapper.init(*fRC, blitter);
        fClip = &fWrapper.getRgn();
        blitter = fWrapper.getBlitter();
        *blitterPtr = blitter;
    }

    // for our arrays
    SkASSERT(0 == SkCanvas::kPoints_PointMode);
    SkASSERT(1 == SkCanvas::kLines_PointMode);
    SkASSERT(2 == SkCanvas::kPolygon_PointMode);
    // init() admitted only the three point modes.
    const size_t mode = static_cast<size_t>(fMode);
    SkASSERT_RELEASE(mode <= static_cast<size_t>(SkCanvas::kPolygon_PointMode));

    if ((*fPaint).isAntiAlias()) {
        if (0 == (*fPaint).getStrokeWidth()) {
            static const Proc gAAProcs[] = {
                aa_square_proc, aa_line_hair_proc, aa_poly_hair_proc
            };
            proc = gAAProcs[mode];
        } else if ((*fPaint).getStrokeCap() != SkPaint::kRound_Cap) {
            SkASSERT(SkCanvas::kPoints_PointMode == fMode);
            proc = aa_square_proc;
        }
    } else {    // BW
        if (fRadius <= 0.5f) {    // small radii and hairline
            static const Proc gBWProcs[] = {
                bw_pt_hair_proc, bw_line_hair_proc, bw_poly_hair_proc
            };
            proc = gBWProcs[mode];
        } else {
            proc = bw_square_proc;
        }
    }
    return proc;
}

// each of these costs 8-bytes of stack space, so don't make it too large
// must be even for lines/polygon to work
static constexpr size_t kMaxDevPts = 32;

// Whether every coordinate of the points is finite.
static bool points_are_finite(SkSpan<const SkPoint> points) {
    for (const SkPoint& p : points) {
        if (!p.isFinite()) {
            return false;
        }
    }
    return true;
}

void Draw::drawPoints(SkCanvas::PointMode mode,
                      SkSpan<const SkPoint> points,
                      const SkPaint& paint,
                      SkDevice* device) const {
    // if we're in lines mode, force count to be even
    if (SkCanvas::kLines_PointMode == mode) {
        points = points.first(points.size() & ~1);   // force it to be even
    }

    (*this).validate();

     // nothing to draw
    if (points.empty() || (*fRC).isEmpty()) {
        return;
    }

    PtProcRec rec;
    if (!device && rec.init(mode, paint, fCTM, fRC)) {
        // Can't easily get bounds of points so don't try.
        SkAutoBlitterChoose blitter(*this, nullptr, paint, SkRect::MakeEmpty());

        SkPoint             devPts[kMaxDevPts] = {};
        SkBlitter*          bltr = blitter.get();
        const PtProcRec::Proc proc = rec.chooseProc(&bltr);
        // we have to back up subsequent passes if we're in polygon mode
        const size_t backup = SkCanvas::kPolygon_PointMode == mode ? 1 : 0;

        // Each pass maps up to kMaxDevPts points starting at first.
        size_t count = points.size();
        size_t first = 0;
        do {
            const size_t n = std::min(count, kMaxDevPts);
            const SkSpan<SkPoint> mapped(devPts, n);
            (*fCTM).mapPoints(mapped, points.subspan(first, n));
            if (!points_are_finite(mapped)) {
                return;
            }
            proc(rec, mapped, bltr);
            first += n - backup;
            SkASSERT(count >= n);
            count -= n;
            if (count > 0) {
                count += backup;
            }
        } while (count != 0);
    } else {
        (*this).drawDevicePoints(mode, points, paint, device);
    }
}

static bool clipped_out(const SkMatrix& m, const SkRasterClip& c,
                        const SkRect& srcR) {
    const SkRect dstR = m.mapRect(srcR);
    const bool rejected = c.quickReject(dstR.roundOut());
    return rejected;
}

static bool clipped_out(const SkMatrix& matrix, const SkRasterClip& clip,
                        int width, int height) {
    const SkRect r = SkRect::MakeIWH(width, height);
    const bool rejected = clipped_out(matrix, clip, r);
    return rejected;
}

static bool clipHandlesSprite(const SkRasterClip& clip, int x, int y, const SkPixmap& pmap) {
    const bool handles =
            clip.isBW() || clip.quickContains(SkIRect::MakeXYWH(x, y, pmap.width(), pmap.height()));
    return handles;
}

void Draw::drawBitmap(const SkBitmap& bitmap,
                      const SkMatrix& prematrix,
                      const SkRect* dstBounds,
                      const SkSamplingOptions& sampling,
                      const SkPaint& origPaint,
                      sk_sp<SkMipmap> mips) const {
    (*this).validate();

    // nothing to draw
    if ((*fRC).isEmpty() ||
            bitmap.width() == 0 || bitmap.height() == 0 ||
            bitmap.colorType() == kUnknown_SkColorType) {
        return;
    }

    SkTCopyOnFirstWrite<SkPaint> paint(origPaint);
    if (origPaint.getStyle() != SkPaint::kFill_Style) {
        (*paint.writable()).setStyle(SkPaint::kFill_Style);
    }

    const SkMatrix matrix = *fCTM * prematrix;

    if (clipped_out(matrix, *fRC, bitmap.width(), bitmap.height())) {
        return;
    }

    if (!SkColorTypeIsAlphaOnly(bitmap.colorType()) &&
        SkTreatAsSprite(matrix, bitmap.dimensions(), sampling, (*paint).isAntiAlias())) {
        //
        // It is safe to call lock pixels now, since we know the matrix is
        // (more or less) identity.
        //
        SkPixmap pmap;
        if (!bitmap.peekPixels(&pmap)) {
            return;
        }
        const int ix = SkScalarRoundToInt(matrix.getTranslateX());
        const int iy = SkScalarRoundToInt(matrix.getTranslateY());
        if (clipHandlesSprite(*fRC, ix, iy, pmap)) {
            SkSTArenaAlloc<kSkBlitterContextSize> allocator;
            // blitter will be owned by the allocator.
            SkBlitter* blitter = SkBlitter::ChooseSprite(fDst, *paint, pmap, ix, iy, &allocator,
                                                         (*fRC).clipShader());
            if (blitter) {
                SkScan::FillIRect(SkIRect::MakeXYWH(ix, iy, pmap.width(), pmap.height()),
                                  *fRC, blitter);
                return;
            }
            // if !blitter, then we fall-through to the slower case
        }
    }

    // now make a temp draw on the stack, and use it
    Draw draw(*this);
    draw.fCTM = &matrix;

    const SkPaint paintWithShader =
            make_paint_with_image_and_mips(*paint, bitmap, sampling, nullptr, mips);
    const SkRect srcBounds = SkRect::MakeIWH(bitmap.width(), bitmap.height());
    if (dstBounds) {
        (*this).drawRect(srcBounds, paintWithShader, &prematrix, dstBounds);
    } else {
        draw.drawRect(srcBounds, paintWithShader);
    }
}

void Draw::drawSprite(const SkBitmap& bitmap, int x, int y, const SkPaint& origPaint) const {
    (*this).validate();

    // nothing to draw
    if ((*fRC).isEmpty() ||
            bitmap.width() == 0 || bitmap.height() == 0 ||
            bitmap.colorType() == kUnknown_SkColorType) {
        return;
    }

    const SkIRect bounds = SkIRect::MakeXYWH(x, y, bitmap.width(), bitmap.height());

    if ((*fRC).quickReject(bounds)) {
        return; // nothing to draw
    }

    SkPaint paint(origPaint);
    paint.setStyle(SkPaint::kFill_Style);

    SkPixmap pmap;
    if (!bitmap.peekPixels(&pmap)) {
        return;
    }

    if (nullptr == paint.getColorFilter() && clipHandlesSprite(*fRC, x, y, pmap)) {
        // blitter will be owned by the allocator.
        SkSTArenaAlloc<kSkBlitterContextSize> allocator;
        SkBlitter* blitter = SkBlitter::ChooseSprite(fDst, paint, pmap, x, y, &allocator,
                                                     (*fRC).clipShader());
        if (blitter) {
            SkScan::FillIRect(bounds, *fRC, blitter);
            return;
        }
    }

    // get a scalar version of our rect
    const SkRect r = SkRect::Make(bounds);

    // create shader with offset
    SkMatrix matrix = SkMatrix::Translate(r.fLeft, r.fTop);
    const SkPaint paintWithShader =
            make_paint_with_image(paint, bitmap, SkSamplingOptions(), &matrix);
    Draw draw(*this);
    draw.fCTM = &SkMatrix::I();
    // call ourself with a rect
    draw.drawRect(r, paintWithShader);
}

void Draw::drawDevMask(const SkMask& srcM,
                       const SkPaint& paint,
                       const SkMatrix* paintMatrix) const {
    if (srcM.fBounds.isEmpty()) {
        return;
    }

    const SkMask* mask = &srcM;

    SkMaskBuilder dstM;
    if (paint.getMaskFilter() &&
        (*as_MFB(paint.getMaskFilter())).filterMask(&dstM, srcM, *fCTM, nullptr)) {
        mask = &dstM;
    }
    const SkAutoMaskFreeImage ami(dstM.image());

    SkAutoBlitterChoose blitterChooser(*this, paintMatrix, paint, SkRect::Make(dstM.bounds()));
    SkBlitter* blitter = blitterChooser.get();

    SkAAClipBlitterWrapper wrapper;
    // Borrows the clip's region or the wrapper's; both outlive the blit.
    const SkRegion* clipRgn = nullptr;

    if ((*fRC).isBW()) {
        clipRgn = &(*fRC).bwRgn();
    } else {
        wrapper.init(*fRC, blitter);
        clipRgn = &wrapper.getRgn();
        blitter = wrapper.getBlitter();
    }
    (*blitter).blitMaskRegion(*mask, *clipRgn);
}

void Draw::drawBitmapAsMask(const SkBitmap& bitmap,
                            const SkSamplingOptions& sampling,
                            const SkPaint& paint,
                            const SkMatrix* paintMatrix) const {
    SkASSERT(bitmap.colorType() == kAlpha_8_SkColorType);

    // nothing to draw
    if ((*fRC).isEmpty()) {
        return;
    }

    if (SkTreatAsSprite(*fCTM, bitmap.dimensions(), sampling, paint.isAntiAlias())) {
        const int ix = SkScalarRoundToInt((*fCTM).getTranslateX());
        const int iy = SkScalarRoundToInt((*fCTM).getTranslateY());

        SkPixmap pmap;
        if (!bitmap.peekPixels(&pmap)) {
            return;
        }
        const SkMask mask(pmap.addr8(0, 0),
                    SkIRect::MakeXYWH(ix, iy, pmap.width(), pmap.height()),
                    SkToU32(pmap.rowBytes()),
                    SkMask::kA8_Format);

        (*this).drawDevMask(mask, paint, paintMatrix);
    } else {    // need to xform the bitmap first
        SkMaskBuilder mask;

        const SkRect r = (*fCTM).mapRect(SkRect::MakeIWH(bitmap.width(), bitmap.height()));
        r.round(&mask.bounds());

        // set the mask's bounds to the transformed bitmap-bounds,
        // clipped to the actual device and further limited by the clip bounds
        {
            SkASSERT(fDst.bounds().contains((*fRC).getBounds()));
            SkIRect devBounds = fDst.bounds();
            devBounds.intersect((*fRC).getBounds().makeOutset(1, 1));
            // need intersect(l, t, r, b) on irect
            if (!mask.bounds().intersect(devBounds)) {
                return;
            }
        }

        mask.format() = SkMask::kA8_Format;
        mask.rowBytes() = SkAlign4(mask.fBounds.width());
        const size_t size = mask.computeImageSize();
        if (0 == size) {
            // the mask is too big to allocated, draw nothing
            return;
        }

        // allocate (and clear) our temp buffer to hold the transformed bitmap
        AutoTMalloc<uint8_t> storage(size);
        mask.image() = storage.get();
        memset(mask.image(), 0, size);

        // now draw our bitmap(src) into mask(dst), transformed by the matrix
        {
            SkBitmap    device;
            device.installPixels(SkImageInfo::MakeA8(mask.fBounds.width(), mask.fBounds.height()),
                                 mask.image(), mask.fRowBytes);

            SkCanvas c(device);
            // need the unclipped top/left for the translate
            c.translate(-SkIntToScalar(mask.fBounds.fLeft),
                        -SkIntToScalar(mask.fBounds.fTop));
            c.concat(*fCTM);

            // We can't call drawBitmap, or we'll infinitely recurse. Instead
            // we manually build a shader and draw that into our new mask
            SkPaint tmpPaint;
            tmpPaint.setAntiAlias(paint.isAntiAlias());
            tmpPaint.setDither(paint.isDither());
            const SkPaint paintWithShader =
                    make_paint_with_image(tmpPaint, bitmap, sampling, nullptr);
            const SkRect rr = SkRect::MakeIWH(bitmap.width(), bitmap.height());
            c.drawRect(rr, paintWithShader);
        }
        (*this).drawDevMask(mask, paint, paintMatrix);
    }
}

///////////////////////////////////////////////////////////////////////////////

bool Draw::computeConservativeLocalClipBounds(SkRect* localBounds) const {
    if ((*fRC).isEmpty()) {
        return false;
    }

    const std::optional<SkMatrix> inverse = (*fCTM).invert();
    if (inverse.has_value()) {
        SkIRect devBounds = (*fRC).getBounds();
        // outset to have slop for antialasing and hairlines
        devBounds.outset(1, 1);
        (*inverse).mapRect(localBounds, SkRect::Make(devBounds));
        return true;
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////

void Draw::drawPaint(const SkPaint& paint) const {
    (*this).validate();

    if ((*fRC).isEmpty()) {
        return;
    }

    const SkIRect devRect = SkIRect::MakeWH(fDst.width(), fDst.height());

    SkAutoBlitterChoose blitter(*this, nullptr, paint, SkRect::Make(devRect));
    SkScan::FillIRect(devRect, *fRC, blitter.get());
}

///////////////////////////////////////////////////////////////////////////////

static inline SkPoint compute_stroke_size(const SkPaint& paint, const SkMatrix& matrix) {
    SkASSERT(matrix.rectStaysRect());
    SkASSERT(SkPaint::kFill_Style != paint.getStyle());

    const SkVector size = matrix.mapVector({paint.getStrokeWidth(), paint.getStrokeWidth()});
    const SkPoint strokeSize = SkPoint::Make(SkScalarAbs(size.fX), SkScalarAbs(size.fY));
    return strokeSize;
}

static bool easy_rect_join(const SkRect& rect,
                           const SkPaint& paint,
                           const SkMatrix& matrix,
                           SkPoint* strokeSize) {
    if (rect.isEmpty() || SkPaint::kMiter_Join != paint.getStrokeJoin() ||
        paint.getStrokeMiter() < SK_ScalarSqrt2) {
        return false;
    }

    *strokeSize = compute_stroke_size(paint, matrix);
    return true;
}

Draw::RectType Draw::ComputeRectType(const SkRect& rect,
                                     const SkPaint& paint,
                                     const SkMatrix& matrix,
                                     SkPoint* strokeSize) {
    const SkScalar width = paint.getStrokeWidth();
    const bool zeroWidth = (0 == width);
    SkPaint::Style style = paint.getStyle();

    if ((SkPaint::kStrokeAndFill_Style == style) && zeroWidth) {
        style = SkPaint::kFill_Style;
    }

    if (paint.getPathEffect() || paint.getMaskFilter() || !matrix.rectStaysRect() ||
        SkPaint::kStrokeAndFill_Style == style) {
        return RectType::kPath;
    }
    if (SkPaint::kFill_Style == style) {
        return RectType::kFill;
    }
    if (zeroWidth) {
        return RectType::kHair;
    }
    if (easy_rect_join(rect, paint, matrix, strokeSize)) {
        return RectType::kStroke;
    }
    return RectType::kPath;
}

static void draw_rect_as_path(const Draw& orig,
                              const SkRect& prePaintRect,
                              const SkPaint& paint,
                              const SkMatrix& ctm) {
    Draw draw(orig);
    draw.fCTM = &ctm;
    draw.drawPath(SkPath::Rect(prePaintRect), paint, nullptr);
}

void Draw::drawRect(const SkRect& prePaintRect,
                    const SkPaint& paint,
                    const SkMatrix* paintMatrix,
                    const SkRect* postPaintRect) const {
    (*this).validate();

    // nothing to draw
    if ((*fRC).isEmpty()) {
        return;
    }

    SkTCopyOnFirstWrite<SkMatrix> matrix(fCTM);
    if (paintMatrix) {
        SkASSERT(postPaintRect);
        (*matrix.writable()).preConcat(*paintMatrix);
    } else {
        SkASSERT(!postPaintRect);
    }

    SkPoint strokeSize = {0, 0};
    const RectType rtype = ComputeRectType(prePaintRect, paint, *fCTM, &strokeSize);

    if (RectType::kPath == rtype) {
        draw_rect_as_path(*this, prePaintRect, paint, *matrix);
        return;
    }

    const SkRect& paintRect = paintMatrix ? *postPaintRect : prePaintRect;
    // skip the paintMatrix when transforming the rect by the CTM; map the two corners
    const SkPoint corners[2] = {{paintRect.fLeft, paintRect.fTop},
                                {paintRect.fRight, paintRect.fBottom}};
    SkPoint devCorners[2] = {};
    (*fCTM).mapPoints(SkSpan<SkPoint>(devCorners, 2), SkSpan<const SkPoint>(corners, 2));
    SkRect devRect = SkRect::MakeLTRB(devCorners[0].fX, devCorners[0].fY, devCorners[1].fX,
                                      devCorners[1].fY);
    devRect.sort();

    // look for the quick exit, before we build a blitter
    SkRect bbox = devRect;
    if (paint.getStyle() != SkPaint::kFill_Style) {
        // extra space for hairlines
        if (paint.getStrokeWidth() == 0) {
            bbox.outset(1, 1);
        } else {
            // For RectType::kStroke, strokeSize is already computed.
            const SkPoint& ssize = (RectType::kStroke == rtype)
                ? strokeSize
                : compute_stroke_size(paint, *fCTM);
            bbox.outset(ssize.x() / 2.f, ssize.y() / 2.f);
        }
    }
    if (SkPathPriv::TooBigForMath(bbox)) {
        return;
    }

    if (!SkRectPriv::FitsInFixed(bbox) && rtype != RectType::kHair) {
        draw_rect_as_path(*this, prePaintRect, paint, *matrix);
        return;
    }

    const SkIRect ir = bbox.roundOut();
    if ((*fRC).quickReject(ir)) {
        return;
    }

    SkAutoBlitterChoose blitterStorage(*this, matrix, paint, devRect);
    const SkRasterClip& clip = *fRC;
    SkBlitter* blitter = blitterStorage.get();

    // we want to "fill" if we are kFill or kStrokeAndFill, since in the latter
    // case we are also hairline (if we've gotten to here), which devolves to
    // effectively just kFill
    switch (rtype) {
        case RectType::kFill:
            if (paint.isAntiAlias()) {
                SkScan::AntiFillRect(devRect, clip, blitter);
            } else {
                SkScan::FillRect(devRect, clip, blitter);
            }
            break;
        case RectType::kStroke:
            if (paint.isAntiAlias()) {
                SkScan::AntiFrameRect(devRect, strokeSize, clip, blitter);
            } else {
                SkScan::FrameRect(devRect, strokeSize, clip, blitter);
            }
            break;
        case RectType::kHair:
            if (paint.isAntiAlias()) {
                SkScan::AntiHairRect(devRect, clip, blitter);
            } else {
                SkScan::HairRect(devRect, clip, blitter);
            }
            break;
        default:
            // kPath returned above; nothing else is drawn here.
            SkDEBUGFAIL("bad rtype");
            break;
    }
}

static float fast_len(const SkVector& vec) {
    const float x = std::abs(vec.fX);
    const float y = std::abs(vec.fY);
    const float longer = std::max(x, y);
    const float shorter = std::min(x, y);
    const float length = longer + (shorter / 2.f);
    return length;
}

bool DrawTreatAAStrokeAsHairline(SkScalar strokeWidth, const SkMatrix& matrix, SkScalar* coverage) {
    SkASSERT(strokeWidth > 0);
    // We need to try to fake a thick-stroke with a modulated hairline.

    if (matrix.hasPerspective()) {
        return false;
    }

    const SkVector src[2] = {{strokeWidth, 0}, {0, strokeWidth}};
    SkVector dst[2] = {};
    matrix.mapVectors(dst, src);
    const SkScalar len0 = fast_len(dst[0]);
    const SkScalar len1 = fast_len(dst[1]);
    if (len0 <= SK_Scalar1 && len1 <= SK_Scalar1) {
        if (coverage) {
            *coverage = sk_float_midpoint(len0, len1);
        }
        return true;
    }
    return false;
}

void Draw::drawOval(const SkRect& oval, const SkPaint& paint) const {
    (*this).validate();

    if ((*fRC).isEmpty()) {
        return;
    }

    (*this).drawPath(SkPath::Oval(oval), paint, nullptr);
}

void Draw::drawRRect(const SkRRect& rrect, const SkPaint& paint) const {
    (*this).validate();

    if ((*fRC).isEmpty()) {
        return;
    }

    // TODO: Investigate optimizing these options. They are in the same
    // order as skcpu::Draw::drawPath, which handles each case. It may be
    // that there is no way to optimize for these using the SkRRect path.
    SkScalar coverage = 0;
    const bool needsPath = skcpu::DrawTreatAsHairline(paint, *fCTM, &coverage) ||
                           paint.getPathEffect() || paint.getStyle() != SkPaint::kFill_Style;

    if (!needsPath && paint.getMaskFilter()) {
        if ((*this).drawRRectNinePatch(rrect, paint)) {
            return;
        }
    }

    // Now fall back to the default case of using a path.
    (*this).drawPath(SkPath::RRect(rrect), paint, nullptr);
}

bool Draw::drawRRectNinePatch(const SkRRect& rrect, const SkPaint& paint) const {
    SkASSERT(paint.getMaskFilter());

    const std::optional<SkRRect> rr = rrect.transform(*fCTM);
    if (rr.has_value()) {
        SkAutoBlitterChoose blitter(*this, nullptr, paint, rrect.getBounds());
        SkResourceCache* cache = nullptr;  // a null cache selects the process-wide mask cache
        const SkMaskFilterBase* maskFilter = as_MFB(paint.getMaskFilter());
        if (rrect.getType() == SkRRect::kRect_Type) {
            const SkRect devRect = (*rr).rect();
            if ((*maskFilter).filterRects(SkSpan(&devRect, 1), *fCTM, *fRC, blitter.get(), cache) ==
                SkMaskFilterBase::FilterReturn::kTrue) {
                return true;
            }
        } else {
            if ((*maskFilter).filterRRect(*rr, *fCTM, *fRC, blitter.get(), cache)) {
                return true;  // filterRRect() called the blitter, so we're done
            }
        }
    }
    return false;
}

void Draw::drawDevPath(const SkPathRaw& raw,
                       const SkPaint& paint,
                       SkDrawCoverage drawCoverage,
                       SkBlitter* customBlitter,
                       bool doFill) const {
    if (SkPathPriv::TooBigForMath(raw.bounds())) {
        return;
    }

    SkBlitter* blitter = nullptr;
    SkAutoBlitterChoose blitterStorage;
    if (nullptr == customBlitter) {
        blitter = blitterStorage.choose(*this, nullptr, paint, raw.bounds(), drawCoverage);
    } else {
        blitter = customBlitter;
    }

    if (paint.getMaskFilter()) {
        const SkStrokeRec::InitStyle style = doFill ? SkStrokeRec::kFill_InitStyle
                                                    : SkStrokeRec::kHairline_InitStyle;
        SkResourceCache* cache = nullptr;  // a null cache selects the process-wide mask cache
        if ((*as_MFB(paint.getMaskFilter())).filterPath(raw, *fCTM, *fRC, blitter, style, cache)) {
            return;  // filterPath() called the blitter, so we're done
        }
    }

    using PathProc = void (*)(const SkPathRaw&, const SkRasterClip&, SkBlitter*);
    PathProc proc = nullptr;
    if (doFill) {
        if (paint.isAntiAlias()) {
            proc = SkScan::AntiFillPath;
        } else {
            proc = SkScan::FillPath;
        }
    } else {  // hairline
        if (paint.isAntiAlias()) {
            switch (paint.getStrokeCap()) {
                case SkPaint::kButt_Cap:
                    proc = SkScan::AntiHairPath;
                    break;
                case SkPaint::kSquare_Cap:
                    proc = SkScan::AntiHairSquarePath;
                    break;
                case SkPaint::kRound_Cap:
                    proc = SkScan::AntiHairRoundPath;
                    break;
            }
        } else {
            switch (paint.getStrokeCap()) {
                case SkPaint::kButt_Cap:
                    proc = SkScan::HairPath;
                    break;
                case SkPaint::kSquare_Cap:
                    proc = SkScan::HairSquarePath;
                    break;
                case SkPaint::kRound_Cap:
                    proc = SkScan::HairRoundPath;
                    break;
            }
        }
    }
    if (!proc) {
        // A cap outside the enumeration draws nothing.
        return;
    }
    proc(raw, *fRC, blitter);
}

/*
 *  Tricky idea: can we treat thin strokes as hairlines? If so, depending on how
 *  thin, we may decide to modulate the paint's alpha to 'simulate' very think
 *  strokes, even though hairline is always 1-pixel wide.
 *
 *  The motivation at the time was performance: hairlines draw faster than constructing
 *  the inner/outer contours and filling that (as we do for normal stroking).
 *
 *  Questionable decision, since our hairline algorithm draws each segment of the path
 *  separately, meaning a path that crosses itself can have blending artifacts.
 *  Note: this doesn't happen with normal stroking, as the built inner/outer path
 *  never double-hits a pixel.
 */
static std::optional<SkPaint> modifyPaintForHairlines(const SkPaint& origPaint,
                                                      const SkMatrix& matrix) {
    float coverage = 0;
    if (DrawTreatAsHairline(origPaint, matrix, &coverage)) {
        const std::optional<SkBlendMode> bm = origPaint.asBlendMode();
        if (coverage == 1) {
            SkPaint paint(origPaint);
            paint.setStrokeWidth(0);
            return paint;
        } else if (bm && SkBlendMode_SupportsCoverageAsAlpha(bm.value())) {
            // this is the old technique, which we preserve for now so
            // we don't change previous results (testing); coverage is at most one
            const int scale = static_cast<int>(coverage * 256);
            const U8CPU newAlpha = origPaint.getAlpha() * scale >> 8;
            SkPaint paint(origPaint);
            paint.setStrokeWidth(0);
            paint.setAlpha(newAlpha);
            return paint;
        }
    }
    return std::nullopt;
}

void Draw::drawPath(const SkPath& origSrcPath,
                    const SkPaint& origPaint,
                    const SkMatrix* prePathMatrix,
                    SkDrawCoverage drawCoverage,
                    SkBlitter* customBlitter) const {
    (*this).validate();

    // nothing to draw
    if ((*fRC).isEmpty()) {
        return;
    }

    const std::optional<SkPaint> newPaint = modifyPaintForHairlines(origPaint, *fCTM);
    // Borrows the modified paint or the caller's.
    const SkPaint* paint = newPaint.has_value() ? &newPaint.value()
                                                : &origPaint;

    const bool needsFillPath = (*paint).getPathEffect() || (*paint).getStyle() != SkPaint::kFill_Style;

    SkPathBuilder builder;
    std::optional<SkPathRaw> raw;      // will point to either origSrcPath or builder
    bool          doFill = true;

    sk_sp<SkPathData> pdata;

    if (needsFillPath) {
        SkRect cullRect = SkRect::MakeEmpty();
        const SkRect* cullRectPtr = nullptr;
        if ((*this).computeConservativeLocalClipBounds(&cullRect)) {
            cullRectPtr = &cullRect;
        }

        std::optional<SkPath> prePathStorage;
        const SkPath* pathPtr = &origSrcPath;
        if (prePathMatrix) {
            prePathStorage = (*pathPtr).tryMakeTransform(*prePathMatrix);
            if (!prePathStorage.has_value()) {
                return;
            }
            pathPtr = &prePathStorage.value();
        }
        doFill = skpathutils::FillPathWithPaint(*pathPtr, *paint, &builder, cullRectPtr, *fCTM);
        builder.transform(*fCTM);
        raw = SkPathPriv::Raw(builder, SkResolveConvexity::kYes);
    } else {
        SkMatrix matrix = *fCTM;
        if (prePathMatrix) {
            matrix.preConcat(*prePathMatrix);
        }

        if (matrix.isIdentity()) {
            raw = SkPathPriv::Raw(origSrcPath, SkResolveConvexity::kYes);
        } else {
            raw = SkPathPriv::Raw(origSrcPath, SkResolveConvexity::kNo);
            if (raw.has_value()) {
                pdata = SkPathData::MakeTransform(*raw, matrix);
            }
            if (!pdata) {
                return; // failed to create pdata
            }
            raw = (*pdata).raw(origSrcPath.getFillType(), SkResolveConvexity::kYes);
        }
    }

    if (!raw) {
        return;
    }

    (*this).drawDevPath(*raw, *paint, drawCoverage, customBlitter, doFill);
}

////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef SK_DEBUG

void Draw::validate() const {
    SkASSERT(fCTM != nullptr);
    SkASSERT(fRC != nullptr);

    const SkIRect& cr = (*fRC).getBounds();
    const SkIRect br = SkIRect::MakeWH(fDst.width(), fDst.height());
    SkASSERT(cr.isEmpty() || br.contains(cr));
}

#endif

////////////////////////////////////////////////////////////////////////////////////////////////

static bool compute_mask_bounds(const SkRect& devPathBounds,
                                const SkIRect& clipBounds,
                                const SkMaskFilter* filter,
                                const SkMatrix* filterMatrix,
                                SkIRect* bounds) {
    SkASSERT(filter);
    SkASSERT(filterMatrix);
    //  init our bounds from the path
    *bounds = devPathBounds.makeOutset(SK_ScalarHalf, SK_ScalarHalf).roundOut();

    SkIVector margin = SkIPoint::Make(0, 0);
    const SkMask srcM(nullptr, *bounds, 0, SkMask::kA8_Format);
    SkMaskBuilder dstM;
    if (!(*as_MFB(filter)).filterMask(&dstM, srcM, *filterMatrix, &margin)) {
        return false;
    }

    // trim the bounds to reflect the clip (plus whatever slop the filter needs)
    // Ugh. Guard against gigantic margins from wacky filters. Without this
    // check we can request arbitrary amounts of slop beyond our visible
    // clip, and bring down the renderer (at least on finite RAM machines
    // like handsets, etc.). Need to balance this invented value between
    // quality of large filters like blurs, and the corresponding memory
    // requests.
    static constexpr int kMaxMargin = 128;
    if (!(*bounds).intersect(clipBounds.makeOutset(std::min(margin.fX, kMaxMargin),
                                                 std::min(margin.fY, kMaxMargin)))) {
        return false;
    }

    return true;
}

static void draw_into_mask(const SkMask& mask,
                           SkPathRaw raw,
                           SkStrokeRec::InitStyle style) {
    SkPixmap dst;
    if (!dst.reset(mask)) {
        return;
    }

    const float dx = static_cast<float>(-mask.fBounds.fLeft);
    const float dy = static_cast<float>(-mask.fBounds.fTop);
    const SkMatrix translate = SkMatrix::Translate(dx, dy);

    SkPaint paint;
    paint.setAntiAlias(true);
    SkBlitterSizedArena alloc;
    SkBlitter* blitter = SkChooseA8Blitter(dst, translate, paint, &alloc,
                                           SkDrawCoverage::kNo, nullptr);

    // transform a copy of the points, so we can apply the ctm/translate
    skia_private::AutoSTArray<32, SkPoint> devPoints(raw.fPoints.size());
    translate.mapPoints(devPoints, raw.fPoints);
    raw.fPoints = devPoints;
    raw.fBounds = raw.fBounds.makeOffset(dx, dy);
    if (!raw.fBounds.isFinite()) {
        return;
    }

    const SkRasterClip clip(SkIRect::MakeWH(mask.fBounds.width(), mask.fBounds.height()));

    switch (style) {
        case SkStrokeRec::kHairline_InitStyle:
            SkScan::AntiHairPath(raw, clip, blitter);
            break;
        case SkStrokeRec::kFill_InitStyle:
            SkScan::AntiFillPath(raw, clip, blitter);
            break;
    }
}

bool DrawToMask(const SkPathRaw& devRaw,
                const SkIRect& clipBounds,
                const SkMaskFilter* filter,
                const SkMatrix* filterMatrix,
                SkMaskBuilder* dst,
                SkMaskBuilder::CreateMode mode,
                SkStrokeRec::InitStyle style) {
    SkASSERT(filter);
    if (devRaw.empty()) {
        return false;
    }

    if (SkMaskBuilder::kJustRenderImage_CreateMode != mode) {
        // By using infinite bounds for inverse fills, compute_mask_bounds is able to clip it to
        // 'clipBounds' outset by whatever extra margin the mask filter requires.
        static const SkRect kInverseBounds = {SK_ScalarNegativeInfinity,
                                              SK_ScalarNegativeInfinity,
                                              SK_ScalarInfinity,
                                              SK_ScalarInfinity};
        const SkRect pathBounds = devRaw.isInverseFillType() ? kInverseBounds : devRaw.bounds();
        if (!compute_mask_bounds(pathBounds, clipBounds, filter, filterMatrix, &(*dst).bounds())) {
            return false;
        }
    }

    if (SkMaskBuilder::kComputeBoundsAndRenderImage_CreateMode == mode) {
        (*dst).format() = SkMask::kA8_Format;
        (*dst).rowBytes() = (*dst).fBounds.width();
        const size_t size = (*dst).computeImageSize();
        if (0 == size) {
            // we're too big to allocate the mask, abort
            return false;
        }
        (*dst).image() = SkMaskBuilder::AllocImage(size, SkMaskBuilder::kZeroInit_Alloc);
    }

    if (SkMaskBuilder::kJustComputeBounds_CreateMode != mode) {
        draw_into_mask(*dst, devRaw, style);
    }
    return true;
}

void Draw::drawDevicePoints(SkCanvas::PointMode mode,
                            SkSpan<const SkPoint> points,
                            const SkPaint& paint,
                            SkDevice* device) const {
    // if we're in lines mode, force count to be even
    if (SkCanvas::kLines_PointMode == mode) {
        points = points.first(points.size() & ~1);  // force it to be even
    }

    (*this).validate();

     // nothing to draw
    if (points.empty() || (*fRC).isEmpty()) {
        return;
    }

    // needed?
    if (!points_are_finite(points)) {
        return;
    }

    switch (mode) {
        case SkCanvas::kPoints_PointMode: {
            // temporarily mark the paint as filling.
            SkPaint newPaint(paint);
            newPaint.setStyle(SkPaint::kFill_Style);

            const float width = newPaint.getStrokeWidth();
            const float radius = width / 2.f;

            if (newPaint.getStrokeCap() == SkPaint::kRound_Cap) {
                if (device) {
                    for (const SkPoint& pt : points) {
                        const SkRect r = SkRect::MakeLTRB(pt.fX - radius, pt.fY - radius,
                                                          pt.fX + radius, pt.fY + radius);
                        (*device).drawOval(r, newPaint);
                    }
                } else {
                    const SkPath path = SkPath::Circle(0, 0, radius);

                    for (const SkPoint& pt : points) {
                        const SkMatrix preMatrix = SkMatrix::Translate(pt.fX, pt.fY);
                        (*this).drawPath(path, newPaint, &preMatrix);
                    }
                }
            } else {
                for (const SkPoint& pt : points) {
                    SkRect r = SkRect::MakeEmpty();
                    r.fLeft = pt.fX - radius;
                    r.fTop = pt.fY - radius;
                    r.fRight = r.fLeft + width;
                    r.fBottom = r.fTop + width;
                    if (device) {
                        (*device).drawRect(r, newPaint);
                    } else {
                        (*this).drawRect(r, newPaint);
                    }
                }
            }
            break;
        }
        case SkCanvas::kLines_PointMode:
            if (2 == points.size() && paint.getPathEffect()) {
                // most likely a dashed line - see if it is one of the ones
                // we can accelerate
                const SkStrokeRec stroke(paint);
                SkPathEffectBase::PointData pointData;

                const SkPath path = SkPath::Line(points[0], points[1]);

                const SkRect cullRect = SkRect::Make((*fRC).getBounds());

                if ((*as_PEB(paint.getPathEffect())
                            ).asPoints(&pointData, path, stroke, *fCTM, &cullRect)) {
                    // 'asPoints' managed to find some fast path

                    SkPaint newP(paint);
                    newP.setPathEffect(nullptr);
                    newP.setStyle(SkPaint::kFill_Style);

                    if (!pointData.fFirst.isEmpty()) {
                        if (device) {
                            (*device).drawPath(pointData.fFirst, newP);
                        } else {
                            (*this).drawPath(pointData.fFirst, newP, nullptr);
                        }
                    }

                    if (!pointData.fLast.isEmpty()) {
                        if (device) {
                            (*device).drawPath(pointData.fLast, newP);
                        } else {
                            (*this).drawPath(pointData.fLast, newP, nullptr);
                        }
                    }

                    if (pointData.fSize.fX == pointData.fSize.fY) {
                        // The rest of the dashed line can just be drawn as points
                        SkASSERT(pointData.fSize.fX == newP.getStrokeWidth() / 2.f);

                        if (SkPathEffectBase::PointData::kCircles_PointFlag & pointData.fFlags) {
                            newP.setStrokeCap(SkPaint::kRound_Cap);
                        } else {
                            newP.setStrokeCap(SkPaint::kButt_Cap);
                        }

                        if (device) {
                            (*device).drawPoints(
                                    SkCanvas::kPoints_PointMode, pointData.points(), newP);
                        } else {
                            (*this).drawDevicePoints(
                                    SkCanvas::kPoints_PointMode, pointData.points(), newP, device);
                        }
                        break;
                    } else {
                        // The rest of the dashed line must be drawn as rects
                        SkASSERT(!(SkPathEffectBase::PointData::kCircles_PointFlag &
                                   pointData.fFlags));

                        for (const SkPoint& pt : pointData.points()) {
                            const SkRect r = SkRect::MakeLTRB(pt.fX - pointData.fSize.fX,
                                                              pt.fY - pointData.fSize.fY,
                                                              pt.fX + pointData.fSize.fX,
                                                              pt.fY + pointData.fSize.fY);
                            if (device) {
                                (*device).drawRect(r, newP);
                            } else {
                                (*this).drawRect(r, newP);
                            }
                        }
                    }

                    break;
                }
            }
            [[fallthrough]];  // couldn't take fast path
        case SkCanvas::kPolygon_PointMode: {
            const size_t count = points.size() - 1;
            SkPaint p(paint);
            p.setStyle(SkPaint::kStroke_Style);
            const size_t inc = (SkCanvas::kLines_PointMode == mode) ? 2 : 1;

            for (size_t i = 0; i < count; i += inc) {
                const SkPath path = SkPath::Line(points[i], points[i + 1]);
                if (device) {
                    (*device).drawPath(path, p);
                } else {
                    (*this).drawPath(path, p, nullptr);
                }
            }
            break;
        }
    }
}

}  // namespace skcpu
