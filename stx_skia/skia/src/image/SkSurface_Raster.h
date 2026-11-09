/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkSurface_Raster_DEFINED
#define SkSurface_Raster_DEFINED

#include "include/core/SkBitmap.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSurface.h"
#include "src/image/SkSurface_Base.h"

#include <cstring>

class SkCanvas;
class SkImage;
class SkPaint;
class SkPixelRef;
class SkPixmap;
class SkRecorder;
class SkSurfaceProps;
struct SkIRect;

namespace skcpu { class RecorderImpl; }

class SkSurface_Raster : public SkSurface_Base {
public:
    SkSurface_Raster(const SkImageInfo&, void*, size_t rb,
                     SkSurfaces::PixelsReleaseProc releaseProc, void* context,
                     const SkSurfaceProps*);
    SkSurface_Raster(const SkImageInfo& info, sk_sp<SkPixelRef>, const SkSurfaceProps*);

    SkSurface_Raster(skcpu::RecorderImpl* recorder,
                     const SkImageInfo&,
                     void* pixels,
                     size_t rowBytes,
                     SkSurfaces::PixelsReleaseProc releaseProc,
                     void* context,
                     const SkSurfaceProps*);
    SkSurface_Raster(skcpu::RecorderImpl* recorder,
                     const SkImageInfo&,
                     sk_sp<SkPixelRef>,
                     const SkSurfaceProps*);

    // From SkSurface.h
    SkImageInfo imageInfo() const override {
        const SkImageInfo info = fBitmap.info();
        return info;
    }

    // From SkSurface_Base.h
    SkSurface_Base::Type type() const override { return SkSurface_Base::Type::kRaster; }

    SkCanvas* onNewCanvas() override;
    sk_sp<SkSurface> onNewSurface(const SkImageInfo&) override;
    sk_sp<SkImage> onNewImageSnapshot(const SkIRect* subset) override;
    void onWritePixels(const SkPixmap&, int x, int y) override;
    void onDraw(SkCanvas*, SkScalar, SkScalar, const SkSamplingOptions&, const SkPaint*) override;
    bool onCopyOnWrite(ContentChangeMode) override;
    void onRestoreBackingMutability() override;
    SkRecorder* onGetBaseRecorder() const override;

private:
    // Observes the recorder supplied at construction; it must outlive the surface.
    skcpu::RecorderImpl* fRecorder = nullptr;
    SkBitmap fBitmap;
    bool fWeOwnThePixels = false;
};

#endif
