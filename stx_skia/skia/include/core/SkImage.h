/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkImage_DEFINED
#define SkImage_DEFINED

#include "include/core/SkAlphaType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSize.h"
#include "include/private/SkAPI.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

class SkBitmap;
class SkColorSpace;
class SkData;
class SkImage;
class SkImageGenerator;
class SkMatrix;
class SkMipmap;
class SkPixmap;
class SkRecorder;
class SkShader;
class SkSurfaceProps;
enum SkColorType : int;
enum class SkTileMode;
struct SkSamplingOptions;

namespace SkImages {
using ReleaseContext = void*;
using RasterReleaseProc = void(const void* pixels, ReleaseContext context);

// Factories require positive supported dimensions, valid pixel formats and row
// strides. Invalid input returns null. Allocation follows the library policy.
// A mutable bitmap is copied; an immutable bitmap may share its pixel reference.
SK_API sk_sp<SkImage> RasterFromBitmap(const SkBitmap& bitmap);
// Copy caller pixels, including the row gaps in their declared byte extent.
SK_API sk_sp<SkImage> RasterFromPixmapCopy(const SkPixmap& pixmap);
// Share caller pixels without copying. Pixels must remain immutable until the
// release callback, whose context must also survive. Invalid arguments do not
// transfer ownership or invoke the callback. A null callback leaves lifetime
// management with the caller, who must outlive all derived image owners.
SK_API sk_sp<SkImage> RasterFromPixmap(const SkPixmap& pixmap,
                                      RasterReleaseProc releaseProc, ReleaseContext context);
// Retain data without copying; callers relinquish writable aliases. Data must
// contain *info.checkedByteSize(rowBytes) bytes. Returned images retain its owner.
SK_API sk_sp<SkImage> RasterFromData(const SkImageInfo& info, sk_sp<SkData> pixels, size_t rowBytes);
// Transfers provider ownership, including on failure. Pixel generation is deferred
// until needed; provider calls are serialized across images sharing it.
SK_API sk_sp<SkImage> DeferredFromGenerator(std::unique_ptr<SkImageGenerator> generator);
// Compatibility rejection for encoded-input/deserialization callers. This CPU
// package has no decoder: it always returns null. Decode pixels externally.
SK_API sk_sp<SkImage> DeferredFromEncodedData(sk_sp<const SkData> encoded,
                                             std::optional<SkAlphaType> alphaType = std::nullopt);
}  // namespace SkImages

// Immutable CPU pixels, either retained directly or produced by a deferred
// generator. Internal caches may be populated on demand. Use the factories;
// external subclasses cannot supply the private image implementation contract.
class SK_API SkImage : public SkRefCnt {
public:
    const SkImageInfo& imageInfo() const { return fInfo; }
    int width() const {
        const int pixelWidth = fInfo.width();
        return pixelWidth;
    }
    int height() const {
        const int pixelHeight = fInfo.height();
        return pixelHeight;
    }
    SkISize dimensions() const {
        const SkISize size = SkISize::Make(fInfo.width(), fInfo.height());
        return size;
    }
    SkIRect bounds() const {
        const SkIRect rectangle = SkIRect::MakeWH(fInfo.width(), fInfo.height());
        return rectangle;
    }
    uint32_t uniqueID() const { return fUniqueID; }
    SkAlphaType alphaType() const;
    SkColorType colorType() const;
    // Borrowed color space, possibly null (untagged); refColorSpace retains it.
    SkColorSpace* colorSpace() const;
    sk_sp<SkColorSpace> refColorSpace() const;
    bool isAlphaOnly() const;
    bool isOpaque() const {
        const bool opaque = SkAlphaTypeIsOpaque(alphaType());
        return opaque;
    }

    // Shaders retain the image and copy the optional matrix during construction.
    // Overloads without tile modes clamp both axes. Unsupported sampling returns null.
    sk_sp<SkShader> makeShader(SkTileMode x, SkTileMode y, const SkSamplingOptions& sampling,
                              const SkMatrix* matrix = nullptr) const;
    sk_sp<SkShader> makeShader(SkTileMode x, SkTileMode y, const SkSamplingOptions& sampling,
                              const SkMatrix& matrix) const;
    sk_sp<SkShader> makeShader(const SkSamplingOptions& sampling, const SkMatrix& matrix) const;
    sk_sp<SkShader> makeShader(const SkSamplingOptions& sampling,
                              const SkMatrix* matrix = nullptr) const;
    // Raw shaders omit color-space conversion and automatic premultiplication.
    // Cubic sampling is unsupported and returns null for these raw-data shaders.
    sk_sp<SkShader> makeRawShader(SkTileMode x, SkTileMode y, const SkSamplingOptions& sampling,
                                 const SkMatrix* matrix = nullptr) const;
    sk_sp<SkShader> makeRawShader(SkTileMode x, SkTileMode y, const SkSamplingOptions& sampling,
                                 const SkMatrix& matrix) const;
    sk_sp<SkShader> makeRawShader(const SkSamplingOptions& sampling, const SkMatrix& matrix) const;
    sk_sp<SkShader> makeRawShader(const SkSamplingOptions& sampling,
                                 const SkMatrix* matrix = nullptr) const;

    // Borrow pixels without forcing generation. False leaves the output unchanged.
    // A successful view stays valid while this immutable image remains alive.
    // Passing null asks only whether the pixels are directly accessible.
    bool peekPixels(SkPixmap* pixmap) const;
    virtual bool isValid(SkRecorder*) const = 0;
    virtual bool isLazyGenerated() const = 0;

    enum CachingHint { kAllow_CachingHint, kDisallow_CachingHint };
    // Read/convert the source-destination intersection into caller-owned disjoint
    // storage. Negative offsets preserve leading destination pixels. Outside pixels
    // and padding remain unchanged. False reports unavailable pixels, invalid
    // destination/stride, unsupported conversion or empty intersection.
    bool readPixels(const SkImageInfo& info, void* pixels, size_t rowBytes,
                    int sourceX, int sourceY, CachingHint hint = kAllow_CachingHint) const;
    bool readPixels(const SkPixmap& pixels, int sourceX, int sourceY,
                    CachingHint hint = kAllow_CachingHint) const;
    // Preserve existing null-context call sites while excluding backend pointers.
    bool readPixels(std::nullptr_t, const SkImageInfo& info, void* pixels, size_t rowBytes,
                    int sourceX, int sourceY, CachingHint hint = kAllow_CachingHint) const;
    bool readPixels(std::nullptr_t, const SkPixmap& pixels, int sourceX, int sourceY,
                    CachingHint hint = kAllow_CachingHint) const;

    // Owned read result: the current CPU implementation supplies one pixel plane.
    // data and rowBytes accept a valid plane index; returned data borrows this owner.
    class AsyncReadResult {
    public:
        AsyncReadResult(const AsyncReadResult&) = delete;
        AsyncReadResult(AsyncReadResult&&) = delete;
        AsyncReadResult& operator=(const AsyncReadResult&) = delete;
        AsyncReadResult& operator=(AsyncReadResult&&) = delete;
        virtual ~AsyncReadResult() = default;
        virtual int count() const = 0;
        virtual const void* data(int index) const = 0;
        virtual size_t rowBytes(int index) const = 0;
    protected:
        AsyncReadResult() = default;
    };
    using ReadPixelsContext = void*;
    using ReadPixelsCallback = void(ReadPixelsContext, std::unique_ptr<const AsyncReadResult>);
    enum class RescaleGamma : bool { kSrc, kLinear };
    enum class RescaleMode { kNearest, kLinear, kRepeatedLinear, kRepeatedCubic };

    // Synchronous despite the inherited name. A non-null callback runs once with
    // owned result data or null on failure. Context is borrowed until return;
    // keep the image alive through completion. Source bounds must be contained.
    void asyncRescaleAndReadPixels(const SkImageInfo& info, const SkIRect& sourceBounds,
                                   RescaleGamma gamma, RescaleMode mode,
                                   ReadPixelsCallback callback, ReadPixelsContext context) const;
    // Writes a scaled image into caller-owned, disjoint destination storage.
    // The hint controls caching generated source pixels, not the scaled result.
    bool scalePixels(const SkPixmap& destination, const SkSamplingOptions& sampling,
                      CachingHint hint = kAllow_CachingHint) const;
    sk_sp<SkImage> makeScaled(SkRecorder*, const SkImageInfo&, const SkSamplingOptions&) const;
    // Legacy property parameter is currently ignored; kept for source compatibility.
    sk_sp<SkImage> makeScaled(SkRecorder*, const SkImageInfo&, const SkSamplingOptions&,
                              const SkSurfaceProps&) const;
    sk_sp<SkImage> makeScaled(const SkImageInfo&, const SkSamplingOptions&) const;

    // Optional original bytes supplied by a deferred provider; this does not encode.
    sk_sp<const SkData> refEncodedData() const;
    struct RequiredProperties {
        bool fMipmapped = false;
        bool operator==(const RequiredProperties& other) const {
            const bool equal = fMipmapped == other.fMipmapped;
            return equal;
        }
        bool operator!=(const RequiredProperties& other) const {
            const bool different = fMipmapped != other.fMipmapped;
            return different;
        }
        bool operator<(const RequiredProperties& other) const {
            const bool less = fMipmapped < other.fMipmapped;
            return less;
        }
    };
    // Bounds must be nonempty and wholly contained. Raster subsets copy pixels;
    // deferred subsets first materialize the source. Failure returns null.
    virtual sk_sp<SkImage> makeSubset(SkRecorder*, const SkIRect&, RequiredProperties) const = 0;
    bool hasMipmaps() const;
    // Returns a new raster image with generated levels, or this image when no
    // replacement is available. Deferred images currently retain their provider.
    sk_sp<SkImage> withDefaultMipmaps() const;
    // Materialize deferred pixels; already accessible images retain their identity.
    sk_sp<SkImage> makeRasterImage(CachingHint hint = kDisallow_CachingHint) const;
    sk_sp<SkImage> makeRasterImage(std::nullptr_t, CachingHint hint = kDisallow_CachingHint) const;
    enum LegacyBitmapMode { kRO_LegacyBitmapMode };
    // Returns immutable bitmap pixels, sharing where safe or copying/converting.
    bool asLegacyBitmap(SkBitmap*, LegacyBitmapMode mode = kRO_LegacyBitmapMode) const;
    // Convert pixels (raster) or request a converted deferred representation.
    // Current color conversion paths do not create mipmaps from RequiredProperties.
    virtual sk_sp<SkImage> makeColorSpace(SkRecorder*, sk_sp<SkColorSpace>,
                                          RequiredProperties) const = 0;
    virtual sk_sp<SkImage> makeColorTypeAndColorSpace(SkRecorder*, SkColorType,
                                                     sk_sp<SkColorSpace>, RequiredProperties) const = 0;
    // Change the color-space interpretation without converting stored pixel values.
    sk_sp<SkImage> reinterpretColorSpace(sk_sp<SkColorSpace> colorSpace) const;

private:
    SkImage(const SkImageInfo& info, uint32_t uniqueID);
    friend class SkBitmap;
    friend class SkImage_Base;
    friend class SkImage_Raster;
    friend class SkMipmapBuilder;
    SkImageInfo fInfo;
    const uint32_t fUniqueID;
    sk_sp<SkImage> withMipmaps(sk_sp<SkMipmap>) const;
};

#endif
