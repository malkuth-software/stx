/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/shaders/SkColorFilterShader.h"

#include "include/core/SkColorFilter.h"
#include "include/core/SkShader.h"
#include "include/private/SkAssert.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpList.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"

#include <utility>

SkColorFilterShader::SkColorFilterShader(sk_sp<SkShader> shader,
                                         float alpha,
                                         sk_sp<SkColorFilter> filter)
    : fShader(std::move(shader))
    , fFilter(as_CFB_sp(std::move(filter)))
    , fAlpha (alpha)
{
    SkASSERT(fShader);
    SkASSERT(fFilter);
}

sk_sp<SkShader> SkColorFilterShader::Make(sk_sp<SkShader> shader,
                                          float alpha,
                                          sk_sp<SkColorFilter> filter) {
    if (!shader) {
        return nullptr;
    }
    if (!filter) {
        return shader;
    }
    sk_sp<SkShader> result(new SkColorFilterShader(std::move(shader), alpha, std::move(filter)));
    return result;
}

sk_sp<SkFlattenable> SkColorFilterShader::CreateProc(SkReadBuffer& buffer) {
    sk_sp<SkShader> shader = buffer.readShader();
    sk_sp<SkColorFilter> filter = buffer.readColorFilter();
    sk_sp<SkFlattenable> result = Make(std::move(shader), 1.0f, std::move(filter));
    return result;
}

bool SkColorFilterShader::isOpaque() const {
    const bool opaque = (*fShader).isOpaque() && fAlpha == 1.0f && (*fFilter).isAlphaUnchanged();
    return opaque;
}

void SkColorFilterShader::flatten(SkWriteBuffer& buffer) const {
    buffer.writeFlattenable(fShader.get());
    SkASSERT(fAlpha == 1.0f);  // Not exposed in public API SkShader::makeWithColorFilter().
    buffer.writeFlattenable(fFilter.get());
}

bool SkColorFilterShader::appendStages(const SkStageRec& rec,
                                       const SkShaders::MatrixRec& mRec) const {
    const SkShaderBase& shader = *as_SB(fShader);
    if (!shader.appendStages(rec, mRec)) {
        return false;
    }
    if (fAlpha != 1.0f) {
        const float* alpha = (*rec.fAlloc).make<float>(fAlpha);
        (*rec.fPipeline).append(SkRasterPipelineOp::scale_1_float, alpha);
    }
    const bool inputIsOpaque = fAlpha == 1.0f && shader.isOpaque();
    if (!(*fFilter).appendStages(rec, inputIsOpaque)) {
        return false;
    }
    return true;
}
