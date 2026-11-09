/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file
 */

#include "src/core/SkSpecialImage.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPoint.h"
#include "include/core/SkShader.h"
#include "include/private/SkAssert.h"
#include "src/core/SkNextID.h"
#include "src/image/SkImage_Base.h"

SkSpecialImage::SkSpecialImage(const SkIRect& subset,
                               uint32_t uniqueID,
                               const SkColorInfo& colorInfo,
                               const SkSurfaceProps& props)
    : fSubset(subset)
    , fUniqueID(kNeedNewImageUniqueID_SpecialImage == uniqueID ? SkNextID::ImageID() : uniqueID)
    , fColorInfo(colorInfo)
    , fProps(props) {
}

void SkSpecialImage::draw(SkCanvas* canvas,
                          SkScalar x, SkScalar y,
                          const SkSamplingOptions& sampling,
                          const SkPaint* paint, bool strict) const {
    const SkIRect& content = subset();
    const SkRect destination = SkRect::MakeXYWH(x, y, content.width(), content.height());
    const SkRect source = SkRect::Make(content);
    const SkCanvas::SrcRectConstraint constraint = strict ? SkCanvas::kStrict_SrcRectConstraint
                                                        : SkCanvas::kFast_SrcRectConstraint;
    const sk_sp<SkImage> image = asImage();
    (*canvas).drawImageRect(image, source, destination, sampling, paint, constraint);
}

class SkSpecialImage_Raster final : public SkSpecialImage {
public:
    SkSpecialImage_Raster(const SkIRect& subset, const SkBitmap& bm, const SkSurfaceProps& props)
            : SkSpecialImage(subset, bm.getGenerationID(), bm.info().colorInfo(), props)
            , fBitmap(bm) {
        SkASSERT(bm.pixelRef());
        SkASSERT(fBitmap.getPixels());
    }

    bool getROPixels(SkBitmap* bm) const {
        const bool extracted = fBitmap.extractSubset(bm, subset());
        return extracted;
    }

    SkISize backingStoreDimensions() const override {
        const SkISize dimensions = fBitmap.dimensions();
        return dimensions;
    }

    size_t getSize() const override {
        // A special image wraps allocated pixels, so their extent fits.
        const std::optional<size_t> bytes = fBitmap.checkedByteSize();
        SkASSERT_RELEASE(bytes.has_value());
        return *bytes;
    }

    sk_sp<SkImage> asImage() const override {
        sk_sp<SkImage> image = fBitmap.asImage();
        return image;
    }

    sk_sp<SkSpecialImage> onMakeBackingStoreSubset(const SkIRect& subset) const override {
        // Retain the backing pixels; getROPixels extracts the requested view on demand.
        sk_sp<SkSpecialImage> image = SkSpecialImages::MakeFromRaster(subset, fBitmap, props());
        return image;
    }

    sk_sp<SkShader> asShader(SkTileMode tileMode,
                             const SkSamplingOptions& sampling,
                             const SkMatrix& lm,
                             bool strict) const override {
        if (strict) {
            // Bound every sample to the pixel view, including filter taps and mip generation.
            SkBitmap subsetBitmap{};
            if (!getROPixels(&subsetBitmap)) {
                return nullptr;
            }
            sk_sp<SkShader> shader = subsetBitmap.makeShader(tileMode, tileMode, sampling, lm);
            return shader;
        }

        // Content coordinates start at the subset origin, even when sampling the backing image.
        const SkIPoint origin = subset().topLeft();
        SkMatrix subsetOrigin = SkMatrix::Translate(-origin);
        subsetOrigin.postConcat(lm);
        sk_sp<SkShader> shader = fBitmap.makeShader(tileMode, tileMode, sampling, subsetOrigin);
        return shader;
    }

private:
    // The copied bitmap retains its pixel reference; caller-owned bitmap metadata may expire.
    SkBitmap fBitmap;
};

namespace SkSpecialImages {

sk_sp<SkSpecialImage> MakeFromRaster(const SkIRect& subset,
                                     const SkBitmap& bm,
                                     const SkSurfaceProps& props) {
    SkASSERT(bm.bounds().contains(subset));

    if (!bm.pixelRef()) {
        return nullptr;
    }
    sk_sp<SkSpecialImage> image = sk_make_sp<SkSpecialImage_Raster>(subset, bm, props);
    return image;
}

sk_sp<SkSpecialImage> CopyFromRaster(const SkIRect& subset,
                                     const SkBitmap& bm,
                                     const SkSurfaceProps& props) {
    SkASSERT(bm.bounds().contains(subset));

    if (!bm.pixelRef()) {
        return nullptr;
    }
    SkBitmap copy{};
    const SkISize dimensions = subset.size();
    const SkImageInfo info = bm.info().makeDimensions(dimensions);
    if (!copy.tryAllocPixels(info)) {
        return nullptr;
    }
    if (!bm.readPixels(copy.info(), copy.getPixels(), copy.rowBytes(), subset.x(), subset.y())) {
        return nullptr;
    }
    // Since we're making a copy of the raster, the resulting special image is the exact size
    // of the requested subset of the original and no longer needs to be offset by subset's left
    // and top, since those were relative to the original's buffer.
    const SkIRect bounds = SkIRect::MakeSize(dimensions);
    sk_sp<SkSpecialImage> image = sk_make_sp<SkSpecialImage_Raster>(bounds, copy, props);
    return image;
}

sk_sp<SkSpecialImage> MakeFromRaster(const SkIRect& subset,
                                     sk_sp<SkImage> image,
                                     const SkSurfaceProps& props) {
    if (!image || subset.isEmpty()) {
        return nullptr;
    }

    SkASSERT((*image).bounds().contains(subset));

    SkBitmap bitmap{};
    SkImage_Base& rasterSource = *as_IB(image);
    if (!rasterSource.getROPixels(&bitmap)) {
        return nullptr;
    }
    sk_sp<SkSpecialImage> result = MakeFromRaster(subset, bitmap, props);
    return result;
}

bool AsBitmap(const SkSpecialImage* img, SkBitmap* result) {
    if (!img || (*img).isGaneshBacked() || (*img).isGraphiteBacked()) {
        return false;
    }
    const SkSpecialImage_Raster& rasterImage = *static_cast<const SkSpecialImage_Raster*>(img);
    const bool extracted = rasterImage.getROPixels(result);
    return extracted;
}

}  // namespace SkSpecialImages
