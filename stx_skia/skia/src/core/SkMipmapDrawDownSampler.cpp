/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkTypes.h"

#ifdef SK_USE_DRAWING_MIPMAP_DOWNSAMPLER

#include "include/core/SkBitmap.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkScalar.h"
#include "src/core/SkDraw.h"
#include "src/core/SkMipmap.h"
#include "src/core/SkRasterClip.h"
#include <memory>

namespace {

class DrawDownSampler final : public SkMipmapDownSampler {
public:
    DrawDownSampler() {
        // Each level replaces its destination; prior destination pixels do not
        // participate in blending. Drawing remains synchronous and CPU-only.
        fPaint.setBlendMode(SkBlendMode::kSrc);
    }
    void buildLevel(const SkPixmap& destination, const SkPixmap& source) override;
private:
    SkPaint fPaint{};
};

SkSamplingOptions choose_options(const SkPixmap& destination, const SkPixmap& source) {
    const bool exactHalf = destination.width() * 2 == source.width() &&
                           destination.height() * 2 == source.height();
    if (exactHalf) {
        const SkSamplingOptions sampling(SkFilterMode::kLinear, SkMipmapMode::kNone);
        return sampling;
    }
    const SkCubicResampler cubic = SkCubicResampler::CatmullRom();
    const SkSamplingOptions sampling(cubic);
    return sampling;
}

void DrawDownSampler::buildLevel(const SkPixmap& destination, const SkPixmap& source) {
    // The caller supplies disjoint complete buffers and a valid halved shape.
    // Reject an empty view before forming scale ratios. Failed bitmap wrapping
    // leaves destination pixels unchanged; no pixel ownership is transferred.
    if (source.width() <= 0 || source.height() <= 0 || source.addr() == nullptr ||
        destination.width() <= 0 || destination.height() <= 0 || destination.addr() == nullptr) {
        return;
    }
    SkBitmap bitmap{};
    if (!bitmap.installPixels(source)) {
        return;
    }
    const SkIRect bounds = destination.bounds();
    const SkRasterClip clip(bounds);
    const SkScalar scaleX = SkIntToScalar(destination.width()) / source.width();
    const SkScalar scaleY = SkIntToScalar(destination.height()) / source.height();
    const SkMatrix matrix = SkMatrix::Scale(scaleX, scaleY);
    const SkSamplingOptions sampling = choose_options(destination, source);
    const SkMatrix identity = SkMatrix::I();

    // Draw only borrows these views/state; every owner precedes it in scope.
    skcpu::Draw draw{};
    draw.fDst = destination;
    draw.fCTM = &matrix;
    draw.fRC = &clip;
    draw.drawBitmap(bitmap, identity, nullptr, sampling, fPaint, nullptr);
}

} // namespace

std::unique_ptr<SkMipmapDownSampler> SkMipmap::MakeDownSampler(const SkPixmap&) {
    std::unique_ptr<SkMipmapDownSampler> sampler = std::make_unique<DrawDownSampler>();
    return sampler;
}

#endif
