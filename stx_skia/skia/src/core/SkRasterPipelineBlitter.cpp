/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkAlphaType.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkBlender.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSurfaceProps.h"
#include "include/private/SkAssert.h"
#include "include/private/SkCPUTypes.h"
#include "include/private/SkOnce.h"
#include "include/private/SkTemplates.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkBlendModePriv.h"
#include "src/core/SkBlenderBase.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkColorSpacePriv.h"
#include "src/core/SkColorSpaceXformSteps.h"
#include "src/core/SkConvertPixels.h"
#include "src/core/SkCoreBlitters.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkMask.h"
#include "src/core/SkMemset.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpContexts.h"
#include "src/core/SkRasterPipelineOpList.h"
#include "src/core/SkRasterPipelineVizualizer.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"
#include "src/shaders/SkShaderBase.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

class SkColorSpace;
class SkShader;

static bool can_direct_blit(const SkPaint& paint) {
    if (paint.getShader() || paint.getColorFilter() || paint.isDither()) {
        return false;
    }
    const std::optional<SkBlendMode> mode = paint.asBlendMode();
    if (mode.has_value()) {
        switch (mode.value()) {
            case SkBlendMode::kClear: [[fallthrough]];
            case SkBlendMode::kSrc:
                return true;
            case SkBlendMode::kSrcOver: {
                const bool opaque = paint.getAlphaf() == 1;
                return opaque;
            }
            default:
                break;
        }
    }
    return false;
}

// Writes the memset color to the w x h rectangle at (x, y). Each row is addressed directly.
using Memset2DProc = void (*)(SkPixmap* dst, int x, int y, int w, int h, uint64_t color);

static void memset2d_8(SkPixmap* dst, int x, int y, int w, int h, uint64_t c) {
    for (int row = y; row < y + h; ++row) {
        memset((*dst).writable_addr(x, row), static_cast<int>(c), static_cast<size_t>(w));
    }
}

static void memset2d_16(SkPixmap* dst, int x, int y, int w, int h, uint64_t c) {
    SkOpts::rect_memset16((*dst).writable_addr16(x, y), static_cast<uint16_t>(c), w,
                          (*dst).rowBytes(), h);
}

static void memset2d_32(SkPixmap* dst, int x, int y, int w, int h, uint64_t c) {
    SkOpts::rect_memset32((*dst).writable_addr32(x, y), static_cast<uint32_t>(c), w,
                          (*dst).rowBytes(), h);
}

static void memset2d_64(SkPixmap* dst, int x, int y, int w, int h, uint64_t c) {
    SkOpts::rect_memset64((*dst).writable_addr64(x, y), c, w, (*dst).rowBytes(), h);
}

// A compiled pipeline over the rectangle (x, y, width, height).
using BlitPipeline = std::function<void(size_t, size_t, size_t, size_t)>;

class SkRasterPipelineBlitter final : public SkBlitter {
public:
    // This is our common entrypoint for creating the blitter once we've sorted out shaders.
    static SkBlitter* Create(const SkPixmap& dst,
                             const SkPaint& paint,
                             const SkColor4f& dstPaintColor,
                             SkArenaAlloc* alloc,
                             const SkRasterPipeline& shaderPipeline,
                             bool is_opaque,
                             bool is_constant,
                             const SkShader* clipShader);

    // Borrows alloc, which owns the pipelines' stages and must outlive the blitter.
    SkRasterPipelineBlitter(SkPixmap dst,
                            const SkPaint& paint,
                            SkArenaAlloc* alloc)
        : fDst(std::move(dst))
        , fAlloc(alloc)
        , fColorPipeline(alloc)
        , fBlendPipeline(alloc)
        , fCanDirectBlit(can_direct_blit(paint))
        , fDirectBlitPaintColor(paint.getColor4f())
    {}

    void blitH     (int x, int y, int w)                            override;
    void blitAntiH (int x, int y, const SkAlpha[], const int16_t[]) override;
    void blitAntiH2(int x, int y, U8CPU a0, U8CPU a1)               override;
    void blitAntiV2(int x, int y, U8CPU a0, U8CPU a1)               override;
    void blitMask  (const SkMask&, const SkIRect& clip)             override;
    void blitRect  (int x, int y, int width, int height)            override;
    void blitV     (int x, int y, int height, SkAlpha alpha)        override;
    std::optional<DirectBlit> canDirectBlit()                       override;

private:
    // These append fDstPtr, which pipelines repoint during their runs.
    void appendLoadDst      (SkRasterPipeline*);
    void appendStore        (SkRasterPipeline*);

    // these check internally, and only append if there was a native clipShader
    void appendClipScale    (SkRasterPipeline*) const;
    void appendClipLerp     (SkRasterPipeline*) const;

    SkPixmap               fDst;
    SkArenaAlloc*          fAlloc;
    SkRasterPipeline       fColorPipeline;
    SkRasterPipeline       fBlendPipeline;
    // If the blender is a blend-mode, we retain that information for late-stage optimizations
    std::optional<SkBlendMode> fBlendMode;
    // set to pipeline storage (for alpha) if we have a clipShader
    // Clip coverage in lane storage: float for a highp program, uint16_t lanes for a lowp one.
    float*                 fClipShaderBuffer = nullptr;

    bool fCanDirectBlit;
    const SkColor4f fDirectBlitPaintColor;
    std::optional<uint64_t> fDirectBlitValue;

    SkRasterPipelineContexts::MemoryCtx fDstPtr = {nullptr, 0};   // Always points to the top-left of fDst.
    SkRasterPipelineContexts::MemoryCtx fMaskPtr = {nullptr, 0};  // Updated each call to blitMask().
    SkRasterPipelineContexts::EmbossCtx fEmbossCtx = {{nullptr, 0}, {nullptr, 0}};  // Used only for k3D_Format masks.

    // We may be able to specialize blitH() or blitRect() into a memset.
    Memset2DProc fMemset2D = nullptr;
    uint64_t fMemsetColor = 0;   // Big enough for largest memsettable dst format, F16.

    // Built lazily on first use.
    BlitPipeline fBlitRect;
    BlitPipeline fBlitAntiH;
    BlitPipeline fBlitMaskA8;
    BlitPipeline fBlitMaskLCD16;
    BlitPipeline fBlitMask3D;

    // These values are pointed to by the blit pipelines above,
    // which allows us to adjust them from call to call.
    float fCurrentCoverage = 0.0f;
    float fDitherRate      = 0.0f;

    using INHERITED = SkBlitter;
};

static SkColor4f paint_color_to_dst(const SkPaint& paint, const SkPixmap& dst) {
    SkColor4f paintColor = paint.getColor4f();
    const SkColorSpaceXformSteps steps(sk_srgb_singleton(), kUnpremul_SkAlphaType,
                                       dst.colorSpace(),    kUnpremul_SkAlphaType);
    steps.apply(paintColor.vec());
    return paintColor;
}

static bool create_pipeline_for_blitter(const SkPixmap& dst,
                                        const SkPaint& paint,
                                        const SkMatrix& ctm,
                                        SkArenaAlloc* alloc,
                                        const SkSurfaceProps& props,
                                        SkRasterPipeline* shaderPipeline,
                                        SkColor4f* dstPaintColor,
                                        bool* isOpaqueOut,
                                        bool* isConstantOut,
                                        const SkRect& devBounds) {
    *dstPaintColor = paint_color_to_dst(paint, dst);

    SkShaderBase* shader = as_SB(paint.getShader());

    if (!shader) {
        // Having no shader makes things nice and easy... just use the paint color
        (*shaderPipeline).appendConstantColor(alloc, (*dstPaintColor).premul().vec());
        *isOpaqueOut = (*dstPaintColor).fA == 1.0f;
        *isConstantOut = true;
        return true;
    }

    SkColorSpace* dstCS = dst.colorSpace();
    const SkColorType dstCT = dst.colorType();
    *isOpaqueOut = (*shader).isOpaque() && (*dstPaintColor).fA == 1.0f;
    *isConstantOut = (*shader).isConstant();
    const SkStageRec rec{shaderPipeline, alloc, dstCT, dstCS, *dstPaintColor, props, devBounds};
    if ((*shader).appendRootStages(rec, ctm)) {
        if ((*dstPaintColor).fA != 1.0f) {
            (*shaderPipeline).append(SkRasterPipelineOp::scale_1_float,
                                     (*alloc).make<float>((*dstPaintColor).fA));
        }
        return true;
    }
    // The shader can't draw with SkRasterPipeline.
    return false;
}

SkBlitter* SkCreateRasterPipelineBlitter(const SkPixmap& dst,
                                         const SkPaint& paint,
                                         const SkMatrix& ctm,
                                         SkArenaAlloc* alloc,
                                         sk_sp<SkShader> clipShader,
                                         const SkSurfaceProps& props,
                                         const SkRect& devBounds) {
    SkRasterPipeline_<256> shaderPipeline;
    SkColor4f dstPaintColor = SkColors::kTransparent;
    bool is_opaque = false;
    bool is_constant = false;
    if (!create_pipeline_for_blitter(dst,
                                     paint,
                                     ctm,
                                     alloc,
                                     props,
                                     &shaderPipeline,
                                     &dstPaintColor,
                                     &is_opaque,
                                     &is_constant,
                                     devBounds)) {
        return nullptr;
    }

    SkBlitter* blitter = SkRasterPipelineBlitter::Create(dst,
                                                         paint,
                                                         dstPaintColor,
                                                         alloc,
                                                         shaderPipeline,
                                                         is_opaque,
                                                         is_constant,
                                                         clipShader.get());
    return blitter;
}

namespace SkRasterPipelineVisualizer {

static void dump_pipeline(const SkRasterPipeline* pipeline) {
    (*pipeline).dump();
}

SkBlitter* CreateBlitter(const SkPixmap& dst,
                         const std::vector<DebugStage>& debugStages,
                         const SkPaint& paint,
                         const SkMatrix& ctm,
                         SkArenaAlloc* alloc,
                         sk_sp<SkShader> clipShader,
                         const SkSurfaceProps& props) {
    SkRasterPipeline_<256> shaderPipeline;
    SkColor4f dstPaintColor = SkColors::kTransparent;
    bool is_opaque = false;
    bool is_constant = false;
    if (!create_pipeline_for_blitter(dst,
                                     paint,
                                     ctm,
                                     alloc,
                                     props,
                                     &shaderPipeline,
                                     &dstPaintColor,
                                     &is_opaque,
                                     &is_constant,
                                     SkRect::MakeEmpty())) {
        return nullptr;
    }

    static SkOnce once;
    const SkRasterPipeline* dumped = &shaderPipeline;
    once(dump_pipeline, dumped);

    std::vector<SkRasterPipeline::StageList> stages;
    for (const SkRasterPipeline::StageList* st = shaderPipeline.getStageList(); st;
         st = (*st).prev) {
        stages.push_back(*st);
    }
    std::reverse(stages.begin(), stages.end());

    SkASSERT_RELEASE(stages.size() == debugStages.size());
    SkRasterPipeline_<256> shaderPipeline2;

    for (size_t i = 0; i < stages.size(); i++) {
        shaderPipeline2.appendRecordedStage(stages[i]);
        const DebugStage& stage = debugStages[i];
        SkASSERT_RELEASE(stage.panels.size() == stage.ops.size());
        for (size_t j = 0; j < stage.panels.size() && j < stage.ops.size(); j++) {
            SkRasterPipelineContexts::MemoryCtx* panelCtx =
                    (*alloc).make<SkRasterPipelineContexts::MemoryCtx>();
            (*panelCtx).pixels = stage.panels[j].pixmap().writable_addr();
            (*panelCtx).stride = stage.panels[j].pixmap().rowBytesAsPixels();
            shaderPipeline2.append(stage.ops[j], panelCtx);
        }
    }

    SkBlitter* blitter = SkRasterPipelineBlitter::Create(dst,
                                                         paint,
                                                         dstPaintColor,
                                                         alloc,
                                                         shaderPipeline2,
                                                         is_opaque,
                                                         is_constant,
                                                         clipShader.get());
    return blitter;
}

}  // namespace SkRasterPipelineVisualizer

SkBlitter* SkCreateRasterPipelineBlitter(const SkPixmap& dst,
                                         const SkPaint& paint,
                                         const SkRasterPipeline& shaderPipeline,
                                         bool is_opaque,
                                         SkArenaAlloc* alloc,
                                         sk_sp<SkShader> clipShader) {
    const bool is_constant = false;  // If this were the case, it'd be better to just set a paint color.
    SkBlitter* blitter = SkRasterPipelineBlitter::Create(dst, paint, paint_color_to_dst(paint, dst),
                                                         alloc, shaderPipeline, is_opaque,
                                                         is_constant, clipShader.get());
    return blitter;
}

// Clip coverage written by the clip shader's stages and read by the blit pipelines.
struct ClipShaderStorage {
    // large enough for highp (float) or lowp(U16)
    float fA[SkRasterPipelineContexts::kMaxStride];
};

SkBlitter* SkRasterPipelineBlitter::Create(const SkPixmap& dst,
                                           const SkPaint& paint,
                                           const SkColor4f& dstPaintColor,
                                           SkArenaAlloc* alloc,
                                           const SkRasterPipeline& shaderPipeline,
                                           bool is_opaque,
                                           bool is_constant,
                                           const SkShader* clipShader) {
    SkRasterPipelineBlitter* blitter = (*alloc).make<SkRasterPipelineBlitter>(dst, paint, alloc);
    bool opaque = is_opaque;
    bool constant = is_constant;

    // Our job in this factory is to fill out the blitter's color and blend pipelines.
    // The color pipeline is the common front of the full blit pipeline. The blend pipeline is just
    // the portion that does the actual blending math (and assumes that src and dst are already
    // loaded).
    //
    // The full blit pipelines are each constructed lazily on first use, and include the color
    // pipeline, reading the dst, the blend pipeline, coverage, dithering, and writing the dst.

    // Start with the color pipeline
    SkRasterPipeline* colorPipeline = &(*blitter).fColorPipeline;

    if (clipShader) {
        SkRasterPipeline* clipP = colorPipeline;
        const SkColorType clipCT = kRGBA_8888_SkColorType;
        SkColorSpace* clipCS = nullptr;
        const SkSurfaceProps props{}; // default OK; clipShader doesn't render text
        const SkStageRec rec = {
                clipP, alloc, clipCT, clipCS, SkColors::kBlack, props, SkRect::MakeEmpty()};
        if (!(*as_SB(clipShader)).appendRootStages(rec, SkMatrix::I())) {
            return nullptr;
        }
        ClipShaderStorage* storage = (*alloc).make<ClipShaderStorage>();
        (*clipP).append(SkRasterPipelineOp::store_src_a, (*storage).fA);
        (*blitter).fClipShaderBuffer = (*storage).fA;
        constant = false;
    }

    // Let's get the shader in first.
    (*colorPipeline).extend(shaderPipeline);

    // If there's a color filter it comes next.
    SkColorFilter* colorFilter = paint.getColorFilter();
    if (colorFilter) {
        const SkSurfaceProps props{}; // default OK; colorFilter doesn't render text
        const SkStageRec rec = {colorPipeline,
                                alloc,
                                dst.colorType(),
                                dst.colorSpace(),
                                dstPaintColor,
                                props,
                                SkRect::MakeEmpty()};
        if (!(*as_CFB(colorFilter)).appendStages(rec, opaque)) {
            return nullptr;
        }
        opaque = opaque && (*as_CFB(colorFilter)).isAlphaUnchanged();
    }

    // Not all formats make sense to dither (think, F16).  We set their dither rate
    // to zero.  We only dither non-constant shaders, so is_constant won't change here.
    if (paint.isDither() && !constant) {
        switch (dst.info().colorType()) {
            case kARGB_4444_SkColorType:
                (*blitter).fDitherRate = 1 / 15.0f;
                break;
            case kRGB_565_SkColorType:
                (*blitter).fDitherRate = 1 / 63.0f;
                break;
            case kGray_8_SkColorType:
            case kRGB_888x_SkColorType:
            case kRGBA_8888_SkColorType:
            case kBGRA_8888_SkColorType:
            case kSRGBA_8888_SkColorType:
            case kR8_unorm_SkColorType:
                (*blitter).fDitherRate = 1 / 255.0f;
                break;
            case kRGB_101010x_SkColorType:
            case kRGBA_1010102_SkColorType:
            case kBGR_101010x_SkColorType:
            case kBGRA_1010102_SkColorType:
            case kBGRA_10101010_XR_SkColorType:
            case kRGBA_10x6_SkColorType:
                (*blitter).fDitherRate = 1 / 1023.0f;
                break;

            case kUnknown_SkColorType:
            case kAlpha_8_SkColorType:
            case kBGR_101010x_XR_SkColorType:
            case kRGBA_F16_SkColorType:
            case kRGB_F16F16F16x_SkColorType:
            case kRGBA_F16Norm_SkColorType:
            case kRGBA_F32_SkColorType:
            case kR8G8_unorm_SkColorType:
            case kA16_float_SkColorType:
            case kA16_unorm_SkColorType:
            case kR16G16_float_SkColorType:
            case kR16_unorm_SkColorType:
            case kR16_float_SkColorType:
            case kR16G16_unorm_SkColorType:
            case kR16G16B16A16_unorm_SkColorType:
                (*blitter).fDitherRate = 0.0f;
                break;
        }
        // An unlisted color type keeps the initial rate of zero: no dithering.
        if ((*blitter).fDitherRate > 0.0f) {
            (*colorPipeline).append(SkRasterPipelineOp::dither, &(*blitter).fDitherRate);
        }
    }

    // Optimization: A pipeline that's still constant here can collapse back into a constant color.
    if (constant) {
        SkColor4f constantColor = SkColors::kTransparent;
        SkRasterPipelineContexts::MemoryCtx constantColorPtr = {&constantColor, 0};
        // We could remove this clamp entirely, but if the destination is 8888, doing the clamp
        // here allows the color pipeline to still run in lowp (we'll use uniform_color, rather than
        // unbounded_uniform_color).
        (*colorPipeline).appendClampIfNormalized(dst.info());
        (*colorPipeline).append(SkRasterPipelineOp::store_f32, &constantColorPtr);
        (*colorPipeline).run(0,0,1,1);
        (*colorPipeline).reset();
        (*colorPipeline).appendConstantColor(alloc, constantColor);

        opaque = constantColor.fA == 1.0f;
    }

    // Now we'll build the blend pipeline
    SkRasterPipeline* blendPipeline = &(*blitter).fBlendPipeline;

    sk_sp<SkBlender> blender = paint.refBlender();
    if (!blender) {
        blender = SkBlender::Mode(SkBlendMode::kSrcOver);
    }

    // We can strength-reduce SrcOver into Src when opaque.
    if (opaque && (*as_BB(blender)).asBlendMode() == SkBlendMode::kSrcOver) {
        blender = SkBlender::Mode(SkBlendMode::kSrc);
    }

    // When we're drawing a constant color in Src mode, we can sometimes just memset.
    // (The previous two optimizations help find more opportunities for this one.)
    if (constant && (*as_BB(blender)).asBlendMode() == SkBlendMode::kSrc &&
        dst.info().bytesPerPixel() <= static_cast<int>(sizeof((*blitter).fMemsetColor))) {
        // Run our color pipeline all the way through to produce what we'd memset when we can.
        // Not all blits can memset, so we need to keep colorPipeline too.
        SkRasterPipeline_<256> p;
        p.extend(*colorPipeline);
        (*blitter).fDstPtr = SkRasterPipelineContexts::MemoryCtx{&(*blitter).fMemsetColor, 0};
        (*blitter).appendStore(&p);
        p.run(0,0,1,1);

        switch ((*blitter).fDst.shiftPerPixel()) {
            case 0: (*blitter).fMemset2D = memset2d_8;  break;
            case 1: (*blitter).fMemset2D = memset2d_16; break;
            case 2: (*blitter).fMemset2D = memset2d_32; break;
            case 3: (*blitter).fMemset2D = memset2d_64; break;
            // TODO(F32)?
            default:
                // Wider pixels are not memset; the pipelines draw them.
                break;
        }
    }

    {
        const SkSurfaceProps props{};  // default OK; blender doesn't render text
        const SkStageRec rec = {blendPipeline,
                                alloc,
                                dst.colorType(),
                                dst.colorSpace(),
                                dstPaintColor,
                                props,
                                SkRect::MakeEmpty()};
        if (!(*as_BB(blender)).appendStages(rec)) {
            return nullptr;
        }
        (*blitter).fBlendMode = (*as_BB(blender)).asBlendMode();
    }

    (*blitter).fDstPtr = SkRasterPipelineContexts::MemoryCtx{
        (*blitter).fDst.writable_addr(),
        (*blitter).fDst.rowBytesAsPixels(),
    };

    return blitter;
}

void SkRasterPipelineBlitter::appendLoadDst(SkRasterPipeline* p) {
    (*p).appendLoadDst(fDst.info().colorType(), &fDstPtr);
    if (fDst.info().alphaType() == kUnpremul_SkAlphaType) {
        (*p).append(SkRasterPipelineOp::premul_dst);
    }
}

void SkRasterPipelineBlitter::appendStore(SkRasterPipeline* p) {
    if (fDst.info().alphaType() == kUnpremul_SkAlphaType) {
        (*p).append(SkRasterPipelineOp::unpremul);
    }
    (*p).appendStore(fDst.info().colorType(), &fDstPtr);
}

void SkRasterPipelineBlitter::appendClipScale(SkRasterPipeline* p) const {
    if (fClipShaderBuffer) {
        (*p).append(SkRasterPipelineOp::scale_native, fClipShaderBuffer);
    }
}

void SkRasterPipelineBlitter::appendClipLerp(SkRasterPipeline* p) const {
    if (fClipShaderBuffer) {
        (*p).append(SkRasterPipelineOp::lerp_native, fClipShaderBuffer);
    }
}

void SkRasterPipelineBlitter::blitH(int x, int y, int w) {
    (*this).blitRect(x,y,w,1);
}

void SkRasterPipelineBlitter::blitRect(int x, int y, int w, int h) {
    if (fMemset2D) {
        fMemset2D(&fDst, x,y, w,h, fMemsetColor);
        return;
    }

    if (!fBlitRect) {
        SkRasterPipeline p(fAlloc);
        p.extend(fColorPipeline);
        p.appendClampIfNormalized(fDst.info());
        if (fBlendMode == SkBlendMode::kSrcOver
                && (fDst.info().colorType() == kRGBA_8888_SkColorType ||
                    fDst.info().colorType() == kBGRA_8888_SkColorType)
                && !fDst.colorSpace()
                && fDst.info().alphaType() != kUnpremul_SkAlphaType
                && fDitherRate == 0.0f) {
            if (fDst.info().colorType() == kBGRA_8888_SkColorType) {
                p.append(SkRasterPipelineOp::swap_rb);
            }
            (*this).appendClipScale(&p);
            p.append(SkRasterPipelineOp::srcover_rgba_8888, &fDstPtr);
        } else {
            if (fBlendMode != SkBlendMode::kSrc) {
                (*this).appendLoadDst(&p);
                p.extend(fBlendPipeline);
                (*this).appendClipLerp(&p);
            } else if (fClipShaderBuffer) {
                (*this).appendLoadDst(&p);
                (*this).appendClipLerp(&p);
            }
            (*this).appendStore(&p);
        }
        fBlitRect = p.compile();
    }

    fBlitRect(x,y,w,h);
}

void SkRasterPipelineBlitter::blitAntiH(int x, int y, const SkAlpha aa[], const int16_t runs[]) {
    if (!fBlitAntiH) {
        SkRasterPipeline p(fAlloc);
        p.extend(fColorPipeline);
        p.appendClampIfNormalized(fDst.info());
        if (fBlendMode.has_value() &&
            SkBlendMode_ShouldPreScaleCoverage(*fBlendMode, /*rgb_coverage=*/false)) {
            p.append(SkRasterPipelineOp::scale_1_float, &fCurrentCoverage);
            (*this).appendClipScale(&p);
            (*this).appendLoadDst(&p);
            p.extend(fBlendPipeline);
        } else {
            (*this).appendLoadDst(&p);
            p.extend(fBlendPipeline);
            p.append(SkRasterPipelineOp::lerp_1_float, &fCurrentCoverage);
            (*this).appendClipLerp(&p);
        }

        (*this).appendStore(&p);
        fBlitAntiH = p.compile();
    }

    int index = 0;
    while (runs[index] > 0) {
        const int run = runs[index];
        switch (aa[index]) {
            case 0x00:
                break;
            case 0xff:
                (*this).blitRect(x + index, y, run, 1);
                break;
            default:
                fCurrentCoverage = aa[index] * (1/255.0f);
                fBlitAntiH(x + index, y, run, 1);
                break;
        }
        index += run;
    }
}

void SkRasterPipelineBlitter::blitAntiH2(int x, int y, U8CPU a0, U8CPU a1) {
    const SkIRect clip = {x,y, x+2,y+1};
    const uint8_t coverage[] = {static_cast<uint8_t>(a0), static_cast<uint8_t>(a1)};
    const SkMask mask(coverage, clip, 2, SkMask::kA8_Format);
    (*this).blitMask(mask, clip);
}

void SkRasterPipelineBlitter::blitAntiV2(int x, int y, U8CPU a0, U8CPU a1) {
    const SkIRect clip = {x,y, x+1,y+2};
    const uint8_t coverage[] = {static_cast<uint8_t>(a0), static_cast<uint8_t>(a1)};
    const SkMask mask(coverage, clip, 1, SkMask::kA8_Format);
    (*this).blitMask(mask, clip);
}

void SkRasterPipelineBlitter::blitV(int x, int y, int height, SkAlpha alpha) {
    const SkIRect clip = {x,y, x+1,y+height};
    const SkMask mask(&alpha, clip,
                      0,     // so we reuse the 1 "row" for all of height
                      SkMask::kA8_Format);
    (*this).blitMask(mask, clip);
}

// Points ctx at one plane of mask, offset so that the pipeline's (x, y) addressing, which is
// relative to the destination's origin, lands on the mask pixel at (x, y). The address is
// computed in integers: it lies outside the mask whenever the mask is away from the origin.
static void extract_mask_plane(const SkMask& mask, int plane,
                               SkRasterPipelineContexts::MemoryCtx* ctx) {
    // LCD is 16-bit per pixel; A8 and 3D are 8-bit per pixel.
    const size_t bpp = mask.fFormat == SkMask::kLCD16_Format ? 2 : 1;

    // Select the right mask plane.  Usually plane == 0 and this is just mask.fImage.
    const uintptr_t ptr = reinterpret_cast<uintptr_t>(mask.fImage) +
                          static_cast<size_t>(plane) * mask.computeImageSize();

    // mask.fRowBytes is a uint32_t, which would break our addressing math on 64-bit builds.
    const size_t rowBytes = mask.fRowBytes;
    (*ctx).stride = static_cast<int>(rowBytes / bpp);
    const uintptr_t origin = ptr - static_cast<size_t>(mask.fBounds.left()) * bpp
                                 - static_cast<size_t>(mask.fBounds.top()) * rowBytes;
    (*ctx).pixels = reinterpret_cast<void*>(origin);
}

void SkRasterPipelineBlitter::blitMask(const SkMask& mask, const SkIRect& clip) {
    if (mask.fFormat == SkMask::kBW_Format) {
        // TODO: native BW masks?
        (*this).INHERITED::blitMask(mask, clip);
        return;
    }

    // ARGB and SDF masks shouldn't make it here.
    SkASSERT(mask.fFormat == SkMask::kA8_Format
          || mask.fFormat == SkMask::kLCD16_Format
          || mask.fFormat == SkMask::k3D_Format);

    extract_mask_plane(mask, 0, &fMaskPtr);
    if (mask.fFormat == SkMask::k3D_Format) {
        extract_mask_plane(mask, 1, &fEmbossCtx.mul);
        extract_mask_plane(mask, 2, &fEmbossCtx.add);
    }

    // Lazily build whichever pipeline we need, specialized for each mask format.
    if (mask.fFormat == SkMask::kA8_Format && !fBlitMaskA8) {
        SkRasterPipeline p(fAlloc);
        p.extend(fColorPipeline);
        p.appendClampIfNormalized(fDst.info());
        if (fBlendMode.has_value() &&
            SkBlendMode_ShouldPreScaleCoverage(*fBlendMode, /*rgb_coverage=*/false)) {
            p.append(SkRasterPipelineOp::scale_u8, &fMaskPtr);
            (*this).appendClipScale(&p);
            (*this).appendLoadDst(&p);
            p.extend(fBlendPipeline);
        } else {
            (*this).appendLoadDst(&p);
            p.extend(fBlendPipeline);
            p.append(SkRasterPipelineOp::lerp_u8, &fMaskPtr);
            (*this).appendClipLerp(&p);
        }
        (*this).appendStore(&p);
        fBlitMaskA8 = p.compile();
    }
    if (mask.fFormat == SkMask::kLCD16_Format && !fBlitMaskLCD16) {
        SkRasterPipeline p(fAlloc);
        p.extend(fColorPipeline);
        p.appendClampIfNormalized(fDst.info());
        if (fBlendMode.has_value() &&
            SkBlendMode_ShouldPreScaleCoverage(*fBlendMode, /*rgb_coverage=*/true)) {
            // Somewhat unusually, scale_565 needs dst loaded first.
            (*this).appendLoadDst(&p);
            p.append(SkRasterPipelineOp::scale_565, &fMaskPtr);
            (*this).appendClipScale(&p);
            p.extend(fBlendPipeline);
        } else {
            (*this).appendLoadDst(&p);
            p.extend(fBlendPipeline);
            p.append(SkRasterPipelineOp::lerp_565, &fMaskPtr);
            (*this).appendClipLerp(&p);
        }
        (*this).appendStore(&p);
        fBlitMaskLCD16 = p.compile();
    }
    if (mask.fFormat == SkMask::k3D_Format && !fBlitMask3D) {
        SkRasterPipeline p(fAlloc);
        p.extend(fColorPipeline);
        // This bit is where we differ from kA8_Format:
        p.append(SkRasterPipelineOp::emboss, &fEmbossCtx);
        // Now onward just as kA8.
        p.appendClampIfNormalized(fDst.info());
        if (fBlendMode.has_value() &&
            SkBlendMode_ShouldPreScaleCoverage(*fBlendMode, /*rgb_coverage=*/false)) {
            p.append(SkRasterPipelineOp::scale_u8, &fMaskPtr);
            (*this).appendClipScale(&p);
            (*this).appendLoadDst(&p);
            p.extend(fBlendPipeline);
        } else {
            (*this).appendLoadDst(&p);
            p.extend(fBlendPipeline);
            p.append(SkRasterPipelineOp::lerp_u8, &fMaskPtr);
            (*this).appendClipLerp(&p);
        }
        (*this).appendStore(&p);
        fBlitMask3D = p.compile();
    }

    BlitPipeline* blitter = nullptr;
    switch (mask.fFormat) {
        case SkMask::kA8_Format:    blitter = &fBlitMaskA8;    break;
        case SkMask::kLCD16_Format: blitter = &fBlitMaskLCD16; break;
        case SkMask::k3D_Format:    blitter = &fBlitMask3D;    break;
        default:
            SkASSERT(false);
            return;
    }

    SkASSERT(blitter);
    (*blitter)(clip.left(),clip.top(), clip.width(),clip.height());
}

std::optional<SkBlitter::DirectBlit> SkRasterPipelineBlitter::canDirectBlit() {
    if (!fCanDirectBlit) {
        return std::nullopt;
    }
    if (!fDirectBlitValue.has_value()) {
        // Converted bytes are read back through an integer of the pixel's width.
        alignas(uint64_t) uint8_t dstBuffer[8] = {};
        const SkImageInfo dst =
                SkImageInfo::Make(1, 1, fDst.info().colorType(), fDst.info().alphaType());
        const SkImageInfo src = SkImageInfo::Make(1, 1, kRGBA_F32_SkColorType,
                                                  kUnpremul_SkAlphaType);
        if (!SkConvertPixels(dst, dstBuffer, sizeof(dstBuffer),
                             src, &fDirectBlitPaintColor, sizeof(fDirectBlitPaintColor))) {
            fCanDirectBlit = false;
            return std::nullopt;
        }
        switch (dst.bytesPerPixel()) {
            case 1: {
                uint8_t value = 0;
                memcpy(&value, dstBuffer, sizeof(value));
                fDirectBlitValue = value;
                break;
            }
            case 2: {
                uint16_t value = 0;
                memcpy(&value, dstBuffer, sizeof(value));
                fDirectBlitValue = value;
                break;
            }
            case 4: {
                uint32_t value = 0;
                memcpy(&value, dstBuffer, sizeof(value));
                fDirectBlitValue = value;
                break;
            }
            case 8: {
                uint64_t value = 0;
                memcpy(&value, dstBuffer, sizeof(value));
                fDirectBlitValue = value;
                break;
            }
            default:
                fCanDirectBlit = false;
                return std::nullopt;
        }
    }
    const DirectBlit blit{fDst, fDirectBlitValue.value()};
    return blit;
}
