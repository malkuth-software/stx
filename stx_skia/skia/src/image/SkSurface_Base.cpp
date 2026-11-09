/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/image/SkSurface_Base.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSize.h"
#include "include/core/SkSurface.h"
#include "src/image/SkRescaleAndReadPixels.h"

#include <atomic>
#include <cstdint>

class SkPaint;
class SkSurfaceProps;

SkSurface_Base::SkSurface_Base(int width, int height, const SkSurfaceProps* props)
        : SkSurface(width, height, props) {}

SkSurface_Base::SkSurface_Base(const SkImageInfo& info, const SkSurfaceProps* props)
        : SkSurface(info, props) {}

SkSurface_Base::~SkSurface_Base() {
    // Revoke the observer before notifying and destroying the owned canvas.
    if (fCanvas) {
        (*fCanvas).setSurfaceBase(nullptr);
        (*fCanvas).onSurfaceDelete();
    }
}


SkRecorder* SkSurface_Base::onGetBaseRecorder() const { return nullptr; }

void SkSurface_Base::onDraw(SkCanvas* canvas, SkScalar x, SkScalar y,
                            const SkSamplingOptions& sampling, const SkPaint* paint) {
    sk_sp<SkImage> image = makeTemporaryImage();
    if (image) {
        (*canvas).drawImage(image.get(), x, y, sampling, paint);
    }
}

void SkSurface_Base::onAsyncRescaleAndReadPixels(const SkImageInfo& info,
                                                 SkIRect origSrcRect,
                                                 SkSurface::RescaleGamma rescaleGamma,
                                                 RescaleMode rescaleMode,
                                                 SkSurface::ReadPixelsCallback callback,
                                                 SkSurface::ReadPixelsContext context) {
    SkBitmap source;
    const std::optional<SkPixmap> sourceView = peekPixels();
    SkIRect sourceRect = SkIRect::MakeEmpty();
    if (sourceView) {
        source.installPixels(*sourceView);
        sourceRect = origSrcRect;
    } else {
        const SkISize sourceSize = origSrcRect.size();
        const SkImageInfo surfaceInfo = imageInfo();
        const SkImageInfo sourceInfo = surfaceInfo.makeDimensions(sourceSize);
        if (!source.tryAllocPixels(sourceInfo)) {
            callback(context, nullptr);
            return;
        }
        const bool read = readPixels(source, origSrcRect.x(), origSrcRect.y());
        if (!read) {
            callback(context, nullptr);
            return;
        }
        sourceRect = SkIRect::MakeSize(source.dimensions());
    }
    SkRescaleAndReadPixels(source, info, sourceRect, rescaleGamma, rescaleMode,
                          callback, context);
}

bool SkSurface_Base::outstandingImageSnapshot() const {
    const bool shared = fCachedImage && !(*fCachedImage).unique();
    return shared;
}

bool SkSurface_Base::aboutToDraw(ContentChangeMode mode) {
    dirtyGenerationID();
    SkASSERT(!fCanvas || (*fCanvas).getSurfaceBase() == this);

    if (!fCachedImage) {
        if (mode == kDiscard_ContentChangeMode) {
            onDiscard();
        }
        return true;
    }

    const bool exclusivelyCached = (*fCachedImage).unique();
    if (!exclusivelyCached) {
        const bool separated = onCopyOnWrite(mode);
        if (!separated) {
            return false;
        }
    }

    // End the cached image's lifetime before making its backing mutable again.
    // External image owners keep their allocation; failed separation above
    // leaves both the cached snapshot and the surface backing unchanged.
    fCachedImage.reset();
    if (exclusivelyCached) {
        onRestoreBackingMutability();
    }
    return true;
}

uint32_t SkSurface_Base::newGenerationID() {
    SkASSERT(!fCanvas || (*fCanvas).getSurfaceBase() == this);
    static std::atomic<uint32_t> nextID{1};
    const uint32_t generation = nextID.fetch_add(1, std::memory_order_relaxed);
    return generation;
}
