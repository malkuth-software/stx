/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/image/SkImage_Lazy.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCPURecorder.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImageGenerator.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRecorder.h"
#include "include/core/SkSurface.h"
#include "src/core/SkBitmapCache.h"
#include "src/core/SkNextID.h"

#include <utility>

sk_sp<SharedGenerator> SharedGenerator::Make(std::unique_ptr<SkImageGenerator> generator) {
    if (!generator) {
        return nullptr;
    }
    sk_sp<SharedGenerator> shared(new SharedGenerator(std::move(generator)));
    return shared;
}

SharedGenerator::SharedGenerator(std::unique_ptr<SkImageGenerator> generator)
        : fGenerator(std::move(generator)) {
    SkASSERT(fGenerator);
}

SkImage_Lazy::Validator::Validator(sk_sp<SharedGenerator> shared,
                                   const SkColorType* colorType,
                                   sk_sp<SkColorSpace> colorSpace)
        : fSharedGenerator(std::move(shared)) {
    if (!fSharedGenerator) {
        return;
    }
    const SharedGenerator& owner = *fSharedGenerator;
    const SkImageGenerator& generator = *owner.fGenerator;
    // These two fields are immutable throughout the generator lifetime.
    fInfo = generator.getInfo();
    if (fInfo.isEmpty()) {
        fSharedGenerator.reset();
        return;
    }
    fUniqueID = generator.uniqueID();
    if (colorType && *colorType == fInfo.colorType()) {
        colorType = nullptr;
    }
    if (!colorType && !colorSpace) {
        return;
    }
    if (colorType) {
        fInfo = fInfo.makeColorType(*colorType);
    }
    if (colorSpace) {
        fInfo = fInfo.makeColorSpace(std::move(colorSpace));
    }
    fUniqueID = SkNextID::ImageID();
}

SkImage_Lazy::SkImage_Lazy(Validator* validator)
        : SkImage_Base((*validator).fInfo, (*validator).fUniqueID),
          fSharedGenerator(std::move((*validator).fSharedGenerator)) {
    SkASSERT(fSharedGenerator);
}

static void check_output_bitmap(const SkBitmap& bitmap) {
    SkASSERT(bitmap.isImmutable());
    SkASSERT(bitmap.getPixels());
    (void)bitmap;
}

bool SkImage_Lazy::getROPixels( SkBitmap* bitmap, CachingHint hint) const {
    const SkBitmapCacheDesc description = SkBitmapCacheDesc::Make(this);
    if (SkBitmapCache::Find(description, bitmap)) {
        check_output_bitmap(*bitmap);
        return true;
    }
    SharedGenerator& owner = *fSharedGenerator;
    if (hint == kAllow_CachingHint) {
        SkPixmap destination;
        SkBitmapCache::RecPtr record = SkBitmapCache::Alloc(description, imageInfo(), &destination);
        if (!record) {
            return false;
        }
        bool generated = false;
        {
            SkAutoMutexExclusive generatorLock(owner.fMutex);
            SkImageGenerator& generator = *owner.fGenerator;
            generated = generator.getPixels(destination);
        }
        if (!generated) {
            return false;
        }
        // Publish only complete pixels, after releasing the generator lock.
        SkBitmapCache::Add(std::move(record), bitmap);
        notifyAddedToRasterCache();
    } else {
        if (!(*bitmap).tryAllocPixels(imageInfo())) {
            return false;
        }
        const SkPixmap destination = (*bitmap).pixmap();
        bool generated = false;
        {
            SkAutoMutexExclusive generatorLock(owner.fMutex);
            SkImageGenerator& generator = *owner.fGenerator;
            generated = generator.getPixels(destination);
        }
        if (!generated) {
            return false;
        }
        (*bitmap).setImmutable();
    }
    check_output_bitmap(*bitmap);
    return true;
}

bool SkImage_Lazy::onReadPixels( const SkImageInfo& destinationInfo,
                                void* destinationPixels, size_t destinationRowBytes,
                                int sourceX, int sourceY, CachingHint hint) const {
    SkBitmap bitmap;
    if (!getROPixels(&bitmap, hint)) {
        return false;
    }
    const bool copied = bitmap.readPixels(destinationInfo, destinationPixels,
                                          destinationRowBytes, sourceX, sourceY);
    return copied;
}

sk_sp<const SkData> SkImage_Lazy::onRefEncoded() const {
    SharedGenerator& owner = *fSharedGenerator;
    SkImageGenerator& generator = *owner.fGenerator;
    // Converted images cannot claim the original encoded representation.
    if (generator.uniqueID() != uniqueID()) {
        return nullptr;
    }
    SkAutoMutexExclusive generatorLock(owner.fMutex);
    sk_sp<const SkData> data = generator.refEncodedData();
    return data;
}

bool SkImage_Lazy::isValid(SkRecorder* recorder) const {
    SharedGenerator& owner = *fSharedGenerator;
    SkAutoMutexExclusive generatorLock(owner.fMutex);
    const SkImageGenerator& generator = *owner.fGenerator;
    const bool valid = generator.isValid(recorder);
    return valid;
}

sk_sp<SkImage> SkImage_Lazy::onMakeSubset(SkRecorder*, const SkIRect& bounds,
                                         RequiredProperties properties) const {
    sk_sp<SkImage> raster = makeRasterImage(nullptr);
    if (!raster) {
        return nullptr;
    }
    sk_sp<SkImage> subset = (*raster).makeSubset(nullptr, bounds, properties);
    return subset;
}

sk_sp<SkSurface> SkImage_Lazy::onMakeSurface(SkRecorder* recorder,
                                            const SkImageInfo& info) const {
    if (!recorder) {
        recorder = skcpu::Recorder::TODO();
    }
    skcpu::Recorder* cpu = (*recorder).cpuRecorder();
    if (!cpu) {
        return nullptr;
    }
    sk_sp<SkSurface> surface = (*cpu).makeBitmapSurface(info, 0, nullptr);
    return surface;
}

sk_sp<SkImage> SkImage_Lazy::makeColorTypeAndColorSpace(SkRecorder*, SkColorType colorType,
                                                      sk_sp<SkColorSpace> colorSpace,
                                                      RequiredProperties) const {
    SkAutoMutexExclusive resultLock(fOnMakeColorTypeAndSpaceMutex);
    if (fOnMakeColorTypeAndSpaceResult) {
        const SkImage& cached = *fOnMakeColorTypeAndSpaceResult;
        const bool matches = colorType == cached.colorType() &&
                             SkColorSpace::Equals(colorSpace.get(), cached.colorSpace());
        if (matches) {
            return fOnMakeColorTypeAndSpaceResult;
        }
    }
    Validator validator(fSharedGenerator, &colorType, std::move(colorSpace));
    if (!validator) {
        return nullptr;
    }
    sk_sp<SkImage> result(new SkImage_Lazy(&validator));
    fOnMakeColorTypeAndSpaceResult = result;
    return result;
}

sk_sp<SkImage> SkImage_Lazy::onReinterpretColorSpace(sk_sp<SkColorSpace> colorSpace) const {
    // The generator cannot be cloned with new metadata. Generate with its
    // original color space into a bitmap labelled with the requested space.
    const SkImageInfo originalInfo = imageInfo();
    const SkImageInfo destinationInfo = originalInfo.makeColorSpace(std::move(colorSpace));
    SkBitmap bitmap;
    if (!bitmap.tryAllocPixels(destinationInfo)) {
        return nullptr;
    }
    SkPixmap destination = bitmap.pixmap();
    destination.setColorSpace(refColorSpace());
    bool generated = false;
    {
        SharedGenerator& owner = *fSharedGenerator;
        SkAutoMutexExclusive generatorLock(owner.fMutex);
        SkImageGenerator& generator = *owner.fGenerator;
        generated = generator.getPixels(destination);
    }
    if (!generated) {
        return nullptr;
    }
    bitmap.setImmutable();
    sk_sp<SkImage> image = SkImages::RasterFromBitmap(bitmap);
    return image;
}

namespace SkImages {
sk_sp<SkImage> DeferredFromGenerator(std::unique_ptr<SkImageGenerator> generator) {
    sk_sp<SharedGenerator> shared = SharedGenerator::Make(std::move(generator));
    SkImage_Lazy::Validator validator(std::move(shared), nullptr, nullptr);
    if (!validator) {
        return nullptr;
    }
    sk_sp<SkImage> image = sk_make_sp<SkImage_Lazy>(&validator);
    return image;
}
}  // namespace SkImages
