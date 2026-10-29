/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/image/SkImage_Base.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkSize.h"
#include "include/core/SkTypes.h"
#include "include/private/SkDebug.h"
#include "src/core/SkBitmapCache.h"
#include "src/image/SkRescaleAndReadPixels.h"

#include <atomic>
#include <utility>

SkImage_Base::SkImage_Base(const SkImageInfo& info, uint32_t uniqueID)
        : SkImage(info, uniqueID) {}

SkImage_Base::~SkImage_Base() {
    if (fAddedToRasterCache.load()) {
        const uint32_t id = uniqueID();
        SkNotifyBitmapGenIDIsStale(id);
    }
}

void SkImage_Base::onAsyncRescaleAndReadPixels(const SkImageInfo& info,
                                               SkIRect origSrcRect,
                                               RescaleGamma rescaleGamma,
                                               RescaleMode rescaleMode,
                                               ReadPixelsCallback callback,
                                               ReadPixelsContext context) const {
    SkBitmap source;
    const std::optional<SkPixmap> sourceView = peekPixels();
    SkIRect sourceRect = SkIRect::MakeEmpty();
    if (sourceView) {
        if (!source.installPixels(*sourceView)) {
            callback(context, nullptr);
            return;
        }
        sourceRect = origSrcRect;
    } else {
        const SkImageInfo currentInfo = imageInfo();
        const SkISize sourceSize = origSrcRect.size();
        const SkImageInfo sourceInfo = currentInfo.makeDimensions(sourceSize);
        if (!source.tryAllocPixels(sourceInfo)) {
            callback(context, nullptr);
            return;
        }
        const SkPixmap pixels = source.pixmap();
        const bool copied = readPixels(pixels, origSrcRect.x(), origSrcRect.y());
        if (!copied) {
            callback(context, nullptr);
            return;
        }
        sourceRect = SkIRect::MakeSize(source.dimensions());
    }
    SkRescaleAndReadPixels(source, info, sourceRect, rescaleGamma, rescaleMode, callback, context);
}

bool SkImage_Base::onAsLegacyBitmap( SkBitmap* bitmap) const {
    // As the base-class, all we can do is make a copy (regardless of mode).
    // Subclasses that want to be more optimal should override.
    const SkImageInfo colorInfo = fInfo.makeColorType(kN32_SkColorType);
    const SkImageInfo info = colorInfo.makeColorSpace(nullptr);
    if (!(*bitmap).tryAllocPixels(info)) {
        return false;
    }

    if (!readPixels(
                (*bitmap).info(), (*bitmap).getPixels(), (*bitmap).rowBytes(), 0, 0)) {
        (*bitmap).reset();
        return false;
    }

    (*bitmap).setImmutable();
    return true;
}

sk_sp<SkImage> SkImage_Base::makeSubset(SkRecorder* recorder,
                                        const SkIRect& subset,
                                        RequiredProperties requiredProps) const {
    if (subset.isEmpty()) {
        return nullptr;
    }

    const SkIRect bounds = SkIRect::MakeWH(width(), height());
    if (!bounds.contains(subset)) {
        return nullptr;
    }

    sk_sp<SkImage> image = onMakeSubset(recorder, subset, requiredProps);
    return image;
}

sk_sp<SkImage> SkImage_Base::makeColorSpace(SkRecorder* recorder,
                                            sk_sp<SkColorSpace> target,
                                            RequiredProperties props) const {
    const SkColorType targetType = colorType();
    sk_sp<SkImage> image = makeColorTypeAndColorSpace(recorder, targetType, std::move(target), props);
    return image;
}
