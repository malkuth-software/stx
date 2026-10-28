/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/image/SkImage_Raster.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkCPURecorder.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixelRef.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRecorder.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSize.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypes.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkRectMemcpy.h"
#include "src/image/SkImage_Base.h"
#include "src/shaders/SkImageShader.h"

#include <cstddef>
#include <cstdint>
#include <utility>

class SkSurfaceProps;

// A subset must not reuse the parent pixel-ref identity: caches describe the
// image dimensions, while the shared pixel ref describes the full allocation.
static uint32_t bitmap_image_id(const SkBitmap& bitmap) {
    const SkPixelRef* pixels = bitmap.pixelRef();
    SkASSERT(pixels);
    const SkISize allocationSize = SkISize::Make((*pixels).width(), (*pixels).height());
    const SkISize imageSize = bitmap.dimensions();
    SkASSERT(allocationSize != imageSize || bitmap.pixelRefOrigin().isZero());
    if (allocationSize == imageSize) {
        const uint32_t id = bitmap.getGenerationID();
        return id;
    }
    return kNeedNewImageUniqueID;
}

static void release_data(void*, void* context) {
    SkData& data = *static_cast<SkData*>(context);
    data.unref();
}

SkImage_Raster::SkImage_Raster(const SkImageInfo& info, sk_sp<SkData> data,
                               size_t rowBytes, sk_sp<SkMipmap> mips, uint32_t id)
        : SkImage_Base(info, id), fMips(std::move(mips)) {
    // Validated factories transfer one data reference to the bitmap's release
    // callback. The bitmap is the image's sole owner of this pixel reference.
    void* address = const_cast<void*>((*data).data());
    SkData* retainedData = data.release();
    const bool installed = fBitmap.installPixels(info, address, rowBytes,
                                                 release_data, retainedData);
    SkASSERT(installed);
    (void)installed;
    fBitmap.setImmutable();
}

SkImage_Raster::SkImage_Raster(const SkBitmap& bitmap, sk_sp<SkMipmap> mips,
                               bool bitmapMayBeMutable)
        : SkImage_Base(bitmap.info(), bitmap_image_id(bitmap)),
          fBitmap(bitmap), fMips(std::move(mips)) {
    SkASSERT(bitmapMayBeMutable || fBitmap.isImmutable());
}

SkImage_Raster::~SkImage_Raster() {}

bool SkImage_Raster::onReadPixels(
                                  const SkImageInfo& dstInfo,
                                  void* dstPixels,
                                  size_t dstRowBytes,
                                  int srcX,
                                  int srcY,
                                  CachingHint) const {
    // readPixels is const; retaining a second pixel-ref owner adds no protection.
    const bool copied = fBitmap.readPixels(dstInfo, dstPixels, dstRowBytes, srcX, srcY);
    return copied;
}

bool SkImage_Raster::onPeekPixels(SkPixmap* pm) const {
    const bool available = fBitmap.peekPixels(pm);
    return available;
}

bool SkImage_Raster::getROPixels( SkBitmap* dst, CachingHint) const {
    *dst = fBitmap;
    return true;
}

sk_sp<SkSurface> SkImage_Raster::onMakeSurface(SkRecorder* recorder,
                                               const SkImageInfo& info) const {
    if (!recorder) {
        // Legacy scaling without an explicit recorder uses the shared CPU recorder.
        recorder = skcpu::Recorder::TODO();
    }
    const SkSurfaceProps* props = nullptr;
    constexpr size_t rowBytes = 0;
    skcpu::Recorder* cpu = (*recorder).cpuRecorder();
    if (!cpu) {
        return nullptr;
    }
    sk_sp<SkSurface> surface = (*cpu).makeBitmapSurface(info, rowBytes, props);
    return surface;
}

static SkBitmap copy_bitmap_subset(const SkBitmap& orig, const SkIRect& subset) {
    const SkImageInfo originalInfo = orig.info();
    const SkISize subsetSize = subset.size();
    const SkImageInfo info = originalInfo.makeDimensions(subsetSize);
    SkBitmap bitmap;
    if (!bitmap.tryAllocPixels(info)) {
        return SkBitmap{};
    }

    void* dst = bitmap.getPixels();
    const void* src = orig.getAddr(subset.x(), subset.y());
    if (!dst || !src) {
        SkDEBUGFAIL("SkImage_Raster::onMakeSubset with nullptr src or dst");
        return SkBitmap{};
    }

    SkRectMemcpy(dst, bitmap.rowBytes(), src, orig.rowBytes(), bitmap.rowBytes(),
                 subset.height());

    bitmap.setImmutable();
    return bitmap;
}

static sk_sp<SkMipmap> copy_mipmaps(const SkBitmap& src, SkMipmap* srcMips) {
    if (!srcMips) {
        return nullptr;
    }

    const SkPixmap sourcePixels = src.pixmap();
    sk_sp<SkMipmap> destination(SkMipmap::Build(sourcePixels, nullptr, false));
    if (!destination) {
        return nullptr;
    }
    const int levelCount = (*destination).countLevels();
    for (int i = 0; i < levelCount; ++i) {
        SkMipmap::Level sourceLevel{};
        SkMipmap::Level destinationLevel{};
        if (!(*srcMips).getLevel(i, &sourceLevel) ||
            !(*destination).getLevel(i, &destinationLevel)) {
            return nullptr;
        }
        const bool copied = sourceLevel.fPixmap.readPixels(destinationLevel.fPixmap);
        if (!copied) {
            return nullptr;
        }
    }
    return destination;
}

sk_sp<SkImage> SkImage_Raster::onMakeSubset(SkRecorder*,
                                            const SkIRect& subset,
                                            RequiredProperties requiredProperties) const {
    sk_sp<SkImage> image = nullptr;

    if (requiredProperties.fMipmapped) {
        const SkIRect fullBounds = SkIRect::MakeSize(fBitmap.dimensions());
        const bool fullCopy = subset == fullBounds;

        sk_sp<SkMipmap> mips = nullptr;
        if (fullCopy) {
            mips = copy_mipmaps(fBitmap, fMips.get());
        }

        // SkImage::withMipmaps will always make a copy for us so we can temporarily share
        // the pixel ref with fBitmap
        SkBitmap tmpSubset;
        if (!fBitmap.extractSubset(&tmpSubset, subset)) {
            return nullptr;
        }

        sk_sp<SkImage> tmp(
                new SkImage_Raster(tmpSubset, /*mips=*/nullptr, /* bitmapMayBeMutable= */ true));

        // withMipmaps will auto generate the mipmaps if a nullptr is passed in
        SkASSERT(!mips || (*mips).validForRootLevel((*tmp).imageInfo()));
        image = (*tmp).withMipmaps(std::move(mips));
    } else {
        SkBitmap copy = copy_bitmap_subset(fBitmap, subset);
        if (!copy.isNull()) {
            image = SkImages::RasterFromBitmap(copy);
        }
    }

    return image;
}

sk_sp<SkImage_Raster> SkImage_Raster::MakeFromBitmap(const SkBitmap& bm,
                                              SkCopyPixelsMode cpm,
                                              sk_sp<SkMipmap> mips) {
    if (!SkImageInfoIsValid(bm.info()) || bm.rowBytes() < bm.info().minRowBytes()) {
        return nullptr;
    }

    if (!bm.getPixels()) {
        return nullptr;
    }

    if (SkCopyPixelsMode::kAlways == cpm ||
        (!bm.isImmutable() && SkCopyPixelsMode::kNever != cpm)) {
        const std::optional<size_t> size = bm.checkedByteSize();
        if (!size) {
            return nullptr;
        }

        sk_sp<SkData> data(SkData::MakeWithCopy(bm.getPixels(), *size));

        sk_sp<SkImage_Raster> image(new SkImage_Raster(
            bm.info(), std::move(data), bm.rowBytes(), std::move(mips), kNeedNewImageUniqueID));
        return image;
    }
    const bool mayBeMutable = cpm == SkCopyPixelsMode::kNever;
    sk_sp<SkImage_Raster> image(new SkImage_Raster(bm, std::move(mips), mayBeMutable));
    return image;
}

bool SkImage_Raster::onAsLegacyBitmap( SkBitmap* bitmap) const {
    // When we're a snapshot from a surface, our bitmap may not be marked immutable
    // even though logically always we are, but in that case we can't physically share our
    // pixelref since the caller might call setImmutable() themselves
    // (thus changing our state).
    if (fBitmap.isImmutable()) {
        const SkIPoint origin = fBitmap.pixelRefOrigin();
        (*bitmap).setInfo(fBitmap.info(), fBitmap.rowBytes());
        sk_sp<SkPixelRef> pixels = sk_ref_sp(fBitmap.pixelRef());
        (*bitmap).setPixelRef(std::move(pixels), origin.x(), origin.y());
        return true;
    }
    const bool copied = SkImage_Base::onAsLegacyBitmap(bitmap);
    return copied;
}

sk_sp<SkShader> SkImage_Raster::makeShaderForPaint(const SkPaint& paint,
                                                   SkTileMode tmx,
                                                   SkTileMode tmy,
                                                   const SkSamplingOptions& sampling,
                                                   const SkMatrix* localMatrix) {
    sk_sp<SkImage> image = sk_ref_sp<SkImage>(this);
    sk_sp<SkShader> shader = SkImageShader::Make(std::move(image), tmx, tmy, sampling, localMatrix);
    if (!shader) {
        return nullptr;
    }
    if (SkColorTypeIsAlphaOnly(colorType()) && paint.getShader()) {
        // Compose the image shader with the paint's shader. Alpha images+shaders should output the
        // texture's alpha multiplied by the shader's color. DstIn (d*sa) will achieve this with
        // the source image and dst shader (MakeBlend takes dst first, src second).
        sk_sp<SkShader> colorShader = paint.refShader();
        shader = SkShaders::Blend(SkBlendMode::kDstIn, std::move(colorShader), std::move(shader));
    }
    return shader;
}

sk_sp<SkImage> SkImage_Raster::onMakeWithMipmaps(sk_sp<SkMipmap> mips) const {
    // It's dangerous to have two SkBitmaps that share a SkPixelRef but have different SkMipmaps
    // since various caches key on SkPixelRef's generation ID. Also, SkPixelRefs that back
    // SkSurfaces are marked "temporarily immutable" and making an image that uses the same
    // SkPixelRef can interact badly with SkSurface/SkImage copy-on-write. So we just always
    // make a copy with a new ID.
    if (!mips) {
        const SkPixmap pixels = fBitmap.pixmap();
        SkMipmap* created = SkMipmap::Build(pixels, nullptr);
        mips.reset(created);
    }
    sk_sp<SkImage> image = MakeFromBitmap(fBitmap, SkCopyPixelsMode::kAlways, std::move(mips));
    return image;
}

sk_sp<SkImage> SkImage_Raster::makeColorTypeAndColorSpace(SkRecorder*,
                                                          SkColorType targetColorType,
                                                          sk_sp<SkColorSpace> targetColorSpace,
                                                          RequiredProperties) const {
    SkPixmap source;
    if (!fBitmap.peekPixels(&source)) {
        return nullptr;
    }
    const SkImageInfo originalInfo = fBitmap.info();
    const SkImageInfo typedInfo = originalInfo.makeColorType(targetColorType);
    const SkImageInfo destinationInfo = typedInfo.makeColorSpace(std::move(targetColorSpace));
    SkBitmap destination;
    if (!destination.tryAllocPixels(destinationInfo)) {
        return nullptr;
    }
    if (!destination.writePixels(source)) {
        return nullptr;
    }
    destination.setImmutable();
    sk_sp<SkImage> image = SkImages::RasterFromBitmap(destination);
    return image;
}

sk_sp<SkImage> SkImage_Raster::onReinterpretColorSpace(sk_sp<SkColorSpace> newCS) const {
    // TODO: If our bitmap is immutable, then we could theoretically create another image sharing
    // our pixelRef. That doesn't work (without more invasive logic), because the image gets its
    // gen ID from the bitmap, which gets it from the pixelRef.
    SkPixmap pixmap = fBitmap.pixmap();
    pixmap.setColorSpace(std::move(newCS));
    sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(pixmap);
    return image;
}
