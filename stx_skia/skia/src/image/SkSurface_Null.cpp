/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSurface.h"
#include "include/utils/SkNoDrawCanvas.h"
#include "src/image/SkSurface_Base.h"

class SkCanvas;
class SkPaint;
class SkPixmap;
struct SkIRect;
struct SkSamplingOptions;

class SkNullSurface : public SkSurface_Base {
public:
    SkNullSurface(int width, int height) : SkSurface_Base(width, height, nullptr) {}

    // From SkSurface_Base.h
    SkSurface_Base::Type type() const override { return SkSurface_Base::Type::kNull; }

protected:
    SkCanvas* onNewCanvas() override {
        SkCanvas* canvas = new SkNoDrawCanvas(width(), height());
        return canvas;
    }
    sk_sp<SkSurface> onNewSurface(const SkImageInfo& info) override {
        sk_sp<SkSurface> surface = SkSurfaces::Null(info.width(), info.height());
        return surface;
    }
    sk_sp<SkImage> onNewImageSnapshot(const SkIRect* subsetOrNull) override { return nullptr; }
    void onWritePixels(const SkPixmap&, int x, int y) override {}
    void onDraw(SkCanvas*, SkScalar, SkScalar, const SkSamplingOptions&, const SkPaint*) override {}
    bool onCopyOnWrite(ContentChangeMode) override { return true; }
    SkImageInfo imageInfo() const override {
        const SkImageInfo info = SkImageInfo::MakeUnknown(width(), height());
        return info;
    }
};

namespace SkSurfaces {

sk_sp<SkSurface> Null(int width, int height) {
    if (width < 1 || height < 1) {
        return nullptr;
    }
    sk_sp<SkSurface> surface = sk_make_sp<SkNullSurface>(width, height);
    return surface;
}

}
