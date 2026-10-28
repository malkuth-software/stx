/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkSurface.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSurfaceProps.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkSurfacePriv.h"
#include "src/image/SkSurface_Base.h"

#include <cstddef>
#include <cstdint>

class SkPaint;

SkSurfaceProps::SkSurfaceProps()
    : fFlags(0)
    , fPixelGeometry(kUnknown_SkPixelGeometry)
    , fTextContrast(SK_GAMMA_CONTRAST)
    , fTextGamma(SK_GAMMA_EXPONENT) {}

SkSurfaceProps::SkSurfaceProps(uint32_t flags, SkPixelGeometry pg)
    : fFlags(flags)
    , fPixelGeometry(pg)
    , fTextContrast(SK_GAMMA_CONTRAST)
    , fTextGamma(SK_GAMMA_EXPONENT) {}

SkSurfaceProps::SkSurfaceProps(uint32_t flags,
                               SkPixelGeometry pg,
                               SkScalar textContrast,
                               SkScalar textGamma)
    : fFlags(flags), fPixelGeometry(pg), fTextContrast(textContrast), fTextGamma(textGamma) {}

SkSurface::SkSurface(int width, int height, const SkSurfaceProps* props)
    : fProps(SkSurfacePropsCopyOrDefault(props)), fWidth(width), fHeight(height)
{
    SkASSERT(fWidth > 0);
    SkASSERT(fHeight > 0);
}

SkSurface::SkSurface(const SkImageInfo& info, const SkSurfaceProps* props)
    : fProps(SkSurfacePropsCopyOrDefault(props)), fWidth(info.width()), fHeight(info.height())
{
    SkASSERT(fWidth > 0);
    SkASSERT(fHeight > 0);
}

uint32_t SkSurface::generationID() {
    if (fGenerationID == 0) {
        SkSurface_Base& base = *asSB(this);
        fGenerationID = base.newGenerationID();
    }
    return fGenerationID;
}

bool SkSurface::notifyContentWillChange(ContentChangeMode mode) {
    SkSurface_Base& base = *asSB(this);
    const bool prepared = base.aboutToDraw(mode);
    return prepared;
}

SkCanvas* SkSurface::getCanvas() {
    SkSurface_Base& base = *asSB(this);
    SkCanvas* canvas = base.getCachedCanvas();
    return canvas;
}

sk_sp<SkImage> SkSurface::makeImageSnapshot() {
    SkSurface_Base& base = *asSB(this);
    sk_sp<SkImage> image = base.refCachedImage();
    return image;
}

sk_sp<SkImage> SkSurface::makeImageSnapshot(const SkIRect& sourceBounds) {
    const SkIRect surfaceBounds = {0, 0, fWidth, fHeight};
    SkIRect bounds = sourceBounds;
    if (!bounds.intersect(surfaceBounds)) {
        return nullptr;
    }
    SkASSERT(!bounds.isEmpty());
    if (bounds == surfaceBounds) {
        sk_sp<SkImage> image = makeImageSnapshot();
        return image;
    }
    SkSurface_Base& base = *asSB(this);
    sk_sp<SkImage> image = base.onNewImageSnapshot(&bounds);
    return image;
}

sk_sp<SkImage> SkSurface::makeTemporaryImage() {
    SkSurface_Base& base = *asSB(this);
    sk_sp<SkImage> image = base.onMakeTemporaryImage();
    return image;
}

sk_sp<SkSurface> SkSurface::makeSurface(const SkImageInfo& info) {
    SkSurface_Base& base = *asSB(this);
    sk_sp<SkSurface> surface = base.onNewSurface(info);
    return surface;
}

sk_sp<SkSurface> SkSurface::makeSurface(int width, int height) {
    const SkImageInfo currentInfo = imageInfo();
    const SkImageInfo requestedInfo = currentInfo.makeWH(width, height);
    sk_sp<SkSurface> surface = makeSurface(requestedInfo);
    return surface;
}

void SkSurface::draw(SkCanvas* canvas, SkScalar x, SkScalar y,
                     const SkSamplingOptions& sampling, const SkPaint* paint) {
    SkSurface_Base& base = *asSB(this);
    base.onDraw(canvas, x, y, sampling, paint);
}

bool SkSurface::peekPixels(SkPixmap* pixels) {
    SkCanvas& canvas = *getCanvas();
    const bool available = canvas.peekPixels(pixels);
    return available;
}

bool SkSurface::readPixels(const SkPixmap& pixels, int sourceX, int sourceY) {
    SkCanvas& canvas = *getCanvas();
    const bool copied = canvas.readPixels(pixels, sourceX, sourceY);
    return copied;
}

bool SkSurface::readPixels(const SkImageInfo& destinationInfo, void* destinationPixels,
                           size_t destinationRowBytes, int sourceX, int sourceY) {
    const SkPixmap destination(destinationInfo, destinationPixels, destinationRowBytes);
    const bool copied = readPixels(destination, sourceX, sourceY);
    return copied;
}

bool SkSurface::readPixels(const SkBitmap& bitmap, int sourceX, int sourceY) {
    SkPixmap pixels;
    if (!bitmap.peekPixels(&pixels)) {
        return false;
    }
    const bool copied = readPixels(pixels, sourceX, sourceY);
    return copied;
}

void SkSurface::asyncRescaleAndReadPixels(const SkImageInfo& info,
                                         const SkIRect& sourceRect,
                                         RescaleGamma rescaleGamma,
                                         RescaleMode rescaleMode,
                                         ReadPixelsCallback callback,
                                         ReadPixelsContext context) {
    const SkIRect bounds = SkIRect::MakeWH(width(), height());
    if (!bounds.contains(sourceRect) || !SkImageInfoIsValid(info)) {
        callback(context, nullptr);
        return;
    }
    SkSurface_Base& base = *asSB(this);
    base.onAsyncRescaleAndReadPixels(info, sourceRect, rescaleGamma, rescaleMode,
                                   callback, context);
}

void SkSurface::writePixels(const SkPixmap& pixels, int x, int y) {
    if (pixels.addr() == nullptr || pixels.width() <= 0 || pixels.height() <= 0) {
        return;
    }
    const SkIRect sourceBounds = SkIRect::MakeXYWH(x, y, pixels.width(), pixels.height());
    const SkIRect destinationBounds = SkIRect::MakeWH(width(), height());
    if (!SkIRect::Intersects(sourceBounds, destinationBounds)) {
        return;
    }
    ContentChangeMode mode = kRetain_ContentChangeMode;
    if (sourceBounds.contains(destinationBounds)) {
        mode = kDiscard_ContentChangeMode;
    }
    SkSurface_Base& base = *asSB(this);
    if (!base.aboutToDraw(mode)) {
        return;
    }
    base.onWritePixels(pixels, x, y);
}

void SkSurface::writePixels(const SkBitmap& source, int x, int y) {
    SkPixmap pixels;
    if (!source.peekPixels(&pixels)) {
        return;
    }
    writePixels(pixels, x, y);
}

SkRecorder* SkSurface::baseRecorder() const {
    const SkSurface_Base& base = *asConstSB(this);
    SkRecorder* recorder = base.onGetBaseRecorder();
    return recorder;
}
