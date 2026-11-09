/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMipmap_DEFINED
#define SkMipmap_DEFINED

#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSize.h"
#include "src/core/SkCachedData.h"

#include <cstddef>
#include <memory>

class SkBitmap;
class SkColorSpace;
class SkDiscardableMemory;

// A synchronous allocator returns an owned, locked, suitably aligned block of
// at least the requested size, or null. The callback must not throw. SkCachedData
// releases/unlocks a successful allocation after the final client/cache release.
using SkDiscardableFactoryProc = SkDiscardableMemory* (*)(size_t bytes);

struct SkMipmapDownSampler {
    virtual ~SkMipmapDownSampler() {}
    // Source and destination are disjoint; destination is the next halved level.
    // Caller supplies complete, correctly aligned buffers with the same format.
    virtual void buildLevel(const SkPixmap& destination, const SkPixmap& source) = 0;
};

// Owns a reduced image chain. Index zero is the first reduced level, not the
// source. A client reference locks storage; a cache reference alone does not.
class SkMipmap : public SkCachedData {
public:
    ~SkMipmap() override;

    // Returns one owned client reference, or null for invalid/unsupported input
    // or unavailable pixel allocation. Source is borrowed for this call only.
    // With computeContents=true it must supply the full declared, aligned extent.
    // With false only metadata is consumed; every returned level must be filled
    // by the caller before sampling/publication. Pixel storage is uninitialized.
    // Metadata and total storage sizes are validated before the allocator runs.
    static SkMipmap* Build(const SkPixmap& source, SkDiscardableFactoryProc,
                           bool computeContents = true);
    static SkMipmap* Build(const SkBitmap& source, SkDiscardableFactoryProc);

    // Halve each axis with max(1, floor(axis/2)) until both reach one.
    // Invalid dimensions and a 1x1 source have no reduced levels.
    static int ComputeLevelCount(int baseWidth, int baseHeight);
    static int ComputeLevelCount(SkISize dimensions) {
        const int count = ComputeLevelCount(dimensions.width(), dimensions.height());
        return count;
    }
    static SkISize ComputeLevelSize(int baseWidth, int baseHeight, int level);
    static SkISize ComputeLevelSize(SkISize dimensions, int level) {
        const SkISize size = ComputeLevelSize(dimensions.width(), dimensions.height(), level);
        return size;
    }

    // Fractional level with the original half-level sharpening bias; -1 means
    // no reduction. Unlike stored indices, zero denotes the source image.
    static float ComputeLevel(SkSize scale);

    // Descriptors precede pixel storage in the same discardable allocation.
    // Eight-byte alignment also aligns F16 pixel starts on 32-bit platforms.
    struct alignas(8) Level {
        SkPixmap fPixmap{};
        SkSize fScale{}; // level dimensions divided by source dimensions
    };

    // Output pixel views borrow this object's locked storage; their copied color
    // space is retained separately. Keep a client reference through every use.
    // Null output queries availability only. Failure leaves output unchanged.
    // extractLevel rounds and clamps to the last level; getLevel rejects indices
    // outside the chain. Neither returns the original source image.
    bool extractLevel(SkSize scale, Level* output) const;
    bool getLevel(int index, Level* output) const;
    int countLevels() const;
    bool validForRootLevel(const SkImageInfo&) const;

    static std::unique_ptr<SkMipmapDownSampler> MakeDownSampler(const SkPixmap&);

protected:
    void onDataChange(void*, void* newData) const override {
        fLevels = static_cast<Level*>(newData);
    }

private:
    // Descriptors inside discardable storage hold null color spaces: discarding
    // the block must not strand owned references. Returned copies receive fCS.
    sk_sp<SkColorSpace> fCS{};
    mutable Level* fLevels = nullptr;
    int fCount = 0;

    SkMipmap(void* allocation, size_t size);
    SkMipmap(size_t size, SkDiscardableMemory*);
    static size_t AllocLevelsSize(int levelCount, size_t pixelBytes);
};

#endif
