/*
 * Copyright 2008 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkBitmap.h"

#include "include/core/SkColorSpace.h"  // IWYU pragma: keep
#include "include/core/SkColorType.h"
#include "include/core/SkImage.h"
#include "include/core/SkMallocPixelRef.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixelRef.h"
#include "include/core/SkRect.h"
#include "include/core/SkShader.h"
#include "include/core/SkTileMode.h"
#include "include/private/SkAlign.h"
#include "include/private/SkTFitsIn.h"
#include "include/private/SkTo.h"
#include "src/core/SkConvertPixels.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkMask.h"
#include "src/core/SkMaskFilterBase.h"
#include "src/core/SkPixelRefPriv.h"
#include "src/core/SkWritePixelsRec.h"
#include "src/image/SkImage_Raster.h"
#include "src/shaders/SkImageShader.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
class SkMaskFilter;

SkBitmap::SkBitmap() : fPixelRef(nullptr), fPixmap() {}

SkBitmap::SkBitmap(const SkBitmap& src) : fPixelRef(src.fPixelRef), fPixmap(src.fPixmap) {
    SkDEBUGCODE(src.validate();)
    SkDEBUGCODE(validate();)
}

SkBitmap::SkBitmap(SkBitmap&& other)
        : fPixelRef(std::move(other.fPixelRef)), fPixmap(std::move(other.fPixmap)) {
    SkASSERT(!other.fPixelRef);
    other.fPixmap.reset();
}

SkBitmap::~SkBitmap() {}

SkBitmap& SkBitmap::operator=(const SkBitmap& src) {
    if (this != &src) {
        fPixelRef = src.fPixelRef;
        fPixmap = src.fPixmap;
    }
    SkDEBUGCODE(validate();)
    return *this;
}

SkBitmap& SkBitmap::operator=(SkBitmap&& other) {
    if (this != &other) {
        fPixelRef = std::move(other.fPixelRef);
        fPixmap = std::move(other.fPixmap);
        SkASSERT(!other.fPixelRef);
        other.fPixmap.reset();
    }
    return *this;
}

void SkBitmap::swap(SkBitmap& other) {
    using std::swap;
    swap(*this, other);
    SkDEBUGCODE(validate();)
}

void SkBitmap::reset() {
    fPixelRef = nullptr;  // Free pixels.
    fPixmap.reset();
}

void SkBitmap::getBounds(SkRect* bounds) const {
    SkASSERT(bounds);
    *bounds = SkRect::Make(dimensions());
}

void SkBitmap::getBounds(SkIRect* bounds) const {
    SkASSERT(bounds);
    *bounds = fPixmap.bounds();
}

SkColorSpace* SkBitmap::colorSpace() const {
    SkColorSpace* colorSpace = fPixmap.colorSpace();
    return colorSpace;
}

sk_sp<SkColorSpace> SkBitmap::refColorSpace() const {
    sk_sp<SkColorSpace> colorSpace = fPixmap.info().refColorSpace();
    return colorSpace;
}

///////////////////////////////////////////////////////////////////////////////

bool SkBitmap::setInfo(const SkImageInfo& info, size_t rowBytes) {
    SkAlphaType correctedAlpha = info.alphaType();
    if (!SkColorTypeValidateAlphaType(info.colorType(), info.alphaType(), &correctedAlpha)) {
        reset();
        return false;
    }
    // Bitmap row strides retain their signed 32-bit representation limit.
    const int64_t minimumRowBytes = info.minRowBytes64();
    if (!SkTFitsIn<int32_t>(minimumRowBytes)) {
        reset();
        return false;
    }
    if (!SkTFitsIn<int32_t>(rowBytes)) {
        reset();
        return false;
    }

    if (info.width() < 0 || info.height() < 0) {
        reset();
        return false;
    }

    if (kUnknown_SkColorType == info.colorType()) {
        rowBytes = 0;
    } else if (0 == rowBytes) {
        rowBytes = static_cast<size_t>(minimumRowBytes);
    } else if (!info.validRowBytes(rowBytes)) {
        reset();
        return false;
    }

    fPixelRef = nullptr;  // Free pixels.
    const SkImageInfo correctedInfo = info.makeAlphaType(correctedAlpha);
    fPixmap.reset(correctedInfo, nullptr, rowBytes);
    SkDEBUGCODE(validate();)
    return true;
}

bool SkBitmap::setAlphaType(SkAlphaType newAlphaType) {
    if (!SkColorTypeValidateAlphaType(colorType(), newAlphaType, &newAlphaType)) {
        return false;
    }
    if (alphaType() != newAlphaType) {
        const SkImageInfo newInfo = fPixmap.info().makeAlphaType(newAlphaType);
        fPixmap.reset(newInfo, fPixmap.addr(), fPixmap.rowBytes());
    }
    SkDEBUGCODE(validate();)
    return true;
}

void SkBitmap::setColorSpace(sk_sp<SkColorSpace> newColorSpace) {
    if (colorSpace() != newColorSpace.get()) {
        const SkImageInfo newInfo = fPixmap.info().makeColorSpace(std::move(newColorSpace));
        fPixmap.reset(newInfo, fPixmap.addr(), fPixmap.rowBytes());
    }
    SkDEBUGCODE(validate();)
}

SkIPoint SkBitmap::pixelRefOrigin() const {
    const char* addr = static_cast<const char*>(fPixmap.addr());
    const char* pix = static_cast<const char*>(fPixelRef ? (*fPixelRef).pixels() : nullptr);
    const size_t rb = rowBytes();
    if (!pix || 0 == rb) {
        return {0, 0};
    }
    SkASSERT(bytesPerPixel() > 0);
    SkASSERT(bytesPerPixel() == (1 << shiftPerPixel()));
    SkASSERT(addr >= pix);
    const size_t byteOffset = static_cast<size_t>(addr - pix);
    const size_t column = (byteOffset % rb) >> shiftPerPixel();
    const size_t row = byteOffset / rb;
    const SkIPoint origin{SkToS32(column), SkToS32(row)};
    return origin;
}

void SkBitmap::setPixelRef(sk_sp<SkPixelRef> pr, int dx, int dy) {
    const bool knownColor = kUnknown_SkColorType != colorType();
    if (pr && knownColor) {
        const int refWidth = (*pr).width();
        const int refHeight = (*pr).height();
        const int bitmapWidth = width();
        const int bitmapHeight = height();
        const size_t pixelBytes = static_cast<size_t>(bytesPerPixel());
        const size_t refRowBytes = (*pr).rowBytes();
        const bool invalidShape = refWidth < bitmapWidth || refHeight < bitmapHeight ||
                                  refWidth < 0 || refHeight < 0;
        const bool invalidStride = pixelBytes == 0 ||
                                   !info().validRowBytes(refRowBytes) ||
                                   (refWidth >= 0 &&
                                    static_cast<size_t>(refWidth) > refRowBytes / pixelBytes);
        const bool invalidExtent = refHeight > 0 &&
                                   refRowBytes > std::numeric_limits<size_t>::max() /
                                                         static_cast<size_t>(refHeight);
        if (invalidShape || invalidStride || invalidExtent) {
            // No origin can make this reference a valid bitmap view.
            pr.reset();
        } else if (refHeight == 0) {
            // A reference with no rows has no addressable origin beyond its base.
            dx = 0;
            dy = 0;
        } else {
            const int maxX = refWidth - bitmapWidth;
            const int maxY = refHeight - bitmapHeight;
            SkASSERT(dx >= 0 && dx <= maxX);
            SkASSERT(dy >= 0 && dy <= maxY);
            dx = std::clamp(dx, 0, maxX);
            dy = std::clamp(dy, 0, maxY);
            if (bitmapHeight == 0 && dy == refHeight) {
                // Keep a zero-height view within the final row's storage.
                dy = refHeight - 1;
            }
        }
    }
    fPixelRef = knownColor ? std::move(pr) : nullptr;
    void* p = nullptr;
    size_t rowBytes = fPixmap.rowBytes();
    if (fPixelRef) {
        rowBytes = (*fPixelRef).rowBytes();
        p = (*fPixelRef).pixels();
        if (p) {
            const size_t rowOffset = static_cast<size_t>(dy) * rowBytes;
            const size_t columnOffset = static_cast<size_t>(dx) * bytesPerPixel();
            const size_t offset = rowOffset + columnOffset;
            p = static_cast<char*>(p) + offset;
        }
    }
    fPixmap.reset(fPixmap.info(), p, rowBytes);
    SkDEBUGCODE(validate();)
}

void SkBitmap::setPixels(void* p) {
    if (kUnknown_SkColorType == colorType()) {
        p = nullptr;
    }
    size_t rb = rowBytes();
    fPixmap.reset(fPixmap.info(), p, rb);
    sk_sp<SkPixelRef> owner(nullptr);
    if (p != nullptr) {
        owner = sk_make_sp<SkPixelRef>(width(), height(), p, rb);
    }
    fPixelRef = std::move(owner);
    SkDEBUGCODE(validate();)
}

bool SkBitmap::tryAllocPixels(Allocator* allocator) {
    HeapAllocator stdalloc{};

    if (nullptr == allocator) {
        allocator = &stdalloc;
    }
    const bool allocated = (*allocator).allocPixelRef(this);
    return allocated;
}

bool SkBitmap::tryAllocN32Pixels(int width, int height, bool isOpaque) {
    const SkImageInfo info = SkImageInfo::MakeN32(width, height,
            isOpaque ? kOpaque_SkAlphaType : kPremul_SkAlphaType);
    const bool allocated = tryAllocPixels(info);
    return allocated;
}

void SkBitmap::allocN32Pixels(int width, int height, bool isOpaque) {
    const SkImageInfo info = SkImageInfo::MakeN32(width, height,
                                        isOpaque ? kOpaque_SkAlphaType : kPremul_SkAlphaType);
    allocPixels(info);
}

void SkBitmap::allocPixels() {
    allocPixels(static_cast<Allocator*>(nullptr));
}

void SkBitmap::allocPixels(Allocator* allocator) {
    if (!tryAllocPixels(allocator)) {
        const SkImageInfo& info = fPixmap.info();
        SK_ABORT("SkBitmap::tryAllocPixels failed "
                 "ColorType:%d AlphaType:%d [w:%d h:%d] rb:%zu",
                 info.colorType(), info.alphaType(), info.width(), info.height(), rowBytes());
    }
}

void SkBitmap::allocPixelsFlags(const SkImageInfo& info, uint32_t flags) {
    const bool allocated = tryAllocPixelsFlags(info, flags);
    SkASSERTF_RELEASE(allocated,
                      "ColorType:%d AlphaType:%d [w:%d h:%d] rb:%zu flags: 0x%x",
                      info.colorType(), info.alphaType(), info.width(), info.height(),
                      rowBytes(), flags);
}

void SkBitmap::allocPixels(const SkImageInfo& info, size_t rowBytes) {
    const bool allocated = tryAllocPixels(info, rowBytes);
    SkASSERTF_RELEASE(allocated,
                      "ColorType:%d AlphaType:%d [w:%d h:%d] rb:%zu",
                      info.colorType(), info.alphaType(), info.width(), info.height(),
                      fPixmap.rowBytes());
}

void SkBitmap::allocPixels(const SkImageInfo& info) {
    allocPixels(info, info.minRowBytes());
}

///////////////////////////////////////////////////////////////////////////////

bool SkBitmap::tryAllocPixels(const SkImageInfo& requestedInfo, size_t rowBytes) {
    if (!setInfo(requestedInfo, rowBytes)) {
        reset();
        return false;
    }

    // setInfo may have corrected info (e.g. 565 is always opaque).
    const SkImageInfo& correctedInfo = info();
    if (kUnknown_SkColorType == correctedInfo.colorType()) {
        return true;
    }
    // setInfo may have computed a valid rowbytes if 0 were passed in
    rowBytes = fPixmap.rowBytes();

    sk_sp<SkPixelRef> pr = SkMallocPixelRef::MakeAllocate(correctedInfo, rowBytes);
    if (!pr) {
        reset();
        return false;
    }
    setPixelRef(std::move(pr), 0, 0);
    if (nullptr == getPixels()) {
        reset();
        return false;
    }
    SkDEBUGCODE(validate();)
    return true;
}

bool SkBitmap::tryAllocPixelsFlags(const SkImageInfo& requestedInfo, uint32_t allocFlags) {
    if (!setInfo(requestedInfo)) {
        reset();
        return false;
    }

    // setInfo may have corrected info (e.g. 565 is always opaque).
    const SkImageInfo& correctedInfo = info();

    sk_sp<SkPixelRef> pr = SkMallocPixelRef::MakeAllocate(correctedInfo,
                                                          correctedInfo.minRowBytes());
    if (!pr) {
        reset();
        return false;
    }
    setPixelRef(std::move(pr), 0, 0);
    if (nullptr == getPixels()) {
        reset();
        return false;
    }
    SkDEBUGCODE(validate();)
    return true;
}

static void invoke_release_proc(void (*proc)(void* pixels, void* ctx), void* pixels, void* ctx) {
    if (proc) {
        proc(pixels, ctx);
    }
}

bool SkBitmap::installPixels(const SkImageInfo& requestedInfo, void* pixels, size_t rb,
                             void (*releaseProc)(void* addr, void* context), void* context) {
    if (!setInfo(requestedInfo, rb)) {
        invoke_release_proc(releaseProc, pixels, context);
        reset();
        return false;
    }
    if (nullptr == pixels) {
        invoke_release_proc(releaseProc, pixels, context);
        return true;    // we behaved as if they called setInfo()
    }

    // setInfo may have corrected info (e.g. 565 is always opaque).
    const SkImageInfo& correctedInfo = info();
    sk_sp<SkPixelRef> owner = SkMakePixelRefWithProc(correctedInfo.width(), correctedInfo.height(),
                                                   rb, pixels, releaseProc, context);
    setPixelRef(std::move(owner), 0, 0);
    SkDEBUGCODE(validate();)
    return true;
}

bool SkBitmap::installPixels(const SkPixmap& pixmap) {
    const bool installed = installPixels(pixmap.info(), pixmap.writable_addr(), pixmap.rowBytes(),
                                          nullptr, nullptr);
    return installed;
}

///////////////////////////////////////////////////////////////////////////////

uint32_t SkBitmap::getGenerationID() const {
    const uint32_t generation = fPixelRef ? (*fPixelRef).getGenerationID() : 0;
    return generation;
}

void SkBitmap::notifyPixelsChanged() const {
    SkASSERT(!isImmutable());
    if (fPixelRef) {
        (*fPixelRef).notifyPixelsChanged();
    }
}

///////////////////////////////////////////////////////////////////////////////

/** We explicitly use the same allocator for our pixels that SkMask does,
 so that we can freely assign memory allocated by one class to the other.
 */
bool SkBitmap::HeapAllocator::allocPixelRef(SkBitmap* dst) {
    const SkImageInfo& info = (*dst).info();
    if (kUnknown_SkColorType == info.colorType()) {
        return false;
    }

    sk_sp<SkPixelRef> pr = SkMallocPixelRef::MakeAllocate(info, (*dst).rowBytes());
    if (!pr) {
        return false;
    }

    (*dst).setPixelRef(std::move(pr), 0, 0);
    SkDEBUGCODE((*dst).validate();)
    return true;
}

///////////////////////////////////////////////////////////////////////////////

bool SkBitmap::isImmutable() const {
    const bool immutable = fPixelRef ? (*fPixelRef).isImmutable() : false;
    return immutable;
}

void SkBitmap::setImmutable() {
    if (fPixelRef) {
        (*fPixelRef).setImmutable();
    }
}

void* SkBitmap::getAddr(int x, int y) const {
    SkASSERT(static_cast<unsigned>(x) < static_cast<unsigned>(width()));
    SkASSERT(static_cast<unsigned>(y) < static_cast<unsigned>(height()));

    char* base = static_cast<char*>(getPixels());
    if (base) {
        const size_t rowOffset = static_cast<size_t>(y) * rowBytes();
        const size_t columnOffset = static_cast<size_t>(x) << shiftPerPixel();
        base += rowOffset + columnOffset;
    }
    return base;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void SkBitmap::erase(SkColor4f c, const SkIRect& area) const {
    SkDEBUGCODE(validate();)

    if (kUnknown_SkColorType == colorType()) {
        return;
    }

    SkPixmap result{};
    if (!peekPixels(&result)) {
        return;
    }

    if (result.erase(c, &area)) {
        notifyPixelsChanged();
    }
}

void SkBitmap::erase(SkColor c, const SkIRect& area) const {
    erase(SkColor4f::FromColor(c), area);
}

void SkBitmap::eraseColor(SkColor4f c) const {
    erase(c, SkIRect::MakeWH(width(), height()));
}

void SkBitmap::eraseColor(SkColor c) const {
    erase(SkColor4f::FromColor(c), SkIRect::MakeWH(width(), height()));
}

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////

bool SkBitmap::extractSubset(SkBitmap* result, const SkIRect& subset) const {
    SkDEBUGCODE(validate();)

    if (nullptr == result || !fPixelRef) {
        return false;   // no src pixels
    }

    SkIRect srcRect{};
    SkIRect r{};
    srcRect.setWH(width(), height());
    if (!r.intersect(srcRect, subset)) {
        return false;   // r is empty (i.e. no intersection)
    }

    // If the upper left of the rectangle was outside the bounds of this SkBitmap, we should have
    // exited above.
    SkASSERT(static_cast<unsigned>(r.fLeft) < static_cast<unsigned>(width()));
    SkASSERT(static_cast<unsigned>(r.fTop) < static_cast<unsigned>(height()));

    SkBitmap dst{};
    dst.setInfo(info().makeDimensions(r.size()), rowBytes());

    if (fPixelRef) {
        const SkIPoint origin = pixelRefOrigin();
        // share the pixelref with a custom offset
        dst.setPixelRef(fPixelRef, origin.x() + r.fLeft, origin.y() + r.fTop);
    }
    SkDEBUGCODE(dst.validate();)

    // we know we're good, so commit to result
    (*result).swap(dst);
    return true;
}

///////////////////////////////////////////////////////////////////////////////

bool SkBitmap::readPixels(const SkImageInfo& requestedDstInfo, void* dstPixels, size_t dstRB,
                          int x, int y) const {
    SkPixmap src{};
    if (!peekPixels(&src)) {
        return false;
    }
    const bool copied = src.readPixels(requestedDstInfo, dstPixels, dstRB, x, y);
    return copied;
}

bool SkBitmap::readPixels(const SkPixmap& dst, int srcX, int srcY) const {
    const bool copied = readPixels(dst.info(), dst.writable_addr(), dst.rowBytes(), srcX, srcY);
    return copied;
}

bool SkBitmap::writePixels(const SkPixmap& src, int dstX, int dstY) {
    if (!SkImageInfoValidConversion(info(), src.info())) {
        return false;
    }

    SkWritePixelsRec rec(src.info(), src.addr(), src.rowBytes(), dstX, dstY);
    if (!rec.trim(width(), height())) {
        return false;
    }

    void* dstPixels = getAddr(rec.fX, rec.fY);
    const SkImageInfo dstInfo = info().makeDimensions(rec.fInfo.dimensions());
    if (!SkConvertPixels(dstInfo,     dstPixels, rowBytes(),
                         rec.fInfo, rec.fPixels,   rec.fRowBytes)) {
        return false;
    }
    notifyPixelsChanged();
    return true;
}

///////////////////////////////////////////////////////////////////////////////

// Source and destination pixel storage are disjoint. Only active alpha bytes
// are written; padding is not part of the extracted image.
static bool CopyBitmapAlpha(const SkBitmap& source, uint8_t* alpha, size_t alphaRowBytes) {
    SkASSERT(alpha != nullptr);
    const size_t width = static_cast<size_t>(source.width());
    const int height = source.height();
    SkASSERT(alphaRowBytes >= width);

    SkPixmap pixels{};
    if (!source.peekPixels(&pixels)) {
        for (int y = 0; y < height; ++y) {
            uint8_t* row = alpha + static_cast<size_t>(y) * alphaRowBytes;
            std::memset(row, 0, width);
        }
        return false;
    }
    const SkImageInfo alphaInfo = SkImageInfo::MakeA8(pixels.width(), pixels.height());
    const bool converted = SkConvertPixels(alphaInfo, alpha, alphaRowBytes,
                                           pixels.info(), pixels.addr(), pixels.rowBytes());
    return converted;
}

static bool ExtractUnfilteredAlpha(const SkBitmap& source, SkBitmap& destination,
                                   SkBitmap::Allocator* allocator, SkIPoint* offset,
                                   size_t rowBytes) {
    SkBitmap extracted{};
    const SkImageInfo alphaInfo = SkImageInfo::MakeA8(source.width(), source.height());
    if (!extracted.setInfo(alphaInfo, rowBytes)) {
        return false;
    }
    if (!extracted.tryAllocPixels(allocator)) {
        SkDebugf("extractAlpha failed to allocate (%d,%d) alpha bitmap\n",
                 extracted.width(), extracted.height());
        return false;
    }
    CopyBitmapAlpha(source, extracted.getAddr8(0, 0), rowBytes);
    if (offset != nullptr) {
        (*offset).set(0, 0);
    }
    extracted.swap(destination);
    return true;
}

enum class AlphaFilterResult {
    kExtracted,
    kUnfiltered,
    kFailed,
};

// A filter's false result requests the unfiltered fallback. Invalid output or
// allocation failure fails extraction. Both leave the caller's output intact;
// all filter-owned pixels are released before the fallback begins.
static AlphaFilterResult ExtractFilteredAlpha(const SkBitmap& source, SkBitmap& destination,
                                              const SkMaskFilterBase& filter,
                                              SkBitmap::Allocator* allocator, SkIPoint* offset,
                                              uint32_t rowBytes) {
    const SkIRect sourceBounds = SkIRect::MakeWH(source.width(), source.height());
    const SkMask metadata(nullptr, sourceBounds, rowBytes, SkMask::kA8_Format);
    const SkMatrix identity = SkMatrix::I();
    SkAutoMaskFreeImage filteredPixels(nullptr);
    SkMaskBuilder filteredMask{};
    const bool boundsReady = filter.filterMask(&filteredMask, metadata, identity, nullptr);
    filteredPixels.reset(filteredMask.image());
    if (!boundsReady) {
        return AlphaFilterResult::kUnfiltered;
    }
    if (filteredMask.image() != nullptr || filteredMask.bounds().isEmpty() ||
        filteredMask.bounds().width64() > std::numeric_limits<int>::max() - 3) {
        return AlphaFilterResult::kFailed;
    }
    filteredMask.rowBytes() = SkAlign4(filteredMask.bounds().width());

    const size_t sourceSize = metadata.computeImageSize();
    if (sourceSize == 0) {
        return AlphaFilterResult::kFailed;
    }
    SkAutoMaskFreeImage sourcePixels(SkMaskBuilder::AllocImage(sourceSize));
    const SkMask sourceMask(sourcePixels.get(), sourceBounds, rowBytes, SkMask::kA8_Format);
    CopyBitmapAlpha(source, sourcePixels.get(), rowBytes);

    const bool filtered = filter.filterMask(&filteredMask, sourceMask, identity, nullptr);
    filteredPixels.reset(filteredMask.image());
    if (!filtered) {
        return AlphaFilterResult::kUnfiltered;
    }
    if (filteredMask.bounds().isEmpty() ||
        filteredMask.bounds().width64() > std::numeric_limits<int>::max() - 3) {
        return AlphaFilterResult::kFailed;
    }
    const size_t filteredSize = filteredMask.computeImageSize();
    if (filteredMask.image() == nullptr || filteredSize == 0) {
        return AlphaFilterResult::kFailed;
    }

    SkBitmap extracted{};
    const SkImageInfo alphaInfo = SkImageInfo::MakeA8(filteredMask.fBounds.width(),
                                                    filteredMask.fBounds.height());
    if (!extracted.setInfo(alphaInfo, filteredMask.fRowBytes)) {
        return AlphaFilterResult::kFailed;
    }
    if (!extracted.tryAllocPixels(allocator)) {
        SkDebugf("extractAlpha failed to allocate (%d,%d) alpha bitmap\n",
                 extracted.width(), extracted.height());
        return AlphaFilterResult::kFailed;
    }
    // Bitmap allocation omits padding after the final row; the mask includes it.
    const std::optional<size_t> destinationSize = extracted.checkedByteSize();
    if (!destinationSize || *destinationSize > filteredSize) {
        return AlphaFilterResult::kFailed;
    }
    std::memcpy(extracted.getPixels(), filteredMask.fImage, *destinationSize);
    if (offset != nullptr) {
        (*offset).set(filteredMask.fBounds.fLeft, filteredMask.fBounds.fTop);
    }
    SkDEBUGCODE(extracted.validate();)
    extracted.swap(destination);
    return AlphaFilterResult::kExtracted;
}

bool SkBitmap::extractAlpha(SkBitmap* destination, const SkPaint* paint,
                            Allocator* allocator, SkIPoint* offset) const {
    SkDEBUGCODE(validate();)
    if (width() == 0 || height() == 0 || width() > std::numeric_limits<int>::max() - 3) {
        return false;
    }
    const uint32_t rowBytes = SkAlign4(width());
    SkMaskFilter* filter = paint != nullptr ? (*paint).getMaskFilter() : nullptr;
    if (filter != nullptr) {
        const SkMaskFilterBase& maskFilter = *as_MFB(filter);
        const AlphaFilterResult result = ExtractFilteredAlpha(*this, *destination, maskFilter,
                                                              allocator, offset, rowBytes);
        if (result != AlphaFilterResult::kUnfiltered) {
            const bool extracted = result == AlphaFilterResult::kExtracted;
            return extracted;
        }
    }
    const bool extracted = ExtractUnfilteredAlpha(*this, *destination, allocator, offset, rowBytes);
    return extracted;
}

///////////////////////////////////////////////////////////////////////////////

#ifdef SK_DEBUG
void SkBitmap::validate() const {
    info().validate();

    SkASSERT(info().validRowBytes(rowBytes()));

    if (fPixelRef && (*fPixelRef).pixels()) {
        SkASSERT(getPixels());
    } else {
        SkASSERT(!getPixels());
    }

    if (getPixels()) {
        SkASSERT(fPixelRef);
        SkASSERT((*fPixelRef).rowBytes() == rowBytes());
        SkIPoint origin = pixelRefOrigin();
        SkASSERT(origin.fX >= 0);
        SkASSERT(origin.fY >= 0);
        SkASSERT((*fPixelRef).width() >= width() + origin.fX);
        SkASSERT((*fPixelRef).height() >= height() + origin.fY);
        SkASSERT((*fPixelRef).rowBytes() >= info().minRowBytes());
    }
}
#endif

///////////////////////////////////////////////////////////////////////////////

bool SkBitmap::peekPixels(SkPixmap* pmap) const {
    if (getPixels()) {
        if (pmap) {
            *pmap = fPixmap;
        }
        return true;
    }
    return false;
}

sk_sp<SkImage> SkBitmap::asImage() const {
    sk_sp<SkImage> image = SkImages::RasterFromBitmap(*this);
    return image;
}

sk_sp<SkShader> SkBitmap::makeShader(const SkSamplingOptions& sampling,
                                     const SkMatrix& lm) const {
    sk_sp<SkShader> shader = makeShader(SkTileMode::kClamp, SkTileMode::kClamp, sampling, &lm);
    return shader;
}

sk_sp<SkShader> SkBitmap::makeShader(const SkSamplingOptions& sampling,
                                     const SkMatrix* lm) const {
    sk_sp<SkShader> shader = makeShader(SkTileMode::kClamp, SkTileMode::kClamp, sampling, lm);
    return shader;
}

sk_sp<SkShader> SkBitmap::makeShader(SkTileMode tmx, SkTileMode tmy,
                                     const SkSamplingOptions& sampling,
                                     const SkMatrix& lm) const {
    if (!lm.invert()) {
        return nullptr;
    }
    sk_sp<SkImage> image = SkImage_Raster::MakeFromBitmap(*this, SkCopyPixelsMode::kIfMutable);
    sk_sp<SkShader> shader = SkImageShader::Make(std::move(image), tmx, tmy, sampling, &lm);
    return shader;
}

sk_sp<SkShader> SkBitmap::makeShader(SkTileMode tmx, SkTileMode tmy,
                                     const SkSamplingOptions& sampling,
                                     const SkMatrix* lm) const {
    if (lm && !(*lm).invert()) {
        return nullptr;
    }
    sk_sp<SkImage> image = SkImage_Raster::MakeFromBitmap(*this, SkCopyPixelsMode::kIfMutable);
    sk_sp<SkShader> shader = SkImageShader::Make(std::move(image), tmx, tmy, sampling, lm);
    return shader;
}
