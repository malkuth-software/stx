/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/core/SkMipmap.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorType.h"
#include "include/core/SkTypes.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkMathPriv.h"
#include "src/core/SkSafeMath.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>

SkMipmap::SkMipmap(void* allocation, size_t size) : SkCachedData(allocation, size) {}
SkMipmap::SkMipmap(size_t size, SkDiscardableMemory* memory) : SkCachedData(size, memory) {}
SkMipmap::~SkMipmap() {}

size_t SkMipmap::AllocLevelsSize(int levelCount, size_t pixelBytes) {
    if (levelCount <= 0) {
        return 0;
    }
    SkSafeMath safe;
    // Retain the original extra descriptor-sized tail allowance. Pixel storage
    // begins immediately after the live descriptors, not after this allowance.
    const size_t slots = safe.add(static_cast<size_t>(levelCount), 1);
    const size_t descriptorBytes = safe.mul(slots, sizeof(Level));
    const size_t allocationBytes = safe.add(descriptorBytes, pixelBytes);
    if (!safe.ok() || allocationBytes > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        return 0;
    }
    return allocationBytes;
}

SkMipmap* SkMipmap::Build(const SkPixmap& source, SkDiscardableFactoryProc factory,
                        bool computeContents) {
    const SkImageInfo& sourceInfo = source.info();
    const SkColorType colorType = source.colorType();
    const SkAlphaType alphaType = source.alphaType();
    if (!SkColorTypeIsValid(static_cast<unsigned>(colorType)) ||
        !SkAlphaTypeIsValid(static_cast<unsigned>(alphaType)) ||
        !SkImageInfoIsValid(sourceInfo)) {
        return nullptr;
    }
    const int levelCount = ComputeLevelCount(source.width(), source.height());
    if (levelCount == 0) {
        return nullptr;
    }
    if (computeContents) {
        if (source.addr() == nullptr || !sourceInfo.validRowBytes(source.rowBytes())) {
            return nullptr;
        }
        const size_t sourceBytes = source.computeByteSize();
        if (SkImageInfo::ByteSizeOverflowed(sourceBytes)) {
            return nullptr;
        }
    }

    // Sum the complete layout without touching source pixels or invoking the
    // allocation callback. Each axis halves until both reach one.
    SkSafeMath safe;
    const size_t bytesPerPixel = static_cast<size_t>(SkColorTypeBytesPerPixel(colorType));
    size_t pixelBytes = 0;
    int levelWidth = source.width();
    int levelHeight = source.height();
    for (int index = 0; index < levelCount; ++index) {
        levelWidth = std::max(1, levelWidth >> 1);
        levelHeight = std::max(1, levelHeight >> 1);
        const size_t rowBytes = safe.mul(static_cast<size_t>(levelWidth), bytesPerPixel);
        const size_t levelBytes = safe.mul(rowBytes, static_cast<size_t>(levelHeight));
        pixelBytes = safe.add(pixelBytes, levelBytes);
    }
    if (!safe.ok()) {
        return nullptr;
    }
    const size_t allocationBytes = AllocLevelsSize(levelCount, pixelBytes);
    if (allocationBytes == 0) {
        return nullptr;
    }

    std::unique_ptr<SkMipmapDownSampler> downsampler{};
    if (computeContents) {
        downsampler = MakeDownSampler(source);
        if (!downsampler) {
            return nullptr;
        }
    }

    sk_sp<SkMipmap> owner{};
    if (factory != nullptr) {
        SkDiscardableMemory* memory = factory(allocationBytes);
        if (memory == nullptr) {
            return nullptr;
        }
        owner.reset(new SkMipmap(allocationBytes, memory));
    } else {
        void* allocation = sk_malloc_canfail(allocationBytes);
        if (allocation == nullptr) {
            return nullptr;
        }
        owner.reset(new SkMipmap(allocation, allocationBytes));
    }

    SkMipmap& mipmap = *owner;
    mipmap.fCS = sk_ref_sp(sourceInfo.colorSpace());
    mipmap.fCount = levelCount;
    mipmap.fLevels = static_cast<Level*>(mipmap.writable_data());
    SkASSERT(mipmap.fLevels != nullptr);
    Level* levels = mipmap.fLevels;
    uint8_t* allocation = static_cast<uint8_t*>(mipmap.writable_data());
    const size_t descriptorBytes = static_cast<size_t>(levelCount) * sizeof(Level);
    uint8_t* pixelBase = allocation + descriptorBytes;
    SkASSERT(SkIsAlign8(reinterpret_cast<uintptr_t>(pixelBase)));

    size_t pixelOffset = 0;
    levelWidth = source.width();
    levelHeight = source.height();
    SkPixmap previous = source;
    for (int index = 0; index < levelCount; ++index) {
        levelWidth = std::max(1, levelWidth >> 1);
        levelHeight = std::max(1, levelHeight >> 1);
        // The first pass proves these products fit the allocation's signed
        // 32-bit extent limit. No allocation or format dispatch occurs here.
        const size_t rowBytes = static_cast<size_t>(levelWidth) * bytesPerPixel;
        const size_t levelBytes = static_cast<size_t>(levelHeight) * rowBytes;
        const SkImageInfo levelInfo = SkImageInfo::Make(levelWidth, levelHeight,
                                                       colorType, alphaType);
        const SkPixmap pixels(levelInfo, pixelBase + pixelOffset, rowBytes);
        const SkScalar scaleX = SkIntToScalar(levelWidth) / source.width();
        const SkScalar scaleY = SkIntToScalar(levelHeight) / source.height();
        const SkSize scale = SkSize::Make(scaleX, scaleY);
        // Discardable storage may vanish without element destructors. These
        // descriptors therefore own no color-space reference or pixel storage;
        // fCS owns the color space and getLevel adds it to the returned copy.
        new (&levels[index]) Level{.fPixmap = pixels, .fScale = scale};
        const SkPixmap& destination = levels[index].fPixmap;
        if (downsampler) {
            (*downsampler).buildLevel(destination, previous);
        }
        previous = destination;
        pixelOffset += levelBytes;
    }
    SkASSERT(pixelOffset == pixelBytes);
    SkMipmap* result = owner.release();
    return result;
}

int SkMipmap::ComputeLevelCount(int baseWidth, int baseHeight) {
    if (baseWidth < 1 || baseHeight < 1) {
        return 0;
    }
    const int largestAxis = std::max(baseWidth, baseHeight);
    if (largestAxis < 2) {
        return 0;
    }
    // floor(log2(largestAxis)); the base image is not stored in this object.
    const int leadingZeros = SkCLZ(static_cast<uint32_t>(largestAxis));
    const int count = 31 - leadingZeros;
    return count;
}

SkISize SkMipmap::ComputeLevelSize(int baseWidth, int baseHeight, int level) {
    const int count = ComputeLevelCount(baseWidth, baseHeight);
    if (level < 0 || level >= count) {
        const SkISize empty = SkISize::Make(0, 0);
        return empty;
    }
    const int width = std::max(1, baseWidth >> (level + 1));
    const int height = std::max(1, baseHeight >> (level + 1));
    const SkISize dimensions = SkISize::Make(width, height);
    return dimensions;
}

float SkMipmap::ComputeLevel(SkSize scaleSize) {
    SkASSERT(scaleSize.width() >= 0 && scaleSize.height() >= 0);
#ifndef SK_SUPPORT_LEGACY_ANISOTROPIC_MIPMAP_SCALE
    const float scale = std::min(scaleSize.width(), scaleSize.height());
#else
    const float scale = std::sqrt(scaleSize.width() * scaleSize.height());
#endif
    if (scale >= 1.0f || scale <= 0.0f || !SkIsFinite(scale)) {
        return -1.0f;
    }
    // Preserve the original half-level sharpening bias and operation order.
    const float level = std::max(-SkScalarLog2(scale) - 0.5f, 0.0f);
    if (!SkIsFinite(level)) {
        return -1.0f;
    }
    return level;
}

bool SkMipmap::extractLevel(SkSize scale, Level* output) const {
    if (fLevels == nullptr) {
        return false;
    }
    const float fractionalLevel = ComputeLevel(scale);
    int level = sk_float_round2int(fractionalLevel);
    if (level <= 0) {
        return false;
    }
    level = std::min(level, fCount);
    const bool found = getLevel(level - 1, output);
    return found;
}

bool SkMipmap::validForRootLevel(const SkImageInfo& root) const {
    if (fLevels == nullptr) {
        return false;
    }
    const SkISize dimensions = root.dimensions();
    if (dimensions.width() <= 1 && dimensions.height() <= 1) {
        return false;
    }
    const int firstWidth = std::max(1, dimensions.width() >> 1);
    const int firstHeight = std::max(1, dimensions.height() >> 1);
    if (fLevels[0].fPixmap.width() != firstWidth || fLevels[0].fPixmap.height() != firstHeight) {
        return false;
    }
    for (int index = 0; index < fCount; ++index) {
        const SkPixmap& pixels = fLevels[index].fPixmap;
        if (pixels.colorType() != root.colorType() || pixels.alphaType() != root.alphaType()) {
            return false;
        }
    }
    return true;
}

SkMipmap* SkMipmap::Build(const SkBitmap& source, SkDiscardableFactoryProc factory) {
    SkPixmap pixels{};
    if (!source.peekPixels(&pixels)) {
        return nullptr;
    }
    SkMipmap* mipmap = Build(pixels, factory);
    return mipmap;
}

int SkMipmap::countLevels() const {
    return fCount;
}

bool SkMipmap::getLevel(int index, Level* output) const {
    if (fLevels == nullptr || index < 0 || index >= fCount) {
        return false;
    }
    if (output != nullptr) {
        *output = fLevels[index];
        (*output).fPixmap.setColorSpace(fCS);
    }
    return true;
}
