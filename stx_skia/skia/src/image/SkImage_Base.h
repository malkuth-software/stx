/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkImage_Base_DEFINED
#define SkImage_Base_DEFINED

#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"
#include "src/core/SkMipmap.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

class SkBitmap;
class SkColorSpace;
class SkPixmap;
class SkRecorder;
class SkSurface;
struct SkIRect;
struct SkISize;
struct SkImageInfo;

enum {
    kNeedNewImageUniqueID = 0
};

class SkImage_Base : public SkImage {
public:
    ~SkImage_Base() override;

    // From SkImage.h
    sk_sp<SkImage> makeColorSpace(SkRecorder*,
                                  sk_sp<SkColorSpace>,
                                  RequiredProperties) const override;

    sk_sp<SkImage> makeSubset(SkRecorder*, const SkIRect&, RequiredProperties) const override;

    // Methods that we want to use elsewhere in Skia, but not be a part of the public API.
    virtual bool onPeekPixels(SkPixmap*) const { return false; }

    virtual const SkBitmap* onPeekBitmap() const { return nullptr; }

    virtual bool onReadPixels(
                              const SkImageInfo& dstInfo,
                              void* dstPixels,
                              size_t dstRowBytes,
                              int srcX,
                              int srcY,
                              CachingHint) const = 0;

    virtual bool onHasMipmaps() const = 0;

    virtual SkMipmap* onPeekMips() const { return nullptr; }

    sk_sp<SkMipmap> refMips() const {
        SkMipmap* borrowed = onPeekMips();
        sk_sp<SkMipmap> mips = sk_ref_sp(borrowed);
        return mips;
    }

    /**
     * Default implementation does a rescale/read and then calls the callback.
     */
    virtual void onAsyncRescaleAndReadPixels(const SkImageInfo&,
                                             SkIRect srcRect,
                                             RescaleGamma,
                                             RescaleMode,
                                             ReadPixelsCallback,
                                             ReadPixelsContext) const;
    // return a read-only copy of the pixels. We promise to not modify them,
    // but only inspect them (or encode them).
    virtual bool getROPixels( SkBitmap*,
                             CachingHint = kAllow_CachingHint) const = 0;

    virtual sk_sp<SkImage> onMakeSubset(SkRecorder*, const SkIRect&, RequiredProperties) const = 0;

    virtual sk_sp<const SkData> onRefEncoded() const { return nullptr; }

    virtual bool onAsLegacyBitmap( SkBitmap*) const;

    // Create the CPU surface used by makeScaled.
    virtual sk_sp<SkSurface> onMakeSurface(SkRecorder*, const SkImageInfo&) const = 0;

    enum class Type { kRaster, kLazy };
    virtual Type type() const = 0;
    bool isLazyGenerated() const override {
        const bool lazy = type() == Type::kLazy;
        return lazy;
    }
    bool isRasterBacked() const {
        const bool raster = type() == Type::kRaster;
        return raster;
    }

    // Call when this image is part of the key to a resourcecache entry. This allows the cache
    // to know automatically those entries can be purged when this SkImage deleted.
    virtual void notifyAddedToRasterCache() const {
        fAddedToRasterCache.store(true);
    }

    virtual sk_sp<SkImage> onReinterpretColorSpace(sk_sp<SkColorSpace>) const = 0;

    // on failure, returns nullptr
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    virtual sk_sp<SkImage> onMakeWithMipmaps(sk_sp<SkMipmap>) const {
        return nullptr;
    }

protected:
    SkImage_Base(const SkImageInfo& info, uint32_t uniqueID);
private:
    // Set true by caches when they cache content that's derived from the current pixels.

    mutable std::atomic<bool> fAddedToRasterCache{false};
};

static inline SkImage_Base* as_IB(SkImage* image) {
    SkImage_Base* base = static_cast<SkImage_Base*>(image);
    return base;
}

static inline SkImage_Base* as_IB(const sk_sp<SkImage>& image) {
    SkImage_Base* base = static_cast<SkImage_Base*>(image.get());
    return base;
}

static inline const SkImage_Base* as_IB(const SkImage* image) {
    const SkImage_Base* base = static_cast<const SkImage_Base*>(image);
    return base;
}

static inline const SkImage_Base* as_IB(const sk_sp<const SkImage>& image) {
    const SkImage_Base* base = static_cast<const SkImage_Base*>(image.get());
    return base;
}

// A returned borrow cannot extend the lifetime of a temporary owning handle.
static SkImage_Base* as_IB(sk_sp<SkImage>&&) = delete;
static SkImage_Base* as_IB(const sk_sp<SkImage>&&) = delete;
static const SkImage_Base* as_IB(sk_sp<const SkImage>&&) = delete;
static const SkImage_Base* as_IB(const sk_sp<const SkImage>&&) = delete;

#endif
