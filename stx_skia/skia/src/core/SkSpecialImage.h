/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file
 */

#ifndef SkSpecialImage_DEFINED
#define SkSpecialImage_DEFINED

#include "include/core/SkImageInfo.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSize.h"
#include "include/core/SkSurfaceProps.h"

#include <cstddef>
#include <cstdint>

class GrRecordingContext;
class SkBitmap;
class SkCanvas;
class SkColorSpace;
class SkImage;
class SkMatrix;
class SkPaint;
class SkShader;
enum SkAlphaType : int;
enum SkColorType : int;
enum class SkTileMode;

enum {
    kNeedNewImageUniqueID_SpecialImage = 0
};

// An internal image view retaining a rectangular part of a shared raster backing store.
// Strict sampling is confined to that view. Non-strict callers must establish that
// adjacent pixels are valid and may contribute to filtering.
class SkSpecialImage : public SkRefCnt {
public:
    using ReleaseContext = void*;
    using RasterReleaseProc = void (*)(void* pixels, ReleaseContext);

    const SkSurfaceProps& props() const & { return fProps; }
    const SkSurfaceProps& props() const && = delete;

    int width() const {
        const int value = fSubset.width();
        return value;
    }
    int height() const {
        const int value = fSubset.height();
        return value;
    }
    SkISize dimensions() const {
        const SkISize size{width(), height()};
        return size;
    }
    const SkIRect& subset() const & { return fSubset; }
    const SkIRect& subset() const && = delete;

    uint32_t uniqueID() const { return fUniqueID; }

    virtual SkISize backingStoreDimensions() const = 0;

    virtual size_t getSize() const = 0;

    bool isExactFit() const {
        const SkISize dimensions = backingStoreDimensions();
        const SkIRect bounds = SkIRect::MakeSize(dimensions);
        const bool exact = fSubset == bounds;
        return exact;
    }

    const SkColorInfo& colorInfo() const & { return fColorInfo; }
    const SkColorInfo& colorInfo() const && = delete;
    SkAlphaType alphaType() const {
        const SkAlphaType value = fColorInfo.alphaType();
        return value;
    }
    SkColorType colorType() const {
        const SkColorType value = fColorInfo.colorType();
        return value;
    }
    SkColorSpace* getColorSpace() const & {
        SkColorSpace* space = fColorInfo.colorSpace();
        return space;
    }
    SkColorSpace* getColorSpace() const && = delete;

    /**
     *  Draw this SpecialImage into the canvas, automatically taking into account the image's subset
     */
    void draw(SkCanvas* canvas,
              SkScalar x, SkScalar y,
              const SkSamplingOptions& sampling,
              const SkPaint* paint,
              bool strict = true) const;
    void draw(SkCanvas* canvas, SkScalar x, SkScalar y) const {
        const SkSamplingOptions sampling{};
        draw(canvas, x, y, sampling, nullptr);
    }

    /**
     * Extract a subset of this special image and return it as a special image.
     * It may or may not point to the same backing memory. The input 'subset' is relative to the
     * special image's content rect.
     */
    sk_sp<SkSpecialImage> makeSubset(const SkIRect& subset) const {
        const SkIPoint origin = fSubset.topLeft();
        const SkIRect absolute = subset.makeOffset(origin);
        sk_sp<SkSpecialImage> image = onMakeBackingStoreSubset(absolute);
        return image;
    }

    /**
     * Return a special image with a 1px larger subset in the backing store compared to this image.
     * This should only be used when it's externally known that those outer pixels are valid.
     */
    sk_sp<SkSpecialImage> makePixelOutset() const {
        const SkIRect expanded = fSubset.makeOutset(1, 1);
        sk_sp<SkSpecialImage> image = onMakeBackingStoreSubset(expanded);
        return image;
    }

    /**
     * Return the full backing image, without applying the content subset. Immutable
     * raster pixels can be shared; mutable bitmap pixels are copied by SkBitmap::asImage.
     */
    virtual sk_sp<SkImage> asImage() const = 0;

    /**
     * Create an SkShader that samples the contents of this special image, applying tile mode for
     * any sample that falls outside its internal subset.
     *
     * Strict sampling uses an extracted pixel view. Non-strict sampling keeps the
     * full backing store; the caller must allow filtering through adjacent pixels.
     */
    virtual sk_sp<SkShader> asShader(SkTileMode,
                                     const SkSamplingOptions&,
                                     const SkMatrix& lm,
                                     bool strict=true) const = 0;

    /**
     *  If the SpecialImage is backed by a gpu texture, return true.
     */
    virtual bool isGaneshBacked() const { return false; }
    virtual bool isGraphiteBacked() const { return false; }

    /**
     * Return the GrRecordingContext if the SkSpecialImage is GrTexture-backed
     */
    virtual GrRecordingContext* getContext() const { return nullptr; }

protected:
    SkSpecialImage(const SkIRect& subset,
                   uint32_t uniqueID,
                   const SkColorInfo&,
                   const SkSurfaceProps&);

    // This subset is relative to the backing store's coordinate frame, it has already been mapped
    // from the content rect by the non-virtual makeSubset(). The provided 'subset' is not
    // necessarily contained within this special image's subset.
    virtual sk_sp<SkSpecialImage> onMakeBackingStoreSubset(const SkIRect& subset) const = 0;

private:
    const SkIRect        fSubset;
    const uint32_t       fUniqueID;
    const SkColorInfo    fColorInfo;
    const SkSurfaceProps fProps;
};

namespace SkSpecialImages {

// Callers supply a nonempty subset within the backing image. Bitmap factories
// retain pixels; CopyFromRaster allocates an independent, tightly bounded image.
sk_sp<SkSpecialImage> MakeFromRaster(const SkIRect& subset, sk_sp<SkImage>, const SkSurfaceProps&);
sk_sp<SkSpecialImage> MakeFromRaster(const SkIRect& subset, const SkBitmap&, const SkSurfaceProps&);
sk_sp<SkSpecialImage> CopyFromRaster(const SkIRect& subset, const SkBitmap&, const SkSurfaceProps&);

// On success, writes a view retaining the source pixels. Output must be non-null.
// A null or incompatible source fails without replacing the output bitmap.
bool AsBitmap(const SkSpecialImage* img, SkBitmap*);

}  // namespace SkSpecialImages

#endif // SkSpecialImage_DEFINED
