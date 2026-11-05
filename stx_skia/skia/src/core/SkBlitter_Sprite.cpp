/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkAlphaType.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkShader.h"
#include "include/private/SkAssert.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkColorSpacePriv.h"
#include "src/core/SkColorSpaceXformSteps.h"
#include "src/core/SkCoreBlitters.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpContexts.h"
#include "src/core/SkRasterPipelineOpList.h"
#include "src/core/SkSpriteBlitter.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

struct SkIRect;
struct SkMask;

extern bool gSkForceRasterPipelineBlitter;

SkSpriteBlitter::SkSpriteBlitter(const SkPixmap& source)
    : fSource(source) {}

bool SkSpriteBlitter::setup(const SkPixmap& dst, int left, int top, const SkPaint& paint) {
    fDst = dst;
    fLeft = left;
    fTop = top;
    fPaint = &paint;
    return true;
}

void SkSpriteBlitter::blitH(int x, int y, int width) {
    SkDEBUGFAIL("how did we get here?");

    // Fallback to blitRect.
    (*this).blitRect(x, y, width, 1);
}

void SkSpriteBlitter::blitAntiH(int x, int y, const SkAlpha antialias[], const int16_t runs[]) {
    SkDEBUGFAIL("how did we get here?");

    // No fallback strategy.
}

void SkSpriteBlitter::blitV(int x, int y, int height, SkAlpha alpha) {
    SkDEBUGFAIL("how did we get here?");

    // Fall back to superclass if the code gets here in release mode.
    INHERITED::blitV(x, y, height, alpha);
}

void SkSpriteBlitter::blitMask(const SkMask& mask, const SkIRect& clip) {
    SkDEBUGFAIL("how did we get here?");

    // Fall back to superclass if the code gets here in release mode.
    INHERITED::blitMask(mask, clip);
}

///////////////////////////////////////////////////////////////////////////////

class SkSpriteBlitter_Memcpy final : public SkSpriteBlitter {
public:
    static bool Supports(const SkPixmap& dst, const SkPixmap& src, const SkPaint& paint) {
        // the caller has already inspected the colorspace on src and dst
        SkASSERT(0 == SkColorSpaceXformSteps(src,dst).fFlags.mask());

        if (dst.colorType() != src.colorType()) {
            return false;
        }
        if (paint.getMaskFilter() || paint.getColorFilter()) {
            return false;
        }
        if (0xFF != paint.getAlpha()) {
            return false;
        }
        const std::optional<SkBlendMode> mode = paint.asBlendMode();
        const bool copies = mode == SkBlendMode::kSrc ||
                            (mode == SkBlendMode::kSrcOver && src.isOpaque());
        return copies;
    }

    explicit SkSpriteBlitter_Memcpy(const SkPixmap& src)
        : INHERITED(src) {}

    // Each row's addresses come from the destination and the source.
    void blitRect(int x, int y, int width, int height) override {
        SkASSERT(fDst.colorType() == fSource.colorType());
        SkASSERT(width > 0 && height > 0);

        const size_t bytesToCopy = static_cast<size_t>(width) << fSource.shiftPerPixel();

        for (int row = y; row < y + height; ++row) {
            void* dst = fDst.writable_addr(x, row);
            const void* src = fSource.addr(x - fLeft, row - fTop);
            memcpy(dst, src, bytesToCopy);
        }
    }

private:
    using INHERITED = SkSpriteBlitter;
};

class SkRasterPipelineSpriteBlitter : public SkSpriteBlitter {
public:
    SkRasterPipelineSpriteBlitter(const SkPixmap& src, SkArenaAlloc* alloc,
                                  sk_sp<SkShader> clipShader)
        : INHERITED(src)
        , fAlloc(alloc)
        , fClipShader(std::move(clipShader))
    {}

    bool setup(const SkPixmap& dst, int left, int top, const SkPaint& paint) override {
        fDst  = dst;
        fLeft = left;
        fTop  = top;
        fPaintColor = paint.getColor4f();

        SkRasterPipeline p(fAlloc);
        p.appendLoad(fSource.colorType(), &fSrcPtr);

        if (SkColorTypeIsAlphaOnly(fSource.colorType())) {
            // The color for A8 images comes from the (sRGB) paint color.
            p.appendSetRGB(fAlloc, fPaintColor);
            p.append(SkRasterPipelineOp::premul);
        }
        SkColorSpace* dstCS = fDst.colorSpace();
        if (dstCS) {
            SkColorSpace* srcCS = fSource.colorSpace();
            if (!srcCS || SkColorTypeIsAlphaOnly(fSource.colorType())) {
                // We treat untagged images as sRGB.
                // Alpha-only images get their r,g,b from the paint color, so they're also sRGB.
                srcCS = sk_srgb_singleton();
            }
            const SkAlphaType srcAT = fSource.isOpaque() ? kOpaque_SkAlphaType
                                                         : kPremul_SkAlphaType;
            SkColorSpaceXformSteps* steps = (*fAlloc).make<SkColorSpaceXformSteps>(
                    srcCS, srcAT, dstCS, kPremul_SkAlphaType);
            (*steps).apply(&p);
        }
        if (fPaintColor.fA != 1.0f) {
            p.append(SkRasterPipelineOp::scale_1_float, &fPaintColor.fA);
        }

        const bool is_opaque = fSource.isOpaque() && fPaintColor.fA == 1.0f;
        fBlitter = SkCreateRasterPipelineBlitter(fDst, paint, p, is_opaque, fAlloc, fClipShader);
        const bool ready = fBlitter != nullptr;
        return ready;
    }

    void blitRect(int x, int y, int width, int height) override {
        fSrcPtr.stride = fSource.rowBytesAsPixels();

        // We really want fSrcPtr.pixels = fSource.addr(-fLeft, -fTop) here, but that asserts.
        // Instead we ask for addr(-fLeft+x, -fTop+y), then back up (x,y) manually. The backed-up
        // address lies outside the source whenever the source does not start at the
        // destination's origin, so it is computed in integers. Representing bpp as a size_t
        // keeps all this math in size_t instead of int, which could wrap around with large
        // enough fSrcPtr.stride and y.
        const size_t bpp = fSource.info().bytesPerPixel();
        const uintptr_t visible = reinterpret_cast<uintptr_t>(
                fSource.writable_addr(-fLeft + x, -fTop + y));
        const uintptr_t origin = visible - bpp * static_cast<size_t>(x)
                                         - bpp * static_cast<size_t>(y) *
                                                 static_cast<size_t>(fSrcPtr.stride);
        fSrcPtr.pixels = reinterpret_cast<void*>(origin);

        (*fBlitter).blitRect(x,y,width,height);
    }

private:
    SkArenaAlloc*              fAlloc;
    SkBlitter*                 fBlitter = nullptr;
    SkRasterPipelineContexts::MemoryCtx fSrcPtr = {nullptr, 0};
    SkColor4f                  fPaintColor = SkColors::kTransparent;
    sk_sp<SkShader>            fClipShader;

    using INHERITED = SkSpriteBlitter;
};

// returning null means the caller will call SkBlitter::Choose() and
// have wrapped the source bitmap inside a shader
SkBlitter* SkBlitter::ChooseSprite(const SkPixmap& dst, const SkPaint& paint,
                                   const SkPixmap& source, int left, int top,
                                   SkArenaAlloc* alloc, sk_sp<SkShader> clipShader) {
    /*  We currently ignore antialiasing and filtertype, meaning we will take our
        special blitters regardless of these settings. Ignoring filtertype seems fine
        since by definition there is no scale in the matrix. Ignoring antialiasing is
        a bit of a hack, since we "could" pass in the fractional left/top for the bitmap,
        and respect that by blending the edges of the bitmap against the device. To support
        this we could either add more special blitters here, or detect antialiasing in the
        paint and return null if it is set, forcing the client to take the slow shader case
        (which does respect soft edges).
    */
    SkASSERT(alloc != nullptr);

    // TODO: in principle SkRasterPipelineSpriteBlitter could be made to handle this.
    if (source.alphaType() == kUnpremul_SkAlphaType) {
        return nullptr;
    }

    SkSpriteBlitter* blitter = nullptr;

    if (gSkForceRasterPipelineBlitter) {
        // Do not use any of these optimized memory blitters
    } else if (0 == SkColorSpaceXformSteps(source,dst).fFlags.mask() && !clipShader) {
        if (!blitter && SkSpriteBlitter_Memcpy::Supports(dst, source, paint)) {
            blitter = (*alloc).make<SkSpriteBlitter_Memcpy>(source);
        }
        if (!blitter) {
            switch (dst.colorType()) {
                case kN32_SkColorType:
                    blitter = SkSpriteBlitter::ChooseL32(source, paint, alloc);
                    break;
                default:
                    break;
            }
        }
    }
    if (!blitter && !paint.getMaskFilter()) {
        blitter = (*alloc).make<SkRasterPipelineSpriteBlitter>(source, alloc, clipShader);
    }

    if (blitter && (*blitter).setup(dst, left,top, paint)) {
        return blitter;
    }

    return nullptr;
}
