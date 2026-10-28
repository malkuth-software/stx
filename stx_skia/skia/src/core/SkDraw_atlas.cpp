/*
 * Copyright 2019 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkAlphaType.h"
#include "include/core/SkColor.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkShader.h"
#include "include/core/SkSpan.h"
#include "include/core/SkSurfaceProps.h"
#include "include/private/SkAssert.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkBlendModePriv.h"
#include "src/core/SkBlenderBase.h"
#include "src/core/SkColorSpacePriv.h"
#include "src/core/SkColorSpaceXformSteps.h"
#include "src/core/SkCoreBlitters.h"
#include "src/core/SkDraw.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkPathRawShapes.h"
#include "src/core/SkRasterClip.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpContexts.h"
#include "src/core/SkRasterPipelineOpList.h"
#include "src/core/SkScan.h"
#include "src/core/SkSurfacePriv.h"
#include "src/shaders/SkShaderBase.h"
#include "src/shaders/SkTransformShader.h"

#include <cstddef>
#include <cstdint>
#include <optional>

class SkBlender;
class SkBlitter;
enum class SkBlendMode;
struct SkRSXform;

static void fill_rect(const SkMatrix& ctm, const SkRasterClip& rc,
                      const SkRect& r, SkBlitter* blitter) {
    if (ctm.rectStaysRect()) {
        SkScan::FillRect(ctm.mapRect(r), rc, blitter);
    } else {
        SkPathRawShapes::Rect raw(r);
        ctm.mapPoints(raw.fStorage);
        const std::optional<SkRect> bounds = SkRect::Bounds(raw.fStorage);
        if (bounds.has_value()) {
            raw.fBounds = *bounds;
            SkScan::FillPath(raw, rc, blitter);
        }
    }
}

// Stores a premultiplied color in both the low-precision and float forms of the context.
static void load_color(SkRasterPipelineContexts::UniformColorCtx* ctx, const float rgba[]) {
    // only need one of these. can I query the pipeline to know if its lowp or highp?
    for (int channel = 0; channel < 4; ++channel) {
        (*ctx).rgba[channel] = static_cast<uint16_t>(SkScalarRoundToInt(rgba[channel] * 255));
    }
    (*ctx).r = rgba[0];
    (*ctx).g = rgba[1];
    (*ctx).b = rgba[2];
    (*ctx).a = rgba[3];
}

namespace skcpu {
void Draw::drawAtlas(SkSpan<const SkRSXform> xform,
                     SkSpan<const SkRect> textures,
                     SkSpan<const SkColor> colors,
                     sk_sp<SkBlender> blender,
                     const SkPaint& paint) {
    SkASSERT(xform.size() == textures.size());
    SkASSERT(colors.empty() || (xform.size() == colors.size()));

    sk_sp<SkShader> atlasShader = paint.refShader();
    if (!atlasShader || xform.empty()) {
        return;
    }

    SkSTArenaAlloc<256> alloc;

    SkPaint p(paint);
    p.setAntiAlias(false);  // we never respect this for drawAtlas(or drawVertices)
    p.setStyle(SkPaint::kFill_Style);
    p.setShader(nullptr);
    p.setMaskFilter(nullptr);

    // The RSXForms can't contain perspective - only the CTM can.
    const bool perspective = (*fCTM).hasPerspective();

    SkTransformShader* transformShader =
            alloc.make<SkTransformShader>(*as_SB(atlasShader), perspective);

    SkRasterPipeline pipeline(&alloc);
    const SkSurfaceProps props = SkSurfacePropsCopyOrDefault(fProps);
    const SkStageRec rec = {&pipeline,
                      &alloc,
                      fDst.colorType(),
                      fDst.colorSpace(),
                      p.getColor4f(),
                      props,
                      SkRect::MakeEmpty()};
    // We pass an identity matrix here rather than the CTM. The CTM gets folded into the
    // per-triangle matrix.
    if (!(*as_SB(transformShader)).appendRootStages(rec, SkMatrix::I())) {
        return;
    }

    SkRasterPipelineContexts::UniformColorCtx* uniformCtx = nullptr;
    const SkColorSpaceXformSteps steps(sk_srgb_singleton(), kUnpremul_SkAlphaType,
                                       rec.fDstCS, kUnpremul_SkAlphaType);
    if (!colors.empty()) {
        // we will late-bind the values in ctx, once for each color in the loop
        uniformCtx = alloc.make<SkRasterPipelineContexts::UniformColorCtx>();
        (*rec.fPipeline).append(SkRasterPipelineOp::uniform_color_dst, uniformCtx);
        const std::optional<SkBlendMode> bm = (*as_BB(blender)).asBlendMode();
        if (!bm.has_value()) {
            return;
        }
        SkBlendMode_AppendStages(*bm, rec.fPipeline);
    }

    bool isOpaque = colors.empty() && (*transformShader).isOpaque();
    if (p.getAlphaf() != 1) {
        (*rec.fPipeline).append(SkRasterPipelineOp::scale_1_float, alloc.make<float>(p.getAlphaf()));
        isOpaque = false;
    }

    SkBlitter* blitter = SkCreateRasterPipelineBlitter(fDst, p, pipeline, isOpaque, &alloc,
                                                       (*fRC).clipShader());
    if (!blitter) {
        return;
    }

    for (size_t i = 0; i < xform.size(); ++i) {
        if (!colors.empty()) {
            SkColor4f c4 = SkColor4f::FromColor(colors[i]);
            steps.apply(c4.vec());
            load_color(uniformCtx, c4.premul().vec());
        }

        SkMatrix mx;
        mx.setRSXform(xform[i]);
        mx.preTranslate(-textures[i].fLeft, -textures[i].fTop);
        mx.postConcat(*fCTM);
        const std::optional<SkMatrix> inv = mx.invert();
        if (!inv.has_value()) {
            return; // non-invertible
        }
        if ((*transformShader).update(*inv)) {
            fill_rect(mx, *fRC, textures[i], blitter);
        }
    }
}
}  // namespace skcpu
