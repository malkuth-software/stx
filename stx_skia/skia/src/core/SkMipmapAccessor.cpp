/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkMipmapAccessor.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSize.h"
#include "include/private/SkFloatingPoint.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkBitmapCache.h"
#include "src/core/SkMipmap.h"
#include "src/image/SkImage_Base.h"

namespace {
// Retain image-provided levels first, then cached levels, then construct/cache
// them from readable pixels. Each returned handle owns its cache reference.
sk_sp<const SkMipmap> try_load_mips(const SkImage_Base& image) {
    sk_sp<const SkMipmap> mips = image.refMips();
    if (!mips) {
        const SkBitmapCacheDesc description = SkBitmapCacheDesc::Make(&image);
        const SkMipmap* cached = SkMipmapCache::FindAndRef(description);
        mips.reset(cached);
    }
    if (!mips) {
        const SkMipmap* generated = SkMipmapCache::AddAndRef(&image);
        mips.reset(generated);
    }
    return mips;
}

SkMatrix level_scale(const SkPixmap& pixels, const SkImage_Base& image) {
    const SkScalar scaleX = SkIntToScalar(pixels.width()) / image.width();
    const SkScalar scaleY = SkIntToScalar(pixels.height()) / image.height();
    const SkMatrix scale = SkMatrix::Scale(scaleX, scaleY);
    return scale;
}
}

void SkMipmapAccessor::loadUpperFromBase(const SkImage_Base& image) {
    if (fBaseStorage.getPixels() != nullptr) {
        return;
    }
    (void)image.getROPixels(&fBaseStorage);
    fUpper.reset(fBaseStorage.info(), fBaseStorage.getPixels(), fBaseStorage.rowBytes());
}

SkMipmapAccessor::SkMipmapAccessor(const SkImage_Base* imagePointer, const SkMatrix& inverse,
                                 SkMipmapMode requestedMode) {
    const SkImage_Base& image = *imagePointer;
    SkMipmapMode resolvedMode = requestedMode;
    float level = 0.0f;
    if (requestedMode != SkMipmapMode::kNone) {
        SkSize scale{};
        if (!inverse.decomposeScale(&scale, nullptr)) {
            resolvedMode = SkMipmapMode::kNone;
        } else {
            const SkSize forwardScale{1.0f / scale.width(), 1.0f / scale.height()};
            level = SkMipmap::ComputeLevel(forwardScale);
            if (level <= 0.0f) {
                resolvedMode = SkMipmapMode::kNone;
                level = 0.0f;
            }
        }
    }

    // Level zero is the source image. Nearest rounds; linear brackets the
    // fractional level with floor and floor+1. SkMipmap indices omit the source.
    int levelNumber = 0;
    if (resolvedMode == SkMipmapMode::kNearest) {
        levelNumber = sk_float_round2int(level);
    } else {
        levelNumber = sk_float_floor2int(level);
    }
    const float lowerWeight = level - levelNumber;
    SkASSERT(levelNumber >= 0);

    if (levelNumber == 0) {
        (*this).loadUpperFromBase(image);
    }
    const bool needsLower = resolvedMode == SkMipmapMode::kLinear && lowerWeight > 0.0f;
    if (levelNumber > 0 || needsLower) {
        fCurrMip = try_load_mips(image);
        if (!fCurrMip) {
            (*this).loadUpperFromBase(image);
            resolvedMode = SkMipmapMode::kNone;
        } else {
            const SkMipmap& mips = *fCurrMip;
            SkMipmap::Level selected{};
            SkASSERT(resolvedMode != SkMipmapMode::kNone);
            if (levelNumber > 0) {
                if (mips.getLevel(levelNumber - 1, &selected)) {
                    fUpper = selected.fPixmap;
                } else {
                    (*this).loadUpperFromBase(image);
                    resolvedMode = SkMipmapMode::kNone;
                }
            }

            if (resolvedMode == SkMipmapMode::kLinear) {
                if (mips.getLevel(levelNumber, &selected)) {
                    fLower = selected.fPixmap;
                    fLowerWeight = lowerWeight;
                    fLowerInv = level_scale(fLower, image);
                }
                // A missing lower level leaves weight zero: upper-only sampling.
            }
        }
    }
    fUpperInv = level_scale(fUpper, image);
}

SkMipmapAccessor* SkMipmapAccessor::Make(SkArenaAlloc* allocator, const SkImage* image,
                                       const SkMatrix& inverse, SkMipmapMode mode) {
    const SkImage_Base* base = as_IB(image);
    SkMipmapAccessor* accessor = (*allocator).make<SkMipmapAccessor>(base, inverse, mode);
    if ((*accessor).fUpper.addr() == nullptr) {
        return nullptr;
    }
    return accessor;
}
