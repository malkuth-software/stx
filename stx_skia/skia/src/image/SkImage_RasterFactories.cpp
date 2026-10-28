/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkAlphaType.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkColorType.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/private/SkMath.h"
#include "src/image/SkImage_Base.h"
#include "src/image/SkImage_Raster.h"

#include <cstddef>
#include <cstdint>
#include <utility>

struct SkIPoint;
struct SkIRect;

static bool valid_args(const SkImageInfo& info, size_t rowBytes, size_t* minSize) {
    const int maxDimension = SK_MaxS32 >> 2;

    // TODO(mtklein): eliminate anything here that setInfo() has already checked.
    SkBitmap bitmap;
    if (!bitmap.setInfo(info, rowBytes)) {
        return false;
    }

    if (info.width() <= 0 || info.height() <= 0) {
        return false;
    }
    if (info.width() > maxDimension || info.height() > maxDimension) {
        return false;
    }
    if (static_cast<unsigned>(info.colorType()) > static_cast<unsigned>(kLastEnum_SkColorType)) {
        return false;
    }
    if (static_cast<unsigned>(info.alphaType()) > static_cast<unsigned>(kLastEnum_SkAlphaType)) {
        return false;
    }

    if (kUnknown_SkColorType == info.colorType()) {
        return false;
    }
    if (!info.validRowBytes(rowBytes)) {
        return false;
    }

    const size_t size = info.computeByteSize(rowBytes);
    if (SkImageInfo::ByteSizeOverflowed(size)) {
        return false;
    }

    if (minSize) {
        *minSize = size;
    }
    return true;
}

namespace SkImages {
sk_sp<SkImage> RasterFromBitmap(const SkBitmap& bm) {
    if (!bm.pixelRef()) {
        return nullptr;
    }

    sk_sp<SkImage> image = SkImage_Raster::MakeFromBitmap(bm, SkCopyPixelsMode::kIfMutable);
    return image;
}

sk_sp<SkImage> RasterFromPixmapCopy(const SkPixmap& pmap) {
    size_t size = 0;
    if (!valid_args(pmap.info(), pmap.rowBytes(), &size) || !pmap.addr()) {
        return nullptr;
    }

    // Here we actually make a copy of the caller's pixel data
    sk_sp<SkData> data(SkData::MakeWithCopy(pmap.addr(), size));
    sk_sp<SkImage> image = sk_make_sp<SkImage_Raster>(
            pmap.info(), std::move(data), pmap.rowBytes(), /*mips=*/nullptr, kNeedNewImageUniqueID);
    return image;
}

sk_sp<SkImage> RasterFromData(const SkImageInfo& info, sk_sp<SkData> data, size_t rowBytes) {
    size_t size = 0;
    if (!valid_args(info, rowBytes, &size) || !data) {
        return nullptr;
    }

    // did they give us enough data?
    if ((*data).size() < size) {
        return nullptr;
    }

    sk_sp<SkImage> image = sk_make_sp<SkImage_Raster>(
            info, std::move(data), rowBytes, /*mips=*/nullptr, kNeedNewImageUniqueID);
    return image;
}

sk_sp<SkImage> RasterFromPixmap(const SkPixmap& pmap, RasterReleaseProc proc, ReleaseContext ctx) {
    size_t size = 0;
    if (!valid_args(pmap.info(), pmap.rowBytes(), &size) || !pmap.addr()) {
        return nullptr;
    }

    sk_sp<SkData> data(SkData::MakeWithProc(pmap.addr(), size, proc, ctx));
    sk_sp<SkImage> image = sk_make_sp<SkImage_Raster>(
            pmap.info(), std::move(data), pmap.rowBytes(), /*mips=*/nullptr, kNeedNewImageUniqueID);
    return image;
}

}  // namespace SkImages
