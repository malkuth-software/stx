/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/image/SkSurface_Raster.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCPURecorder.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMallocPixelRef.h"
#include "include/core/SkPixelRef.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSurface.h"
#include "include/private/SkAssert.h"
#include "include/private/SkMath.h"
#include "src/core/SkBitmapDevice.h"
#include "src/core/SkCPURecorderImpl.h"
#include "src/core/SkDevice.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkSurfacePriv.h"
#include "src/image/SkImage_Raster.h"

#include <cstdint>
#include <cstring>
#include <utility>

class SkImage;
class SkPaint;
class SkPixmap;
class SkSurfaceProps;

bool SkSurfaceValidateRasterInfo(const SkImageInfo& info, size_t rowBytes) {
    if (!SkImageInfoIsValid(info)) {
        return false;
    }

    if (kIgnoreRowBytesValue == rowBytes) {
        return true;
    }

    // SkBitmap stores the stride in a signed 32-bit field. Reject larger
    // caller strides before converting or multiplying them.
    if (rowBytes > SK_MaxS32 || !info.validRowBytes(rowBytes)) {
        return false;
    }

    const uint64_t height = static_cast<uint64_t>(info.height());
    const uint64_t stride = static_cast<uint64_t>(rowBytes);
    const uint64_t size = height * stride;
    constexpr size_t kMaxTotalSize = SK_MaxS32;
    if (size > kMaxTotalSize) {
        return false;
    }

    return true;
}

SkSurface_Raster::SkSurface_Raster(const SkImageInfo& info,
                                   void* pixels,
                                   size_t rb,
                                   SkSurfaces::PixelsReleaseProc releaseProc,
                                   void* context,
                                   const SkSurfaceProps* props)
        : SkSurface_Raster(
                  asRRI(skcpu::Recorder::TODO()), info, pixels, rb, releaseProc, context, props) {}

SkSurface_Raster::SkSurface_Raster(const SkImageInfo& info,
                                   sk_sp<SkPixelRef> pr,
                                   const SkSurfaceProps* props)
        : SkSurface_Raster(asRRI(skcpu::Recorder::TODO()), info, std::move(pr), props) {}

SkSurface_Raster::SkSurface_Raster(skcpu::RecorderImpl* recorder,
                                   const SkImageInfo& info,
                                   void* pixels,
                                   size_t rowBytes,
                                   SkSurfaces::PixelsReleaseProc releaseProc,
                                   void* context,
                                   const SkSurfaceProps* props)
        : SkSurface_Base(info, props), fRecorder(recorder) {
    fBitmap.installPixels(info, pixels, rowBytes, releaseProc, context);
    fWeOwnThePixels = false;    // We are "Direct"
}

SkSurface_Raster::SkSurface_Raster(skcpu::RecorderImpl* recorder,
                                   const SkImageInfo& info,
                                   sk_sp<SkPixelRef> pr,
                                   const SkSurfaceProps* props)
        : SkSurface_Base((*pr).width(), (*pr).height(), props), fRecorder(recorder) {
    fBitmap.setInfo(info, (*pr).rowBytes());
    fBitmap.setPixelRef(std::move(pr), 0, 0);
    fWeOwnThePixels = true;
}

SkCanvas* SkSurface_Raster::onNewCanvas() {
    SkASSERT(fRecorder);
    sk_sp<SkBitmapDevice> device = sk_make_sp<SkBitmapDevice>(fRecorder, fBitmap, props());
    SkCanvas* canvas = new SkCanvas(std::move(device));
    return canvas;
}

sk_sp<SkSurface> SkSurface_Raster::onNewSurface(const SkImageInfo& info) {
    SkASSERT(fRecorder);
    const SkSurfaceProps& properties = props();
    sk_sp<SkSurface> surface = (*fRecorder).makeBitmapSurface(info, 0, &properties);
    return surface;
}

void SkSurface_Raster::onDraw(SkCanvas* canvas, SkScalar x, SkScalar y,
                              const SkSamplingOptions& sampling, const SkPaint* paint) {
    sk_sp<SkImage> image = fBitmap.asImage();
    (*canvas).drawImage(image.get(), x, y, sampling, paint);
}

sk_sp<SkImage> SkSurface_Raster::onNewImageSnapshot(const SkIRect* subset) {
    if (subset) {
        const SkIRect bounds = SkIRect::MakeWH(fBitmap.width(), fBitmap.height());
        SkASSERT(bounds.contains(*subset));
        SkBitmap dst;
        const SkImageInfo subsetInfo = fBitmap.info().makeDimensions((*subset).size());
        if (!dst.tryAllocPixels(subsetInfo)) {
            return nullptr;
        }
        const SkPixmap subsetPixels = dst.pixmap();
        const bool copied = fBitmap.readPixels(subsetPixels, (*subset).left(), (*subset).top());
        if (!copied) {
            return nullptr;
        }
        dst.setImmutable(); // key, so MakeFromBitmap doesn't make a copy of the buffer
        sk_sp<SkImage> image = dst.asImage();
        return image;
    }

    SkCopyPixelsMode cpm = SkCopyPixelsMode::kIfMutable;
    if (fWeOwnThePixels) {
        // SkImage_raster requires these pixels are immutable for its full lifetime.
        // We'll undo this via onRestoreBackingMutability() if we can avoid the COW.
        if (SkPixelRef* pr = fBitmap.pixelRef()) {
            (*pr).setTemporarilyImmutable();
        }
    } else {
        cpm = SkCopyPixelsMode::kAlways;
    }

    // Owned snapshots share temporarily immutable storage; wrapped pixels copy.
    sk_sp<SkImage> image = SkImage_Raster::MakeFromBitmap(fBitmap, cpm);
    return image;
}

void SkSurface_Raster::onWritePixels(const SkPixmap& src, int x, int y) {
    fBitmap.writePixels(src, x, y);
}

void SkSurface_Raster::onRestoreBackingMutability() {
    SkASSERT(!hasCachedImage());  // Shouldn't be any snapshots out there.
    if (SkPixelRef* pr = fBitmap.pixelRef()) {
        (*pr).restoreMutability();
    }
}

bool SkSurface_Raster::onCopyOnWrite(ContentChangeMode mode) {
    // are we sharing pixelrefs with the image?
    sk_sp<SkImage> cached(refCachedImage());
    SkASSERT(cached);
    SkImage_Base& cachedBase = *as_IB(cached);
    SkASSERT(cachedBase.isRasterBacked());
    SkImage_Raster& cachedRaster = static_cast<SkImage_Raster&>(cachedBase);
    if (cachedRaster.getPixelRef() == fBitmap.pixelRef()) {
        SkASSERT(fWeOwnThePixels);
        if (kDiscard_ContentChangeMode == mode) {
            if (!fBitmap.tryAllocPixels()) {
                return false;
            }
        } else {
            SkBitmap prev(fBitmap);
            if (!fBitmap.tryAllocPixels()) {
                return false;
            }
            SkASSERT(prev.info() == fBitmap.info());
            SkASSERT(prev.rowBytes() == fBitmap.rowBytes());
            memcpy(fBitmap.getPixels(), prev.getPixels(), fBitmap.computeByteSize());
        }

        // Now fBitmap is a deep copy of itself (and therefore different from
        // what is being used by the image. Next we update the canvas to use
        // this as its backend, so we can't modify the image's pixels anymore.
        SkCanvas* canvas = getCachedCanvas();
        SkASSERT(canvas);
        SkDevice* root = (*canvas).rootDevice();
        SkBitmapDevice* bmDev = static_cast<SkBitmapDevice*>(root);
        (*bmDev).replaceBitmapBackendForRasterSurface(fBitmap);
    }
    return true;
}

SkRecorder* SkSurface_Raster::onGetBaseRecorder() const {
    return fRecorder;
}

///////////////////////////////////////////////////////////////////////////////
namespace SkSurfaces {
sk_sp<SkSurface> WrapPixels(const SkImageInfo& info,
                            void* pixels,
                            size_t rowBytes,
                            PixelsReleaseProc releaseProc,
                            void* context,
                            const SkSurfaceProps* props) {
    if (nullptr == releaseProc) {
        context = nullptr;
    }
    // The ignored-stride marker is private to callers that allocate pixels.
    // A wrapped surface must validate the stride it was actually given.
    if (rowBytes == kIgnoreRowBytesValue || !SkSurfaceValidateRasterInfo(info, rowBytes)) {
        return nullptr;
    }
    if (nullptr == pixels) {
        return nullptr;
    }

    sk_sp<SkSurface> surface = sk_make_sp<SkSurface_Raster>(
        info, pixels, rowBytes, releaseProc, context, props);
    return surface;
}

sk_sp<SkSurface> WrapPixels(const SkImageInfo& info,
                            void* pixels,
                            size_t rowBytes,
                            const SkSurfaceProps* props) {
    sk_sp<SkSurface> surface = WrapPixels(info, pixels, rowBytes, nullptr, nullptr, props);
    return surface;
}

sk_sp<SkSurface> Raster(const SkImageInfo& info, size_t rowBytes, const SkSurfaceProps* props) {
    if (!SkSurfaceValidateRasterInfo(info)) {
        return nullptr;
    }
    const size_t effectiveRowBytes = rowBytes == 0 ? info.minRowBytes() : rowBytes;
    if (effectiveRowBytes == kIgnoreRowBytesValue ||
        !SkSurfaceValidateRasterInfo(info, effectiveRowBytes)) {
        return nullptr;
    }

    sk_sp<SkPixelRef> pr = SkMallocPixelRef::MakeAllocate(info, rowBytes);
    if (!pr) {
        return nullptr;
    }
    if (rowBytes) {
        SkASSERT((*pr).rowBytes() == rowBytes);
    }
    sk_sp<SkSurface> surface = sk_make_sp<SkSurface_Raster>(info, std::move(pr), props);
    return surface;
}

}  // namespace SkSurfaces

namespace skcpu {

sk_sp<SkSurface> Recorder::makeBitmapSurface(const SkImageInfo& imageInfo,
                                             const SkSurfaceProps* surfaceProps) {
    sk_sp<SkSurface> surface = makeBitmapSurface(imageInfo, 0, surfaceProps);
    return surface;
}

sk_sp<SkSurface> Recorder::makeBitmapSurface(const SkImageInfo& imageInfo,
                                             size_t rowBytes,
                                             const SkSurfaceProps* surfaceProps) {
    if (!SkSurfaceValidateRasterInfo(imageInfo)) {
        return nullptr;
    }
    const size_t effectiveRowBytes = rowBytes == 0 ? imageInfo.minRowBytes() : rowBytes;
    if (effectiveRowBytes == kIgnoreRowBytesValue ||
        !SkSurfaceValidateRasterInfo(imageInfo, effectiveRowBytes)) {
        return nullptr;
    }

    sk_sp<SkPixelRef> pr = SkMallocPixelRef::MakeAllocate(imageInfo, rowBytes);
    if (!pr) {
        return nullptr;
    }
    if (rowBytes) {
        SkASSERT((*pr).rowBytes() == rowBytes);
    }

    RecorderImpl* recorder = asRRI(this);
    sk_sp<SkSurface> surface = sk_make_sp<SkSurface_Raster>(
        recorder, imageInfo, std::move(pr), surfaceProps);
    return surface;
}

}  // namespace skcpu
