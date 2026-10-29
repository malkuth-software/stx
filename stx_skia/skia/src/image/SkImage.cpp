/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkImage.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"
#include "include/core/SkSurfaceProps.h"
#include "include/core/SkTileMode.h"
#include "src/core/SkColorSpacePriv.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkMipmap.h"
#include "src/core/SkNextID.h"
#include "src/image/SkImage_Base.h"
#include "src/shaders/SkImageShader.h"

#include <utility>

SkImage::SkImage(const SkImageInfo& info, uint32_t uniqueID)
        : fInfo(info),
          fUniqueID(uniqueID == kNeedNewImageUniqueID ? SkNextID::ImageID() : uniqueID) {
    SkASSERT(info.width() > 0);
    SkASSERT(info.height() > 0);
}

std::optional<SkPixmap> SkImage::peekPixels() const {
    const SkImage_Base& base = *as_IB(this);
    std::optional<SkPixmap> pixels = base.onPeekPixels();
    return pixels;
}

bool SkImage::readPixels(const SkImageInfo& info, void* pixels, size_t rowBytes,
                         int sourceX, int sourceY, CachingHint hint) const {
    const SkImage_Base& base = *as_IB(this);
    const bool copied = base.onReadPixels(info, pixels, rowBytes, sourceX, sourceY, hint);
    return copied;
}

bool SkImage::readPixels(std::nullptr_t, const SkImageInfo& info, void* pixels, size_t rowBytes,
                         int sourceX, int sourceY, CachingHint hint) const {
    const bool copied = readPixels(info, pixels, rowBytes, sourceX, sourceY, hint);
    return copied;
}

bool SkImage::readPixels(const SkPixmap& pixels, int sourceX, int sourceY, CachingHint hint) const {
    const bool copied = readPixels(pixels.info(), pixels.writable_addr(), pixels.rowBytes(),
                                   sourceX, sourceY, hint);
    return copied;
}

bool SkImage::readPixels(std::nullptr_t, const SkPixmap& pixels, int sourceX, int sourceY,
                         CachingHint hint) const {
    const bool copied = readPixels(pixels, sourceX, sourceY, hint);
    return copied;
}

sk_sp<SkImage> SkImage::makeScaled(const SkImageInfo& info, const SkSamplingOptions& sampling) const {
    sk_sp<SkImage> image = makeScaled(nullptr, info, sampling);
    return image;
}

sk_sp<SkImage> SkImage::makeScaled(SkRecorder* recorder, const SkImageInfo& info,
                                  const SkSamplingOptions& sampling) const {
    if (!SkImageInfoIsValid(info)) {
        return nullptr;
    }
    if (info == imageInfo()) {
        sk_sp<SkImage> image = sk_ref_sp(this);
        return image;
    }
    const SkImage_Base& base = *as_IB(this);
    sk_sp<SkSurface> surface = base.onMakeSurface(recorder, info);
    if (!surface) {
        return nullptr;
    }
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kSrc);
    const SkRect destination = SkRect::MakeIWH(info.width(), info.height());
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.drawImageRect(this, destination, sampling, &paint);
    sk_sp<SkImage> image = (*surface).makeImageSnapshot();
    return image;
}

sk_sp<SkImage> SkImage::makeScaled(SkRecorder* recorder, const SkImageInfo& info,
                                  const SkSamplingOptions& sampling, const SkSurfaceProps&) const {
    sk_sp<SkImage> image = makeScaled(recorder, info, sampling);
    return image;
}

void SkImage::asyncRescaleAndReadPixels(const SkImageInfo& info, const SkIRect& sourceBounds,
                                        RescaleGamma gamma, RescaleMode mode,
                                        ReadPixelsCallback callback, ReadPixelsContext context) const {
    const SkIRect imageBounds = bounds();
    if (!imageBounds.contains(sourceBounds) || !SkImageInfoIsValid(info)) {
        callback(context, nullptr);
        return;
    }
    const SkImage_Base& base = *as_IB(this);
    base.onAsyncRescaleAndReadPixels(info, sourceBounds, gamma, mode, callback, context);
}

bool SkImage::scalePixels(const SkPixmap& destination, const SkSamplingOptions& sampling,
                          CachingHint hint) const {
    if (width() == destination.width() && height() == destination.height()) {
        const bool copied = readPixels(destination, 0, 0, hint);
        return copied;
    }
    const SkImage_Base& base = *as_IB(this);
    SkBitmap bitmap;
    if (!base.getROPixels(&bitmap, hint)) {
        return false;
    }
    const std::optional<SkPixmap> sourceView = bitmap.peekPixels();
    if (!sourceView) {
        return false;
    }
    const SkPixmap& source = *sourceView;
    const bool scaled = source.scalePixels(destination, sampling);
    return scaled;
}

SkColorType SkImage::colorType() const {
    const SkColorType type = fInfo.colorType();
    return type;
}
SkAlphaType SkImage::alphaType() const {
    const SkAlphaType type = fInfo.alphaType();
    return type;
}
SkColorSpace* SkImage::colorSpace() const {
    SkColorSpace* space = fInfo.colorSpace();
    return space;
}
sk_sp<SkColorSpace> SkImage::refColorSpace() const {
    sk_sp<SkColorSpace> space = fInfo.refColorSpace();
    return space;
}
bool SkImage::isAlphaOnly() const {
    const bool alphaOnly = SkColorTypeIsAlphaOnly(fInfo.colorType());
    return alphaOnly;
}

sk_sp<SkShader> SkImage::makeShader(const SkSamplingOptions& sampling, const SkMatrix& matrix) const {
    sk_sp<SkShader> shader = makeShader(SkTileMode::kClamp, SkTileMode::kClamp, sampling, &matrix);
    return shader;
}
sk_sp<SkShader> SkImage::makeShader(const SkSamplingOptions& sampling, const SkMatrix* matrix) const {
    sk_sp<SkShader> shader = makeShader(SkTileMode::kClamp, SkTileMode::kClamp, sampling, matrix);
    return shader;
}
sk_sp<SkShader> SkImage::makeShader(SkTileMode x, SkTileMode y, const SkSamplingOptions& sampling,
                                   const SkMatrix& matrix) const {
    sk_sp<SkShader> shader = makeShader(x, y, sampling, &matrix);
    return shader;
}
sk_sp<SkShader> SkImage::makeShader(SkTileMode x, SkTileMode y, const SkSamplingOptions& sampling,
                                   const SkMatrix* matrix) const {
    sk_sp<SkImage> image = sk_ref_sp(const_cast<SkImage*>(this));
    sk_sp<SkShader> shader = SkImageShader::Make(std::move(image), x, y, sampling, matrix);
    return shader;
}
sk_sp<SkShader> SkImage::makeRawShader(const SkSamplingOptions& sampling,
                                      const SkMatrix& matrix) const {
    sk_sp<SkShader> shader = makeRawShader(SkTileMode::kClamp, SkTileMode::kClamp, sampling, &matrix);
    return shader;
}
sk_sp<SkShader> SkImage::makeRawShader(const SkSamplingOptions& sampling,
                                      const SkMatrix* matrix) const {
    sk_sp<SkShader> shader = makeRawShader(SkTileMode::kClamp, SkTileMode::kClamp, sampling, matrix);
    return shader;
}
sk_sp<SkShader> SkImage::makeRawShader(SkTileMode x, SkTileMode y, const SkSamplingOptions& sampling,
                                      const SkMatrix& matrix) const {
    sk_sp<SkShader> shader = makeRawShader(x, y, sampling, &matrix);
    return shader;
}
sk_sp<SkShader> SkImage::makeRawShader(SkTileMode x, SkTileMode y, const SkSamplingOptions& sampling,
                                      const SkMatrix* matrix) const {
    sk_sp<SkImage> image = sk_ref_sp(const_cast<SkImage*>(this));
    sk_sp<SkShader> shader = SkImageShader::MakeRaw(std::move(image), x, y, sampling, matrix);
    return shader;
}

sk_sp<const SkData> SkImage::refEncodedData() const {
    const SkImage_Base& base = *as_IB(this);
    sk_sp<const SkData> data = base.onRefEncoded();
    return data;
}
bool SkImage::asLegacyBitmap(SkBitmap* bitmap, LegacyBitmapMode) const {
    const SkImage_Base& base = *as_IB(this);
    const bool copied = base.onAsLegacyBitmap(bitmap);
    return copied;
}

sk_sp<SkImage> SkImage::reinterpretColorSpace(sk_sp<SkColorSpace> target) const {
    if (!target) {
        return nullptr;
    }
    SkColorSpace* original = colorSpace();
    if (!original) {
        original = sk_srgb_singleton();
    }
    if (SkColorSpace::Equals(original, target.get()) || isAlphaOnly()) {
        sk_sp<SkImage> image = sk_ref_sp(const_cast<SkImage*>(this));
        return image;
    }
    const SkImage_Base& base = *as_IB(this);
    sk_sp<SkImage> image = base.onReinterpretColorSpace(std::move(target));
    return image;
}

sk_sp<SkImage> SkImage::makeRasterImage(CachingHint hint) const {
    if (peekPixels()) {
        sk_sp<SkImage> image = sk_ref_sp(const_cast<SkImage*>(this));
        return image;
    }
    const size_t rowBytes = fInfo.minRowBytes();
    const std::optional<size_t> size = fInfo.checkedByteSize(rowBytes);
    if (!size) {
        return nullptr;
    }
    sk_sp<SkData> data = SkData::MakeUninitialized(*size);
    // Suppress conversion here: the copied values keep the source's color label.
    const SkImageInfo untagged = fInfo.makeColorSpace(nullptr);
    const SkPixmap destination(untagged, (*data).writable_data(), rowBytes);
    if (!readPixels(destination, 0, 0, hint)) {
        return nullptr;
    }
    sk_sp<SkImage> image = SkImages::RasterFromData(fInfo, std::move(data), rowBytes);
    return image;
}
sk_sp<SkImage> SkImage::makeRasterImage(std::nullptr_t, CachingHint hint) const {
    sk_sp<SkImage> image = makeRasterImage(hint);
    return image;
}
bool SkImage::hasMipmaps() const {
    const SkImage_Base& base = *as_IB(this);
    const bool present = base.onHasMipmaps();
    return present;
}
sk_sp<SkImage> SkImage::withMipmaps(sk_sp<SkMipmap> mips) const {
    if (!mips || (*mips).validForRootLevel(imageInfo())) {
        const SkImage_Base& base = *as_IB(this);
        sk_sp<SkImage> image = base.onMakeWithMipmaps(std::move(mips));
        if (image) {
            return image;
        }
    }
    sk_sp<SkImage> image = sk_ref_sp(const_cast<SkImage*>(this));
    return image;
}
sk_sp<SkImage> SkImage::withDefaultMipmaps() const {
    sk_sp<SkImage> image = withMipmaps(nullptr);
    return image;
}
