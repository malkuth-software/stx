/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/effects/colorfilters/SkColorFilterBase.h"

#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h" // IWYU pragma: keep
#include "include/core/SkColorType.h"
#include "include/core/SkSurfaceProps.h"
#include "include/private/SkAssert.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkColorData.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpContexts.h"
#include "src/core/SkRasterPipelineOpList.h"

#include <cstddef>

enum class SkBlendMode;

bool SkColorFilterBase::onAsAColorMode(SkColor*, SkBlendMode*) const {
    return false;
}

bool SkColorFilterBase::onAsAColorMatrix(float matrix[20]) const {
    return false;
}

SkPMColor4f SkColorFilterBase::onFilterColor4f(const SkPMColor4f& color,
                                               SkColorSpace* dstCS) const {
    constexpr size_t kEnoughForCommonFilters = 2048;
    SkSTArenaAlloc<kEnoughForCommonFilters> alloc{};
    // Owners precede the pipeline which borrows their output and context.
    SkPMColor4f dst{};
    SkRasterPipelineContexts::MemoryCtx dstPtr = {&dst, 0};
    SkRasterPipeline pipeline(&alloc);
    pipeline.appendConstantColor(&alloc, color.vec());
    SkSurfaceProps props{}; // default OK; colorFilters don't render text
    const SkStageRec rec = {&pipeline,
                      &alloc,
                      kRGBA_F32_SkColorType,
                      dstCS,
                      color.unpremul(),
                      props,
                      SkRect::MakeEmpty()};

    if (appendStages(rec, color.fA == 1.0f)) {
        pipeline.append(SkRasterPipelineOp::store_f32, &dstPtr);
        pipeline.run(0,0, 1,1);
        return dst;
    }

    SkDEBUGFAIL("onFilterColor4f unimplemented for this filter");
    return dst;
}
