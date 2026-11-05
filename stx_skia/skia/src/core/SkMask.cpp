/*
 * Copyright 2007 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkMask.h"

#include "include/private/SkMalloc.h"
#include "src/core/SkSafeMath.h"

#include <array>
#include <climits>

size_t SkMask::computeImageSize() const {
    const int64_t height = fBounds.height64();
    if (height <= 0 || fRowBytes == 0) return 0;
    // The height can span the full signed-coordinate range. Check division
    // before multiplication so even malformed descriptors cannot wrap the size.
    if (height > INT32_MAX / static_cast<int64_t>(fRowBytes)) return 0;
    const size_t size = static_cast<size_t>(height) * fRowBytes;
    return size;
}

size_t SkMask::computeTotalImageSize() const {
    size_t size = computeImageSize();
    if (fFormat == SkMask::k3D_Format) {
        if (size > INT32_MAX / 3) return 0;
        size *= 3;
    }
    return size;
}

// Use the bitmap-compatible allocator; ownership may transfer across that seam.
uint8_t* SkMaskBuilder::AllocImage(size_t size, AllocType at) {
    SkSafeMath extent;
    const size_t aligned_size = extent.alignUp(size, 4);
    SkASSERT_RELEASE(extent.ok());
    unsigned int flags = SK_MALLOC_THROW;
    if (at == kZeroInit_Alloc) flags |= SK_MALLOC_ZERO_INITIALIZE;
    uint8_t* pixels = static_cast<uint8_t*>(sk_malloc_flags(aligned_size, flags));
    return pixels;
}

void SkMaskBuilder::FreeImage(void* image) {
    sk_free(image);
}

SkMaskBuilder SkMaskBuilder::PrepareDestination(int radiusX, int radiusY, const SkMask& src) {
    SkMaskBuilder dst;
    dst.format() = SkMask::kA8_Format;
    if (radiusX < 0 || radiusY < 0) return dst;
    const int64_t src_width = src.fBounds.width64();
    const int64_t src_height = src.fBounds.height64();
    if (src_width < 0 || src_height < 0) return dst;
    const int64_t width = src_width + 2 * static_cast<int64_t>(radiusX);
    const int64_t height = src_height + 2 * static_cast<int64_t>(radiusY);
    if (width > INT32_MAX || height > INT32_MAX) return dst;
    const int64_t bytes = width * height;
    if (bytes > INT32_MAX) return dst;

    const int64_t left = static_cast<int64_t>(src.fBounds.fLeft) - radiusX;
    const int64_t top = static_cast<int64_t>(src.fBounds.fTop) - radiusY;
    const int64_t right = static_cast<int64_t>(src.fBounds.fRight) + radiusX;
    const int64_t bottom = static_cast<int64_t>(src.fBounds.fBottom) + radiusY;
    if (left < INT32_MIN || top < INT32_MIN || right > INT32_MAX || bottom > INT32_MAX) return dst;
    dst.bounds() = SkIRect::MakeLTRB(static_cast<int32_t>(left), static_cast<int32_t>(top),
                                   static_cast<int32_t>(right), static_cast<int32_t>(bottom));
    dst.rowBytes() = static_cast<uint32_t>(width);
    if (src.fImage != nullptr) {
        dst.image() = SkMaskBuilder::AllocImage(static_cast<size_t>(bytes));
    }
    return dst;
}

static constexpr std::array<int, SkMask::kCountMaskFormats> kMaskFormatShift = {
    -1, // BW uses bit addresses rather than byte-per-pixel addresses.
     0, // A8
     0, // 3D first plane
     2, // ARGB32
     1, // LCD16
     0  // SDF
};

const void* SkMask::getAddr(int x, int y) const {
    if (!IsValidFormat(fFormat) || fFormat == kBW_Format) return nullptr;
    SkASSERT(fBounds.contains(x, y));
    SkASSERT(fImage != nullptr);
    const size_t row = static_cast<size_t>(static_cast<int64_t>(y) - fBounds.fTop) * fRowBytes;
    const size_t column = static_cast<size_t>(static_cast<int64_t>(x) - fBounds.fLeft);
    const size_t column_bytes = column << kMaskFormatShift[fFormat];
    const void* address = fImage + row + column_bytes;
    return address;
}
