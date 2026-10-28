/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/effects/SkShaderMaskFilterImpl.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFlattenable.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkShader.h"
#include "include/effects/SkShaderMaskFilter.h"
#include "src/core/SkMask.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkRectMemcpy.h"
#include "src/core/SkSafeMath.h"
#include "src/core/SkWriteBuffer.h"

#include <cstdint>
#include <cstring>
#include <utility>

class SkMatrix;

sk_sp<SkFlattenable> SkShaderMaskFilterImpl::CreateProc(SkReadBuffer& buffer) {
    sk_sp<SkShader> shader = buffer.readShader();
    sk_sp<SkMaskFilter> filter = SkShaderMaskFilter::Make(std::move(shader));
    sk_sp<SkFlattenable> result = std::move(filter);
    return result;
}

void SkShaderMaskFilterImpl::flatten(SkWriteBuffer& buffer) const {
    buffer.writeFlattenable(fShader.get());
}

// Input pixels are borrowed, output pixels are newly owned. Failure leaves dst
// unchanged. The caller supplies the actual source allocation extent; this
// boundary validates metadata/arithmetic before copying or publishing storage.
bool SkShaderMaskFilterImpl::filterMask(SkMaskBuilder* dst, const SkMask& src, const SkMatrix& ctm,
                                        SkIPoint* margin) const {
    if (dst == nullptr || &src == dst || src.fFormat != SkMask::kA8_Format) return false;
    const int64_t width = src.fBounds.width64();
    const int64_t height = src.fBounds.height64();
    if (width < 0 || height < 0 || width > INT32_MAX || height > INT32_MAX) return false;
    SkMaskBuilder result(nullptr, src.fBounds, static_cast<uint32_t>(width), SkMask::kA8_Format);
    if (src.fImage == nullptr) {
        *dst = std::move(result);
        if (margin != nullptr) (*margin).set(0, 0);
        return true;
    }
    const size_t size = result.computeImageSize();
    if (width == 0 || height == 0 || size == 0) return false;
    if (height > 1 && src.fRowBytes < width) return false;
    SkSafeMath extent;
    const size_t lastRow = extent.mul(static_cast<size_t>(height - 1), src.fRowBytes);
    extent.add(lastRow, static_cast<size_t>(width));
    if (!extent.ok()) return false;

    SkAutoMaskFreeImage pixels(SkMaskBuilder::AllocImage(size));
    SkRectMemcpy(pixels.get(), result.fRowBytes, src.fImage, src.fRowBytes,
                 static_cast<size_t>(width), static_cast<int>(height));
    {
        const SkImageInfo info = SkImageInfo::MakeA8(static_cast<int>(width), static_cast<int>(height));
        SkBitmap bitmap;
        const bool installed = bitmap.installPixels(info, pixels.get(), result.fRowBytes);
        if (!installed) return false;
        SkPaint paint;
        paint.setShader(fShader);
        // Source-in multiplies shader alpha by the original coverage mask.
        paint.setBlendMode(SkBlendMode::kSrcIn);
        SkCanvas canvas(bitmap);
        canvas.translate(-SkIntToScalar(result.fBounds.fLeft), -SkIntToScalar(result.fBounds.fTop));
        canvas.concat(ctm);
        canvas.drawPaint(paint);
    }
    result.image() = pixels.release();
    *dst = std::move(result);
    if (margin != nullptr) (*margin).set(0, 0);
    return true;
}

sk_sp<SkMaskFilter> SkShaderMaskFilter::Make(sk_sp<SkShader> shader) {
    if (!shader) return nullptr;
    sk_sp<SkMaskFilter> filter(new SkShaderMaskFilterImpl(std::move(shader)));
    return filter;
}
