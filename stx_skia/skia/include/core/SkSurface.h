/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkSurface_DEFINED
#define SkSurface_DEFINED

#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSurfaceProps.h"
#include "include/core/SkTypes.h"

#include <cstddef>
#include <cstdint>
#include <memory>

class SkBitmap;
class SkCanvas;
class SkPaint;
class SkRecorder;
class SkSurface;
struct SkIRect;

namespace SkSurfaces {
// Positive dimensions are required. Null surfaces accept drawing without storing
// pixels; their snapshots are null and pixel reads fail.
SK_API sk_sp<SkSurface> Null(int width, int height);

// Allocate zeroed CPU pixels. A zero row stride selects the minimum stride.
// Invalid dimensions, formats, strides or failed pixel allocation return null.
// Snapshots may keep the pixel allocation alive after the surface is destroyed.
SK_API sk_sp<SkSurface> Raster(const SkImageInfo& imageInfo, size_t rowBytes,
                              const SkSurfaceProps* surfaceProps);
inline sk_sp<SkSurface> Raster(const SkImageInfo& imageInfo,
                              const SkSurfaceProps* props = nullptr) {
    sk_sp<SkSurface> surface = Raster(imageInfo, 0, props);
    return surface;
}

// Borrow mutable pixels without initialization. The caller supplies at least
// height * rowBytes bytes, with a valid stride, and keeps them alive until the
// surface is destroyed. Snapshots copy these externally owned pixels.
SK_API sk_sp<SkSurface> WrapPixels(const SkImageInfo& imageInfo, void* pixels,
                                  size_t rowBytes,
                                  const SkSurfaceProps* surfaceProps = nullptr);
inline sk_sp<SkSurface> WrapPixels(const SkPixmap& pixels,
                                 const SkSurfaceProps* props = nullptr) {
    sk_sp<SkSurface> surface = WrapPixels(pixels.info(), pixels.writable_addr(),
                                        pixels.rowBytes(), props);
    return surface;
}

using PixelsReleaseProc = void(void* pixels, void* context);

// The same buffer contract, with a release callback when the surface relinquishes
// its reference. Context must remain valid until that callback. Invalid factory
// arguments return null without taking ownership or invoking the callback.
SK_API sk_sp<SkSurface> WrapPixels(const SkImageInfo& imageInfo, void* pixels,
                                  size_t rowBytes, PixelsReleaseProc releaseProc,
                                  void* context,
                                  const SkSurfaceProps* surfaceProps = nullptr);
}  // namespace SkSurfaces

// Owns a CPU drawing canvas and manages its pixel backing. Use the factories;
// external subclassing cannot supply the required private implementation.
// Dimensions are positive. Mutations and reads require caller synchronization.
class SK_API SkSurface : public SkRefCnt {
public:
    int width() const { return fWidth; }
    int height() const { return fHeight; }
    virtual SkImageInfo imageInfo() const {
        const SkImageInfo info = SkImageInfo::MakeUnknown(fWidth, fHeight);
        return info;
    }

    // Lazily allocated content identity, invalidated by drawing or notification.
    uint32_t generationID();
    enum ContentChangeMode {
        kDiscard_ContentChangeMode,
        kRetain_ContentChangeMode,
    };
    // Notify before external writes. Discard allows replacement backing without
    // copying the old contents; retain preserves them when separating snapshots.
    // Returns false if snapshot separation fails; do not write in that case.
    // Obtain the writable pixel view after a successful notification.
    [[nodiscard]] bool notifyContentWillChange(ContentChangeMode mode);

    // Non-owning CPU recorder; null for a null surface.
    SkRecorder* baseRecorder() const;
    // Stable borrowed canvas. It must not be deleted or outlive this surface.
    SkCanvas* getCanvas();

    // Allocate independent compatible backing with the same surface properties.
    // Invalid dimensions/formats or failed pixel allocation return null.
    sk_sp<SkSurface> makeSurface(const SkImageInfo& imageInfo);
    sk_sp<SkSurface> makeSurface(int width, int height);

    // Owned immutable snapshot, unaffected by later drawing, which may outlive
    // this surface. Null surfaces and failed pixel allocation return null.
    sk_sp<SkImage> makeImageSnapshot();
    // Intersect with surface bounds; an empty intersection returns null.
    sk_sp<SkImage> makeImageSnapshot(const SkIRect& bounds);
    // Owned image for immediate use. Its contents are not promised to remain
    // unchanged after subsequent writes to the source surface.
    sk_sp<SkImage> makeTemporaryImage();

    // Synchronous drawing with sampling and optional paint alpha, color filter
    // and blend mode. Canvas and paint are borrowed for this call only.
    void draw(SkCanvas* canvas, SkScalar x, SkScalar y,
              const SkSamplingOptions& sampling, const SkPaint* paint);
    void draw(SkCanvas* canvas, SkScalar x, SkScalar y, const SkPaint* paint = nullptr) {
        const SkSamplingOptions sampling;
        draw(canvas, x, y, sampling, paint);
    }

    // A borrowed view of the pixels, or nullopt when there is none. Surface
    // destruction or mutation may invalidate this view, including copy-on-write.
    std::optional<SkPixmap> peekPixels();

    // Copy and convert the source/destination intersection without scaling.
    // The caller owns disjoint destination storage with a valid row stride.
    // Negative source offsets leave leading destination pixels untouched.
    // Pixels outside the intersection and row padding remain unchanged. Returns
    // false for unavailable pixels, unsupported conversion or empty intersection.
    bool readPixels(const SkPixmap& dst, int srcX, int srcY);
    bool readPixels(const SkImageInfo& dstInfo, void* dstPixels, size_t dstRowBytes,
                    int srcX, int srcY);
    bool readPixels(const SkBitmap& dst, int srcX, int srcY);

    using AsyncReadResult = SkImage::AsyncReadResult;
    using ReadPixelsContext = void*;
    using ReadPixelsCallback = void(ReadPixelsContext, std::unique_ptr<const AsyncReadResult>);
    using RescaleGamma = SkImage::RescaleGamma;
    using RescaleMode = SkImage::RescaleMode;

    // This CPU API completes synchronously despite its historical name. The
    // non-null callback runs once before return, receiving one owned pixel plane
    // or a null result on failure. Context is borrowed only during this call.
    // srcRect must fit inside the surface; info selects output size and format.
    // Result pixels remain valid for the result's lifetime. Keep the surface
    // alive through completion and do not mutate it reentrantly in the callback.
    void asyncRescaleAndReadPixels(const SkImageInfo& info, const SkIRect& srcRect,
                                  RescaleGamma rescaleGamma, RescaleMode rescaleMode,
                                  ReadPixelsCallback callback, ReadPixelsContext context);

    // Copy and convert the intersection from caller-owned, disjoint source
    // storage. Negative destination offsets clip the source. Surface clipping
    // and transforms do not affect this copy. Disjoint/empty sources do nothing.
    // Full coverage permits discard; partial coverage preserves other pixels.
    void writePixels(const SkPixmap& src, int dstX, int dstY);
    void writePixels(const SkBitmap& src, int dstX, int dstY);

    const SkSurfaceProps& props() const { return fProps; }

protected:
    SkSurface(int width, int height, const SkSurfaceProps* surfaceProps);
    SkSurface(const SkImageInfo& imageInfo, const SkSurfaceProps* surfaceProps);
    void dirtyGenerationID() { fGenerationID = 0; }

private:
    const SkSurfaceProps fProps;
    const int fWidth;
    const int fHeight;
    uint32_t fGenerationID = 0;
};

#endif
